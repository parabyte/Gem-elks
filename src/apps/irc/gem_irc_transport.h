/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

/*
 * gem_irc_transport.h - fixed-buffer ELKS/KTCP transport for GEM IRC
 *
 * the only link between the GEM app and the socket, kept in static near
 * data: two outbound lines can wait out partial writes, one inbound line
 * is pieced together across reads, and a 128-byte chunk holds the bytes
 * left after the first finished line
 */

#ifndef ELKS_GEM_IRC_TRANSPORT_H
#define ELKS_GEM_IRC_TRANSPORT_H

#include "gem_irc_app.h"

#define GEM_IRC_TRANSPORT_HOST_SIZE       64U
#define GEM_IRC_TRANSPORT_CHUNK_SIZE      128U

#define GEM_IRC_TRANSPORT_CLOSED          0U
#define GEM_IRC_TRANSPORT_CONNECTING      1U
#define GEM_IRC_TRANSPORT_CONNECTED       2U
#define GEM_IRC_TRANSPORT_RECONNECT       3U

#define GEM_IRC_TRANSPORT_OK              1
#define GEM_IRC_TRANSPORT_PENDING         2
#define GEM_IRC_TRANSPORT_IDLE            0
#define GEM_IRC_TRANSPORT_BAD_ARGUMENT    (-20)
#define GEM_IRC_TRANSPORT_DNS_FAILED      (-21)
#define GEM_IRC_TRANSPORT_SOCKET_FAILED   (-22)
#define GEM_IRC_TRANSPORT_CONNECT_FAILED  (-23)
#define GEM_IRC_TRANSPORT_IO_FAILED       (-24)
#define GEM_IRC_TRANSPORT_LINE_TOO_LONG   (-25)
#define GEM_IRC_TRANSPORT_BUFFER_SMALL    (-26)
#define GEM_IRC_TRANSPORT_DISCONNECTED    (-27)

/* readiness bits handed back by the select(2) ABI seam */
#define GEM_IRC_TRANSPORT_READY_READ      0x0001U
#define GEM_IRC_TRANSPORT_READY_WRITE     0x0002U
#define GEM_IRC_TRANSPORT_READY_ERROR     0x0004U

typedef struct gem_irc_transport {
	GEM_IRC_WORD descriptor;
	GEM_IRC_UWORD port;
	GEM_IRC_UWORD address_lo;
	GEM_IRC_UWORD address_hi;
	GEM_IRC_UWORD last_errno;
	GEM_IRC_UWORD attempts;
	GEM_IRC_UWORD transmit_offset;
	GEM_IRC_UWORD transmit_length;
	GEM_IRC_UWORD transmit_next_length;
	GEM_IRC_UWORD receive_length;
	GEM_IRC_UWORD chunk_offset;
	GEM_IRC_UWORD chunk_length;
	GEM_IRC_UBYTE state;
	GEM_IRC_UBYTE target_saved;
	GEM_IRC_UBYTE discarding_line;
	GEM_IRC_UBYTE transmit_head;
	char host[GEM_IRC_TRANSPORT_HOST_SIZE];
	char transmit_line[GEM_IRC_LINE_MAX];
	char transmit_next_line[GEM_IRC_LINE_MAX];
	char receive_line[GEM_IRC_LINE_MAX];
	char receive_chunk[GEM_IRC_TRANSPORT_CHUNK_SIZE];
} GEM_IRC_TRANSPORT;

void gem_irc_transport_init(GEM_IRC_TRANSPORT * transport);

/*
 * save host/port, run the resolver, then start one nonblocking connect,
 * PENDING means the timer keeps calling progress, a failed attempt keeps
 * the target and goes to RECONNECT
 */
GEM_IRC_WORD gem_irc_transport_connect(GEM_IRC_TRANSPORT * transport,
	const char *host, GEM_IRC_UWORD port);

/* retry the saved target, nothing is looked up again */
GEM_IRC_WORD gem_irc_transport_reconnect(GEM_IRC_TRANSPORT * transport);

/*
 * push the nonblocking handshake one step, zero is still waiting, one is
 * done, negative moves the saved target to RECONNECT
 */
GEM_IRC_WORD gem_irc_transport_progress(GEM_IRC_TRANSPORT * transport);

void gem_irc_transport_bind_app(GEM_IRC_TRANSPORT * transport,
	GEM_IRC_APP_TRANSPORT * app_transport);

/* public so the smoke harnesses can call them */
GEM_IRC_WORD gem_irc_transport_write(void *context,
	const char *line, GEM_IRC_UWORD length);
GEM_IRC_WORD gem_irc_transport_poll(void *context, char *line,
	GEM_IRC_UWORD capacity, GEM_IRC_UWORD * length);
void gem_irc_transport_close(void *context);

/* stop for good and forget the saved target */
void gem_irc_transport_stop(GEM_IRC_TRANSPORT * transport);
GEM_IRC_WORD gem_irc_transport_is_connected(const GEM_IRC_TRANSPORT *
	transport);
GEM_IRC_WORD gem_irc_transport_is_connecting(const GEM_IRC_TRANSPORT *
	transport);
GEM_IRC_WORD gem_irc_transport_needs_reconnect(const GEM_IRC_TRANSPORT *
	transport);
GEM_IRC_UWORD gem_irc_transport_last_errno(const GEM_IRC_TRANSPORT * transport);
GEM_IRC_UWORD gem_irc_transport_attempts(const GEM_IRC_TRANSPORT * transport);

/*
 * assembly helpers for the ELKS socket calls: resolve a host to an IPv4
 * address, connect to it, and poll the socket, the C side only ever sees
 * plain 16-bit words
 */
GEM_IRC_WORD gem_irc_elks_resolve_ipv4(const char *host,
	GEM_IRC_UWORD * address_lo, GEM_IRC_UWORD * address_hi);
GEM_IRC_WORD gem_irc_elks_connect_ipv4(GEM_IRC_WORD descriptor,
	GEM_IRC_UWORD address_lo, GEM_IRC_UWORD address_hi, GEM_IRC_UWORD port);
GEM_IRC_WORD gem_irc_elks_select_ready(GEM_IRC_WORD descriptor,
	GEM_IRC_UWORD want_write);

#endif				/* ELKS_GEM_IRC_TRANSPORT_H */
