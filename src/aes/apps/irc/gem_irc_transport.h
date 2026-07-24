/*
 * Copyright (C) 2026 Goliath Keet <gatekeeper@xt-emporium.com>
 * SPDX-License-Identifier: GPL-2.0-only
 */

/*
 * gem_irc_transport.h - fixed-buffer ELKS/KTCP transport for native GEM IRC.
 *
 * This is the only interface between the native GEM application and the
 * ELKS socket descriptor.  All public counters, lengths, descriptor values,
 * error values, and IPv4 address pieces are explicit eight- or sixteen-bit
 * quantities.  The IPv4 low/high words are copied in network-memory order;
 * callers must not treat them as a C integer or perform arithmetic on them.
 *
 * Keep GEM_IRC_TRANSPORT in static near data.  Its fixed buffers deliberately
 * trade about 1.8 KiB of data for deterministic behavior: two complete IRC
 * lines can wait for partial socket writes (enough for the immediate NICK/USER
 * registration burst), one complete line can be assembled across reads, and a
 * 128-byte receive chunk can retain bytes following the first completed line.
 * There is no allocation, converted wire object, or helper process.
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

/* Readiness bits returned by the 8086 select(2) ABI seam. */
#define GEM_IRC_TRANSPORT_READY_READ      0x0001U
#define GEM_IRC_TRANSPORT_READY_WRITE     0x0002U
#define GEM_IRC_TRANSPORT_READY_ERROR     0x0004U

typedef struct gem_irc_transport
{
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

void gem_irc_transport_init (GEM_IRC_TRANSPORT * transport);

/*
 * Save host/port, perform the bounded ELKS resolver call, then begin one
 * nonblocking KTCP connect.  PENDING means the AES timer must call progress;
 * a failed attempt retains the target and enters RECONNECT state.
 */
GEM_IRC_WORD gem_irc_transport_connect (GEM_IRC_TRANSPORT * transport,
					const char *host, GEM_IRC_UWORD port);

/* Retry the exact saved target; no DNS/address or command string is widened. */
GEM_IRC_WORD gem_irc_transport_reconnect (GEM_IRC_TRANSPORT * transport);

/*
 * Advance one nonblocking TCP handshake without reading IRC data.  Zero means
 * the handshake is still pending, one means it is complete, and a negative
 * value moves the saved target to RECONNECT.  The AES timer calls this once
 * per tick, outside WIND_UPDATE, so neither drawing nor input waits for TCP.
 */
GEM_IRC_WORD gem_irc_transport_progress (GEM_IRC_TRANSPORT * transport);

/* Fill the app's existing callback ABI without defining a second ABI type. */
void gem_irc_transport_bind_app (GEM_IRC_TRANSPORT * transport,
				 GEM_IRC_APP_TRANSPORT * app_transport);

/* Callback entry points also remain public for small direct smoke harnesses. */
GEM_IRC_WORD gem_irc_transport_write (void *context,
				      const char *line, GEM_IRC_UWORD length);
GEM_IRC_WORD gem_irc_transport_poll (void *context, char *line,
				     GEM_IRC_UWORD capacity,
				     GEM_IRC_UWORD * length);
void gem_irc_transport_close (void *context);

/* Stop permanently and forget the saved target, unlike callback close. */
void gem_irc_transport_stop (GEM_IRC_TRANSPORT * transport);
GEM_IRC_WORD gem_irc_transport_is_connected (const GEM_IRC_TRANSPORT *
					     transport);
GEM_IRC_WORD gem_irc_transport_is_connecting (const GEM_IRC_TRANSPORT *
					      transport);
GEM_IRC_WORD gem_irc_transport_needs_reconnect (const GEM_IRC_TRANSPORT *
						transport);
GEM_IRC_UWORD gem_irc_transport_last_errno (const GEM_IRC_TRANSPORT *
					    transport);
GEM_IRC_UWORD gem_irc_transport_attempts (const GEM_IRC_TRANSPORT *
					  transport);

/*
 * Small-model 8086 ABI seam.  in_gethostbyname() returns the unavoidable
 * four-byte IPv4 value in DX:AX; resolve stores those halves separately.
 * connect_ipv4 constructs the eight-byte sockaddr_in record in assembly.
 * select_ready likewise owns ELKS's four-byte fd_set and eight-byte timeval,
 * returning only the three sixteen-bit readiness flags above.
 */
GEM_IRC_WORD gem_irc_elks_resolve_ipv4 (const char *host,
					GEM_IRC_UWORD * address_lo,
					GEM_IRC_UWORD * address_hi);
GEM_IRC_WORD gem_irc_elks_connect_ipv4 (GEM_IRC_WORD descriptor,
					GEM_IRC_UWORD address_lo,
					GEM_IRC_UWORD address_hi,
					GEM_IRC_UWORD port);
GEM_IRC_WORD gem_irc_elks_select_ready (GEM_IRC_WORD descriptor,
					GEM_IRC_UWORD want_write);

#endif /* ELKS_GEM_IRC_TRANSPORT_H */
