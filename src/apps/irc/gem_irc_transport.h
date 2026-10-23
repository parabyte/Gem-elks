/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* fixed buffers for one ELKS IRC socket */

#ifndef ELKS_GEM_IRC_TRANSPORT_H
#define ELKS_GEM_IRC_TRANSPORT_H

#include "gem_irc_app.h"

#define GEM_IRC_TRANSPORT_HOST_SIZE       64U
/* one netsys reply */
#define GEM_IRC_TRANSPORT_CHUNK_SIZE      256U

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

/* resolve and start a nonblocking connect */
GEM_IRC_WORD gem_irc_transport_connect(GEM_IRC_TRANSPORT * transport,
	const char *host, GEM_IRC_UWORD port);

/* retry the saved address */
GEM_IRC_WORD gem_irc_transport_reconnect(GEM_IRC_TRANSPORT * transport);

/* advance the nonblocking connect */
GEM_IRC_WORD gem_irc_transport_progress(GEM_IRC_TRANSPORT * transport);

GEM_IRC_WORD gem_irc_transport_write(GEM_IRC_TRANSPORT *transport,
	const char *line, GEM_IRC_UWORD length);
GEM_IRC_WORD gem_irc_transport_poll(GEM_IRC_TRANSPORT *transport, char *line,
	GEM_IRC_UWORD capacity, GEM_IRC_UWORD * length);
void gem_irc_transport_close(GEM_IRC_TRANSPORT *transport);

/* close and forget the target */
void gem_irc_transport_stop(GEM_IRC_TRANSPORT * transport);
GEM_IRC_WORD gem_irc_transport_is_connected(const GEM_IRC_TRANSPORT *
	transport);
GEM_IRC_WORD gem_irc_transport_is_connecting(const GEM_IRC_TRANSPORT *
	transport);
GEM_IRC_WORD gem_irc_transport_needs_reconnect(const GEM_IRC_TRANSPORT *
	transport);

#endif				/* ELKS_GEM_IRC_TRANSPORT_H */
