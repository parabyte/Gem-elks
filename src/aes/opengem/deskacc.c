/*
 * deskacc.c - built-in GEM desk accessories (Calculator and Clock).
 *
 * Following the FreeGEM desk-accessory idea, the Calculator and Clock live in
 * the Desk menu and open as ordinary GEM windows that coexist with the
 * Desktop's file windows - the Desktop stays live behind them.  The ELKS port
 * runs the Desktop as the sole GEM client, so the accessories are hosted
 * inside the Desktop process: the Desktop's event loop routes window
 * messages, work-area clicks, and timer ticks to the accessory whose window
 * they belong to, exactly as FreeGEM's AES multiplexes the desktop and its
 * resident accessories.
 *
 * The accessory windows are created directly with wind_create() and are not
 * Desktop WNODEs, so the Desktop's message and button handlers give this
 * module first refusal (acc_message/acc_button) before their own WNODE logic.
 *
 * This accessory code uses libc time and a 32-bit accumulator, so - like the
 * standalone GEM applications - it sits outside the resident-core source and
 * codegen gates that forbid wide scalars on the AES hot paths.
 */

#include "ppddesk.h"

#include <time.h>

#define ACC_CALC		1
#define ACC_CLOCK		2
#define ACC_SLOTS		2


#define ACC_DISP_H		22	/* calculator display strip height */
#define ACC_COLS		4
#define ACC_ROWS		4
#define ACC_FONT_W		8	/* BIOS ROM font cell width */
#define ACC_FONT_H		16	/* BIOS ROM font cell height */

typedef struct acc_slot {
	WORD	kind;		/* 0 closed, ACC_CALC, ACC_CLOCK */
	WORD	wh;		/* window handle */
	WORD	wkx, wky, wkw, wkh;
	long	accum;		/* calculator running total */
	long	shown;		/* calculator display value */
	BYTE	pending;	/* pending operator */
	WORD	fresh;		/* next digit starts a new number */
} ACC_SLOT;

static ACC_SLOT acc_slots[ACC_SLOTS];

static BYTE acc_calc_name[]  = " Calculator ";
static BYTE acc_clock_name[] = " Clock ";

static const BYTE acc_face[ACC_ROWS][ACC_COLS] = {
	{ '7', '8', '9', '/' },
	{ '4', '5', '6', '*' },
	{ '1', '2', '3', '-' },
	{ '0', 'C', '=', '+' }
};

static const BYTE acc_digit[10] =
	{ '0', '1', '2', '3', '4', '5', '6', '7', '8', '9' };

/* -- drawing helpers -------------------------------------------------- */

static VOID acc_draw(ACC_SLOT *a);

static VOID
acc_fill_work(ACC_SLOT *a)
{
	WORD xy[4];

	vswr_mode(gl_handle, 1);
	vsf_interior(gl_handle, 1);
	vsf_color(gl_handle, 0);
	xy[0] = a->wkx;
	xy[1] = a->wky;
	xy[2] = a->wkx + a->wkw - 1;
	xy[3] = a->wky + a->wkh - 1;
	vr_recfl(gl_handle, xy);
}

/* -- calculator ------------------------------------------------------- */

static VOID
acc_format(long value, BYTE *out)
{
	BYTE tmp[16];
	WORD i, n, neg;

	neg = 0;
	if (value < 0) {
		neg = 1;
		value = -value;
	}
	n = 0;
	do {
		tmp[n++] = acc_digit[(WORD) (value % 10)];
		value /= 10;
	} while (value && n < 14);
	if (neg)
		tmp[n++] = '-';
	for (i = 0; i < n; i++)
		out[i] = tmp[n - 1 - i];
	out[n] = 0;
}

static VOID
acc_calc_display(ACC_SLOT *a)
{
	BYTE text[16];
	WORD xy[4];
	WORD len;

	acc_format(a->shown, text);
	vswr_mode(gl_handle, 1);
	vsf_interior(gl_handle, 1);
	vsf_color(gl_handle, 0);
	xy[0] = a->wkx;
	xy[1] = a->wky;
	xy[2] = a->wkx + a->wkw - 1;
	xy[3] = a->wky + ACC_DISP_H - 1;
	vr_recfl(gl_handle, xy);
	len = 0;
	while (text[len])
		len++;
	vst_color(gl_handle, 1);
	v_gtext(gl_handle, a->wkx + a->wkw - 8 - 8 * len,
		a->wky + ACC_DISP_H - 6, text);
}

static VOID
acc_calc_buttons(ACC_SLOT *a)
{
	WORD r, c, cw, ch;
	WORD box[10];
	BYTE label[2];

	cw = a->wkw / ACC_COLS;
	ch = (a->wkh - ACC_DISP_H) / ACC_ROWS;
	label[1] = 0;
	vsl_color(gl_handle, 1);
	vst_color(gl_handle, 1);
	for (r = 0; r < ACC_ROWS; r++) {
		for (c = 0; c < ACC_COLS; c++) {
			WORD x0 = a->wkx + c * cw + 1;
			WORD y0 = a->wky + ACC_DISP_H + r * ch + 1;
			WORD x1 = a->wkx + c * cw + cw - 2;
			WORD y1 = a->wky + ACC_DISP_H + r * ch + ch - 2;

			box[0] = x0; box[1] = y0;
			box[2] = x1; box[3] = y0;
			box[4] = x1; box[5] = y1;
			box[6] = x0; box[7] = y1;
			box[8] = x0; box[9] = y0;
			v_pline(gl_handle, 5, box);
			label[0] = acc_face[r][c];
			v_gtext(gl_handle, x0 + (cw / 2) - 4,
				y0 + (ch / 2) + 4, label);
		}
	}
}

/* Paint the whole calculator face; the caller has set the clip rectangle. */
static VOID
acc_calc_paint(ACC_SLOT *a)
{
	acc_fill_work(a);
	acc_calc_buttons(a);
	acc_calc_display(a);
}

static VOID
acc_calc_press(ACC_SLOT *a, BYTE key)
{
	if (key >= '0' && key <= '9') {
		if (a->fresh) {
			a->shown = 0;
			a->fresh = 0;
		}
		a->shown = a->shown * 10 + (long) (key - '0');
	} else if (key == 'C') {
		a->accum = 0;
		a->shown = 0;
		a->pending = 0;
		a->fresh = 1;
	} else if (key == '=') {
		switch (a->pending) {
		case '+': a->accum += a->shown; break;
		case '-': a->accum -= a->shown; break;
		case '*': a->accum *= a->shown; break;
		case '/': if (a->shown) a->accum /= a->shown; break;
		default:  a->accum = a->shown; break;
		}
		a->shown = a->accum;
		a->pending = 0;
		a->fresh = 1;
	} else {
		switch (a->pending) {
		case '+': a->accum += a->shown; break;
		case '-': a->accum -= a->shown; break;
		case '*': a->accum *= a->shown; break;
		case '/': if (a->shown) a->accum /= a->shown; break;
		default:  a->accum = a->shown; break;
		}
		a->shown = a->accum;
		a->pending = key;
		a->fresh = 1;
	}
	acc_draw(a);
}

static VOID
acc_calc_click(ACC_SLOT *a, WORD mx, WORD my)
{
	WORD cw, ch, c, r;

	if (my < a->wky + ACC_DISP_H)
		return;
	cw = a->wkw / ACC_COLS;
	ch = (a->wkh - ACC_DISP_H) / ACC_ROWS;
	if (cw <= 0 || ch <= 0)		/* window shrunk below the grid */
		return;
	c = (mx - a->wkx) / cw;
	r = (my - a->wky - ACC_DISP_H) / ch;
	if (c < 0 || c >= ACC_COLS || r < 0 || r >= ACC_ROWS)
		return;
	acc_calc_press(a, acc_face[r][c]);
}

/* -- clock ------------------------------------------------------------ */

/* Paint the clock face; the caller has set the clip rectangle. */
static VOID
acc_clock_paint(ACC_SLOT *a)
{
	BYTE face[9];
	time_t now;
	struct tm *lt;
	WORD hh, mm, ss;

	now = time((time_t *) 0);
	lt = localtime(&now);
	if (lt) {
		hh = (WORD) lt->tm_hour;
		mm = (WORD) lt->tm_min;
		ss = (WORD) lt->tm_sec;
	} else {
		hh = mm = ss = 0;
	}
	face[0] = acc_digit[(hh / 10) % 10];
	face[1] = acc_digit[hh % 10];
	face[2] = ':';
	face[3] = acc_digit[(mm / 10) % 10];
	face[4] = acc_digit[mm % 10];
	face[5] = ':';
	face[6] = acc_digit[(ss / 10) % 10];
	face[7] = acc_digit[ss % 10];
	face[8] = 0;

	acc_fill_work(a);
	/* A thin frame just inside the work area, like a digital readout. */
	{
		WORD box[10];
		WORD x0 = a->wkx + 2, y0 = a->wky + 2;
		WORD x1 = a->wkx + a->wkw - 3, y1 = a->wky + a->wkh - 3;

		vsl_color(gl_handle, 1);
		box[0] = x0; box[1] = y0;
		box[2] = x1; box[3] = y0;
		box[4] = x1; box[5] = y1;
		box[6] = x0; box[7] = y1;
		box[8] = x0; box[9] = y0;
		v_pline(gl_handle, 5, box);
	}
	vst_color(gl_handle, 1);
	/*
	 * Centre "HH:MM:SS" in the work area.  The port draws with the fixed
	 * 8x16 BIOS ROM font, so the eight cells span 64 pixels and v_gtext's
	 * baseline sits half a cell (8 px) below the vertical centre.
	 */
	v_gtext(gl_handle,
		a->wkx + (a->wkw - 8 * ACC_FONT_W) / 2,
		a->wky + a->wkh / 2 + ACC_FONT_H / 2,
		face);
}

/* -- shared window handling ------------------------------------------- */

/* Paint an accessory's content; the caller has set the clip rectangle. */
static VOID
acc_paint(ACC_SLOT *a)
{
	if (a->kind == ACC_CALC)
		acc_calc_paint(a);
	else if (a->kind == ACC_CLOCK)
		acc_clock_paint(a);
}

/*
 * Redraw an accessory window, honouring the AES window stack: draw only inside
 * the window's visible rectangle list so a covered accessory (for example a
 * ticking clock behind the calculator) never paints over the windows on top of
 * it.  This is the same rectangle walk the Desktop uses in do_wredraw().
 */
static VOID
acc_draw(ACC_SLOT *a)
{
	GRECT work, t;
	WORD xy[4];

	work.g_x = a->wkx;
	work.g_y = a->wky;
	work.g_w = a->wkw;
	work.g_h = a->wkh;

	gsx_moff();
	wind_get(a->wh, WF_FIRSTXYWH, &t.g_x, &t.g_y, &t.g_w, &t.g_h);
	while (t.g_w && t.g_h) {
		if (rc_intersect(&work, &t)) {
			xy[0] = t.g_x;
			xy[1] = t.g_y;
			xy[2] = t.g_x + t.g_w - 1;
			xy[3] = t.g_y + t.g_h - 1;
			vs_clip(gl_handle, 1, xy);
			acc_paint(a);
		}
		wind_get(a->wh, WF_NEXTXYWH, &t.g_x, &t.g_y, &t.g_w, &t.g_h);
	}
	gsx_mon();
}

static ACC_SLOT *
acc_by_window(WORD wh)
{
	WORD i;

	for (i = 0; i < ACC_SLOTS; i++)
		if (acc_slots[i].kind && acc_slots[i].wh == wh)
			return &acc_slots[i];
	return (ACC_SLOT *) 0;
}

static ACC_SLOT *
acc_by_kind(WORD kind)
{
	WORD i;

	for (i = 0; i < ACC_SLOTS; i++)
		if (acc_slots[i].kind == kind)
			return &acc_slots[i];
	return (ACC_SLOT *) 0;
}

static VOID
acc_refresh_work(ACC_SLOT *a)
{
	wind_get(a->wh, WF_WORKXYWH, &a->wkx, &a->wky, &a->wkw, &a->wkh);
}

static VOID
acc_close(ACC_SLOT *a)
{
	WORD x, y, w, h, i;

	wind_get(a->wh, WF_CURRXYWH, &x, &y, &w, &h);	/* whole window extent */
	wind_close(a->wh);
	wind_delete(a->wh);
	a->kind = 0;
	a->wh = -1;

	/*
	 * Repaint whatever the accessory uncovered.  The resident AES redraws
	 * overlapping windows it owns, but the Desktop's own file windows and
	 * root surface must be told to redraw the vacated rectangle, and any
	 * accessory that was behind this one repaints its newly exposed part.
	 */
	do_wredraw(0, x, y, w, h);
	for (i = 0; i < NUM_WNODES; i++)
		if (G.g_wlist[i].w_id > 0)
			do_wredraw(G.g_wlist[i].w_id, x, y, w, h);
	for (i = 0; i < ACC_SLOTS; i++)
		if (acc_slots[i].kind)
			acc_draw(&acc_slots[i]);
}

/* -- public entry points --------------------------------------------- */

/*
 * The Desk menu accessory area is owned by the resident AES, exactly as in
 * real GEM: the menu renderer builds it from the accessories the client has
 * registered with menu_register(), ignoring the placeholder slots baked into
 * DESKTOP.RSC.  So the Calculator and Clock announce themselves here; the AES
 * copies each name into its own accessory table and later delivers an AC_OPEN
 * message (carrying the accessory's index in word 4) when its Desk entry is
 * chosen.  This is the same code path a stand-alone .ACC desk accessory uses.
 */
static BYTE acc_menu_calc_text[]  = "  Calculator";
static BYTE acc_menu_clock_text[] = "  Clock";
static WORD acc_id_calc  = -1;
static WORD acc_id_clock = -1;

VOID acc_menu_init(VOID)
{
	WORD i;

	for (i = 0; i < ACC_SLOTS; i++) {
		acc_slots[i].kind = 0;
		acc_slots[i].wh = -1;
	}
	acc_id_calc  = menu_register(gl_apid, (LPVOID) acc_menu_calc_text);
	acc_id_clock = menu_register(gl_apid, (LPVOID) acc_menu_clock_text);
}

/* Open (or top, if already open) the accessory window of the given kind. */
static VOID
acc_open(WORD kind)
{
	WORD x, y, w, h;
	ACC_SLOT *a;
	BYTE *name;

	a = acc_by_kind(kind);
	if (a) {			/* already open - bring to top */
		wind_set(a->wh, WF_TOP, 0, 0, 0, 0);
		return;
	}
	a = acc_by_kind(0);
	if (!a)
		return;			/* no free slot */

	if (kind == ACC_CALC) {
		x = 120; y = 44; w = 160; h = 168; name = acc_calc_name;
	} else {
		x = 150; y = 40; w = 120; h = 64; name = acc_clock_name;
	}
	a->wh = wind_create(NAME | CLOSER | MOVER | SIZER | FULLER,
		x, y, w, h);
	if (a->wh < 0) {
		a->wh = -1;
		return;
	}
	a->kind = kind;
	a->accum = 0;
	a->shown = 0;
	a->pending = 0;
	a->fresh = 1;
	wind_set(a->wh, WF_NAME, FP_OFF(name), FP_SEG(name), 0, 0);
	wind_open(a->wh, x, y, w, h);
	acc_refresh_work(a);
	acc_draw(a);
}

/*
 * Give the accessories first refusal of a Desktop message.  Returns TRUE when
 * the message belonged to an accessory window and was handled.
 */
WORD acc_message(WORD *rmsg)
{
	ACC_SLOT *a;

	/* The AES opens an accessory by sending AC_OPEN with its index. */
	if (rmsg[0] == AC_OPEN) {
		if (acc_id_calc >= 0 && rmsg[4] == acc_id_calc)
			acc_open(ACC_CALC);
		else if (acc_id_clock >= 0 && rmsg[4] == acc_id_clock)
			acc_open(ACC_CLOCK);
		else
			return FALSE;
		return TRUE;
	}

	a = acc_by_window(rmsg[3]);
	if (!a)
		return FALSE;
	switch (rmsg[0]) {
	case WM_REDRAW:
		acc_refresh_work(a);
		acc_draw(a);
		break;
	case WM_TOPPED:
		wind_set(a->wh, WF_TOP, 0, 0, 0, 0);
		acc_refresh_work(a);
		acc_draw(a);
		break;
	case WM_MOVED:
	case WM_SIZED:
		wind_set(a->wh, WF_CURRXYWH,
			rmsg[4], rmsg[5], rmsg[6], rmsg[7]);
		acc_refresh_work(a);
		acc_draw(a);
		break;
	case WM_FULLED: {
		WORD cx, cy, cw, ch, fx, fy, fw, fh, px, py, pw, ph;

		wind_get(a->wh, WF_CURRXYWH, &cx, &cy, &cw, &ch);
		wind_get(a->wh, WF_FULLXYWH, &fx, &fy, &fw, &fh);
		if (cw == fw && ch == fh) {
			wind_get(a->wh, WF_PREVXYWH, &px, &py, &pw, &ph);
			wind_set(a->wh, WF_CURRXYWH, px, py, pw, ph);
		} else {
			wind_set(a->wh, WF_CURRXYWH, fx, fy, fw, fh);
		}
		acc_refresh_work(a);
		acc_draw(a);
		break;
	}
	case WM_CLOSED:
		acc_close(a);
		break;
	default:
		break;
	}
	return TRUE;
}

/*
 * Give the accessories first refusal of a work-area button click.  Returns
 * TRUE when the click fell inside an open accessory window.
 */
WORD acc_button(WORD mx, WORD my)
{
	WORD i;

	for (i = 0; i < ACC_SLOTS; i++) {
		ACC_SLOT *a = &acc_slots[i];

		if (!a->kind)
			continue;
		if (mx >= a->wkx && mx < a->wkx + a->wkw
		    && my >= a->wky && my < a->wky + a->wkh) {
			if (a->kind == ACC_CALC)
				acc_calc_click(a, mx, my);
			return TRUE;
		}
	}
	return FALSE;
}

/* Redraw every open clock; called on a Desktop timer tick. */
VOID acc_timer(VOID)
{
	WORD i;

	for (i = 0; i < ACC_SLOTS; i++)
		if (acc_slots[i].kind == ACC_CLOCK)
			acc_draw(&acc_slots[i]);
}

/* TRUE while any clock is open, so the Desktop keeps a one-second timer live. */
WORD acc_clock_open(VOID)
{
	return acc_by_kind(ACC_CLOCK) ? TRUE : FALSE;
}
