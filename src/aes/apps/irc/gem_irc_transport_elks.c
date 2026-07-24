/*
 * Copyright (C) 2026 Goliath Keet <gatekeeper@xt-emporium.com>
 * SPDX-License-Identifier: GPL-2.0-only
 */

/*
 * gem_irc_transport_elks.c - direct ELKS/KTCP socket transport for GEM IRC.
 *
 * socket(), read(), write(), fcntl(), close(), and errno are used directly;
 * the test-name macros below disappear at preprocessing and add no target
 * call layer.  ELKS structures containing four-byte C fields never enter this
 * file.  The companion 8086 assembly seam owns DNS's DX:AX return, IPv4 socket
 * address layout, fd_set, and timeval, exposing only independent 16-bit words.
 *
 * The descriptor is nonblocking before the initial KTCP connect.  Two fixed
 * outbound slots retain the immediate NICK/USER burst.  A short write advances
 * a 16-bit byte offset and select(2) requests another flush later.
 * Incoming bytes are assembled one at a time, so fragmented reads and several
 * lines in one read require neither memmove nor allocation.  Every count is an
 * unscaled byte count; additions are bounded by 512 before they occur.
 */

#include "gem_irc_transport.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

/* Test builds replace names with direct fake functions, not runtime wrappers. */
#ifndef GEM_IRC_TRANSPORT_SOCKET
#define GEM_IRC_TRANSPORT_SOCKET socket
#endif
#ifndef GEM_IRC_TRANSPORT_READ
#define GEM_IRC_TRANSPORT_READ read
#endif
#ifndef GEM_IRC_TRANSPORT_WRITE
#define GEM_IRC_TRANSPORT_WRITE write
#endif
#ifndef GEM_IRC_TRANSPORT_FCNTL
#define GEM_IRC_TRANSPORT_FCNTL fcntl
#endif
#ifndef GEM_IRC_TRANSPORT_CLOSE
#define GEM_IRC_TRANSPORT_CLOSE close
#endif
#ifndef GEM_IRC_TRANSPORT_RESOLVE
#define GEM_IRC_TRANSPORT_RESOLVE gem_irc_elks_resolve_ipv4
#endif
#ifndef GEM_IRC_TRANSPORT_CONNECT
#define GEM_IRC_TRANSPORT_CONNECT gem_irc_elks_connect_ipv4
#endif
#ifndef GEM_IRC_TRANSPORT_SELECT
#define GEM_IRC_TRANSPORT_SELECT gem_irc_elks_select_ready
#endif

static void
gem_irc_transport_clear_buffers (GEM_IRC_TRANSPORT *transport)
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
gem_irc_transport_drop (GEM_IRC_TRANSPORT *transport, GEM_IRC_UBYTE reconnect)
{
  if (transport->descriptor >= 0)
    (void) GEM_IRC_TRANSPORT_CLOSE (transport->descriptor);
  transport->descriptor = -1;
  gem_irc_transport_clear_buffers (transport);
  if (reconnect && transport->target_saved)
    transport->state = GEM_IRC_TRANSPORT_RECONNECT;
  else
    transport->state = GEM_IRC_TRANSPORT_CLOSED;
}

static GEM_IRC_WORD
gem_irc_transport_fail_io (GEM_IRC_TRANSPORT *transport)
{
  GEM_IRC_UWORD saved_errno;

  saved_errno = (GEM_IRC_UWORD) errno;
  gem_irc_transport_drop (transport, 1);
  transport->last_errno = saved_errno;
  return GEM_IRC_TRANSPORT_IO_FAILED;
}

static GEM_IRC_WORD
gem_irc_transport_would_block (void)
{
  return errno == EAGAIN || errno == EINTR;
}

/*
 * EINPROGRESS is the native Gemos nonblocking-connect result.  Some ELKS
 * libc revisions also publish EALREADY, while the deliberately small Gemos
 * errno interface does not.  Test the second value only when the target libc
 * names it; this keeps the interface entirely 16-bit and avoids embedding an
 * undocumented numeric errno in the client.
 */
static GEM_IRC_WORD
gem_irc_transport_connect_pending (GEM_IRC_UWORD saved_errno)
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
gem_irc_transport_init (GEM_IRC_TRANSPORT *transport)
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
  gem_irc_transport_clear_buffers (transport);
}

static GEM_IRC_WORD
gem_irc_transport_save_target (GEM_IRC_TRANSPORT *transport,
			       const char *host, GEM_IRC_UWORD port)
{
  GEM_IRC_UWORD length;

  if (!transport || !host || !*host || !port)
    return GEM_IRC_TRANSPORT_BAD_ARGUMENT;
  length = 0;
  while (host[length])
    {
      if (length >= GEM_IRC_TRANSPORT_HOST_SIZE - 1U)
	return GEM_IRC_TRANSPORT_BAD_ARGUMENT;
      length++;
    }
  length = 0;
  while (host[length])
    {
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
gem_irc_transport_open_saved (GEM_IRC_TRANSPORT *transport)
{
  GEM_IRC_WORD descriptor;
  GEM_IRC_WORD flags;
  GEM_IRC_WORD result;
  GEM_IRC_UWORD saved_errno;

  if (!transport || !transport->target_saved || !transport->host[0]
      || !transport->port)
    return GEM_IRC_TRANSPORT_BAD_ARGUMENT;

  gem_irc_transport_drop (transport, 1);
  if (transport->attempts != 0xffffU)
    transport->attempts++;
  transport->last_errno = 0;
  transport->address_lo = 0;
  transport->address_hi = 0;
  result = GEM_IRC_TRANSPORT_RESOLVE (transport->host,
				      &transport->address_lo,
				      &transport->address_hi);
  if (result <= 0)
    {
      transport->state = GEM_IRC_TRANSPORT_RECONNECT;
      return GEM_IRC_TRANSPORT_DNS_FAILED;
    }
  descriptor = (GEM_IRC_WORD) GEM_IRC_TRANSPORT_SOCKET (AF_INET,
							SOCK_STREAM, 0);
  if (descriptor < 0)
    {
      transport->last_errno = (GEM_IRC_UWORD) errno;
      transport->state = GEM_IRC_TRANSPORT_RECONNECT;
      return GEM_IRC_TRANSPORT_SOCKET_FAILED;
    }
  transport->descriptor = descriptor;
  flags = (GEM_IRC_WORD) GEM_IRC_TRANSPORT_FCNTL (descriptor, F_GETFL, 0);
  if (flags < 0
      || GEM_IRC_TRANSPORT_FCNTL (descriptor, F_SETFL,
				  flags | O_NONBLOCK) < 0)
    return gem_irc_transport_fail_io (transport);

  gem_irc_transport_clear_buffers (transport);
  transport->state = GEM_IRC_TRANSPORT_CONNECTING;
  result = GEM_IRC_TRANSPORT_CONNECT (descriptor,
				      transport->address_lo,
				      transport->address_hi, transport->port);
  if (result == 0)
    {
      transport->state = GEM_IRC_TRANSPORT_CONNECTED;
      return GEM_IRC_TRANSPORT_OK;
    }
  saved_errno = (GEM_IRC_UWORD) errno;
  if (gem_irc_transport_connect_pending (saved_errno))
    {
      transport->last_errno = 0;
      return GEM_IRC_TRANSPORT_PENDING;
    }
  gem_irc_transport_drop (transport, 1);
  transport->last_errno = saved_errno;
  return GEM_IRC_TRANSPORT_CONNECT_FAILED;
}

GEM_IRC_WORD
gem_irc_transport_connect (GEM_IRC_TRANSPORT *transport, const char *host,
			   GEM_IRC_UWORD port)
{
  GEM_IRC_WORD result;

  result = gem_irc_transport_save_target (transport, host, port);
  if (result != GEM_IRC_TRANSPORT_OK)
    return result;
  return gem_irc_transport_open_saved (transport);
}

GEM_IRC_WORD
gem_irc_transport_reconnect (GEM_IRC_TRANSPORT *transport)
{
  if (!transport || !transport->target_saved)
    return GEM_IRC_TRANSPORT_BAD_ARGUMENT;
  return gem_irc_transport_open_saved (transport);
}

GEM_IRC_WORD
gem_irc_transport_progress (GEM_IRC_TRANSPORT *transport)
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

  result = GEM_IRC_TRANSPORT_SELECT (transport->descriptor, 1U);
  if (result < 0)
    {
      if (gem_irc_transport_would_block ())
	return GEM_IRC_TRANSPORT_IDLE;
      return gem_irc_transport_fail_io (transport);
    }
  ready = (GEM_IRC_UWORD) result;
  if (!(ready & (GEM_IRC_TRANSPORT_READY_WRITE
		 | GEM_IRC_TRANSPORT_READY_ERROR)))
    return GEM_IRC_TRANSPORT_IDLE;

  /*
   * ELKS retains the KTCP completion result in the socket while select(2)
   * reports readiness.  Repeating connect(2) consumes that exact result.
   * Writability by itself is not success: a refused handshake is writable
   * and exceptional, and this second call returns its saved errno without
   * starting another TCP handshake.
   */
  result = GEM_IRC_TRANSPORT_CONNECT (transport->descriptor,
				      transport->address_lo,
				      transport->address_hi, transport->port);
  if (result < 0)
    {
      saved_errno = (GEM_IRC_UWORD) errno;
      if (gem_irc_transport_connect_pending (saved_errno))
	return GEM_IRC_TRANSPORT_IDLE;
      gem_irc_transport_drop (transport, 1);
      transport->last_errno = saved_errno;
      return GEM_IRC_TRANSPORT_CONNECT_FAILED;
    }
  transport->state = GEM_IRC_TRANSPORT_CONNECTED;
  transport->last_errno = 0;
  return GEM_IRC_TRANSPORT_OK;
}

/*
 * Attempt one nonblocking write.  A positive return consumes exactly that
 * many queued bytes.  Zero and EAGAIN/EINTR preserve the queue; every other
 * result closes the descriptor and enters reconnect state.
 */
static GEM_IRC_WORD
gem_irc_transport_flush (GEM_IRC_TRANSPORT *transport)
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
  written = (GEM_IRC_WORD) GEM_IRC_TRANSPORT_WRITE (transport->descriptor,
						    line +
						    transport->transmit_offset,
						    remaining);
  if (written > 0)
    {
      if ((GEM_IRC_UWORD) written > remaining)
	return gem_irc_transport_fail_io (transport);
      transport->transmit_offset += (GEM_IRC_UWORD) written;
      if (transport->transmit_offset == transport->transmit_length)
	{
	  transport->transmit_offset = 0;
	  if (transport->transmit_next_length)
	    {
	      transport->transmit_head ^= 1U;
	      transport->transmit_length = transport->transmit_next_length;
	      transport->transmit_next_length = 0;
	    }
	  else
	    {
	      transport->transmit_length = 0;
	      transport->transmit_head = 0;
	    }
	}
      return GEM_IRC_TRANSPORT_OK;
    }
  if (!written || gem_irc_transport_would_block ())
    return GEM_IRC_TRANSPORT_IDLE;
  return gem_irc_transport_fail_io (transport);
}

GEM_IRC_WORD
gem_irc_transport_write (void *context, const char *line,
			 GEM_IRC_UWORD length)
{
  GEM_IRC_TRANSPORT *transport;
  char *destination;
  GEM_IRC_UWORD index;
  GEM_IRC_WORD result;

  transport = (GEM_IRC_TRANSPORT *) context;
  if (!transport || !line || !length || length > GEM_IRC_LINE_MAX)
    return GEM_IRC_TRANSPORT_BAD_ARGUMENT;
  if (transport->state != GEM_IRC_TRANSPORT_CONNECTED
      || transport->descriptor < 0)
    return GEM_IRC_TRANSPORT_DISCONNECTED;
  if (transport->transmit_length && transport->transmit_next_length)
    return GEM_IRC_TRANSPORT_IDLE;
  if (!transport->transmit_length)
    {
      transport->transmit_head = 0;
      destination = transport->transmit_line;
    }
  else if (transport->transmit_head)
    {
      destination = transport->transmit_line;
    }
  else
    {
      destination = transport->transmit_next_line;
    }

  index = 0;
  while (index < length)
    {
      destination[index] = line[index];
      index++;
    }
  if (!transport->transmit_length)
    {
      transport->transmit_offset = 0;
      transport->transmit_length = length;
    }
  else
    {
      transport->transmit_next_length = length;
    }
  result = gem_irc_transport_flush (transport);
  if (result < 0)
    return result;
  /* Positive means the complete line is owned by this bounded queue. */
  return GEM_IRC_TRANSPORT_OK;
}

/*
 * Consume already-read bytes until one complete line is available.  A line
 * may contain at most 512 wire bytes including its LF.  A byte beyond that
 * boundary starts discard mode; the connection is failed when its LF arrives,
 * preventing a partial attacker-controlled line from reaching the parser.
 */
static GEM_IRC_WORD
gem_irc_transport_assemble (GEM_IRC_TRANSPORT *transport, char *line,
			    GEM_IRC_UWORD capacity, GEM_IRC_UWORD *length)
{
  GEM_IRC_UWORD index;
  GEM_IRC_UBYTE value;

  while (transport->chunk_offset < transport->chunk_length)
    {
      value = (GEM_IRC_UBYTE)
	transport->receive_chunk[transport->chunk_offset++];
      if (transport->discarding_line)
	{
	  if (value == (GEM_IRC_UBYTE) '\n')
	    {
	      transport->discarding_line = 0;
	      transport->receive_length = 0;
	      return GEM_IRC_TRANSPORT_LINE_TOO_LONG;
	    }
	  continue;
	}
      if (transport->receive_length >= GEM_IRC_LINE_MAX)
	{
	  transport->discarding_line = 1;
	  transport->receive_length = 0;
	  if (value == (GEM_IRC_UBYTE) '\n')
	    {
	      transport->discarding_line = 0;
	      return GEM_IRC_TRANSPORT_LINE_TOO_LONG;
	    }
	  continue;
	}
      transport->receive_line[transport->receive_length++] = (char) value;
      if (value == (GEM_IRC_UBYTE) '\n')
	{
	  if (transport->receive_length > capacity)
	    {
	      transport->receive_length = 0;
	      return GEM_IRC_TRANSPORT_BUFFER_SMALL;
	    }
	  index = 0;
	  while (index < transport->receive_length)
	    {
	      line[index] = transport->receive_line[index];
	      index++;
	    }
	  *length = transport->receive_length;
	  transport->receive_length = 0;
	  if (transport->chunk_offset == transport->chunk_length)
	    {
	      transport->chunk_offset = 0;
	      transport->chunk_length = 0;
	    }
	  return GEM_IRC_TRANSPORT_OK;
	}
    }
  transport->chunk_offset = 0;
  transport->chunk_length = 0;
  return GEM_IRC_TRANSPORT_IDLE;
}

GEM_IRC_WORD
gem_irc_transport_poll (void *context, char *line, GEM_IRC_UWORD capacity,
			GEM_IRC_UWORD *length)
{
  GEM_IRC_TRANSPORT *transport;
  GEM_IRC_WORD result;
  GEM_IRC_WORD received;
  GEM_IRC_UWORD ready;

  transport = (GEM_IRC_TRANSPORT *) context;
  if (!transport || !line || !capacity || !length)
    return GEM_IRC_TRANSPORT_BAD_ARGUMENT;
  *length = 0;
  if (transport->state == GEM_IRC_TRANSPORT_CONNECTING)
    {
      result = gem_irc_transport_progress (transport);
      return result < 0 ? result : GEM_IRC_TRANSPORT_IDLE;
    }
  if (transport->state != GEM_IRC_TRANSPORT_CONNECTED
      || transport->descriptor < 0)
    return GEM_IRC_TRANSPORT_DISCONNECTED;

  result = gem_irc_transport_assemble (transport, line, capacity, length);
  if (result != GEM_IRC_TRANSPORT_IDLE)
    {
      if (result == GEM_IRC_TRANSPORT_LINE_TOO_LONG)
	{
	  transport->last_errno = 0;
	  gem_irc_transport_drop (transport, 1);
	}
      return result;
    }

  result = GEM_IRC_TRANSPORT_SELECT (transport->descriptor,
				     transport->transmit_length ? 1U : 0U);
  if (result < 0)
    {
      if (gem_irc_transport_would_block ())
	return GEM_IRC_TRANSPORT_IDLE;
      return gem_irc_transport_fail_io (transport);
    }
  ready = (GEM_IRC_UWORD) result;
  if (ready & GEM_IRC_TRANSPORT_READY_ERROR)
    return gem_irc_transport_fail_io (transport);
  if ((ready & GEM_IRC_TRANSPORT_READY_WRITE) && transport->transmit_length)
    {
      result = gem_irc_transport_flush (transport);
      if (result < 0)
	return result;
    }
  if (!(ready & GEM_IRC_TRANSPORT_READY_READ))
    return GEM_IRC_TRANSPORT_IDLE;

  received = (GEM_IRC_WORD) GEM_IRC_TRANSPORT_READ (transport->descriptor,
						    transport->receive_chunk,
						    GEM_IRC_TRANSPORT_CHUNK_SIZE);
  if (received == 0)
    {
      transport->last_errno = 0;
      gem_irc_transport_drop (transport, 1);
      return GEM_IRC_TRANSPORT_DISCONNECTED;
    }
  if (received < 0)
    {
      if (gem_irc_transport_would_block ())
	return GEM_IRC_TRANSPORT_IDLE;
      return gem_irc_transport_fail_io (transport);
    }
  if ((GEM_IRC_UWORD) received > GEM_IRC_TRANSPORT_CHUNK_SIZE)
    return gem_irc_transport_fail_io (transport);
  transport->chunk_offset = 0;
  transport->chunk_length = (GEM_IRC_UWORD) received;
  result = gem_irc_transport_assemble (transport, line, capacity, length);
  if (result == GEM_IRC_TRANSPORT_LINE_TOO_LONG)
    {
      transport->last_errno = 0;
      gem_irc_transport_drop (transport, 1);
    }
  return result;
}

void
gem_irc_transport_close (void *context)
{
  GEM_IRC_TRANSPORT *transport;

  transport = (GEM_IRC_TRANSPORT *) context;
  if (transport)
    gem_irc_transport_drop (transport, 1);
}

void
gem_irc_transport_stop (GEM_IRC_TRANSPORT *transport)
{
  if (!transport)
    return;
  transport->target_saved = 0;
  transport->host[0] = '\0';
  transport->port = 0;
  transport->attempts = 0;
  gem_irc_transport_drop (transport, 0);
}

void
gem_irc_transport_bind_app (GEM_IRC_TRANSPORT *transport,
			    GEM_IRC_APP_TRANSPORT *app_transport)
{
  if (!app_transport)
    return;
  app_transport->write = gem_irc_transport_write;
  app_transport->poll = gem_irc_transport_poll;
  app_transport->close = gem_irc_transport_close;
  app_transport->context = transport;
}

GEM_IRC_WORD
gem_irc_transport_is_connected (const GEM_IRC_TRANSPORT *transport)
{
  return transport && transport->state == GEM_IRC_TRANSPORT_CONNECTED;
}

GEM_IRC_WORD
gem_irc_transport_is_connecting (const GEM_IRC_TRANSPORT *transport)
{
  return transport && transport->state == GEM_IRC_TRANSPORT_CONNECTING;
}

GEM_IRC_WORD
gem_irc_transport_needs_reconnect (const GEM_IRC_TRANSPORT *transport)
{
  return transport && transport->state == GEM_IRC_TRANSPORT_RECONNECT;
}

GEM_IRC_UWORD
gem_irc_transport_last_errno (const GEM_IRC_TRANSPORT *transport)
{
  return transport ? transport->last_errno : 0;
}

GEM_IRC_UWORD
gem_irc_transport_attempts (const GEM_IRC_TRANSPORT *transport)
{
  return transport ? transport->attempts : 0;
}
