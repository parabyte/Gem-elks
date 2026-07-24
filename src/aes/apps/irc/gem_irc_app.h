/*
 * Copyright (C) 2026 Goliath Keet <gatekeeper@xt-emporium.com>
 * SPDX-License-Identifier: GPL-2.0-only
 */

/*
 * gem_irc_app.h - fixed-buffer native GEM IRC application state.
 *
 * The application core sits directly between a GEM window controller and a
 * nonblocking ELKS line transport.  It owns no descriptor and starts no helper
 * process.  The injected transport accepts or returns one complete IRC line,
 * allowing the socket implementation to remain a small POSIX boundary while
 * keyboard editing, slash commands, protocol state, and scrollback stay native
 * GEM code.
 *
 * All lengths are unsigned 16-bit byte or line counts with scale 1.  Long
 * transcript text is split across fixed display records; input text which
 * exceeds its fixed slot is rejected.  Every record has an explicit
 * terminator and no count wraps.  The GEM front end supplies the current
 * transcript column count so received passages remain visible without a
 * heap-backed text object.
 */

#ifndef ELKS_GEM_IRC_APP_H
#define ELKS_GEM_IRC_APP_H

#include "gem_irc_protocol.h"

/*
 * Scrollback is now owned per conversation buffer.  Thirty-two 97-byte rows
 * cost 3104 bytes per buffer; five buffers therefore hold 160 total rows,
 * four times the former single ring, while every count remains a scale-one
 * unsigned 16-bit line index that cannot wrap its bounded array.
 */
#define GEM_IRC_APP_SCROLL_LINES       32U
#define GEM_IRC_APP_DISPLAY_SIZE       96U
#define GEM_IRC_APP_INPUT_SIZE         160U
#define GEM_IRC_APP_TARGET_SIZE        64U
/*
 * Drain at most 64 complete wire lines per 100 ms UI timer.  The scale is one
 * line per count, no rounding occurs, and the unsigned 16-bit loop cannot
 * overflow because it stops at this literal bound.  This covers most server
 * registration bursts before their first expensive XT redraw while retaining
 * a finite yield point for window and keyboard events.
 */
#define GEM_IRC_APP_POLL_DEFAULT       64U
#define GEM_IRC_APP_NICK_SLOTS         12U

/*
 * One channel/status buffer plus up to four private-message buffers.  Each
 * incoming PRIVMSG addressed to our own nick opens (or reuses) one bounded
 * PRIVATE buffer keyed by the sender; a full table routes overflow back to
 * the main buffer instead of allocating.  The count is a scale-one buffer
 * index; buffer zero is permanent and is never freed or moved.
 */
#define GEM_IRC_APP_BUFFERS            5U

#define GEM_IRC_APP_BUFFER_FREE        0U
#define GEM_IRC_APP_BUFFER_SERVER      1U
#define GEM_IRC_APP_BUFFER_CHANNEL     2U
#define GEM_IRC_APP_BUFFER_PRIVATE     3U

#define GEM_IRC_APP_LINE_NORMAL        0U
#define GEM_IRC_APP_LINE_STATUS        1U
#define GEM_IRC_APP_LINE_ACTION        2U
#define GEM_IRC_APP_LINE_NOTICE        3U
#define GEM_IRC_APP_LINE_ERROR         4U

#define GEM_IRC_APP_KEY_UNHANDLED      0
#define GEM_IRC_APP_KEY_HANDLED        1
#define GEM_IRC_APP_KEY_SUBMITTED      2
#define GEM_IRC_APP_KEY_CLOSE          3

/*
 * Damage bits have scale one changed UI region per bit.  Several bits may be
 * combined in the eight-bit dirty field; no counter, rounding, or saturation
 * is involved.  The GEM front end consumes and clears the complete mask once
 * per event-loop turn, then submits only those rectangles to VDI.
 */
#define GEM_IRC_APP_DIRTY_TRANSCRIPT   0x01U
#define GEM_IRC_APP_DIRTY_INPUT        0x02U
#define GEM_IRC_APP_DIRTY_TARGET       0x04U
#define GEM_IRC_APP_DIRTY_ROSTER       0x08U
#define GEM_IRC_APP_DIRTY_TOPIC        0x10U
#define GEM_IRC_APP_DIRTY_TABS         0x20U
#define GEM_IRC_APP_DIRTY_ALL          0x3fU

typedef GEM_IRC_WORD (*GEM_IRC_APP_WRITE) (void *context,
					   const char *line,
					   GEM_IRC_UWORD length);

/*
 * poll is nonblocking: positive returns one complete line, zero means no
 * input, and a negative value means that the connection has failed or closed.
 * capacity is always 512 in the current app; *length must not exceed it.
 */
typedef GEM_IRC_WORD (*GEM_IRC_APP_POLL) (void *context, char *line,
					  GEM_IRC_UWORD capacity,
					  GEM_IRC_UWORD * length);

typedef void (*GEM_IRC_APP_CLOSE) (void *context);

typedef struct gem_irc_app_transport
{
  GEM_IRC_APP_WRITE write;
  GEM_IRC_APP_POLL poll;
  GEM_IRC_APP_CLOSE close;
  void *context;
} GEM_IRC_APP_TRANSPORT;

typedef struct gem_irc_app_line
{
  GEM_IRC_UBYTE kind;
  char text[GEM_IRC_APP_DISPLAY_SIZE];
} GEM_IRC_APP_LINE;

/*
 * One bounded roster cell holds one IRC nickname and its terminator.  The
 * twelve-cell roster costs 384 bytes in the client's static near data.  A
 * server which reports more names sets nick_overflow; no slot is overwritten
 * and the window renders an explicit "+ more" marker.
 */
typedef struct gem_irc_app_nick
{
  char text[GEM_IRC_NICK_SIZE];
} GEM_IRC_APP_NICK;

/*
 * One conversation buffer: a bounded scrollback ring with its own target
 * name, retained topic, unread-activity marker, and scroll viewport.  kind
 * selects FREE, SERVER, CHANNEL, or PRIVATE.  Buffer zero is the permanent
 * server/channel buffer whose roster is the app-global nick table; PRIVATE
 * buffers carry no roster.  Each buffer is 3272 near bytes and holds no
 * pointer, so a whole buffer may be moved by plain structure assignment when
 * a private tab is closed.
 */
typedef struct gem_irc_app_buffer
{
  GEM_IRC_UBYTE kind;
  GEM_IRC_UBYTE activity;
  GEM_IRC_UWORD line_head;
  GEM_IRC_UWORD line_count;
  GEM_IRC_UWORD scroll_offset;
  char name[GEM_IRC_APP_TARGET_SIZE];
  char topic[GEM_IRC_APP_DISPLAY_SIZE];
  GEM_IRC_APP_LINE lines[GEM_IRC_APP_SCROLL_LINES];
} GEM_IRC_APP_BUFFER;

/*
 * Keep this object in static near data.  Its largest members are the five
 * bounded conversation buffers, followed by the protocol's two 513-byte line
 * buffers.  Nothing in the native app is placed on the small ELKS heap.  The
 * active index selects the drawn buffer; the roster stays app-global because
 * only buffer zero is ever a channel in this port.
 */
typedef struct gem_irc_app
{
  GEM_IRC_CLIENT protocol;
  GEM_IRC_APP_TRANSPORT transport;
  GEM_IRC_UBYTE connected;
  GEM_IRC_UBYTE closing;
  GEM_IRC_UBYTE dirty;
  GEM_IRC_UBYTE active;
  GEM_IRC_UBYTE buffer_count;
  GEM_IRC_UWORD display_columns;
  GEM_IRC_UWORD input_length;
  GEM_IRC_UWORD input_cursor;
  GEM_IRC_UBYTE nick_count;
  GEM_IRC_UBYTE nick_overflow;
  char input[GEM_IRC_APP_INPUT_SIZE];
  char last_input[GEM_IRC_APP_INPUT_SIZE];
  char network_line[GEM_IRC_LINE_MAX + 1U];
  char command_line[GEM_IRC_APP_INPUT_SIZE];
  char format_line[GEM_IRC_APP_INPUT_SIZE];
  GEM_IRC_APP_NICK nicks[GEM_IRC_APP_NICK_SLOTS];
  GEM_IRC_APP_BUFFER buffers[GEM_IRC_APP_BUFFERS];
} GEM_IRC_APP;

void gem_irc_app_init (GEM_IRC_APP * app,
		       const GEM_IRC_APP_TRANSPORT * transport);
/*
 * Set the scale-one byte columns stored in each future transcript row.
 * Values saturate at 95, the fixed record payload.  Zero selects one column.
 * Existing rows are left intact, so a resize performs no expensive reflow.
 */
void gem_irc_app_set_display_columns (GEM_IRC_APP * app,
				      GEM_IRC_UWORD columns);
/* Publish transport progress before the first bounded resolver call. */
void gem_irc_app_connecting (GEM_IRC_APP * app, const char *host);
void gem_irc_app_connection_failed (GEM_IRC_APP * app);
GEM_IRC_WORD gem_irc_app_start (GEM_IRC_APP * app, const char *nick,
				const char *user, const char *real_name);
GEM_IRC_WORD gem_irc_app_join (GEM_IRC_APP * app, const char *channel,
			       const char *key);
/*
 * Queue one channel or private message and append its local echo only after
 * the complete IRC line has been accepted by the transport.  A caller may
 * therefore use a positive return as an at-most-once acceptance latch.
 */
GEM_IRC_WORD gem_irc_app_send_message (GEM_IRC_APP * app,
				       const char *target, const char *text);
GEM_IRC_WORD gem_irc_app_request_close (GEM_IRC_APP * app,
					const char *reason);
void gem_irc_app_disconnect (GEM_IRC_APP * app, const char *reason);

/* Receive directly or poll at most limit complete lines without UI starvation. */
GEM_IRC_WORD gem_irc_app_receive (GEM_IRC_APP * app,
				  const char *line, GEM_IRC_UWORD length);
GEM_IRC_WORD gem_irc_app_poll (GEM_IRC_APP * app, GEM_IRC_UWORD limit);

/* Keyboard words retain GEM's scan byte in bits 8..15 and ASCII in 0..7. */
GEM_IRC_WORD gem_irc_app_key (GEM_IRC_APP * app, GEM_IRC_UWORD key,
			      GEM_IRC_UWORD visible_rows);
GEM_IRC_WORD gem_irc_app_submit (GEM_IRC_APP * app);

void gem_irc_app_scroll_up (GEM_IRC_APP * app, GEM_IRC_UWORD amount,
			    GEM_IRC_UWORD visible_rows);
void gem_irc_app_scroll_down (GEM_IRC_APP * app, GEM_IRC_UWORD amount,
			      GEM_IRC_UWORD visible_rows);
void gem_irc_app_clear (GEM_IRC_APP * app);

GEM_IRC_UWORD gem_irc_app_line_count (const GEM_IRC_APP * app);
GEM_IRC_UWORD gem_irc_app_visible_count (const GEM_IRC_APP * app,
					 GEM_IRC_UWORD visible_rows);
const GEM_IRC_APP_LINE *gem_irc_app_visible_line (const GEM_IRC_APP * app,
						  GEM_IRC_UWORD row,
						  GEM_IRC_UWORD visible_rows);

const char *gem_irc_app_input (const GEM_IRC_APP * app);
GEM_IRC_UWORD gem_irc_app_input_cursor (const GEM_IRC_APP * app);
const char *gem_irc_app_target (const GEM_IRC_APP * app);
const char *gem_irc_app_topic (const GEM_IRC_APP * app);
GEM_IRC_WORD gem_irc_app_is_closing (const GEM_IRC_APP * app);

/*
 * Conversation-buffer navigation for the tab bar.  Indexes have scale one
 * buffer and are always below the current count.  Switching or cycling clears
 * the destination's unread marker and repaints the whole work area; closing a
 * PRIVATE buffer compacts the table and never removes buffer zero.  Labels are
 * the display name ("Server", a channel, or a nick) shown on each tab.
 */
GEM_IRC_UWORD gem_irc_app_buffer_count (const GEM_IRC_APP * app);
GEM_IRC_UWORD gem_irc_app_active_index (const GEM_IRC_APP * app);
GEM_IRC_UWORD gem_irc_app_active_kind (const GEM_IRC_APP * app);
const char *gem_irc_app_buffer_label (const GEM_IRC_APP * app,
				      GEM_IRC_UWORD index);
GEM_IRC_UWORD gem_irc_app_buffer_kind (const GEM_IRC_APP * app,
				       GEM_IRC_UWORD index);
GEM_IRC_UWORD gem_irc_app_buffer_activity (const GEM_IRC_APP * app,
					   GEM_IRC_UWORD index);
void gem_irc_app_switch (GEM_IRC_APP * app, GEM_IRC_UWORD index);
void gem_irc_app_cycle (GEM_IRC_APP * app, GEM_IRC_WORD direction);
GEM_IRC_WORD gem_irc_app_close_buffer (GEM_IRC_APP * app, GEM_IRC_UWORD index);

/* Toolbar actions: open a query tab, part or request names on the channel. */
GEM_IRC_WORD gem_irc_app_query (GEM_IRC_APP * app, const char *nick);
GEM_IRC_WORD gem_irc_app_part_active (GEM_IRC_APP * app);
GEM_IRC_WORD gem_irc_app_names (GEM_IRC_APP * app);

/* Return and clear the combined scale-one UI-region damage mask. */
GEM_IRC_WORD gem_irc_app_take_dirty (GEM_IRC_APP * app);

#endif /* ELKS_GEM_IRC_APP_H */
