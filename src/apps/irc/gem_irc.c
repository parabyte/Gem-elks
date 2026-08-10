/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

/*
 * gem_irc.c - native GEM window front end for the ELKS IRC client
 *
 * GEM_IRC_TRANSPORT owns the one socket and hands complete lines to
 * gem_irc_app.c, the timer ticks every 100 ms so ten ticks is a second
 */

#include "aes.h"
#include "gem_irc_app.h"
#include "gem_irc_layout.h"
#include "gem_irc_transport.h"

#include <string.h>
#include <unistd.h>

#define IRC_WINDOW_KIND (NAME | CLOSER | FULLER | MOVER | SIZER \
	| UPARROW | DNARROW)
#define IRC_WINDOW_MIN_WIDTH       GEM_IRC_LAYOUT_MIN_WIDTH
#define IRC_WINDOW_MIN_HEIGHT      GEM_IRC_LAYOUT_MIN_HEIGHT
#define IRC_WINDOW_MARGIN          GEM_IRC_LAYOUT_MARGIN
#define IRC_INPUT_PADDING          GEM_IRC_LAYOUT_INPUT_PADDING
#define IRC_TOPIC_PADDING          GEM_IRC_LAYOUT_TOPIC_PADDING
#define IRC_SIDE_WIDTH             GEM_IRC_LAYOUT_SIDE_WIDTH
#define IRC_PANE_PADDING           GEM_IRC_LAYOUT_PANE_PADDING
#define IRC_DRAW_TEXT_SIZE         GEM_IRC_LAYOUT_TEXT_SIZE
#define IRC_EVENT_TIMER_MS         100U
#define IRC_RECONNECT_TICKS        50U
#define IRC_CONNECT_TICKS          150U
#define IRC_QUIT_FLUSH_TICKS       10U
#define IRC_SMOKE_CONNECT_TICKS    600U
#define IRC_SMOKE_DISPLAY_TICKS    20U
#define IRC_DEFAULT_PORT           6667U
#define IRC_BEG_UPDATE             1
#define IRC_END_UPDATE             0

/*
 * the AES OBJECT-tree input line is the default build,
 * -DIRC_AES_INPUT=0 brings back the hand-drawn editor
 */
#ifndef IRC_AES_INPUT
#define IRC_AES_INPUT 1
#endif
/* the toolbar needs the left-button click in every build */
#define IRC_EVENT_FLAGS  (MU_MESAG | MU_KEYBD | MU_TIMER | MU_BUTTON)
#define IRC_EVENT_BCLK   1U
#define IRC_EVENT_BMSK   1U
#define IRC_EVENT_BST    1U

/* double a word, on ELKS an inline shift so gcc dont turn it into a MUL */
#if defined(ELKS) && ELKS
#define IRC_DOUBLE_WORD(value) \
	__asm__ volatile ("shlw %0" : "+r" (value) : : "cc")
#else
#define IRC_DOUBLE_WORD(value) ((value) = (UWORD) ((value) + (value)))
#endif

static WORD irc_application = -1;
static WORD irc_vdi = -1;
static WORD irc_window = -1;
static WORD irc_character_width;
static WORD irc_character_height;
static WORD irc_box_width;
static WORD irc_box_height;
static WORD irc_work_in[11];
static WORD irc_work_out[57];
static WORD irc_message[8];
static UWORD irc_event_mouse_x;
static UWORD irc_event_mouse_y;
static UWORD irc_event_mouse_buttons;
static UWORD irc_event_key_state;
static UWORD irc_event_key;
static UWORD irc_event_clicks;
static GRECT irc_desktop;
static GRECT irc_work;
static GRECT irc_previous;
static GRECT irc_redraw_pending;
static WORD irc_was_full;

static GEM_IRC_APP irc_app;
static GEM_IRC_TRANSPORT irc_transport;
static GEM_IRC_APP_TRANSPORT irc_app_transport;

static UBYTE irc_join_sent;
static UBYTE irc_registration_started;
static UBYTE irc_return_desktop;
static UBYTE irc_smoke_enabled;
static UBYTE irc_smoke_sent;
static UWORD irc_connect_ticks;
static UWORD irc_reconnect_ticks;
static UWORD irc_quit_ticks;
static UWORD irc_smoke_ticks;
static BYTE irc_title[] = "GEM IRC";
static BYTE irc_draw_text[IRC_DRAW_TEXT_SIZE];
static BYTE irc_server[GEM_IRC_TRANSPORT_HOST_SIZE];
static BYTE irc_channel[GEM_IRC_APP_TARGET_SIZE];
static BYTE irc_nick[GEM_IRC_NICK_SIZE];
static BYTE irc_user[GEM_IRC_NICK_SIZE];
static BYTE irc_real_name[64];
static BYTE irc_smoke_message[GEM_IRC_APP_INPUT_SIZE];
static UWORD irc_port;

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

/* peel off one hex digit by repeated subtraction */
static BYTE
irc_take_hex_digit(UWORD *value, UWORD place)
{
	UBYTE digit;

	digit = 0;
	while (*value >= place && digit < 15U) {
		*value -= place;
		digit++;
	}
	if (digit < 10U)
		return (BYTE) ('0' + digit);
	return (BYTE) ('A' + digit - 10U);
}

/* seven-byte default nick, "GEM" plus the 16-bit ELKS process ID */
static VOID
irc_make_default_nick(VOID)
{
	UWORD value;

	value = (UWORD) getpid();
	irc_nick[0] = 'G';
	irc_nick[1] = 'E';
	irc_nick[2] = 'M';
	irc_nick[3] = irc_take_hex_digit(&value, 4096U);
	irc_nick[4] = irc_take_hex_digit(&value, 256U);
	irc_nick[5] = irc_take_hex_digit(&value, 16U);
	irc_nick[6] = irc_take_hex_digit(&value, 1U);
	irc_nick[7] = '\0';
}

/* parse a decimal port with overflow checks */
static WORD
irc_parse_port(const BYTE *text, UWORD *port)
{
	UWORD value;
	UWORD digit;
	UWORD twice;
	UWORD four;
	UWORD eight;

	if (!text || !*text || !port)
		return FALSE;
	value = 0;
	while (*text) {
		if (*text < '0' || *text > '9')
			return FALSE;
		digit = (UWORD) (*text++ - '0');
		if (value > 6553U || (value == 6553U && digit > 5U))
			return FALSE;
		twice = value;
		IRC_DOUBLE_WORD(twice);
		four = twice;
		IRC_DOUBLE_WORD(four);
		eight = four;
		IRC_DOUBLE_WORD(eight);
		value = eight + twice + digit;
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

	server = (const BYTE *) "irc.starlink-irc.org";
	channel = (const BYTE *) "#parabytetest";
	nick = (const BYTE *) 0;
	port_text = (const BYTE *) 0;
	irc_smoke_enabled = 0;
	irc_smoke_sent = 0;
	irc_smoke_ticks = 0;
	irc_smoke_message[0] = '\0';
	irc_return_desktop = 0U;
	/* swallow only the desktop's exact private invocation */
	if (argc == 2 && argv[1]
		&& !strcmp((const char *) argv[1], "--return-desktop")) {
		irc_return_desktop = 1U;
		argc = 1;
	}
	if (argc > 1 && argv[1] && argv[1][0])
		server = argv[1];
	if (argc > 2 && argv[2] && argv[2][0])
		channel = argv[2];
	if (argc > 3 && argv[3] && argv[3][0])
		nick = argv[3];
	if (argc > 4 && argv[4] && argv[4][0])
		port_text = argv[4];
	if (argc > 5) {
		/* a normal desktop launch carries no flag, so it can never post */
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
			(const BYTE *) "Native GEM IRC"))
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

/* the toolbar strip sits below the one-line editor, the rest lays out from here */
static WORD
irc_toolbar_top_y(VOID)
{
	return irc_work.g_y + irc_work.g_h - irc_character_height - 4;
}

static WORD
irc_input_top_y(VOID)
{
	return irc_toolbar_top_y() - irc_character_height - IRC_INPUT_PADDING;
}

/*
 * merge one AES damage rectangle into the pending bounding box, a zero
 * width marks the box empty
 */
static VOID
irc_queue_redraw(GRECT *request)
{
	WORD *incoming;
	WORD *queued;
	WORD edge;
	UBYTE axes;

	if (request->g_w <= 0 || request->g_h <= 0)
		return;
	if (irc_redraw_pending.g_w <= 0) {
		irc_redraw_pending = *request;
		return;
	}

	/* GRECT is x,y,w,h, so bumping both pointers turns the x/w pass into y/h */
	incoming = &request->g_x;
	queued = &irc_redraw_pending.g_x;
	axes = 2U;
	while (axes) {
		edge = incoming[0] + incoming[2];
		if (edge > queued[0] + queued[2])
			queued[2] = edge - queued[0];
		if (incoming[0] < queued[0]) {
			queued[2] += queued[0] - incoming[0];
			queued[0] = incoming[0];
		}
		incoming++;
		queued++;
		axes--;
	}
}

/* a content or geometry change replaces any smaller pending rectangle */
static VOID
irc_queue_full_redraw(VOID)
{
	irc_redraw_pending = irc_work;
}

/* turn the controller's region mask into per-pane damage boxes */
static VOID
irc_queue_app_redraw(UWORD dirty)
{
	GRECT request;
	WORD input_top;
	WORD topic_bottom;
	WORD right_edge;

	if (!dirty)
		return;
	if ((dirty & GEM_IRC_APP_DIRTY_ALL) == GEM_IRC_APP_DIRTY_ALL) {
		irc_queue_full_redraw();
		return;
	}

	input_top = irc_input_top_y();
	topic_bottom = irc_work.g_y + irc_character_height + IRC_TOPIC_PADDING;
	right_edge = irc_work.g_x + irc_work.g_w - IRC_SIDE_WIDTH;

	if (dirty & GEM_IRC_APP_DIRTY_TOPIC) {
		request.g_x = irc_work.g_x;
		request.g_y = irc_work.g_y;
		request.g_w = irc_work.g_w;
		request.g_h = irc_character_height + IRC_TOPIC_PADDING + 1;
		irc_queue_redraw(&request);
	}
	if (dirty & GEM_IRC_APP_DIRTY_TRANSCRIPT) {
		request.g_x = irc_work.g_x + IRC_SIDE_WIDTH;
		request.g_y = topic_bottom;
		request.g_w =
			irc_work.g_w - IRC_SIDE_WIDTH - IRC_SIDE_WIDTH + 1;
		request.g_h = input_top - topic_bottom + 1;
		irc_queue_redraw(&request);
	}
	if (dirty & GEM_IRC_APP_DIRTY_TARGET) {
		request.g_x = irc_work.g_x;
		request.g_y = topic_bottom;
		request.g_w = IRC_SIDE_WIDTH + 1;
		request.g_h = input_top - topic_bottom + 1;
		irc_queue_redraw(&request);
	}
	if (dirty & GEM_IRC_APP_DIRTY_ROSTER) {
		request.g_x = right_edge;
		request.g_y = topic_bottom;
		request.g_w = IRC_SIDE_WIDTH;
		request.g_h = input_top - topic_bottom + 1;
		irc_queue_redraw(&request);
	}
	if (dirty & GEM_IRC_APP_DIRTY_INPUT) {
		request.g_x = irc_work.g_x;
		request.g_y = input_top;
		request.g_w = irc_work.g_w;
		request.g_h = irc_work.g_y + irc_work.g_h - input_top;
		irc_queue_redraw(&request);
	}
	/* a tab change repaints the whole work area once */
	if (dirty & GEM_IRC_APP_DIRTY_TABS) {
		request.g_x = irc_work.g_x;
		request.g_y = irc_work.g_y;
		request.g_w = irc_work.g_w;
		request.g_h = irc_work.g_h;
		irc_queue_redraw(&request);
	}
}

static VOID
irc_fill(WORD x, WORD y, WORD width, WORD height, WORD color)
{
	WORD xy[4];

	if (width <= 0 || height <= 0)
		return;
	vsf_interior(irc_vdi, FIS_SOLID);
	vsf_color(irc_vdi, color);
	xy[0] = x;
	xy[1] = y;
	xy[2] = x + width - 1;
	xy[3] = y + height - 1;
	vr_recfl(irc_vdi, xy);
}

static UWORD
irc_visible_rows(VOID)
{
	WORD remaining;
	UWORD rows;

	/* the transcript band is the gap between the topic strip and the editor */
	remaining = irc_input_top_y()
		- (irc_work.g_y + irc_character_height + IRC_TOPIC_PADDING);
	rows = 0;
	while (remaining >= irc_character_height
		&& rows < GEM_IRC_APP_SCROLL_LINES) {
		remaining -= irc_character_height;
		rows++;
	}
	return rows ? rows : 1U;
}

static UWORD
irc_text_columns(WORD pixels, UWORD maximum)
{
	UWORD columns;

	/* room for the text alignment padding */
	pixels -= 7;
	columns = 0;
	while (pixels >= irc_character_width && columns < maximum) {
		pixels -= irc_character_width;
		columns++;
	}
	return columns;
}

/*
 * central transcript width in character cells, the app controller splits
 * its rows at the same count
 */
static UWORD
irc_transcript_columns(VOID)
{
	WORD pixels;

	pixels = irc_work.g_w - IRC_SIDE_WIDTH - IRC_SIDE_WIDTH
		- IRC_PANE_PADDING - IRC_PANE_PADDING;
	if (pixels <= 0)
		return 1U;
	return irc_text_columns(pixels, GEM_IRC_APP_DISPLAY_SIZE - 1U);
}

#if defined(IRC_AES_INPUT) && IRC_AES_INPUT
/*
 * the input line as a native AES OBJECT tree, one G_FTEXT field pointed
 * straight at irc_app.input, only the keys objc_edit understands go
 * through it, everything else stays with gem_irc_app_key
 */
enum {
	IRC_IN_ROOT = 0,
	IRC_IN_FIELD = 1,
	IRC_IN_COUNT
};

#define IRC_INPUT_TMPLT_MAX 128	/* template covers the visible columns */

static OBJECT irc_input_tree[IRC_IN_COUNT] __attribute__((aligned(2)));
static TEDINFO irc_input_ted __attribute__((aligned(2)));
static BYTE irc_input_tmplt[IRC_INPUT_TMPLT_MAX + 1];
static BYTE irc_input_valid[] = "X";	/* "X" allows any character anywhere */
static WORD irc_input_idx;	/* objc_edit caret, mirrors input_cursor */
static WORD irc_input_ready;
static WORD irc_input_field_x;	/* screen x of the first editable glyph */

/* build the two-node tree once, geometry is refreshed on every redraw */
static VOID
irc_input_build(VOID)
{
	OBJECT *root = &irc_input_tree[IRC_IN_ROOT];
	OBJECT *field = &irc_input_tree[IRC_IN_FIELD];

	root->ob_next = NIL;
	root->ob_head = IRC_IN_FIELD;
	root->ob_tail = IRC_IN_FIELD;
	root->ob_type = G_IBOX;	/* transparent container, no paint */
	root->ob_flags = NONE;
	root->ob_state = NORMAL;
	root->ob_spec.lo = 0;	/* G_IBOX: no frame, no fill */
	root->ob_spec.hi = 0;
	root->ob_x = 0;
	root->ob_y = 0;
	root->ob_width = 0;
	root->ob_height = 0;

	irc_input_ted.te_ptext = gem_near_pointer_words(irc_app.input);
	irc_input_ted.te_ptmplt = gem_near_pointer_words(irc_input_tmplt);
	irc_input_ted.te_pvalid = gem_near_pointer_words(irc_input_valid);
	irc_input_ted.te_font = IBM;
	irc_input_ted.te_junk1 = 0;
	irc_input_ted.te_just = TE_LEFT;
	irc_input_ted.te_color = 0x1100;
	irc_input_ted.te_junk2 = 0;
	irc_input_ted.te_thickness = 0;	/* G_FTEXT: no box border */
	irc_input_ted.te_txtlen = GEM_IRC_APP_INPUT_SIZE;
	irc_input_ted.te_tmplen = 1;

	field->ob_next = IRC_IN_ROOT;
	field->ob_head = NIL;
	field->ob_tail = NIL;
	field->ob_type = G_FTEXT;
	field->ob_flags = EDITABLE | LASTOB;
	field->ob_state = NORMAL;
	field->ob_spec = gem_near_pointer_words(&irc_input_ted);
	field->ob_x = 0;
	field->ob_y = 0;
	field->ob_width = 0;
	field->ob_height = 0;

	irc_input_tmplt[0] = '_';
	irc_input_tmplt[1] = '\0';
	irc_input_ready = 1;
	/* EDINIT puts the caret at the current text length (empty => zero) */
	irc_input_idx = 0;
	(void) objc_edit(irc_input_tree, IRC_IN_FIELD, 0, &irc_input_idx,
		EDINIT);
}

static VOID
irc_input_ensure(VOID)
{
	if (!irc_input_ready)
		irc_input_build();
}

/* refresh the field size and the underscore template to the visible columns */
static VOID
irc_input_layout(WORD strip_x, WORD strip_y, WORD prompt_x, WORD cols,
	WORD cell_w, WORD cell_h)
{
	OBJECT *root = &irc_input_tree[IRC_IN_ROOT];
	OBJECT *field = &irc_input_tree[IRC_IN_FIELD];
	WORD index;

	irc_input_ensure();
	if (cols < 1)
		cols = 1;
	if (cols > IRC_INPUT_TMPLT_MAX)
		cols = IRC_INPUT_TMPLT_MAX;
	index = 0;
	while (index < cols) {
		irc_input_tmplt[index] = '_';
		index++;
	}
	irc_input_tmplt[cols] = '\0';
	irc_input_ted.te_tmplen = (WORD) (cols + 1);

	root->ob_x = (UWORD) strip_x;
	root->ob_y = (UWORD) strip_y;
	root->ob_width = (UWORD) (prompt_x - strip_x + cols * cell_w);
	root->ob_height = (UWORD) cell_h;

	field->ob_x = (UWORD) (prompt_x - strip_x);
	field->ob_y = 0;
	field->ob_width = (UWORD) (cols * cell_w);
	field->ob_height = (UWORD) cell_h;

	irc_input_field_x = prompt_x;
}

/* the keys objc_edit can handle itself */
static WORD
irc_input_is_edit_key(UWORD key)
{
	UWORD ascii = key & 0x00ffU;
	UWORD scan = key & 0xff00U;

	if (ascii >= 0x20U && ascii <= 0x7eU)
		return 1;	/* printable insertion */
	if (ascii == 0x08U)
		return 1;	/* backspace (0x0e08) */
	if (scan == 0x5300U)
		return 1;	/* delete */
	if (scan == 0x4b00U)
		return 1;	/* cursor left */
	if (scan == 0x4d00U)
		return 1;	/* cursor right */
	return 0;
}

/* recount the model's length and cursor after objc_edit changed the buffer */
static VOID
irc_input_resync(VOID)
{
	UWORD length = 0;

	while (irc_app.input[length])
		length++;
	irc_app.input_length = length;
	if (irc_input_idx < 0)
		irc_input_idx = 0;
	if ((UWORD) irc_input_idx > length)
		irc_input_idx = (WORD) length;
	irc_app.input_cursor = (UWORD) irc_input_idx;
}

static WORD
irc_aes_key(UWORD key)
{
	WORD result;

	irc_input_ensure();
	if (irc_input_is_edit_key(key)) {
		/* objc_edit changes irc_app.input in place through te_ptext */
		(void) objc_edit(irc_input_tree, IRC_IN_FIELD, (WORD) key,
			&irc_input_idx, EDCHAR);
		irc_input_resync();
		irc_app.dirty |= GEM_IRC_APP_DIRTY_INPUT;
		return GEM_IRC_APP_KEY_HANDLED;
	}
	/* submit, close, scroll, history, and line kills stay with the model */
	result = gem_irc_app_key(&irc_app, key, irc_visible_rows());
	irc_input_idx = (WORD) irc_app.input_cursor;
	return result;
}

/* place the caret from a work-area click, objc_find does the hit-test */
static VOID
irc_aes_click(WORD mouse_x, WORD mouse_y)
{
	WORD hit;
	WORD relative;
	WORD column;

	irc_input_ensure();
	hit = objc_find(irc_input_tree, ROOT, MAX_DEPTH, mouse_x, mouse_y);
	if (hit != IRC_IN_FIELD)
		return;
	relative = mouse_x - irc_input_field_x;
	column = 0;
	while (relative >= irc_character_width
		&& column < (WORD) irc_app.input_length) {
		relative -= irc_character_width;
		column++;
	}
	irc_input_idx = column;
	irc_app.input_cursor = (UWORD) column;
	irc_app.dirty |= GEM_IRC_APP_DIRTY_INPUT;
}
#endif				/* IRC_AES_INPUT */

/*
 * toolbar, one G_BUTTON per open conversation plus four action buttons
 * under a transparent G_IBOX root that spans the work area, so one tree
 * and one draw cover both
 */
enum {
	IRC_TB_ROOT = 0,
	IRC_TB_TAB0,
	IRC_TB_TAB1,
	IRC_TB_TAB2,
	IRC_TB_TAB3,
	IRC_TB_TAB4,
	IRC_TB_SEND,
	IRC_TB_NAMES,
	IRC_TB_PART,
	IRC_TB_CLOSE,
	IRC_TB_COUNT
};

#define IRC_TB_TAB_SLOTS  GEM_IRC_APP_BUFFERS	/* one button per buffer slot */
#define IRC_TB_LABEL_MAX  16

static OBJECT irc_tb_tree[IRC_TB_COUNT] __attribute__((aligned(2)));
static BYTE irc_tb_label[IRC_TB_TAB_SLOTS][IRC_TB_LABEL_MAX + 1];
static BYTE irc_tb_send[] = " Send ";
static BYTE irc_tb_names[] = " Names ";
static BYTE irc_tb_part[] = " Part ";
static BYTE irc_tb_close[] = " Close ";
static WORD irc_tb_ready;

/* pixel width of a NUL-terminated label */
static WORD
irc_tb_text_px(const BYTE *text)
{
	WORD width;

	width = 0;
	while (*text++)
		width += irc_character_width;
	return width;
}

static VOID
irc_tb_action(WORD index, WORD next, BYTE *label)
{
	OBJECT *object = &irc_tb_tree[index];

	object->ob_next = next;
	object->ob_head = NIL;
	object->ob_tail = NIL;
	object->ob_type = G_BUTTON;
	object->ob_flags = SELECTABLE | EXIT;
	object->ob_state = NORMAL;
	object->ob_spec = gem_near_pointer_words(label);
	object->ob_x = 0;
	object->ob_y = 0;
	object->ob_width = 0;
	object->ob_height = 0;
}

/* build the fixed sibling chain once, geometry is refreshed on every draw */
static VOID
irc_tb_build(VOID)
{
	OBJECT *root = &irc_tb_tree[IRC_TB_ROOT];
	OBJECT *tab;
	WORD index;

	root->ob_next = NIL;
	root->ob_head = IRC_TB_TAB0;
	root->ob_tail = IRC_TB_CLOSE;
	root->ob_type = G_IBOX;	/* transparent container, no paint */
	root->ob_flags = NONE;
	root->ob_state = NORMAL;
	root->ob_spec.lo = 0;
	root->ob_spec.hi = 0;
	root->ob_x = 0;
	root->ob_y = 0;
	root->ob_width = 0;
	root->ob_height = 0;

	index = 0;
	while (index < IRC_TB_TAB_SLOTS) {
		tab = &irc_tb_tree[IRC_TB_TAB0 + index];
		irc_tb_label[index][0] = '\0';
		tab->ob_next = IRC_TB_TAB0 + index + 1;	/* last tab -> IRC_TB_SEND */
		tab->ob_head = NIL;
		tab->ob_tail = NIL;
		tab->ob_type = G_BUTTON;
		tab->ob_flags = SELECTABLE | EXIT | HIDETREE;	/* hidden until used */
		tab->ob_state = NORMAL;
		tab->ob_spec = gem_near_pointer_words(irc_tb_label[index]);
		tab->ob_x = 0;
		tab->ob_y = 0;
		tab->ob_width = 0;
		tab->ob_height = 0;
		index++;
	}

	irc_tb_action(IRC_TB_SEND, IRC_TB_NAMES, irc_tb_send);
	irc_tb_action(IRC_TB_NAMES, IRC_TB_PART, irc_tb_names);
	irc_tb_action(IRC_TB_PART, IRC_TB_CLOSE, irc_tb_part);
	irc_tb_action(IRC_TB_CLOSE, IRC_TB_ROOT, irc_tb_close);
	irc_tb_tree[IRC_TB_CLOSE].ob_flags |= LASTOB;
	irc_tb_ready = 1;
}

static VOID
irc_tb_ensure(VOID)
{
	if (!irc_tb_ready)
		irc_tb_build();
}

/* copy a tab label, star in front of an unread private buffer */
static VOID
irc_tb_set_label(WORD slot, const char *name, WORD activity)
{
	BYTE *destination = irc_tb_label[slot];
	WORD length = 0;

	if (activity)
		destination[length++] = '*';
	while (*name && length < IRC_TB_LABEL_MAX)
		destination[length++] = (BYTE) *name++;
	destination[length] = '\0';
}

static VOID
irc_tb_place(WORD index, WORD x, WORD y, WORD width, WORD height, WORD state)
{
	OBJECT *object = &irc_tb_tree[index];

	object->ob_x = (UWORD) x;
	object->ob_y = (UWORD) y;
	object->ob_width = (UWORD) width;
	object->ob_height = (UWORD) height;
	object->ob_state = state;
	object->ob_flags &= ~HIDETREE;
}

/*
 * refresh tab and button geometry, object coords are relative to the
 * root, pinned to the work origin, tabs past the open buffer count or
 * below the editor get HIDETREE
 */
static VOID
irc_tb_layout(VOID)
{
	OBJECT *root = &irc_tb_tree[IRC_TB_ROOT];
	OBJECT *tab;
	WORD topic_bottom;
	WORD input_top;
	WORD toolbar_top;
	WORD tab_height;
	WORD tab_y;
	WORD button_y;
	WORD button_height;
	WORD button_x;
	WORD button_width;
	WORD count;
	WORD active;
	WORD index;

	irc_tb_ensure();
	topic_bottom = irc_work.g_y + irc_character_height + IRC_TOPIC_PADDING;
	input_top = irc_input_top_y();
	toolbar_top = irc_toolbar_top_y();
	tab_height = irc_character_height + 2;
	tab_y = topic_bottom + irc_character_height - irc_work.g_y;
	count = (WORD) gem_irc_app_buffer_count(&irc_app);
	active = (WORD) gem_irc_app_active_index(&irc_app);

	index = 0;
	while (index < IRC_TB_TAB_SLOTS) {
		tab = &irc_tb_tree[IRC_TB_TAB0 + index];
		if (index < count
			&& irc_work.g_y + tab_y + tab_height < input_top) {
			irc_tb_set_label(index,
				gem_irc_app_buffer_label(&irc_app,
					(GEM_IRC_UWORD) index),
				gem_irc_app_buffer_activity(&irc_app,
					(GEM_IRC_UWORD)
					index));
			irc_tb_place(IRC_TB_TAB0 + index, 2, tab_y,
				IRC_SIDE_WIDTH - 4, tab_height,
				index == active ? SELECTED : NORMAL);
			tab_y += tab_height;
		} else
			tab->ob_flags |= HIDETREE;
		index++;
	}

	button_y = toolbar_top - irc_work.g_y + 1;
	button_height = irc_character_height + 2;
	button_x = 4;
	button_width = irc_tb_text_px(irc_tb_send) + 8;
	irc_tb_place(IRC_TB_SEND, button_x, button_y, button_width,
		button_height, NORMAL);
	button_x += button_width + 4;
	button_width = irc_tb_text_px(irc_tb_names) + 8;
	irc_tb_place(IRC_TB_NAMES, button_x, button_y, button_width,
		button_height, NORMAL);
	button_x += button_width + 4;
	button_width = irc_tb_text_px(irc_tb_part) + 8;
	irc_tb_place(IRC_TB_PART, button_x, button_y, button_width,
		button_height, NORMAL);
	button_x += button_width + 4;
	button_width = irc_tb_text_px(irc_tb_close) + 8;
	irc_tb_place(IRC_TB_CLOSE, button_x, button_y, button_width,
		button_height, NORMAL);

	root->ob_x = (UWORD) irc_work.g_x;
	root->ob_y = (UWORD) irc_work.g_y;
	root->ob_width = (UWORD) irc_work.g_w;
	root->ob_height = (UWORD) irc_work.g_h;
}

static VOID
irc_tb_draw(VOID)
{
	irc_tb_layout();
	objc_draw(irc_tb_tree, ROOT, MAX_DEPTH, irc_work.g_x, irc_work.g_y,
		irc_work.g_w, irc_work.g_h);
}

/*
 * route one left-button click through objc_find, a miss is left for the
 * caller (the AES input field) to take
 */
static WORD
irc_tb_click(WORD mouse_x, WORD mouse_y)
{
	WORD hit;
	WORD slot;

	irc_tb_ensure();
	hit = objc_find(irc_tb_tree, ROOT, MAX_DEPTH, mouse_x, mouse_y);
	if (hit < IRC_TB_TAB0)
		return 0;
	if (hit <= IRC_TB_TAB4) {
		slot = hit - IRC_TB_TAB0;
		if ((GEM_IRC_UWORD) slot < gem_irc_app_buffer_count(&irc_app))
			gem_irc_app_switch(&irc_app, (GEM_IRC_UWORD) slot);
		return 1;
	}
	switch (hit) {
	case IRC_TB_SEND:
		(void) gem_irc_app_submit(&irc_app);
		break;
	case IRC_TB_NAMES:
		(void) gem_irc_app_names(&irc_app);
		break;
	case IRC_TB_PART:
		(void) gem_irc_app_part_active(&irc_app);
		break;
	case IRC_TB_CLOSE:
		(void) gem_irc_app_close_buffer(&irc_app,
			gem_irc_app_active_index(&irc_app));
		break;
	default:
		return 0;
	}
	return 1;
}

/*
 * VDI text has no right-edge argument, so copy only the visible columns
 * into a scratch line before drawing
 */
static VOID
irc_draw_limited(WORD x, WORD y, const char *text, UWORD columns)
{
	UWORD length;

	if (!text || !columns)
		return;
	x = (WORD) (((UWORD) x + 7U) & 0xfff8U);
	if (columns >= IRC_DRAW_TEXT_SIZE)
		columns = IRC_DRAW_TEXT_SIZE - 1U;
	length = 0;
	while (text[length] && length < columns) {
		irc_draw_text[length] = (BYTE) text[length];
		length++;
	}
	irc_draw_text[length] = '\0';
	v_gtext(irc_vdi, x, y, irc_draw_text);
}

static WORD
irc_line_color(UBYTE kind)
{
	if (kind == GEM_IRC_APP_LINE_ERROR)
		return RED;
	if (kind == GEM_IRC_APP_LINE_ACTION)
		return GREEN;
	if (kind == GEM_IRC_APP_LINE_NOTICE)
		return BLUE;
	if (kind == GEM_IRC_APP_LINE_STATUS)
		return BLUE;
	return BLACK;
}

/* draw the whole work area, the active VDI clip limits what really lands */
static VOID
irc_draw_content(VOID)
{
	const GEM_IRC_APP_LINE *line;
	const char *input;
	const char *target;
	const char *topic;
	const char *nick;
	GEM_IRC_APP_NICK *nick_slot;
	UWORD rows;
	UWORD count;
	UWORD row;
	UWORD columns;
	UWORD pane_columns;
	UWORD nick_count;
	UWORD cursor;
	UWORD first;
	/* volatile stops gcc turning the repeated add into a MUL */
	volatile UWORD shown_cursor;
	WORD x;
	WORD y;
	WORD left_edge;
	WORD right_edge;
	WORD input_top;
	WORD topic_bottom;
	WORD center_width;
	WORD xy[4];

	irc_fill(irc_work.g_x, irc_work.g_y, irc_work.g_w, irc_work.g_h, WHITE);
	left_edge = irc_work.g_x + IRC_SIDE_WIDTH;
	right_edge = irc_work.g_x + irc_work.g_w - IRC_SIDE_WIDTH;
	input_top = irc_input_top_y();
	topic_bottom = irc_work.g_y + irc_character_height + IRC_TOPIC_PADDING;
	center_width =
		right_edge - left_edge - IRC_PANE_PADDING - IRC_PANE_PADDING;

	/* central transcript, the newest rows clipped to the center pane */
	rows = irc_visible_rows();
	count = gem_irc_app_visible_count(&irc_app, rows);
	x = left_edge + IRC_PANE_PADDING;
	y = topic_bottom + irc_character_height;
	columns = irc_transcript_columns();
	row = 0;
	while (row < count) {
		line = gem_irc_app_visible_line(&irc_app, row, rows);
		if (line) {
			vst_color(irc_vdi, irc_line_color(line->kind));
			irc_draw_limited(x, y, line->text, columns);
		}
		y += irc_character_height;
		row++;
	}

	/* blue pane headers */
	irc_fill(irc_work.g_x, irc_work.g_y, irc_work.g_w,
		irc_character_height + IRC_TOPIC_PADDING, BLUE);

	pane_columns = irc_text_columns(IRC_SIDE_WIDTH
		- IRC_PANE_PADDING - IRC_PANE_PADDING, GEM_IRC_NICK_SIZE - 1U);
	vst_color(irc_vdi, WHITE);
	v_gtext(irc_vdi, irc_work.g_x + IRC_PANE_PADDING,
		irc_work.g_y + irc_character_height, (BYTE *) "CHATS");
	v_gtext(irc_vdi, right_edge + IRC_PANE_PADDING,
		irc_work.g_y + irc_character_height, (BYTE *) "USERS");

	/* the left pane's tabs are G_BUTTON objects drawn by irc_tb_draw() below */

	/* the right roster only applies while a channel buffer is active */
	if (gem_irc_app_active_kind(&irc_app) == GEM_IRC_APP_BUFFER_CHANNEL) {
		nick_count = (UWORD) irc_app.nick_count;
		nick_slot = irc_app.nicks;
		y = topic_bottom + irc_character_height;
		row = 0;
		while (row < nick_count && y < input_top - 2) {
			nick = nick_slot->text;
			irc_draw_limited(right_edge + IRC_PANE_PADDING, y, nick,
				pane_columns);
			nick_slot++;
			y += irc_character_height;
			row++;
		}
		if (irc_app.nick_overflow)
			irc_draw_limited(right_edge + IRC_PANE_PADDING,
				input_top - 2, "+ more", pane_columns);
	}

	target = gem_irc_app_target(&irc_app);
	if (!target || !*target)
		target = (const char *) irc_channel;

	/* topic/status strip, the saved topic if any else the target */
	vst_color(irc_vdi, WHITE);
	topic = gem_irc_app_topic(&irc_app);
	if (!topic || !*topic)
		topic = target;
	columns = irc_text_columns(center_width - 88,
		GEM_IRC_APP_DISPLAY_SIZE - 1U);
	irc_draw_limited(left_edge + IRC_PANE_PADDING,
		irc_work.g_y + irc_character_height, topic, columns);
	v_gtext(irc_vdi, right_edge - 80,
		irc_work.g_y + irc_character_height,
		(BYTE *) (irc_app.connected ? "Online" : "Connect"));

	/* divider lines */
	vsl_color(irc_vdi, BLACK);
	xy[0] = left_edge;
	xy[1] = irc_work.g_y;
	xy[2] = left_edge;
	xy[3] = input_top;
	v_pline(irc_vdi, 2, xy);
	xy[0] = right_edge;
	xy[2] = right_edge;
	v_pline(irc_vdi, 2, xy);
	xy[0] = left_edge;
	xy[1] = topic_bottom;
	xy[2] = right_edge;
	xy[3] = topic_bottom;
	v_pline(irc_vdi, 2, xy);

	/* full-width single-line editor across the bottom */
	xy[0] = irc_work.g_x;
	xy[1] = input_top;
	xy[2] = irc_work.g_x + irc_work.g_w - 1;
	xy[3] = input_top;
	v_pline(irc_vdi, 2, xy);

	y = input_top + irc_character_height + 2;
	vst_color(irc_vdi, BLACK);
	x = (WORD) (((UWORD) (irc_work.g_x + IRC_PANE_PADDING) + 7U) & 0xfff8U);
	v_gtext(irc_vdi, x, y, (BYTE *) "> ");
	x += irc_character_width;
	x += irc_character_width;
	columns =
		irc_text_columns(irc_work.g_w - 12,
		GEM_IRC_APP_INPUT_SIZE - 1U);
	if (columns > 2U)
		columns -= 2U;
	else
		columns = 1U;
#if defined(IRC_AES_INPUT) && IRC_AES_INPUT
	/* objc_draw paints the input line right of the "> " prompt */
	irc_input_layout(irc_work.g_x, input_top + 1, x, (WORD) columns,
		irc_character_width, irc_character_height + 2);
	objc_draw(irc_input_tree, ROOT, MAX_DEPTH, irc_work.g_x, input_top + 1,
		irc_work.g_w, irc_character_height + 3);
	(void) cursor;
	(void) first;
	(void) input;
	(void) shown_cursor;
#else
	cursor = gem_irc_app_input_cursor(&irc_app);
	first = 0;
	if (cursor >= columns)
		first = cursor - columns + 1U;
	input = gem_irc_app_input(&irc_app);
	row = 0;
	while (row < first && *input) {
		input++;
		row++;
	}
	irc_draw_limited(x, y, input, columns);
	shown_cursor = cursor - first;
	while (shown_cursor) {
		x += irc_character_width;
		shown_cursor--;
	}
	xy[0] = x;
	xy[1] = input_top + 3;
	xy[2] = x;
	xy[3] = input_top + irc_character_height + 2;
	v_pline(irc_vdi, 2, xy);
#endif

	/* bottom toolbar, a separator line then the action buttons */
	{
		WORD toolbar_top = irc_toolbar_top_y();

		vsl_color(irc_vdi, BLACK);
		xy[0] = irc_work.g_x;
		xy[1] = toolbar_top;
		xy[2] = irc_work.g_x + irc_work.g_w - 1;
		xy[3] = toolbar_top;
		v_pline(irc_vdi, 2, xy);
	}
	irc_tb_draw();
}

static VOID
irc_redraw(VOID)
{
	GRECT visible;
	GRECT clipped;
	WORD clip[4];

	if (irc_window < 0)
		return;
	wind_get(irc_window, WF_FIRSTXYWH, &visible.g_x, &visible.g_y,
		&visible.g_w, &visible.g_h);
	while (visible.g_w > 0 && visible.g_h > 0) {
		clipped = visible;
		/* AES visible rectangles already sit inside the current work area */
		if (rc_intersect(&irc_redraw_pending, &clipped)) {
			clip[0] = clipped.g_x;
			clip[1] = clipped.g_y;
			clip[2] = clipped.g_x + clipped.g_w - 1;
			clip[3] = clipped.g_y + clipped.g_h - 1;
			vs_clip(irc_vdi, TRUE, clip);
			irc_draw_content();
		}
		wind_get(irc_window, WF_NEXTXYWH, &visible.g_x, &visible.g_y,
			&visible.g_w, &visible.g_h);
	}
	vs_clip(irc_vdi, FALSE, (WORD *) 0);
}

static VOID
irc_update_work(VOID)
{
	wind_get(irc_window, WF_WXYWH, &irc_work.g_x, &irc_work.g_y,
		&irc_work.g_w, &irc_work.g_h);
}

static VOID
irc_geometry(const WORD *message)
{
	WORD x;
	WORD y;
	WORD width;
	WORD height;

	x = message[4];
	y = message[5];
	width = message[6];
	height = message[7];
	if (width < IRC_WINDOW_MIN_WIDTH)
		width = IRC_WINDOW_MIN_WIDTH;
	if (height < IRC_WINDOW_MIN_HEIGHT)
		height = IRC_WINDOW_MIN_HEIGHT;
	wind_set(irc_window, WF_CXYWH, x, y, width, height);
	irc_update_work();
	gem_irc_app_set_display_columns(&irc_app, irc_transcript_columns());
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
	irc_update_work();
	gem_irc_app_set_display_columns(&irc_app, irc_transcript_columns());
}

static VOID
irc_handle_message(WORD *message)
{
	UWORD rows;

	if (message[3] != irc_window)
		return;
	switch (message[0]) {
	case WM_REDRAW:
		/* AES puts the damage GRECT in message words four through seven */
		irc_queue_redraw((GRECT *) &message[4]);
		break;
	case WM_TOPPED:
	case WM_NEWTOP:
		wind_set(irc_window, WF_TOP, irc_window, 0, 0, 0);
		break;
	case WM_CLOSED:
		(void) gem_irc_app_request_close(&irc_app, "Window closed");
		break;
	case WM_FULLED:
		irc_full();
		irc_queue_full_redraw();
		break;
	case WM_MOVED:
	case WM_SIZED:
		irc_geometry(message);
		irc_queue_full_redraw();
		break;
	case WM_ARROWED:
		rows = irc_visible_rows();
		if (message[4] == WA_UPPAGE)
			gem_irc_app_scroll_up(&irc_app, rows, rows);
		else if (message[4] == WA_DNPAGE)
			gem_irc_app_scroll_down(&irc_app, rows, rows);
		else if (message[4] == WA_UPLINE)
			gem_irc_app_scroll_up(&irc_app, 1U, rows);
		else if (message[4] == WA_DNLINE)
			gem_irc_app_scroll_down(&irc_app, 1U, rows);
		break;
	}
}

/* the bindings reach /bin/gem over the two pipes, install before appl_init() */
extern WORD gem_client_install(VOID);

static WORD
irc_open_window(VOID)
{
	WORD index;
	GRECT border;
	GEM_U32_WORDS title;

	if (!gem_client_install())
		return FALSE;
	(void) chdir("/lib/gemsys");
	irc_application = appl_init(NULL);
	if (irc_application < 0)
		return FALSE;
	irc_vdi = graf_handle(&irc_character_width, &irc_character_height,
		&irc_box_width, &irc_box_height);
	index = 0;
	while (index < 10) {
		irc_work_in[index] = 1;
		index++;
	}
	irc_work_in[10] = 2;
	if (!v_opnvwk(irc_work_in, &irc_vdi, irc_work_out))
		return FALSE;
	if (irc_character_width <= 0)
		irc_character_width = 8;
	if (irc_character_height <= 0)
		irc_character_height = 16;
	/*
	 * v_opnvwk() sets no text size or alignment, so rows drift without
	 * them, set the cell height and top-left alignment, this client
	 * passes cell-top y coordinates not a baseline
	 */
	{
		WORD pts_w, pts_h, cell_w, cell_h, prev_h, prev_v;
		WORD height = irc_work_out[0x30];
		if (height > 0) {
			vst_height(irc_vdi, height, &pts_w, &pts_h, &cell_w,
				&cell_h);
			if (cell_w > 0)
				irc_character_width = cell_w;
			if (cell_h > 0)
				irc_character_height = cell_h;
		}
		vst_alignment(irc_vdi, 0, 5, &prev_h, &prev_v);
	}
	vswr_mode(irc_vdi, MD_REPLACE);
	graf_mouse(ARROW, NULL);

	wind_get(0, WF_WXYWH, &irc_desktop.g_x, &irc_desktop.g_y,
		&irc_desktop.g_w, &irc_desktop.g_h);
	border.g_x = irc_desktop.g_x + IRC_WINDOW_MARGIN;
	border.g_y = irc_desktop.g_y + IRC_WINDOW_MARGIN;
	border.g_w = irc_desktop.g_w - IRC_WINDOW_MARGIN - IRC_WINDOW_MARGIN;
	border.g_h = irc_desktop.g_h - IRC_WINDOW_MARGIN - IRC_WINDOW_MARGIN;
	if (border.g_w < IRC_WINDOW_MIN_WIDTH)
		border.g_w = IRC_WINDOW_MIN_WIDTH;
	if (border.g_h < IRC_WINDOW_MIN_HEIGHT)
		border.g_h = IRC_WINDOW_MIN_HEIGHT;
	irc_window = wind_create(IRC_WINDOW_KIND, irc_desktop.g_x,
		irc_desktop.g_y, irc_desktop.g_w, irc_desktop.g_h);
	if (irc_window <= 0)
		return FALSE;
	title = gem_near_pointer_words(irc_title);
	wind_set(irc_window, WF_NAME, title.lo, title.hi, 0, 0);
	if (!wind_open(irc_window, border.g_x, border.g_y, border.g_w,
			border.g_h))
		return FALSE;
	irc_update_work();
	irc_previous = border;
	irc_was_full = FALSE;
	return TRUE;
}

static VOID
irc_close_window(VOID)
{
	if (irc_window > 0) {
		(void) wind_close(irc_window);
		(void) wind_delete(irc_window);
		irc_window = -1;
	}
	if (irc_vdi >= 0) {
		(void) v_clsvwk(irc_vdi);
		irc_vdi = -1;
	}
	if (irc_application >= 0) {
		(void) appl_exit();
		irc_application = -1;
	}
}

static VOID
irc_exec_desktop(VOID)
{
	static char desktop_path[] = "/bin/gemdesk";
	static char desktop_name[] = "gemdesk";
	char *arguments[2];

	arguments[0] = desktop_name;
	arguments[1] = (char *) 0;
	execve(desktop_path, arguments, environ);
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

	gem_irc_connection_reset(&irc_app.protocol);
	result = gem_irc_app_start(&irc_app, (const char *) irc_nick,
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
		(void) irc_start_registration();
	else if (result == GEM_IRC_TRANSPORT_PENDING)
		irc_connect_ticks = IRC_CONNECT_TICKS;
	else
		irc_connection_failed();
}

static VOID
irc_connect_step(VOID)
{
	GEM_IRC_WORD result;

	result = gem_irc_transport_progress(&irc_transport);
	if (result == GEM_IRC_TRANSPORT_OK) {
		(void) irc_start_registration();
		return;
	}
	if (result < 0) {
		irc_connection_failed();
		return;
	}
	if (!irc_timer_elapsed(&irc_connect_ticks))
		return;
	/* fifteen seconds is the fixed upper bound */
	gem_irc_transport_close(&irc_transport);
	irc_connection_failed();
}

static VOID
irc_reconnect_step(VOID)
{
	GEM_IRC_WORD result;

	if (!gem_irc_transport_needs_reconnect(&irc_transport))
		return;
	if (irc_reconnect_ticks && !irc_timer_elapsed(&irc_reconnect_ticks))
		return;
	result = gem_irc_transport_reconnect(&irc_transport);
	irc_connect_result(result);
}

static VOID
irc_network_step(VOID)
{
	GEM_IRC_WORD result;
	const char *target;

	/* no reconnect once the one-shot line was accepted and the link dropped */
	if (irc_smoke_enabled && irc_smoke_sent
		&& !gem_irc_transport_is_connected(&irc_transport)) {
		(void) gem_irc_app_request_close(&irc_app,
			"One-shot disconnected");
		return;
	}

	if (gem_irc_transport_is_connecting(&irc_transport))
		irc_connect_step();
	else if (gem_irc_transport_is_connected(&irc_transport)) {
		(void) gem_irc_app_poll(&irc_app, GEM_IRC_APP_POLL_DEFAULT);
		if (!gem_irc_transport_is_connected(&irc_transport)) {
			irc_registration_started = 0;
			irc_join_sent = 0;
			irc_reconnect_ticks = IRC_RECONNECT_TICKS;
			gem_irc_app_connecting(&irc_app,
				(const char *) irc_server);
		}
	} else
		irc_reconnect_step();
	if (irc_registration_started && !irc_join_sent
		&& gem_irc_is_registered(&irc_app.protocol)) {
		result = gem_irc_app_join(&irc_app,
			(const char *) irc_channel, (const char *) 0);
		if (result > 0)
			irc_join_sent = 1;
	}

	if (!irc_smoke_enabled || gem_irc_app_is_closing(&irc_app))
		return;
	if (!irc_smoke_sent) {
		/*
		 * target only becomes the channel after the JOIN event, the
		 * sent flag is set once the transport queue takes the whole
		 * PRIVMSG, and reconnect handling never clears it
		 */
		target = gem_irc_app_target(&irc_app);
		if (irc_join_sent && gem_irc_is_registered(&irc_app.protocol)
			&& !strcmp(target, (const char *) irc_channel)) {
			result = gem_irc_app_send_message(&irc_app, target,
				(const char *)
				irc_smoke_message);
			if (result > 0) {
				irc_smoke_sent = 1;
				irc_smoke_ticks = IRC_SMOKE_DISPLAY_TICKS;
				return;
			}
		}
		if (!irc_timer_elapsed(&irc_smoke_ticks))
			return;
		(void) gem_irc_app_request_close(&irc_app, "One-shot timeout");
		return;
	}
	if (!irc_timer_elapsed(&irc_smoke_ticks))
		return;
	(void) gem_irc_app_request_close(&irc_app, "One-shot complete");
}

static VOID
irc_event_loop(VOID)
{
	WORD events;
	WORD key_result;
	WORD update_needed;
	WORD closing_before_events;
	UWORD dirty_regions;

	irc_quit_ticks = 0;
	for (;;) {
		irc_event_key = 0;
		irc_message[0] = 0;
		closing_before_events = gem_irc_app_is_closing(&irc_app);
		events = evnt_multi_ds(IRC_EVENT_FLAGS,
			IRC_EVENT_BCLK, IRC_EVENT_BMSK, IRC_EVENT_BST,
			0, 0, 0, 0, 0,
			0, 0, 0, 0, 0,
			irc_message, IRC_EVENT_TIMER_MS, 0,
			&irc_event_mouse_x, &irc_event_mouse_y,
			&irc_event_mouse_buttons, &irc_event_key_state,
			&irc_event_key, &irc_event_clicks);
		/* socket and DNS work never runs while WIND_UPDATE is held */
		if (events & MU_TIMER)
			irc_network_step();
		update_needed = irc_app.dirty || irc_redraw_pending.g_w > 0
			|| (events & (MU_MESAG | MU_KEYBD | MU_BUTTON));
		if (update_needed) {
			wind_update(IRC_BEG_UPDATE);
			if (events & MU_MESAG)
				irc_handle_message(irc_message);
			if (events & MU_KEYBD) {
#if defined(IRC_AES_INPUT) && IRC_AES_INPUT
				key_result = irc_aes_key(irc_event_key);
#else
				key_result =
					gem_irc_app_key(&irc_app, irc_event_key,
					irc_visible_rows());
#endif
				if (key_result == GEM_IRC_APP_KEY_CLOSE) {
					if (!gem_irc_app_is_closing(&irc_app))
						(void) gem_irc_app_request_close
							(&irc_app,
							"Keyboard close");
				}
			}
			if (events & MU_BUTTON) {
				WORD acted =
					irc_tb_click((WORD) irc_event_mouse_x,
					(WORD) irc_event_mouse_y);
#if defined(IRC_AES_INPUT) && IRC_AES_INPUT
				if (!acted)
					irc_aes_click((WORD) irc_event_mouse_x,
						(WORD) irc_event_mouse_y);
#else
				(void) acted;
#endif
			}
			/* take every dirty region once per event turn */
			dirty_regions =
				(UWORD) gem_irc_app_take_dirty(&irc_app);
			irc_queue_app_redraw(dirty_regions);
			if (irc_redraw_pending.g_w > 0) {
				irc_redraw();
				irc_redraw_pending.g_w = 0;
			}
			wind_update(IRC_END_UPDATE);
		}

		/* closing and queue-drain checks need no screen lock */
		if (gem_irc_app_is_closing(&irc_app)) {
			if (!irc_transport.transmit_length)
				break;
			else if (!closing_before_events)
				irc_quit_ticks = IRC_QUIT_FLUSH_TICKS;
			else if ((events & MU_TIMER)
				&& irc_timer_elapsed(&irc_quit_ticks))
				break;
		}
		(void) irc_event_mouse_x;
		(void) irc_event_mouse_y;
		(void) irc_event_mouse_buttons;
		(void) irc_event_key_state;
		(void) irc_event_clicks;
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
		if (irc_return_desktop) {
			irc_exec_desktop();
			return 1;
		}
		return 1;
	}
	gem_irc_transport_init(&irc_transport);
	gem_irc_transport_bind_app(&irc_transport, &irc_app_transport);
	gem_irc_app_init(&irc_app, &irc_app_transport);
	gem_irc_app_set_display_columns(&irc_app, irc_transcript_columns());
	irc_join_sent = 0;
	irc_registration_started = 0;
	irc_connect_ticks = 0;
	irc_reconnect_ticks = 0;
	gem_irc_app_connecting(&irc_app, (const char *) irc_server);
	irc_queue_full_redraw();
	irc_redraw();
	irc_redraw_pending.g_w = 0;
	(void) gem_irc_app_take_dirty(&irc_app);
	connect_result = gem_irc_transport_connect(&irc_transport,
		(const char *) irc_server, irc_port);
	irc_connect_result(connect_result);
	irc_event_loop();
	gem_irc_app_disconnect(&irc_app, "GEM IRC closed");
	gem_irc_transport_stop(&irc_transport);
	irc_close_window();
	/* socket, window, VDI, and AES are all released before the exec */
	if (irc_return_desktop) {
		irc_exec_desktop();
		return 1;
	}
	return 0;
}
