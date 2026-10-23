/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* one nonblocking ELKS IRC socket */

#include "gem_irc_transport.h"

#include <errno.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#define GEM_IRC_READY_READ	0x0001U
#define GEM_IRC_READY_WRITE	0x0002U
#define GEM_IRC_READY_ERROR	0x0004U

static GEM_IRC_WORD
gem_irc_resolve_ipv4(const char *host, GEM_IRC_UWORD *address_lo,
	GEM_IRC_UWORD *address_hi)
{
	ipaddr_t address;

	if (!host || !address_lo || !address_hi)
		return 0;
	address = in_gethostbyname(host);
	if (address == 0 || address == 0xffffffffUL)
		return 0;
	*address_lo = (GEM_IRC_UWORD) address;
	*address_hi = (GEM_IRC_UWORD) (address >> 16);
	return 1;
}

static GEM_IRC_WORD
gem_irc_connect_ipv4(GEM_IRC_WORD descriptor, GEM_IRC_UWORD address_lo,
	GEM_IRC_UWORD address_hi, GEM_IRC_UWORD port)
{
	struct sockaddr_in any;
	struct sockaddr_in remote;

	any.sin_family = AF_INET;
	any.sin_port = 0;
	any.sin_addr.s_addr = INADDR_ANY;
	(void) bind((int) descriptor, (struct sockaddr *) &any, sizeof(any));
	remote.sin_family = AF_INET;
	remote.sin_port = htons(port);
	remote.sin_addr.s_addr = ((unsigned long) address_hi << 16)
		| (unsigned long) address_lo;
	return (GEM_IRC_WORD) connect((int) descriptor,
		(struct sockaddr *) &remote, sizeof(remote));
}

static GEM_IRC_WORD
gem_irc_select_ready(GEM_IRC_WORD descriptor, GEM_IRC_UWORD want_write)
{
	fd_set readers;
	fd_set writers;
	fd_set errors;
	struct timeval now;
	GEM_IRC_WORD ready;

	if (descriptor < 0 || descriptor >= FD_SETSIZE)
		return -1;
	FD_ZERO(&readers);
	FD_ZERO(&writers);
	FD_ZERO(&errors);
	FD_SET((int) descriptor, &readers);
	FD_SET((int) descriptor, &errors);
	if (want_write)
		FD_SET((int) descriptor, &writers);
	now.tv_sec = 0;
	now.tv_usec = 0;
	if (select((int) descriptor + 1, &readers,
			want_write ? &writers : (fd_set *) 0, &errors, &now) <= 0)
		return 0;
	ready = 0;
	if (FD_ISSET((int) descriptor, &readers))
		ready |= GEM_IRC_READY_READ;
	if (FD_ISSET((int) descriptor, &writers))
		ready |= GEM_IRC_READY_WRITE;
	if (FD_ISSET((int) descriptor, &errors))
		ready |= GEM_IRC_READY_ERROR;
	return ready;
}

static void
gem_irc_transport_clear_buffers(GEM_IRC_TRANSPORT *transport)
{
	transport->transmit_offset = 0;
	transport->transmit_length = 0;
	transport->transmit_next_length = 0;
	transport->transmit_head = 0;
	transport->receive_length = 0;
	transport->chunk_offset = 0;
	transport->chunk_length = 0;
	transport->discarding_line = 0;
}

static void
gem_irc_transport_drop(GEM_IRC_TRANSPORT *transport, GEM_IRC_UBYTE reconnect)
{
	if (transport->descriptor >= 0)
		(void) close(transport->descriptor);
	transport->descriptor = -1;
	gem_irc_transport_clear_buffers(transport);
	if (reconnect && transport->target_saved)
		transport->state = GEM_IRC_TRANSPORT_RECONNECT;
	else
		transport->state = GEM_IRC_TRANSPORT_CLOSED;
}

static GEM_IRC_WORD
gem_irc_transport_fail_io(GEM_IRC_TRANSPORT *transport)
{
	GEM_IRC_UWORD saved_errno;

	saved_errno = (GEM_IRC_UWORD) errno;
	gem_irc_transport_drop(transport, 1);
	transport->last_errno = saved_errno;
	return GEM_IRC_TRANSPORT_IO_FAILED;
}

static GEM_IRC_WORD
gem_irc_transport_would_block(void)
{
	return errno == EAGAIN || errno == EINTR;
}

/* true while connect is still running */
static GEM_IRC_WORD
gem_irc_transport_connect_pending(GEM_IRC_UWORD saved_errno)
{
	if (saved_errno == (GEM_IRC_UWORD) EINPROGRESS)
		return 1;
#ifdef EALREADY
	if (saved_errno == (GEM_IRC_UWORD) EALREADY)
		return 1;
#endif
	return 0;
}

void
gem_irc_transport_init(GEM_IRC_TRANSPORT *transport)
{
	if (!transport)
		return;
	transport->descriptor = -1;
	transport->port = 0;
	transport->address_lo = 0;
	transport->address_hi = 0;
	transport->last_errno = 0;
	transport->attempts = 0;
	transport->state = GEM_IRC_TRANSPORT_CLOSED;
	transport->target_saved = 0;
	transport->host[0] = '\0';
	gem_irc_transport_clear_buffers(transport);
}

static GEM_IRC_WORD
gem_irc_transport_save_target(GEM_IRC_TRANSPORT *transport,
	const char *host, GEM_IRC_UWORD port)
{
	GEM_IRC_UWORD length;

	if (!transport || !host || !*host || !port)
		return GEM_IRC_TRANSPORT_BAD_ARGUMENT;
	length = 0;
	while (host[length]) {
		if (length >= GEM_IRC_TRANSPORT_HOST_SIZE - 1U)
			return GEM_IRC_TRANSPORT_BAD_ARGUMENT;
		length++;
	}
	length = 0;
	while (host[length]) {
		transport->host[length] = host[length];
		length++;
	}
	transport->host[length] = '\0';
	transport->port = port;
	transport->target_saved = 1;
	transport->attempts = 0;
	return GEM_IRC_TRANSPORT_OK;
}

static GEM_IRC_WORD
gem_irc_transport_open_saved(GEM_IRC_TRANSPORT *transport)
{
	GEM_IRC_WORD descriptor;
	GEM_IRC_WORD flags;
	GEM_IRC_WORD result;
	GEM_IRC_UWORD saved_errno;

	if (!transport || !transport->target_saved || !transport->host[0]
		|| !transport->port)
		return GEM_IRC_TRANSPORT_BAD_ARGUMENT;

	gem_irc_transport_drop(transport, 1);
	if (transport->attempts != 0xffffU)
		transport->attempts++;
	transport->last_errno = 0;
	transport->address_lo = 0;
	transport->address_hi = 0;
	result = gem_irc_resolve_ipv4(transport->host,
		&transport->address_lo, &transport->address_hi);
	if (result <= 0) {
		transport->state = GEM_IRC_TRANSPORT_RECONNECT;
		return GEM_IRC_TRANSPORT_DNS_FAILED;
	}
	descriptor = (GEM_IRC_WORD) socket(AF_INET,
		SOCK_STREAM, 0);
	if (descriptor < 0) {
		transport->last_errno = (GEM_IRC_UWORD) errno;
		transport->state = GEM_IRC_TRANSPORT_RECONNECT;
		return GEM_IRC_TRANSPORT_SOCKET_FAILED;
	}
	transport->descriptor = descriptor;
	flags = (GEM_IRC_WORD) fcntl(descriptor, F_GETFL, 0);
	if (flags < 0
		|| fcntl(descriptor, F_SETFL,
			flags | O_NONBLOCK) < 0)
		return gem_irc_transport_fail_io(transport);

	gem_irc_transport_clear_buffers(transport);
	transport->state = GEM_IRC_TRANSPORT_CONNECTING;
	result = gem_irc_connect_ipv4(descriptor,
		transport->address_lo, transport->address_hi, transport->port);
	if (result == 0) {
		transport->state = GEM_IRC_TRANSPORT_CONNECTED;
		return GEM_IRC_TRANSPORT_OK;
	}
	saved_errno = (GEM_IRC_UWORD) errno;
	if (gem_irc_transport_connect_pending(saved_errno)) {
		transport->last_errno = 0;
		return GEM_IRC_TRANSPORT_PENDING;
	}
	gem_irc_transport_drop(transport, 1);
	transport->last_errno = saved_errno;
	return GEM_IRC_TRANSPORT_CONNECT_FAILED;
}

GEM_IRC_WORD
gem_irc_transport_connect(GEM_IRC_TRANSPORT *transport, const char *host,
	GEM_IRC_UWORD port)
{
	GEM_IRC_WORD result;

	result = gem_irc_transport_save_target(transport, host, port);
	if (result != GEM_IRC_TRANSPORT_OK)
		return result;
	return gem_irc_transport_open_saved(transport);
}

GEM_IRC_WORD
gem_irc_transport_reconnect(GEM_IRC_TRANSPORT *transport)
{
	if (!transport || !transport->target_saved)
		return GEM_IRC_TRANSPORT_BAD_ARGUMENT;
	return gem_irc_transport_open_saved(transport);
}

GEM_IRC_WORD
gem_irc_transport_progress(GEM_IRC_TRANSPORT *transport)
{
	GEM_IRC_WORD result;
	GEM_IRC_UWORD ready;
	GEM_IRC_UWORD saved_errno;

	if (!transport)
		return GEM_IRC_TRANSPORT_BAD_ARGUMENT;
	if (transport->state == GEM_IRC_TRANSPORT_CONNECTED)
		return GEM_IRC_TRANSPORT_OK;
	if (transport->state != GEM_IRC_TRANSPORT_CONNECTING
		|| transport->descriptor < 0)
		return GEM_IRC_TRANSPORT_DISCONNECTED;

	result = gem_irc_select_ready(transport->descriptor, 1U);
	if (result < 0) {
		if (gem_irc_transport_would_block())
			return GEM_IRC_TRANSPORT_IDLE;
		return gem_irc_transport_fail_io(transport);
	}
	ready = (GEM_IRC_UWORD) result;
	if (!(ready & (GEM_IRC_READY_WRITE | GEM_IRC_READY_ERROR)))
		return GEM_IRC_TRANSPORT_IDLE;

	/* a second connect reads the KTCP result */
	result = gem_irc_connect_ipv4(transport->descriptor,
		transport->address_lo, transport->address_hi, transport->port);
	if (result < 0) {
		saved_errno = (GEM_IRC_UWORD) errno;
		if (saved_errno == (GEM_IRC_UWORD) EISCONN)
			result = 0;
		else {
			if (gem_irc_transport_connect_pending(saved_errno))
				return GEM_IRC_TRANSPORT_IDLE;
			gem_irc_transport_drop(transport, 1);
			transport->last_errno = saved_errno;
			return GEM_IRC_TRANSPORT_CONNECT_FAILED;
		}
	}
	transport->state = GEM_IRC_TRANSPORT_CONNECTED;
	transport->last_errno = 0;
	return GEM_IRC_TRANSPORT_OK;
}

/* flush one nonblocking write */
static GEM_IRC_WORD
gem_irc_transport_flush(GEM_IRC_TRANSPORT *transport)
{
	char *line;
	GEM_IRC_UWORD remaining;
	GEM_IRC_WORD written;

	if (!transport->transmit_length)
		return GEM_IRC_TRANSPORT_OK;
	if (transport->transmit_head)
		line = transport->transmit_next_line;
	else
		line = transport->transmit_line;
	remaining = transport->transmit_length - transport->transmit_offset;
	written = (GEM_IRC_WORD) write(transport->descriptor,
		line + transport->transmit_offset, remaining);
	if (written > 0) {
		if ((GEM_IRC_UWORD) written > remaining)
			return gem_irc_transport_fail_io(transport);
		transport->transmit_offset += (GEM_IRC_UWORD) written;
		if (transport->transmit_offset == transport->transmit_length) {
			transport->transmit_offset = 0;
			if (transport->transmit_next_length) {
				transport->transmit_head ^= 1U;
				transport->transmit_length =
					transport->transmit_next_length;
				transport->transmit_next_length = 0;
			} else {
				transport->transmit_length = 0;
				transport->transmit_head = 0;
			}
		}
		return GEM_IRC_TRANSPORT_OK;
	}
	if (!written || gem_irc_transport_would_block())
		return GEM_IRC_TRANSPORT_IDLE;
	return gem_irc_transport_fail_io(transport);
}

GEM_IRC_WORD
gem_irc_transport_write(GEM_IRC_TRANSPORT *transport, const char *line,
	GEM_IRC_UWORD length)
{
	char *destination;
	GEM_IRC_UWORD index;
	GEM_IRC_WORD result;

	if (!transport || !line || !length || length > GEM_IRC_LINE_MAX)
		return GEM_IRC_TRANSPORT_BAD_ARGUMENT;
	if (transport->state != GEM_IRC_TRANSPORT_CONNECTED
		|| transport->descriptor < 0)
		return GEM_IRC_TRANSPORT_DISCONNECTED;
	if (transport->transmit_length && transport->transmit_next_length)
		return GEM_IRC_TRANSPORT_IDLE;
	if (!transport->transmit_length) {
		transport->transmit_head = 0;
		destination = transport->transmit_line;
	} else if (transport->transmit_head) {
		destination = transport->transmit_line;
	} else {
		destination = transport->transmit_next_line;
	}

	index = 0;
	while (index < length) {
		destination[index] = line[index];
		index++;
	}
	if (!transport->transmit_length) {
		transport->transmit_offset = 0;
		transport->transmit_length = length;
	} else {
		transport->transmit_next_length = length;
	}
	result = gem_irc_transport_flush(transport);
	if (result < 0)
		return result;
	/* the whole line is now queued */
	return GEM_IRC_TRANSPORT_OK;
}

/* return one line, reject anything over 512 bytes */
static GEM_IRC_WORD
gem_irc_transport_assemble(GEM_IRC_TRANSPORT *transport, char *line,
	GEM_IRC_UWORD capacity, GEM_IRC_UWORD *length)
{
	const char *start;
	const char *newline;
	GEM_IRC_UWORD run;

	while (transport->chunk_offset < transport->chunk_length) {
		start = transport->receive_chunk + transport->chunk_offset;
		run = transport->chunk_length - transport->chunk_offset;
		newline = (const char *) memchr(start, '\n', run);
		if (newline)
			run = (GEM_IRC_UWORD) (newline - start) + 1U;
		transport->chunk_offset += run;
		if (transport->discarding_line) {
			if (!newline)
				continue;
			transport->discarding_line = 0;
			transport->receive_length = 0;
			return GEM_IRC_TRANSPORT_LINE_TOO_LONG;
		}
		if (run > GEM_IRC_LINE_MAX - transport->receive_length) {
			transport->receive_length = 0;
			if (newline)
				return GEM_IRC_TRANSPORT_LINE_TOO_LONG;
			transport->discarding_line = 1;
			continue;
		}
		memcpy(transport->receive_line + transport->receive_length,
			start, run);
		transport->receive_length += run;
		if (!newline)
			continue;
		if (transport->receive_length > capacity) {
			transport->receive_length = 0;
			return GEM_IRC_TRANSPORT_BUFFER_SMALL;
		}
		memcpy(line, transport->receive_line, transport->receive_length);
		*length = transport->receive_length;
		transport->receive_length = 0;
		if (transport->chunk_offset == transport->chunk_length) {
			transport->chunk_offset = 0;
			transport->chunk_length = 0;
		}
		return GEM_IRC_TRANSPORT_OK;
	}
	transport->chunk_offset = 0;
	transport->chunk_length = 0;
	return GEM_IRC_TRANSPORT_IDLE;
}

GEM_IRC_WORD
gem_irc_transport_poll(GEM_IRC_TRANSPORT *transport, char *line,
	GEM_IRC_UWORD capacity,
	GEM_IRC_UWORD *length)
{
	GEM_IRC_WORD result;
	GEM_IRC_WORD received;

	if (!transport || !line || !capacity || !length)
		return GEM_IRC_TRANSPORT_BAD_ARGUMENT;
	*length = 0;
	if (transport->state == GEM_IRC_TRANSPORT_CONNECTING) {
		result = gem_irc_transport_progress(transport);
		return result < 0 ? result : GEM_IRC_TRANSPORT_IDLE;
	}
	if (transport->state != GEM_IRC_TRANSPORT_CONNECTED
		|| transport->descriptor < 0)
		return GEM_IRC_TRANSPORT_DISCONNECTED;

	result = gem_irc_transport_assemble(transport, line, capacity, length);
	if (result != GEM_IRC_TRANSPORT_IDLE) {
		if (result == GEM_IRC_TRANSPORT_LINE_TOO_LONG) {
			transport->last_errno = 0;
			gem_irc_transport_drop(transport, 1);
		}
		return result;
	}

	/* direct nonblocking I/O avoids an extra select syscall */
	if (transport->transmit_length) {
		result = gem_irc_transport_flush(transport);
		if (result < 0)
			return result;
	}

	received = (GEM_IRC_WORD) read(transport->descriptor,
		transport->receive_chunk, GEM_IRC_TRANSPORT_CHUNK_SIZE);
	if (received == 0) {
		transport->last_errno = 0;
		gem_irc_transport_drop(transport, 1);
		return GEM_IRC_TRANSPORT_DISCONNECTED;
	}
	if (received < 0) {
		if (gem_irc_transport_would_block())
			return GEM_IRC_TRANSPORT_IDLE;
		return gem_irc_transport_fail_io(transport);
	}
	if ((GEM_IRC_UWORD) received > GEM_IRC_TRANSPORT_CHUNK_SIZE)
		return gem_irc_transport_fail_io(transport);
	transport->chunk_offset = 0;
	transport->chunk_length = (GEM_IRC_UWORD) received;
	result = gem_irc_transport_assemble(transport, line, capacity, length);
	if (result == GEM_IRC_TRANSPORT_LINE_TOO_LONG) {
		transport->last_errno = 0;
		gem_irc_transport_drop(transport, 1);
	}
	return result;
}

void
gem_irc_transport_close(GEM_IRC_TRANSPORT *transport)
{
	if (transport)
		gem_irc_transport_drop(transport, 1);
}

void
gem_irc_transport_stop(GEM_IRC_TRANSPORT *transport)
{
	if (!transport)
		return;
	transport->target_saved = 0;
	transport->host[0] = '\0';
	transport->port = 0;
	transport->attempts = 0;
	gem_irc_transport_drop(transport, 0);
}

GEM_IRC_WORD
gem_irc_transport_is_connected(const GEM_IRC_TRANSPORT *transport)
{
	return transport && transport->state == GEM_IRC_TRANSPORT_CONNECTED;
}

GEM_IRC_WORD
gem_irc_transport_is_connecting(const GEM_IRC_TRANSPORT *transport)
{
	return transport && transport->state == GEM_IRC_TRANSPORT_CONNECTING;
}

GEM_IRC_WORD
gem_irc_transport_needs_reconnect(const GEM_IRC_TRANSPORT *transport)
{
	return transport && transport->state == GEM_IRC_TRANSPORT_RECONNECT;
}
