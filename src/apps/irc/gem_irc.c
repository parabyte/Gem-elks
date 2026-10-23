/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* native GEM front end for Antler IRC */

#include "aes.h"
#include "gem_irc_app.h"
#include "gem_irc_layout.h"
#include "gem_irc_transport.h"

#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#define IRC_WINDOW_KIND (NAME | CLOSER | FULLER | MOVER | SIZER \
	| UPARROW | DNARROW)
#define IRC_TIMER_BUSY_MS          100U	/* while lines are arriving */
#define IRC_TIMER_IDLE_MS          300U	/* after a few quiet polls */
#define IRC_QUIET_POLLS            10U
#define IRC_POLL_TICKS             2U	/* ticks between socket polls */
#define IRC_QUIET_POLL_TICKS       6U	/* the same once polls find nothing */
#define IRC_DRAW_TICKS             6U	/* between redraws as lines pour in */
#define IRC_POLL_LINES             32U	/* lines taken per poll */
#define IRC_KEY_BURST              16U	/* keys taken in one busy pass */
#define IRC_ECHO_TICKS             2U	/* between echoes while keys come */
#define IRC_ECHO_MS                110U	/* the same, for the event timer */
#define IRC_RECONNECT_TICKS        50U
#define IRC_CONNECT_TICKS          150U
#define IRC_QUIT_FLUSH_TICKS       10U
#define IRC_SMOKE_CONNECT_TICKS    600U
#define IRC_SMOKE_DISPLAY_TICKS    20U
#define IRC_DEFAULT_PORT           6667U
#define IRC_DRAW_TEXT_BYTES        80U
#define IRC_UI_DIRTY_MENU          0x0100U
#define IRC_MENU_NONE              0U
#define IRC_MENU_FILE              1U
#define IRC_MENU_CHANNEL           2U
#define IRC_MENU_USER              3U
#define IRC_MENU_OPTIONS           4U
#define IRC_MENU_HELP              5U
#define IRC_MENU_COUNT             5U
#define IRC_DIALOG_TEXT_BYTES      128U

#define IRC_ACTION_RECONNECT       1U
#define IRC_ACTION_DISCONNECT      2U
#define IRC_ACTION_QUIT            3U
#define IRC_ACTION_JOIN            4U
#define IRC_ACTION_PART            5U
#define IRC_ACTION_TOPIC           6U
#define IRC_ACTION_NAMES           7U
#define IRC_ACTION_CLEAR           8U
#define IRC_ACTION_QUERY           9U
#define IRC_ACTION_MESSAGE         10U
#define IRC_ACTION_WHOIS           11U
#define IRC_ACTION_KICK            12U
#define IRC_ACTION_NEXT            13U
#define IRC_ACTION_PREVIOUS        14U
#define IRC_ACTION_CLOSE_TAB       15U
#define IRC_ACTION_COMMANDS        16U
#define IRC_ACTION_SHORTCUTS       17U
#define IRC_ACTION_ABOUT           18U
#define IRC_ACTION_AWAY            19U
#define IRC_ACTION_BACK            20U
#define IRC_ACTION_OP              21U
#define IRC_ACTION_DEOP            22U
#define IRC_ACTION_VOICE           23U
#define IRC_ACTION_DEVOICE         24U
#define IRC_ACTION_CONNECT         25U
#define IRC_ACTION_NICK            26U

typedef struct irc_menu_item {
	const char *label;
	UBYTE action;
} IRC_MENU_ITEM;

typedef struct irc_menu_desc {
	const char *label;
	const IRC_MENU_ITEM *items;
	UBYTE count;
	UBYTE columns;
} IRC_MENU_DESC;

static const IRC_MENU_ITEM irc_file_items[] = {
	{ "Connect...", IRC_ACTION_CONNECT },
	{ "Reconnect", IRC_ACTION_RECONNECT },
	{ "Disconnect", IRC_ACTION_DISCONNECT },
	{ "Quit", IRC_ACTION_QUIT }
};
static const IRC_MENU_ITEM irc_channel_items[] = {
	{ "Join...", IRC_ACTION_JOIN }, { "Part", IRC_ACTION_PART },
	{ "Topic...", IRC_ACTION_TOPIC }, { "Names", IRC_ACTION_NAMES },
	{ "Clear", IRC_ACTION_CLEAR }
};
static const IRC_MENU_ITEM irc_user_items[] = {
	{ "Query...", IRC_ACTION_QUERY }, { "Message...", IRC_ACTION_MESSAGE },
	{ "Whois", IRC_ACTION_WHOIS }, { "Op", IRC_ACTION_OP },
	{ "DeOp", IRC_ACTION_DEOP }, { "Voice", IRC_ACTION_VOICE },
	{ "DeVoice", IRC_ACTION_DEVOICE }, { "Kick...", IRC_ACTION_KICK }
};
static const IRC_MENU_ITEM irc_options_items[] = {
	{ "Nickname...", IRC_ACTION_NICK },
	{ "Away...", IRC_ACTION_AWAY }, { "Back", IRC_ACTION_BACK },
	{ "Next", IRC_ACTION_NEXT }, { "Previous", IRC_ACTION_PREVIOUS },
	{ "Close tab", IRC_ACTION_CLOSE_TAB }
};
static const IRC_MENU_ITEM irc_help_items[] = {
	{ "Commands", IRC_ACTION_COMMANDS },
	{ "Shortcuts", IRC_ACTION_SHORTCUTS },
	{ "About", IRC_ACTION_ABOUT }
};
static const IRC_MENU_DESC irc_menus[IRC_MENU_COUNT] = {
	{ "File", irc_file_items, 4, 12 },
	{ "Channel", irc_channel_items, 5, 10 },
	{ "User", irc_user_items, 8, 12 },
	{ "Options", irc_options_items, 6, 12 },
	{ "Help", irc_help_items, 3, 11 }
};

static WORD irc_application = -1;
static WORD irc_vdi = -1;
static WORD irc_window = -1;
static WORD cw, ch;			/* character cell */
static WORD irc_work_out[57];
static WORD irc_message[8];
static GRECT irc_desktop;
static GRECT irc_previous;
static WORD irc_was_full;

/* the layout, from the work area: see irc_layout() */
static WORD wkx, wky, wkw, wkh;
static WORD text_x;			/* transcript and input text, byte-aligned */
static WORD menu_y, menu_h, tree_x, tree_right, tree_columns;
static WORD topic_y, topic_h, body_y, input_y, rows, edit_x;
static WORD pane_x;			/* nick list text, byte-aligned */
static WORD columns, pane_columns, input_columns;
static WORD tree_row[GEM_IRC_APP_BUFFERS];
static WORD menu_x[IRC_MENU_COUNT + 1U];

static GEM_IRC_APP irc_app;
static GEM_IRC_TRANSPORT irc_transport;

static UBYTE irc_join_sent;
static UBYTE irc_registration_started;
static UBYTE irc_smoke_enabled;
static UBYTE irc_smoke_sent;
static UWORD irc_connect_ticks;
static UWORD irc_reconnect_ticks;
static UWORD irc_quit_ticks;
static UWORD irc_smoke_ticks;
static UWORD irc_quiet_polls;
static UWORD irc_poll_tick;		/* last socket poll */
static UWORD irc_draw_tick;		/* and of the last transcript draw */
static UWORD irc_echo_tick;		/* and of the last input line draw */
static UWORD irc_pending;		/* changes not drawn yet */
static UBYTE irc_menu;
static UBYTE irc_manual_offline;
static UBYTE irc_roster_scroll;
static UBYTE irc_damage_all;
static UBYTE irc_show_connect;
static BYTE irc_title[] = "IRC Chat";
static BYTE irc_server[GEM_IRC_TRANSPORT_HOST_SIZE];
static BYTE irc_channel[GEM_IRC_APP_TARGET_SIZE];
static BYTE irc_nick[GEM_IRC_NICK_SIZE];
static BYTE irc_user[GEM_IRC_NICK_SIZE];
static BYTE irc_real_name[64];
static BYTE irc_smoke_message[GEM_IRC_APP_INPUT_SIZE];
static BYTE irc_selected_nick[GEM_IRC_NICK_SIZE];
static BYTE irc_ui_command[GEM_IRC_APP_INPUT_SIZE];
static UWORD irc_port;

static VOID irc_user_reconnect(VOID);
static VOID irc_user_disconnect(VOID);
static WORD irc_network_step(VOID);

/* use up one 100 ms tick, counts stop at zero */
static WORD
irc_timer_elapsed(UWORD *ticks)
{
	if (*ticks > 1U) {
		(*ticks)--;
		return FALSE;
	}
	*ticks = 0;
	return TRUE;
}

static WORD
irc_copy(BYTE *destination, UWORD capacity, const BYTE *source)
{
	UWORD length;
	UWORD index;

	if (!destination || !capacity || !source)
		return FALSE;
	length = 0;
	while (source[length]) {
		if (length + 1U >= capacity)
			return FALSE;
		length++;
	}
	index = 0;
	while (index < length) {
		destination[index] = source[index];
		index++;
	}
	destination[index] = '\0';
	return TRUE;
}

/* GEM plus the pid */
static VOID
irc_make_default_nick(VOID)
{
	UWORD value;
	WORD i;
	static const char digits[] = "0123456789ABCDEF";

	value = (UWORD) getpid();
	irc_nick[0] = 'G';
	irc_nick[1] = 'E';
	irc_nick[2] = 'M';
	for (i = 6; i >= 3; i--) {
		irc_nick[i] = digits[value & 15U];
		value >>= 4;
	}
	irc_nick[7] = '\0';
}

/* parse a decimal port with overflow checks */
static WORD
irc_parse_port(const BYTE *text, UWORD *port)
{
	UWORD value;
	UWORD digit;

	if (!text || !*text || !port)
		return FALSE;
	value = 0;
	while (*text) {
		if (*text < '0' || *text > '9')
			return FALSE;
		digit = (UWORD) (*text++ - '0');
		if (value > 6553U || (value == 6553U && digit > 5U))
			return FALSE;
		value = (UWORD) ((value << 3) + (value << 1) + digit);
	}
	if (!value)
		return FALSE;
	*port = value;
	return TRUE;
}

static WORD
irc_configuration(WORD argc, BYTE **argv)
{
	const BYTE *server;
	const BYTE *channel;
	const BYTE *nick;
	const BYTE *port_text;

	server = (const BYTE *) "irc.libera.chat";
	channel = (const BYTE *) "#osdev";
	nick = (const BYTE *) 0;
	port_text = (const BYTE *) 0;
	irc_smoke_enabled = 0;
	irc_smoke_sent = 0;
	irc_smoke_ticks = 0;
	irc_smoke_message[0] = '\0';
	irc_show_connect = argc == 1;
	if (argc > 1 && argv[1] && argv[1][0])
		server = argv[1];
	if (argc > 2 && argv[2] && argv[2][0])
		channel = argv[2];
	if (argc > 3 && argv[3] && argv[3][0])
		nick = argv[3];
	if (argc > 4 && argv[4] && argv[4][0])
		port_text = argv[4];
	if (argc > 5) {
		/* normal launches cannot post smoke text */
		if (argc != 7 || !argv[5]
			|| strcmp((const char *) argv[5], "--smoke-once")
			|| !argv[6] || !argv[6][0]
			|| !irc_copy(irc_smoke_message, GEM_IRC_APP_INPUT_SIZE,
				argv[6]))
			return FALSE;
		irc_smoke_enabled = 1;
		irc_smoke_ticks = IRC_SMOKE_CONNECT_TICKS;
	}
	if (!irc_copy(irc_server, GEM_IRC_TRANSPORT_HOST_SIZE, server)
		|| !irc_copy(irc_channel, GEM_IRC_APP_TARGET_SIZE, channel)
		|| !irc_copy(irc_real_name, 64U,
			(const BYTE *) "Antler IRC Chat"))
		return FALSE;
	if (nick) {
		if (!irc_copy(irc_nick, GEM_IRC_NICK_SIZE, nick))
			return FALSE;
	} else {
		irc_make_default_nick();
	}
	if (!irc_copy(irc_user, GEM_IRC_NICK_SIZE, irc_nick))
		return FALSE;
	irc_port = IRC_DEFAULT_PORT;
	if (port_text && !irc_parse_port(port_text, &irc_port))
		return FALSE;
	return TRUE;
}

/* text columns before limit */
static WORD
irc_fit(WORD x, WORD limit, WORD maximum)
{
	WORD n = (limit - x) / cw;

	if (n > maximum)
		n = maximum;
	return n < 1 ? 1 : n;
}

static VOID
irc_layout(VOID)
{
	WORD xy[4];
	WORD i, x;

	wind_get(irc_window, WF_WXYWH, &wkx, &wky, &wkw, &wkh);
	tree_x = (wkx + 4 + 7) & ~7;
	tree_right = tree_x + GEM_IRC_LAYOUT_TREE_WIDTH;
	tree_columns = (GEM_IRC_LAYOUT_TREE_WIDTH - 16) / cw;
	text_x = (tree_right + 8 + 7) & ~7;
	menu_y = wky;
	menu_h = ch + 4;
	topic_y = menu_y + menu_h + 2;
	topic_h = ch + 4;
	body_y = topic_y + topic_h + 2;
	input_y = wky + wkh - ch - 6;
	rows = (input_y - 2 - body_y) / ch;
	if (rows > (WORD) GEM_IRC_APP_SCROLL_LINES)
		rows = GEM_IRC_APP_SCROLL_LINES;
	if (rows < 1)
		rows = 1;
	pane_x = (wkx + wkw - GEM_IRC_LAYOUT_SIDE_WIDTH + 7) & ~7;
	columns = irc_fit(text_x, pane_x - 12, GEM_IRC_APP_DISPLAY_SIZE - 1);
	pane_columns = irc_fit(pane_x, wkx + wkw - 2, GEM_IRC_NICK_SIZE - 1);
	edit_x = text_x + 9 * cw;
	input_columns = irc_fit(edit_x, wkx + wkw - 4,
		IRC_DRAW_TEXT_BYTES - 2);
	x = tree_x;
	for (i = 0; i < (WORD) IRC_MENU_COUNT; i++) {
		menu_x[i] = x;
		x += ((WORD) strlen(irc_menus[i].label) + 2) * cw;
	}
	menu_x[IRC_MENU_COUNT] = x;
	gem_irc_app_set_display_columns(&irc_app, (GEM_IRC_UWORD) columns);
	/* clip to the work area */
	xy[0] = wkx;
	xy[1] = wky;
	xy[2] = wkx + wkw - 1;
	xy[3] = wky + wkh - 1;
	vs_clip(irc_vdi, TRUE, xy);
}

static WORD
irc_line_color(UBYTE kind)
{
	if (kind == GEM_IRC_APP_LINE_ERROR)
		return RED;
	if (kind == GEM_IRC_APP_LINE_ACTION)
		return GREEN;
	if (kind == GEM_IRC_APP_LINE_NOTICE || kind == GEM_IRC_APP_LINE_STATUS)
		return BLUE;
	return BLACK;
}

static VOID
irc_vdi_fill(WORD x, WORD y, WORD width, WORD height, WORD color)
{
	WORD xy[4];

	if (width <= 0 || height <= 0)
		return;
	xy[0] = x;
	xy[1] = y;
	xy[2] = x + width - 1;
	xy[3] = y + height - 1;
	(void) vsf_interior(irc_vdi, FIS_SOLID);
	(void) vsf_color(irc_vdi, color);
	(void) vr_recfl(irc_vdi, xy);
}

static VOID
irc_vdi_line(WORD x1, WORD y1, WORD x2, WORD y2, WORD color)
{
	WORD xy[4];

	xy[0] = x1;
	xy[1] = y1;
	xy[2] = x2;
	xy[3] = y2;
	(void) vsl_color(irc_vdi, color);
	(void) v_pline(irc_vdi, 2, xy);
}

static VOID
irc_vdi_text(WORD x, WORD y, WORD color, WORD effects, const char *text,
	WORD length)
{
	static BYTE buffer[IRC_DRAW_TEXT_BYTES + 1U];
	WORD index;

	if (!text || length <= 0)
		return;
	if (length > IRC_DRAW_TEXT_BYTES)
		length = IRC_DRAW_TEXT_BYTES;
	index = 0;
	while (index < length) {
		buffer[index] = (BYTE) text[index];
		index++;
	}
	buffer[index] = 0;
	(void) vst_color(irc_vdi, color);
	(void) vst_effects(irc_vdi, effects);
	(void) v_gtext(irc_vdi, x, y, buffer);
}

/* text clipped to n columns */
static VOID
irc_text(WORD x, WORD y, WORD color, const char *s, WORD n)
{
	WORD length = 0;

	while (length < n && s[length])
		length++;
	irc_vdi_text(x, y, color, 0, s, length);
}

static VOID
irc_draw_menu_bar(VOID)
{
	WORD index, active;

	irc_vdi_fill(wkx, menu_y, wkw, menu_h, WHITE);
	for (index = 0; index < (WORD) IRC_MENU_COUNT; index++) {
		active = irc_menu == (UBYTE) (index + 1);
		if (active)
			irc_vdi_fill(menu_x[index], menu_y,
				menu_x[index + 1] - menu_x[index], menu_h, BLACK);
		irc_text(menu_x[index] + cw, menu_y + 2,
			active ? WHITE : BLACK, irc_menus[index].label, 12);
	}
	irc_vdi_line(wkx, menu_y + menu_h - 1, wkx + wkw - 1,
		menu_y + menu_h - 1, BLACK);
}

static VOID
irc_draw_menu_popup(VOID)
{
	const IRC_MENU_DESC *menu;
	const IRC_MENU_ITEM *item;
	WORD index;
	WORD x;
	WORD y;
	WORD width;
	WORD height;

	if (!irc_menu || irc_menu > IRC_MENU_COUNT)
		return;
	menu = &irc_menus[irc_menu - 1U];
	x = menu_x[irc_menu - 1U];
	y = menu_y + menu_h;
	width = ((WORD) menu->columns + 2) * cw;
	height = (WORD) menu->count * (ch + 4) + 4;
	irc_vdi_fill(x, y, width, height, WHITE);
	irc_vdi_line(x, y, x + width - 1, y, BLACK);
	irc_vdi_line(x, y, x, y + height - 1, BLACK);
	irc_vdi_line(x + width - 1, y, x + width - 1, y + height - 1, BLACK);
	irc_vdi_line(x, y + height - 1, x + width - 1, y + height - 1, BLACK);
	y += 2;
	item = menu->items;
	for (index = 0; index < (WORD) menu->count; index++, item++) {
		irc_text(x + cw, y + 2, BLACK, item->label, menu->columns);
		y += ch + 4;
	}
}

/* server tree with channel and query children */
static VOID
irc_draw_tree(VOID)
{
	const GEM_IRC_APP_BUFFER *buffer;
	const char *name;
	WORD i, y, x, color, close_x, close_y;

	irc_vdi_fill(wkx, topic_y, tree_right - wkx, wky + wkh - topic_y, WHITE);
	y = topic_y + 2;
	for (i = 0; i < (WORD) GEM_IRC_APP_BUFFERS; i++) {
		tree_row[i] = -1;
		if (i >= irc_app.buffer_count)
			continue;
		buffer = irc_app.order[i];
		if (buffer->kind == GEM_IRC_APP_BUFFER_FREE)
			continue;
		tree_row[i] = y;
		color = i == irc_app.active ? WHITE : BLACK;
		if (i == irc_app.active)
			irc_vdi_fill(tree_x, y - 1, tree_right - tree_x - 2, ch + 2, BLACK);
		x = tree_x + (i ? 16 : 0);
		if (i) {
			irc_vdi_line(tree_x + 4, y, tree_x + 4, y + ch / 2, color);
			irc_vdi_line(tree_x + 4, y + ch / 2, tree_x + 10, y + ch / 2, color);
		}
		name = i ? buffer->name : (const char *) irc_server;
		close_x = tree_right - GEM_IRC_LAYOUT_CLOSE_WIDTH;
		irc_text(x, y, color, name,
			irc_fit(x, close_x - 5, tree_columns));
		if (buffer->activity)
			irc_vdi_fill(close_x - 4, y + 2, 3, 3, color);
		if (i) {
			close_y = y + (ch - 5) / 2;
			irc_vdi_line(close_x + 2, close_y, close_x + 6, close_y + 4, color);
			irc_vdi_line(close_x + 6, close_y, close_x + 2, close_y + 4, color);
		}
		y += ch + 4;
	}
	name = irc_manual_offline ? "Offline" :
		irc_app.protocol.registered ? "Online" : "Connecting";
	irc_text(tree_x, input_y + 3, BLACK, name, tree_columns);
}

/* topic above the transcript */
static VOID
irc_draw_topic(VOID)
{
	const char *topic = gem_irc_app_topic(&irc_app);

	if (!topic || !*topic)
		topic = gem_irc_app_target(&irc_app);
	if (!topic || !*topic)
		topic = (const char *) irc_server;
	irc_vdi_fill(tree_right + 1, topic_y, pane_x - tree_right - 7, topic_h, WHITE);
	irc_text(text_x, topic_y + 2, BLACK, topic, columns);
	irc_vdi_line(tree_right + 1, topic_y + topic_h, pane_x - 6,
		topic_y + topic_h, BLACK);
}

/* draw rows first through last minus one */
static VOID
irc_draw_rows(WORD first, WORD last)
{
	const GEM_IRC_APP_LINE *line;
	WORD row;

	irc_vdi_fill(text_x, body_y + first * ch, columns * cw, (last - first) * ch,
		WHITE);
	for (row = first; row < last; row++) {
		line = gem_irc_app_visible_line(&irc_app, (GEM_IRC_UWORD) row,
			(GEM_IRC_UWORD) rows);
		if (line)
			irc_text(text_x, body_y + row * ch,
				irc_line_color(line->kind), line->text, columns);
	}
}

/* draw the transcript from its model */
static VOID
irc_draw_transcript(VOID)
{
	irc_draw_rows(0, rows);
}

static VOID
irc_draw_nicks(VOID)
{
	const GEM_IRC_APP_NICK *nick;
	char prefix[2];
	WORD row, limit, y, arrow_x, maximum, selected;
	UWORD index;

	irc_vdi_fill(pane_x - 4, topic_y, wkx + wkw - (pane_x - 4), input_y - topic_y,
		WHITE);
	if (gem_irc_app_active_kind(&irc_app) != GEM_IRC_APP_BUFFER_CHANNEL) {
		irc_roster_scroll = 0;
		return;
	}
	irc_text(pane_x, topic_y + 2, BLACK, "Users", pane_columns - 2);
	arrow_x = wkx + wkw - 2 * cw - 2;
	irc_vdi_line(arrow_x + 1, topic_y + ch, arrow_x + 4, topic_y + 3, BLACK);
	irc_vdi_line(arrow_x + 4, topic_y + 3, arrow_x + 7, topic_y + ch, BLACK);
	irc_vdi_line(arrow_x + cw + 1, topic_y + 3,
		arrow_x + cw + 4, topic_y + ch, BLACK);
	irc_vdi_line(arrow_x + cw + 4, topic_y + ch,
		arrow_x + cw + 7, topic_y + 3, BLACK);
	limit = rows - (irc_app.nick_overflow ? 1 : 0);
	if (limit < 1)
		limit = 1;
	maximum = (WORD) irc_app.nick_count - limit;
	if (maximum < 0)
		maximum = 0;
	if ((WORD) irc_roster_scroll > maximum)
		irc_roster_scroll = (UBYTE) maximum;
	y = body_y;
	index = irc_roster_scroll;
	nick = irc_app.nicks + index;
	for (row = 0; row < limit
		&& row + (WORD) irc_roster_scroll < (WORD) irc_app.nick_count;
		row++, nick++) {
		selected = irc_selected_nick[0]
			&& gem_irc_equal((const char *) irc_selected_nick, nick->text);
		if (selected)
			irc_vdi_fill(pane_x - 2, y, wkx + wkw - pane_x, ch, BLACK);
		prefix[0] = nick->prefix ? nick->prefix : ' ';
		prefix[1] = '\0';
		irc_text(pane_x, y, selected ? WHITE : BLACK, prefix, 1);
		irc_text(pane_x + cw, y, selected ? WHITE : BLACK, nick->text,
			pane_columns - 1);
		y += ch;
	}
	if (irc_app.nick_overflow)
		irc_text(pane_x, y, BLACK, "+ more", pane_columns);
}

/* prompt, visible input and caret */
static VOID
irc_draw_input(VOID)
{
	const char *input = gem_irc_app_input(&irc_app);
	UWORD cursor = gem_irc_app_input_cursor(&irc_app);
	UWORD first = 0;
	WORD x = edit_x;
	WORD y = input_y + 3;

	if (cursor >= (UWORD) input_columns)
		first = cursor - (UWORD) input_columns + 1U;
	irc_vdi_fill(text_x, input_y + 1, wkx + wkw - text_x,
		wky + wkh - input_y - 1, WHITE);
	irc_text(text_x, y, BLACK, gem_irc_current_nick(&irc_app.protocol), 8);
	irc_text(x, y, BLACK, input + first, input_columns);
	x += (WORD) (cursor - first) * cw;
	irc_vdi_line(x, y, x, y + ch - 1, BLACK);
}

static VOID
irc_draw_rules(VOID)
{
	irc_vdi_line(tree_right, topic_y, tree_right, wky + wkh - 1, BLACK);
	irc_vdi_line(pane_x - 6, topic_y, pane_x - 6, input_y - 1, BLACK);
	irc_vdi_line(text_x, input_y, wkx + wkw - 1, input_y, BLACK);
}

/* draw the full window */
static VOID
irc_draw_all(VOID)
{
	irc_vdi_fill(wkx, wky, wkw, wkh, WHITE);
	irc_draw_menu_bar();
	irc_draw_tree();
	irc_draw_topic();
	irc_draw_transcript();
	irc_draw_nicks();
	irc_draw_input();
	irc_draw_rules();
	irc_draw_menu_popup();
}

static VOID
irc_draw_dirty(UWORD dirty, WORD all)
{
	if (all) {
		irc_draw_all();
		return;
	}
	if (dirty & IRC_UI_DIRTY_MENU)
		irc_draw_menu_bar();
	if (dirty & GEM_IRC_APP_DIRTY_TOPIC) {
		irc_draw_topic();
		irc_draw_tree();
	} else if (dirty & GEM_IRC_APP_DIRTY_TABS)
		irc_draw_tree();
	if (dirty & GEM_IRC_APP_DIRTY_TRANSCRIPT)
		irc_draw_transcript();
	if (dirty & (GEM_IRC_APP_DIRTY_ROSTER | GEM_IRC_APP_DIRTY_TARGET))
		irc_draw_nicks();
	if (dirty & GEM_IRC_APP_DIRTY_INPUT)
		irc_draw_input();
	if (irc_menu)
		irc_draw_menu_popup();
}

/* draw exposed rectangles */
static VOID
irc_update(UWORD dirty, WORD all)
{
	GRECT work;
	GRECT visible;
	GRECT clip;
	WORD xy[4];

	if (!all && !dirty)
		return;
	work.g_x = wkx;
	work.g_y = wky;
	work.g_w = wkw;
	work.g_h = wkh;
	wind_update(BEG_UPDATE);
	wind_get(irc_window, WF_FIRSTXYWH, &visible.g_x, &visible.g_y,
		&visible.g_w, &visible.g_h);
	while (visible.g_w > 0 && visible.g_h > 0) {
		clip = visible;
		if (rc_intersect(&work, &clip)) {
			xy[0] = clip.g_x;
			xy[1] = clip.g_y;
			xy[2] = clip.g_x + clip.g_w - 1;
			xy[3] = clip.g_y + clip.g_h - 1;
			vs_clip(irc_vdi, TRUE, xy);
			irc_draw_dirty(dirty, all);
		}
		wind_get(irc_window, WF_NEXTXYWH, &visible.g_x, &visible.g_y,
			&visible.g_w, &visible.g_h);
	}
	xy[0] = wkx;
	xy[1] = wky;
	xy[2] = wkx + wkw - 1;
	xy[3] = wky + wkh - 1;
	vs_clip(irc_vdi, TRUE, xy);
	wind_update(END_UPDATE);
}

/* service the socket while a dialog is open */
static WORD
irc_dialog(const char *title, WORD count, const char *const *labels,
	char values[4][IRC_DIALOG_TEXT_BYTES])
{
	static OBJECT tree[12];
	static TEDINFO ted[4] __attribute__((aligned(2)));
	static BYTE empty[] = "";
	static BYTE valid[] = "X";
	static BYTE accept[] = "OK";
	static BYTE cancel[] = "Cancel";
	WORD i, n, ok, last, field, next, keyout, edit_index, object, result;
	WORD x, y, w, h, field_x, field_y, length, pressed;
	UWORD events, mx, my, mb, ks, key, clicks;
	OBJECT *entry;
	TEDINFO *info;

	ok = 2 + 2 * count;
	last = ok + 1;
	memset(tree, 0, sizeof(tree));
	memset(ted, 0, sizeof(ted));
	for (i = 0; i <= last; i++) {
		entry = &tree[i];
		entry->ob_next = i == last ? 0 : i + 1;
		entry->ob_head = entry->ob_tail = NIL;
		entry->ob_type = G_STRING;
		entry->ob_x = 12;
		entry->ob_width = 400;
		entry->ob_height = ch + 4;
	}
	tree[0].ob_next = NIL;
	tree[0].ob_head = 1;
	tree[0].ob_tail = last;
	tree[0].ob_type = G_BOX;
	tree[0].ob_state = OUTLINED;
	tree[0].ob_spec = gem_u32_words(0x1100, 0x0001);
	tree[0].ob_width = 432;
	tree[0].ob_height = (count + 3) * (ch + 10);
	tree[1].ob_y = 8;
	tree[1].ob_spec = gem_near_pointer_words(title);
	for (i = 0, n = 2; i < count; i++, n += 2) {
		tree[n].ob_y = (i + 1) * (ch + 10) + 4;
		tree[n].ob_width = 80;
		tree[n].ob_spec = gem_near_pointer_words(labels[i]);
		entry = &tree[n + 1];
		entry->ob_type = G_BOXTEXT;
		entry->ob_flags = EDITABLE;
		entry->ob_x = 96;
		entry->ob_y = tree[n].ob_y;
		entry->ob_width = 320;
		info = &ted[i];
		info->te_ptext = gem_near_pointer_words(values[i]);
		info->te_ptmplt = gem_near_pointer_words(empty);
		info->te_pvalid = gem_near_pointer_words(valid);
		info->te_font = 3;
		info->te_color = 0x1180;
		info->te_thickness = 1;
		info->te_txtlen = IRC_DIALOG_TEXT_BYTES;
		info->te_tmplen = 1;
		entry->ob_spec = gem_near_pointer_words(info);
	}
	tree[ok].ob_type = tree[last].ob_type = G_BUTTON;
	tree[ok].ob_flags = SELECTABLE | DEFAULT | EXIT;
	tree[last].ob_flags = SELECTABLE | EXIT | LASTOB;
	tree[ok].ob_width = tree[last].ob_width = 80;
	tree[ok].ob_x = 128;
	tree[last].ob_x = 224;
	tree[ok].ob_y = tree[last].ob_y = (count + 2) * (ch + 10);
	tree[ok].ob_spec = gem_near_pointer_words(accept);
	tree[last].ob_spec = gem_near_pointer_words(cancel);
	form_center(tree, &x, &y, &w, &h);
	form_dial(FMD_START, 0, 0, 0, 0, x, y, w, h);
	objc_draw(tree, 0, MAX_DEPTH, x, y, w, h);
	field = 3;
	edit_index = 0;
	pressed = FALSE;
	object = NIL;
	objc_edit(tree, field, 0, &edit_index, 1);
	objc_change(tree, field, 0, x, y, w, h, HIGHLIGHTED, TRUE);
	result = FALSE;
	for (;;) {
		events = evnt_multi_ds(MU_KEYBD | MU_BUTTON | MU_TIMER,
			1, 1, !pressed, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
			irc_message, IRC_TIMER_IDLE_MS, 0,
			&mx, &my, &mb, &ks, &key, &clicks);
		if (events & MU_TIMER)
			irc_network_step();
		next = field;
		if (events & MU_KEYBD) {
			if ((key & 0xffU) == 27U)
				break;
			if (!form_keybd(tree, field, 0, key, &next, &keyout)) {
				result = next == ok;
				break;
			}
			if (keyout)
				objc_edit(tree, field, keyout, &edit_index, 2);
		}
		if (events & MU_BUTTON) {
			pressed = (mb & 1U) != 0;
			if (pressed) {
				object = objc_find(tree, 0, MAX_DEPTH, mx, my);
				if (object > 0 && object <= last &&
				    !form_button(tree, object, clicks, &next)) {
					result = (next & 0x7fff) == ok;
					break;
				}
			}
		}
		if (next > 0 && next <= last && next != field &&
		    (tree[next].ob_flags & EDITABLE)) {
			objc_edit(tree, field, 0, &edit_index, 3);
			objc_change(tree, field, 0, x, y, w, h, NORMAL, TRUE);
			field = next;
			objc_edit(tree, field, 0, &edit_index, 1);
			objc_change(tree, field, 0, x, y, w, h, HIGHLIGHTED, TRUE);
		}
		if ((events & MU_BUTTON) && pressed && object == field) {
			objc_offset(tree, field, &field_x, &field_y);
			length = (WORD)strlen(values[(field - 3) / 2]);
			edit_index = mx > (UWORD)field_x ?
				(WORD)((mx - field_x) / cw) : 0;
			if (edit_index > length)
				edit_index = length;
		}
	}
	objc_edit(tree, field, 0, &edit_index, 3);
	form_dial(FMD_FINISH, 0, 0, 0, 0, x, y, w, h);
	irc_damage_all = 1;
	return result;
}

static BYTE irc_fields[4][IRC_DIALOG_TEXT_BYTES];

static VOID
irc_connect_dialog(VOID)
{
	static const char *const labels[] = { "Server", "Port", "Nickname", "Channel" };
	UWORD port;

	(void) irc_copy(irc_fields[0], sizeof(irc_fields[0]), irc_server);
	snprintf(irc_fields[1], sizeof(irc_fields[1]), "%u", irc_port);
	(void) irc_copy(irc_fields[2], sizeof(irc_fields[2]), irc_nick);
	(void) irc_copy(irc_fields[3], sizeof(irc_fields[3]), irc_channel);
	if (!irc_dialog("IRC connection", 4, labels, irc_fields))
		return;
	if (!irc_fields[0][0] || !irc_fields[2][0]
		|| strlen(irc_fields[0]) >= sizeof(irc_server)
		|| strlen(irc_fields[2]) >= sizeof(irc_nick)
		|| strlen(irc_fields[3]) >= sizeof(irc_channel)
		|| !irc_parse_port(irc_fields[1], &port)) {
		form_alert(1, "[1][Enter a server, nickname|and port from 1 to 65535.][OK]");
		return;
	}
	irc_user_disconnect();
	(void) irc_copy(irc_server, sizeof(irc_server), irc_fields[0]);
	(void) irc_copy(irc_nick, sizeof(irc_nick), irc_fields[2]);
	(void) irc_copy(irc_channel, sizeof(irc_channel), irc_fields[3]);
	(void) irc_copy((BYTE *) irc_app.protocol.nick,
		sizeof(irc_app.protocol.nick), irc_nick);
	irc_port = port;
	irc_user_reconnect();
}

static VOID
irc_command(const char *prefix, const char *argument, WORD submit)
{
	UWORD length;

	length = 0;
	while (*prefix && length + 1U < GEM_IRC_APP_INPUT_SIZE)
		irc_ui_command[length++] = *prefix++;
	if (argument)
		while (*argument && length + 1U < GEM_IRC_APP_INPUT_SIZE)
			irc_ui_command[length++] = *argument++;
	if (!submit && argument && length + 1U < GEM_IRC_APP_INPUT_SIZE)
		irc_ui_command[length++] = ' ';
	irc_ui_command[length] = '\0';
	if (gem_irc_app_set_input(&irc_app, (const char *) irc_ui_command)
		== GEM_IRC_OK && submit) {
		(void) gem_irc_app_submit(&irc_app);
		irc_quiet_polls = 0;
	}
}

static VOID
irc_prompt(const char *label, const char *prefix, const char *initial)
{
	const char *labels[1];

	labels[0] = label;
	(void) irc_copy(irc_fields[0], sizeof(irc_fields[0]),
		(const BYTE *) (initial ? initial : ""));
	if (irc_dialog(label, 1, labels, irc_fields) && irc_fields[0][0])
		irc_command(prefix, irc_fields[0], TRUE);
}

static VOID
irc_action(UBYTE action)
{
	GEM_IRC_UWORD active;

	active = gem_irc_app_active_index(&irc_app);
	if (action == IRC_ACTION_CONNECT)
		irc_connect_dialog();
	else if (action == IRC_ACTION_NICK)
		irc_prompt("Nickname", "/nick ", gem_irc_current_nick(&irc_app.protocol));
	else if (action == IRC_ACTION_RECONNECT)
		irc_user_reconnect();
	else if (action == IRC_ACTION_DISCONNECT)
		irc_user_disconnect();
	else if (action == IRC_ACTION_QUIT)
		(void) gem_irc_app_request_close(&irc_app, "Menu quit");
	else if (action == IRC_ACTION_JOIN)
		irc_prompt("Channel", "/join ", irc_channel);
	else if (action == IRC_ACTION_PART) {
		if (gem_irc_app_active_kind(&irc_app) == GEM_IRC_APP_BUFFER_PRIVATE)
			(void) gem_irc_app_close_buffer(&irc_app, active);
		else if (gem_irc_app_active_kind(&irc_app)
			== GEM_IRC_APP_BUFFER_CHANNEL)
			irc_command("/part", (const char *) 0, TRUE);
	} else if (action == IRC_ACTION_TOPIC)
		irc_prompt("Topic", "/topic ", gem_irc_app_topic(&irc_app));
	else if (action == IRC_ACTION_NAMES)
		irc_command("/names", (const char *) 0, TRUE);
	else if (action == IRC_ACTION_CLEAR)
		gem_irc_app_clear(&irc_app);
	else if (action == IRC_ACTION_QUERY) {
		if (irc_selected_nick[0])
			(void) gem_irc_app_query(&irc_app,
				(const char *) irc_selected_nick);
		else
			irc_prompt("Query", "/query ", "");
	} else if (action == IRC_ACTION_MESSAGE)
		irc_command("/msg ", irc_selected_nick[0]
			? (const char *) irc_selected_nick : (const char *) 0, FALSE);
	else if (action == IRC_ACTION_WHOIS)
		irc_command("/whois ", irc_selected_nick[0]
			? (const char *) irc_selected_nick : (const char *) 0,
			irc_selected_nick[0] != '\0');
	else if (action == IRC_ACTION_KICK)
		irc_command("/kick ", irc_selected_nick[0]
			? (const char *) irc_selected_nick : (const char *) 0, FALSE);
	else if (action == IRC_ACTION_NEXT)
		gem_irc_app_cycle(&irc_app, 1);
	else if (action == IRC_ACTION_PREVIOUS)
		gem_irc_app_cycle(&irc_app, -1);
	else if (action == IRC_ACTION_CLOSE_TAB && active)
		(void) gem_irc_app_close_buffer(&irc_app, active);
	else if (action == IRC_ACTION_COMMANDS)
		irc_command("/help", (const char *) 0, TRUE);
	else if (action == IRC_ACTION_SHORTCUTS)
		(void) form_alert(1, "[0][F1 commands; F2/F3 views|"
			"Ctrl-N/P views; Ctrl-W close|"
			"Tab completes nick; Up/Down history|"
			"Ctrl-S connection; Ctrl-R retry|"
			"PgUp/PgDn scroll; Ctrl-Q exit][OK]");
	else if (action == IRC_ACTION_ABOUT)
		(void) form_alert(1, "[0][Antler IRC Chat|Native GEM client|"
			"Fixed memory and adaptive polling][OK]");
	else if (action == IRC_ACTION_AWAY)
		irc_prompt("Away", "/away ", "");
	else if (action == IRC_ACTION_BACK)
		irc_command("/away", (const char *) 0, TRUE);
	else if (action == IRC_ACTION_OP || action == IRC_ACTION_DEOP
		|| action == IRC_ACTION_VOICE || action == IRC_ACTION_DEVOICE) {
		const char *command = action == IRC_ACTION_OP ? "/mode +o " :
			action == IRC_ACTION_DEOP ? "/mode -o " :
			action == IRC_ACTION_VOICE ? "/mode +v " : "/mode -v ";
		irc_command(command, irc_selected_nick[0]
			? (const char *) irc_selected_nick : (const char *) 0,
			irc_selected_nick[0] != '\0');
	}
}

/* hit test the menu, tree, roster and editor */
static WORD
irc_click(WORD mx, WORD my, WORD clicks)
{
	const IRC_MENU_DESC *menu;
	WORD i;
	WORD x;
	WORD y;
	WORD width;
	WORD row;
	UWORD cursor;
	UWORD first;
	UWORD index;

	if (my >= menu_y && my < menu_y + menu_h) {
		for (i = 0; i < (WORD) IRC_MENU_COUNT; i++)
			if (mx >= menu_x[i] && mx < menu_x[i + 1]) {
				irc_menu = irc_menu == (UBYTE) (i + 1)
					? IRC_MENU_NONE : (UBYTE) (i + 1);
				irc_pending |= IRC_UI_DIRTY_MENU;
				return TRUE;
			}
	}
	if (irc_menu) {
		menu = &irc_menus[irc_menu - 1U];
		x = menu_x[irc_menu - 1U];
		y = menu_y + menu_h + 2;
		width = ((WORD) menu->columns + 2) * cw;
		if (mx >= x && mx < x + width && my >= y
			&& my < y + (WORD) menu->count * (ch + 4)) {
			row = (my - y) / (ch + 4);
			irc_action(menu->items[row].action);
		}
		irc_menu = IRC_MENU_NONE;
		irc_pending |= IRC_UI_DIRTY_MENU;
		return TRUE;
	}
	if (mx >= tree_x && mx < tree_right && my >= topic_y && my < input_y) {
		for (i = 0; i < (WORD) gem_irc_app_buffer_count(&irc_app); i++)
			if (tree_row[i] >= 0 && my >= tree_row[i]
				&& my < tree_row[i] + ch + 4) {
				if (i && mx >= tree_right - GEM_IRC_LAYOUT_CLOSE_WIDTH)
					(void) gem_irc_app_close_buffer(&irc_app, (GEM_IRC_UWORD) i);
				else
					gem_irc_app_switch(&irc_app, (GEM_IRC_UWORD) i);
				irc_roster_scroll = 0;
				irc_selected_nick[0] = '\0';
				return FALSE;
			}
	}
	if (mx >= text_x && mx < pane_x - 6
		&& my >= topic_y && my < topic_y + topic_h
		&& gem_irc_app_active_kind(&irc_app) == GEM_IRC_APP_BUFFER_CHANNEL) {
		irc_action(IRC_ACTION_TOPIC);
		return FALSE;
	}
	if (mx >= pane_x - 4 && my >= topic_y && my < input_y
		&& gem_irc_app_active_kind(&irc_app) == GEM_IRC_APP_BUFFER_CHANNEL) {
		if (my < body_y) {
			x = wkx + wkw - 2 * cw - 2;
			if (mx >= x && mx < x + cw && irc_roster_scroll)
				irc_roster_scroll--;
			else if (mx >= x + cw && mx < x + 2 * cw
				&& irc_roster_scroll + 1U < irc_app.nick_count)
				irc_roster_scroll++;
			irc_app.dirty |= GEM_IRC_APP_DIRTY_ROSTER;
			return FALSE;
		}
		row = (my - body_y) / ch;
		index = (UWORD) row + irc_roster_scroll;
		if (index < (UWORD) irc_app.nick_count) {
			(void) irc_copy(irc_selected_nick, GEM_IRC_NICK_SIZE,
				(const BYTE *) irc_app.nicks[index].text);
			irc_app.dirty |= GEM_IRC_APP_DIRTY_ROSTER;
			if (clicks > 1)
				(void) gem_irc_app_query(&irc_app,
					(const char *) irc_selected_nick);
		}
		return FALSE;
	}
	if (mx >= edit_x && my >= input_y && my < wky + wkh) {
		cursor = gem_irc_app_input_cursor(&irc_app);
		first = cursor >= (UWORD) input_columns
			? cursor - (UWORD) input_columns + 1U : 0;
		if (mx <= edit_x)
			cursor = first;
		else
			cursor = first + (UWORD) ((mx - edit_x) / cw);
		gem_irc_app_set_cursor(&irc_app, cursor);
	}
	return FALSE;
}

static VOID
irc_geometry(const WORD *message)
{
	WORD width = message[6];
	WORD height = message[7];

	if (width < GEM_IRC_LAYOUT_MIN_WIDTH)
		width = GEM_IRC_LAYOUT_MIN_WIDTH;
	if (height < GEM_IRC_LAYOUT_MIN_HEIGHT)
		height = GEM_IRC_LAYOUT_MIN_HEIGHT;
	wind_set(irc_window, WF_CXYWH, message[4], message[5], width, height);
	irc_layout();
	irc_was_full = FALSE;
}

static VOID
irc_full(VOID)
{
	GRECT target;

	if (!irc_was_full) {
		wind_get(irc_window, WF_CXYWH, &irc_previous.g_x,
			&irc_previous.g_y, &irc_previous.g_w,
			&irc_previous.g_h);
		wind_get(irc_window, WF_FXYWH, &target.g_x, &target.g_y,
			&target.g_w, &target.g_h);
		irc_was_full = TRUE;
	} else {
		target = irc_previous;
		irc_was_full = FALSE;
	}
	wind_set(irc_window, WF_CXYWH, target.g_x, target.g_y,
		target.g_w, target.g_h);
	irc_layout();
}

/* true when the window needs a full redraw */
static WORD
irc_handle_message(WORD *message)
{
	if (message[3] != irc_window)
		return FALSE;
	switch (message[0]) {
	case WM_REDRAW:
		return TRUE;
	case WM_TOPPED:
	case WM_NEWTOP:
		wind_set(irc_window, WF_TOP, irc_window, 0, 0, 0);
		return FALSE;
	case WM_CLOSED:
		gem_irc_app_request_close(&irc_app, "Window closed");
		return FALSE;
	case WM_FULLED:
		irc_full();
		return TRUE;
	case WM_MOVED:
	case WM_SIZED:
		irc_geometry(message);
		return TRUE;
	case WM_ARROWED:
		if (message[4] == WA_UPPAGE)
			gem_irc_app_scroll_up(&irc_app, (GEM_IRC_UWORD) rows,
				(GEM_IRC_UWORD) rows);
		else if (message[4] == WA_DNPAGE)
			gem_irc_app_scroll_down(&irc_app, (GEM_IRC_UWORD) rows,
				(GEM_IRC_UWORD) rows);
		else if (message[4] == WA_UPLINE)
			gem_irc_app_scroll_up(&irc_app, 1U, (GEM_IRC_UWORD) rows);
		else if (message[4] == WA_DNLINE)
			gem_irc_app_scroll_down(&irc_app, 1U,
				(GEM_IRC_UWORD) rows);
		return FALSE;
	}
	return FALSE;
}

static WORD
irc_open_window(VOID)
{
	WORD work_in[11];
	WORD index, bw, bh, hor, ver;
	GRECT border;
	GEM_U32_WORDS title;

	if (!aescheck())
		return FALSE;
	chdir("/lib/gemsys");
	irc_application = appl_init(NULL);
	if (irc_application < 0)
		return FALSE;
	irc_vdi = graf_handle(&cw, &ch, &bw, &bh);
	for (index = 0; index < 10; index++)
		work_in[index] = 1;
	work_in[10] = 2;
	if (!v_opnvwk(work_in, &irc_vdi, irc_work_out))
		return FALSE;
	if (cw <= 0)
		cw = 8;
	if (ch <= 0)
		ch = 16;
	vswr_mode(irc_vdi, MD_REPLACE);
	vst_alignment(irc_vdi, 0, 5, &hor, &ver);	/* y is the cell top */
	graf_mouse(ARROW, NULL);

	wind_get(0, WF_WXYWH, &irc_desktop.g_x, &irc_desktop.g_y,
		&irc_desktop.g_w, &irc_desktop.g_h);
	border.g_x = irc_desktop.g_x + GEM_IRC_LAYOUT_MARGIN;
	border.g_y = irc_desktop.g_y + GEM_IRC_LAYOUT_MARGIN;
	border.g_w = irc_desktop.g_w - 2 * GEM_IRC_LAYOUT_MARGIN;
	border.g_h = irc_desktop.g_h - 2 * GEM_IRC_LAYOUT_MARGIN;
	if (border.g_w < GEM_IRC_LAYOUT_MIN_WIDTH)
		border.g_w = GEM_IRC_LAYOUT_MIN_WIDTH;
	if (border.g_h < GEM_IRC_LAYOUT_MIN_HEIGHT)
		border.g_h = GEM_IRC_LAYOUT_MIN_HEIGHT;
	irc_window = wind_create(IRC_WINDOW_KIND, irc_desktop.g_x,
		irc_desktop.g_y, irc_desktop.g_w, irc_desktop.g_h);
	if (irc_window <= 0)
		return FALSE;
	title = gem_near_pointer_words(irc_title);
	wind_set(irc_window, WF_NAME, title.lo, title.hi, 0, 0);
	if (!wind_open(irc_window, border.g_x, border.g_y, border.g_w,
			border.g_h))
		return FALSE;
	irc_layout();
	irc_previous = border;
	irc_was_full = FALSE;
	return TRUE;
}

static VOID
irc_close_window(VOID)
{
	if (irc_window > 0) {
		wind_close(irc_window);
		wind_delete(irc_window);
		irc_window = -1;
	}
	if (irc_vdi >= 0) {
		v_clsvwk(irc_vdi);
		irc_vdi = -1;
	}
	if (irc_application >= 0) {
		appl_exit();
		irc_application = -1;
	}
}

static VOID
irc_connection_failed(VOID)
{
	irc_registration_started = 0;
	irc_join_sent = 0;
	irc_connect_ticks = 0;
	irc_reconnect_ticks = IRC_RECONNECT_TICKS;
	gem_irc_app_connection_failed(&irc_app);
	gem_irc_app_connecting(&irc_app, (const char *) irc_server);
}

static WORD
irc_start_registration(VOID)
{
	GEM_IRC_WORD result;
	const char *nick;

	nick = gem_irc_current_nick(&irc_app.protocol);
	if (!nick || !*nick)
		nick = (const char *) irc_nick;
	gem_irc_connection_reset(&irc_app.protocol);
	result = gem_irc_app_start(&irc_app, nick,
		(const char *) irc_user, (const char *) irc_real_name);
	irc_join_sent = 0;
	if (result <= 0) {
		gem_irc_transport_close(&irc_transport);
		irc_connection_failed();
		return FALSE;
	}
	irc_registration_started = 1;
	irc_connect_ticks = 0;
	return TRUE;
}

static VOID
irc_connect_result(GEM_IRC_WORD result)
{
	if (result == GEM_IRC_TRANSPORT_OK)
		irc_start_registration();
	else if (result == GEM_IRC_TRANSPORT_PENDING)
		irc_connect_ticks = IRC_CONNECT_TICKS;
	else
		irc_connection_failed();
}

static VOID
irc_user_disconnect(VOID)
{
	if (irc_app.connected)
		(void) gem_irc_send_quit(&irc_app.protocol, "Disconnecting");
	gem_irc_transport_stop(&irc_transport);
	irc_manual_offline = 1;
	irc_registration_started = 0;
	irc_join_sent = 0;
	irc_connect_ticks = 0;
	irc_reconnect_ticks = 0;
	gem_irc_app_offline(&irc_app, "Disconnected");
	irc_pending |= GEM_IRC_APP_DIRTY_TABS;
}

static VOID
irc_user_reconnect(VOID)
{
	GEM_IRC_WORD result;

	if (irc_app.connected)
		(void) gem_irc_send_quit(&irc_app.protocol, "Reconnecting");
	gem_irc_transport_stop(&irc_transport);
	irc_manual_offline = 0;
	irc_registration_started = 0;
	irc_join_sent = 0;
	irc_connect_ticks = 0;
	irc_reconnect_ticks = 0;
	gem_irc_app_offline(&irc_app, "Reconnecting");
	gem_irc_app_connecting(&irc_app, (const char *) irc_server);
	result = gem_irc_transport_connect(&irc_transport,
		(const char *) irc_server, irc_port);
	irc_connect_result(result);
	irc_quiet_polls = 0;
	irc_pending |= GEM_IRC_APP_DIRTY_TABS;
}

static VOID
irc_connect_step(VOID)
{
	GEM_IRC_WORD result;

	result = gem_irc_transport_progress(&irc_transport);
	if (result == GEM_IRC_TRANSPORT_OK) {
		irc_start_registration();
		return;
	}
	if (result < 0) {
		irc_connection_failed();
		return;
	}
	if (!irc_timer_elapsed(&irc_connect_ticks))
		return;
	/* fifteen second timeout */
	gem_irc_transport_close(&irc_transport);
	irc_connection_failed();
}

static VOID
irc_reconnect_step(VOID)
{
	GEM_IRC_WORD result;

	if (!gem_irc_transport_needs_reconnect(&irc_transport))
		return;
	if (irc_manual_offline)
		return;
	if (irc_reconnect_ticks && !irc_timer_elapsed(&irc_reconnect_ticks))
		return;
	result = gem_irc_transport_reconnect(&irc_transport);
	irc_connect_result(result);
}

/* true when more lines may be waiting */
static WORD
irc_network_step(VOID)
{
	GEM_IRC_WORD result;
	const char *target;
	WORD backlog = FALSE;

	/* no reconnect after smoke text is sent */
	if (irc_smoke_enabled && irc_smoke_sent
		&& !gem_irc_transport_is_connected(&irc_transport)) {
		gem_irc_app_request_close(&irc_app,
			"One-shot disconnected");
		return FALSE;
	}

	if (gem_irc_transport_is_connecting(&irc_transport))
		irc_connect_step();
	else if (gem_irc_transport_is_connected(&irc_transport)) {
		backlog = gem_irc_app_poll(&irc_app, IRC_POLL_LINES)
			== (GEM_IRC_WORD) IRC_POLL_LINES;
		if (!gem_irc_transport_is_connected(&irc_transport)) {
			irc_registration_started = 0;
			irc_join_sent = 0;
			irc_reconnect_ticks = IRC_RECONNECT_TICKS;
			gem_irc_app_connecting(&irc_app,
				(const char *) irc_server);
		}
	} else
		irc_reconnect_step();
	if (irc_registration_started && !irc_join_sent && irc_channel[0]
		&& gem_irc_is_registered(&irc_app.protocol)) {
		result = gem_irc_app_join(&irc_app,
			(const char *) irc_channel, (const char *) 0);
		if (result > 0)
			irc_join_sent = 1;
	}

	if (!irc_smoke_enabled || gem_irc_app_is_closing(&irc_app))
		return backlog;
	if (!irc_smoke_sent) {
		/* wait for JOIN before sending smoke text */
		target = gem_irc_app_target(&irc_app);
		if (irc_join_sent && gem_irc_is_registered(&irc_app.protocol)
			&& !strcmp(target, (const char *) irc_channel)) {
			result = gem_irc_app_send_message(&irc_app, target,
				(const char *)
				irc_smoke_message);
			if (result > 0) {
				irc_smoke_sent = 1;
				irc_smoke_ticks = IRC_SMOKE_DISPLAY_TICKS;
				return backlog;
			}
		}
		if (irc_timer_elapsed(&irc_smoke_ticks))
			gem_irc_app_request_close(&irc_app,
				"One-shot timeout");
		return backlog;
	}
	if (irc_timer_elapsed(&irc_smoke_ticks))
		gem_irc_app_request_close(&irc_app, "One-shot complete");
	return backlog;
}

static VOID
irc_key(UWORD key)
{
	WORD result;
	UWORD ascii;
	UWORD scan;

	ascii = key & 0x00ffU;
	scan = key & 0xff00U;
	if (scan == 0x3b00U) {
		irc_action(IRC_ACTION_COMMANDS);
		return;
	}
	if (scan == 0x3c00U) {
		gem_irc_app_cycle(&irc_app, 1);
		return;
	}
	if (scan == 0x3d00U) {
		gem_irc_app_cycle(&irc_app, -1);
		return;
	}
	if (ascii == 12U) {
		gem_irc_app_clear(&irc_app);
		return;
	}
	if (ascii == 18U) {
		irc_user_reconnect();
		return;
	}
	if (ascii == 19U) {
		irc_connect_dialog();
		return;
	}
	if (ascii == 23U) {
		irc_action(IRC_ACTION_CLOSE_TAB);
		return;
	}
	if (ascii == 27U && irc_menu) {
		irc_menu = 0;
		irc_damage_all = 1;
		return;
	}

	result = gem_irc_app_key(&irc_app, key, (GEM_IRC_UWORD) rows);
	if (result == GEM_IRC_APP_KEY_CLOSE && !gem_irc_app_is_closing(&irc_app))
		gem_irc_app_request_close(&irc_app, "Keyboard close");
	if (result == GEM_IRC_APP_KEY_SUBMITTED)
		irc_quiet_polls = 0;
}

/* drain a bounded key burst before the next network batch */
static VOID
irc_keys(UWORD key)
{
	UWORD mx, my, mb, ks, clicks;
	UWORD left = IRC_KEY_BURST;

	irc_key(key);
	while (!irc_quiet_polls && --left && !gem_irc_app_is_closing(&irc_app)
		&& (evnt_multi_ds(MU_KEYBD | MU_TIMER, 1, 1, 1, 0, 0, 0, 0, 0,
			0, 0, 0, 0, 0, irc_message, 0, 0, &mx, &my, &mb, &ks,
			&key, &clicks) & MU_KEYBD))
		irc_key(key);
}

/* ELKS time in 10 ms ticks */
static UWORD
irc_now(VOID)
{
	struct timeval now;

	if (gettimeofday(&now, NULL) < 0)
		return 0;
	return (UWORD) ((UWORD) (now.tv_sec * 100L)
		+ (UWORD) (now.tv_usec / 10000L));
}

/* poll and draw on fixed clocks so typing cannot starve the socket */
static VOID
irc_event_loop(VOID)
{
	UWORD events, mx, my, mb, ks, key, clicks, timer, now;
	UWORD appended, draw, user;
	WORD closing_before_events, redraw, backlog, poll_due;

	irc_quit_ticks = 0;
	timer = IRC_TIMER_BUSY_MS;
	backlog = FALSE;
	irc_poll_tick = irc_now();
	irc_draw_tick = irc_poll_tick;
	irc_echo_tick = irc_poll_tick;
	for (;;) {
		irc_message[0] = 0;
		closing_before_events = gem_irc_app_is_closing(&irc_app);
		events = evnt_multi_ds(MU_MESAG | MU_KEYBD | MU_TIMER | MU_BUTTON,
			2, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
			irc_message, (irc_pending
				& GEM_IRC_APP_DIRTY_INPUT) ? IRC_ECHO_MS : timer,
			0, &mx, &my, &mb, &ks, &key, &clicks);
		redraw = FALSE;
		now = irc_now();
		poll_due = (UWORD) (now - irc_poll_tick)
			>= (irc_quiet_polls < 2U ? IRC_POLL_TICKS
				: IRC_QUIET_POLL_TICKS);
		/* keys do not bypass the poll clock */
		if ((events & MU_KEYBD) ? poll_due
			: backlog || (events & MU_TIMER) || poll_due) {
			irc_poll_tick = now;
			appended = gem_irc_app_appended(&irc_app);
			backlog = irc_network_step();
			if (appended != gem_irc_app_appended(&irc_app)
				|| (!irc_app.connected && !irc_manual_offline))
				irc_quiet_polls = 0;
			else if (irc_quiet_polls < IRC_QUIET_POLLS)
				irc_quiet_polls++;
			timer = irc_quiet_polls < IRC_QUIET_POLLS
				? IRC_TIMER_BUSY_MS : IRC_TIMER_IDLE_MS;
		}
		/* user changes draw first */
		irc_pending |= (UWORD) gem_irc_app_take_dirty(&irc_app);
		if (events & MU_MESAG)
			redraw = irc_handle_message(irc_message);
		if (events & MU_KEYBD)
			irc_keys(key);
		if (events & MU_BUTTON)
			redraw |= irc_click((WORD) mx, (WORD) my, (WORD) clicks);
		redraw |= irc_damage_all;
		irc_damage_all = 0;
		user = (UWORD) gem_irc_app_take_dirty(&irc_app);
		irc_pending |= user;
		draw = irc_pending;
		if (!redraw && !(user & ~GEM_IRC_APP_DIRTY_INPUT)
			&& (UWORD) (now - irc_draw_tick) < IRC_DRAW_TICKS)
			draw &= GEM_IRC_APP_DIRTY_INPUT;
		if ((events & MU_KEYBD) && !redraw
			&& (UWORD) (now - irc_echo_tick) < IRC_ECHO_TICKS)
			draw &= ~GEM_IRC_APP_DIRTY_INPUT;
		if (draw & ~GEM_IRC_APP_DIRTY_INPUT)
			irc_draw_tick = now;
		if (draw & GEM_IRC_APP_DIRTY_INPUT)
			irc_echo_tick = now;
		irc_pending &= (UWORD) ~draw;
		irc_update(draw, redraw);

		/* drain queued output before closing */
		if (gem_irc_app_is_closing(&irc_app)) {
			if (!irc_transport.transmit_length)
				break;
			else if (!closing_before_events)
				irc_quit_ticks = IRC_QUIT_FLUSH_TICKS;
			else if ((events & MU_TIMER)
				&& irc_timer_elapsed(&irc_quit_ticks))
				break;
		}
	}
}

int
main(int argc, char **argv)
{
	GEM_IRC_WORD connect_result;

	if (!irc_configuration((WORD) argc, (BYTE **) argv))
		return 1;
	if (!irc_open_window()) {
		irc_close_window();
		return 1;
	}
	gem_irc_transport_init(&irc_transport);
	gem_irc_app_init(&irc_app, &irc_transport);
	gem_irc_app_set_display_columns(&irc_app, (GEM_IRC_UWORD) columns);
	irc_join_sent = 0;
	irc_registration_started = 0;
	irc_connect_ticks = 0;
	irc_reconnect_ticks = 0;
	irc_manual_offline = irc_show_connect;
	if (!irc_show_connect)
		gem_irc_app_connecting(&irc_app, (const char *) irc_server);
	gem_irc_app_take_dirty(&irc_app);
	irc_update(0, TRUE);
	if (irc_show_connect)
		irc_connect_dialog();
	else {
		connect_result = gem_irc_transport_connect(&irc_transport,
			(const char *) irc_server, irc_port);
		irc_connect_result(connect_result);
	}
	irc_event_loop();
	gem_irc_app_disconnect(&irc_app, "IRC Chat closed");
	gem_irc_transport_stop(&irc_transport);
	irc_close_window();
	return 0;
}
