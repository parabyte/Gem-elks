/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* fixed IRC application state */

#ifndef ELKS_GEM_IRC_APP_H
#define ELKS_GEM_IRC_APP_H

#include "gem_irc_protocol.h"

#define GEM_IRC_APP_SCROLL_LINES       32U
#define GEM_IRC_APP_DISPLAY_SIZE       96U
#define GEM_IRC_APP_INPUT_SIZE         160U
#define GEM_IRC_APP_TARGET_SIZE        64U
/* bounded lines per poll */
#define GEM_IRC_APP_POLL_DEFAULT       64U
#define GEM_IRC_APP_NICK_SLOTS         32U
#define GEM_IRC_APP_HISTORY_SLOTS      4U

/* one server, one channel and three query buffers */
#define GEM_IRC_APP_BUFFERS            5U
#define GEM_IRC_APP_CHANNEL            1U

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

/* one dirty bit per UI region */
#define GEM_IRC_APP_DIRTY_TRANSCRIPT   0x01U
#define GEM_IRC_APP_DIRTY_INPUT        0x02U
#define GEM_IRC_APP_DIRTY_TARGET       0x04U
#define GEM_IRC_APP_DIRTY_ROSTER       0x08U
#define GEM_IRC_APP_DIRTY_TOPIC        0x10U
#define GEM_IRC_APP_DIRTY_TABS         0x20U
#define GEM_IRC_APP_DIRTY_ALL          0x3fU

struct gem_irc_transport;

typedef struct gem_irc_app_line {
	GEM_IRC_UBYTE kind;
	char text[GEM_IRC_APP_DISPLAY_SIZE];
} GEM_IRC_APP_LINE;

/* one role prefix and nick */
typedef struct gem_irc_app_nick {
	char prefix;
	char text[GEM_IRC_NICK_SIZE];
} GEM_IRC_APP_NICK;

/* one scrollback ring */
typedef struct gem_irc_app_buffer {
	GEM_IRC_UBYTE kind;
	GEM_IRC_UBYTE activity;
	GEM_IRC_UWORD line_head;
	GEM_IRC_UWORD line_count;
	GEM_IRC_UWORD scroll_offset;
	GEM_IRC_UWORD appended;		/* lines ever added, wraps */
	char name[GEM_IRC_APP_TARGET_SIZE];
	char topic[GEM_IRC_APP_DISPLAY_SIZE];
	GEM_IRC_APP_LINE lines[GEM_IRC_APP_SCROLL_LINES];
} GEM_IRC_APP_BUFFER;

/* static near state */
typedef struct gem_irc_app {
	GEM_IRC_CLIENT protocol;
	struct gem_irc_transport *socket;
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
	GEM_IRC_UBYTE input_full;	/* warning shown */
	GEM_IRC_UBYTE history_count;
	GEM_IRC_UBYTE history_head;
	GEM_IRC_UBYTE history_view;
	char input[GEM_IRC_APP_INPUT_SIZE];
	char history[GEM_IRC_APP_HISTORY_SLOTS][GEM_IRC_APP_INPUT_SIZE];
	char network_line[GEM_IRC_LINE_MAX + 1U];
	char command_line[GEM_IRC_APP_INPUT_SIZE];
	char format_line[GEM_IRC_APP_INPUT_SIZE];
	GEM_IRC_APP_NICK nicks[GEM_IRC_APP_NICK_SLOTS];
	GEM_IRC_APP_BUFFER *order[GEM_IRC_APP_BUFFERS];
	GEM_IRC_APP_BUFFER buffers[GEM_IRC_APP_BUFFERS];
} GEM_IRC_APP;

void gem_irc_app_init(GEM_IRC_APP * app, struct gem_irc_transport *socket);
/* set the width of new transcript rows */
void gem_irc_app_set_display_columns(GEM_IRC_APP * app, GEM_IRC_UWORD columns);
void gem_irc_app_connecting(GEM_IRC_APP * app, const char *host);
void gem_irc_app_connection_failed(GEM_IRC_APP * app);
void gem_irc_app_offline(GEM_IRC_APP * app, const char *reason);
GEM_IRC_WORD gem_irc_app_start(GEM_IRC_APP * app, const char *nick,
	const char *user, const char *real_name);
GEM_IRC_WORD gem_irc_app_join(GEM_IRC_APP * app, const char *channel,
	const char *key);
/* queue one message, then add its local echo */
GEM_IRC_WORD gem_irc_app_send_message(GEM_IRC_APP * app,
	const char *target, const char *text);
GEM_IRC_WORD gem_irc_app_request_close(GEM_IRC_APP * app, const char *reason);
void gem_irc_app_disconnect(GEM_IRC_APP * app, const char *reason);

/* take one line directly, or poll at most limit lines */
GEM_IRC_WORD gem_irc_app_receive(GEM_IRC_APP * app,
	const char *line, GEM_IRC_UWORD length);
GEM_IRC_WORD gem_irc_app_poll(GEM_IRC_APP * app, GEM_IRC_UWORD limit);

/* key words carry GEM's scan code in bits 8..15 and ASCII in bits 0..7 */
GEM_IRC_WORD gem_irc_app_key(GEM_IRC_APP * app, GEM_IRC_UWORD key,
	GEM_IRC_UWORD visible_rows);
GEM_IRC_WORD gem_irc_app_submit(GEM_IRC_APP * app);
GEM_IRC_WORD gem_irc_app_set_input(GEM_IRC_APP * app, const char *text);
void gem_irc_app_set_cursor(GEM_IRC_APP * app, GEM_IRC_UWORD cursor);

void gem_irc_app_scroll_up(GEM_IRC_APP * app, GEM_IRC_UWORD amount,
	GEM_IRC_UWORD visible_rows);
void gem_irc_app_scroll_down(GEM_IRC_APP * app, GEM_IRC_UWORD amount,
	GEM_IRC_UWORD visible_rows);
void gem_irc_app_clear(GEM_IRC_APP * app);

GEM_IRC_UWORD gem_irc_app_line_count(const GEM_IRC_APP * app);
/* lines ever added to the active buffer */
GEM_IRC_UWORD gem_irc_app_appended(const GEM_IRC_APP * app);
GEM_IRC_UWORD gem_irc_app_scroll_offset(const GEM_IRC_APP * app);
const GEM_IRC_APP_LINE *gem_irc_app_visible_line(const GEM_IRC_APP * app,
	GEM_IRC_UWORD row, GEM_IRC_UWORD visible_rows);

const char *gem_irc_app_input(const GEM_IRC_APP * app);
GEM_IRC_UWORD gem_irc_app_input_cursor(const GEM_IRC_APP * app);
const char *gem_irc_app_target(const GEM_IRC_APP * app);
const char *gem_irc_app_topic(const GEM_IRC_APP * app);
GEM_IRC_WORD gem_irc_app_is_closing(const GEM_IRC_APP * app);

/* buffer navigation */
GEM_IRC_UWORD gem_irc_app_buffer_count(const GEM_IRC_APP * app);
GEM_IRC_UWORD gem_irc_app_active_index(const GEM_IRC_APP * app);
GEM_IRC_UWORD gem_irc_app_active_kind(const GEM_IRC_APP * app);
const char *gem_irc_app_buffer_label(const GEM_IRC_APP * app,
	GEM_IRC_UWORD index);
GEM_IRC_UWORD gem_irc_app_buffer_activity(const GEM_IRC_APP * app,
	GEM_IRC_UWORD index);
void gem_irc_app_switch(GEM_IRC_APP * app, GEM_IRC_UWORD index);
void gem_irc_app_cycle(GEM_IRC_APP * app, GEM_IRC_WORD direction);
GEM_IRC_WORD gem_irc_app_close_buffer(GEM_IRC_APP * app, GEM_IRC_UWORD index);

/* open a query */
GEM_IRC_WORD gem_irc_app_query(GEM_IRC_APP * app, const char *nick);

/* take and clear the dirty mask */
GEM_IRC_WORD gem_irc_app_take_dirty(GEM_IRC_APP * app);

#endif				/* ELKS_GEM_IRC_APP_H */
