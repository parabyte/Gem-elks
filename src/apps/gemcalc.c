/*
 * gemcalc.c - GEM calculator
 *
 * one window, a number display and a 4x4 button grid, whole-number
 * arithmetic, the close box goes back to the desktop
 */

#include <unistd.h>

#include "aes.h"

WORD gem_client_install(VOID);

#define CALC_X		96
#define CALC_Y		40
#define CALC_W		160
#define CALC_H		176
#define DISP_H		28	/* display strip height */
#define COLS		4
#define ROWS		4

static WORD handle;
static WORD work_in[11];
static WORD work_out[57];
static WORD wh;
static WORD wkx, wky, wkw, wkh;
static WORD msg[8];
static BYTE title[] = " Calculator ";

/* button faces, row-major */
static const BYTE face[ROWS][COLS] = {
	{ '7', '8', '9', '/' },
	{ '4', '5', '6', '*' },
	{ '1', '2', '3', '-' },
	{ '0', 'C', '=', '+' }
};

static long accum;		/* running total */
static long shown;		/* number being entered / shown */
static BYTE pending;		/* pending operator, 0 if none */
static WORD fresh;		/* next digit starts a new number */
static BYTE display[16];

static VOID
format_number(long value)
{
	BYTE tmp[16];
	WORD i, n;
	WORD neg;

	neg = 0;
	if (value < 0) {
		neg = 1;
		value = -value;
	}
	n = 0;
	do {
		tmp[n++] = (BYTE) ('0' + (WORD) (value % 10));
		value /= 10;
	} while (value && n < 14);
	if (neg)
		tmp[n++] = '-';
	for (i = 0; i < n; i++)
		display[i] = tmp[n - 1 - i];
	display[n] = 0;
}

static VOID
paint_display(VOID)
{
	WORD xy[4];
	WORD tx;

	format_number(shown);
	graf_mouse(M_OFF, (LPVOID) 0);
	vswr_mode(handle, 1);
	vsf_interior(handle, 1);
	vsf_color(handle, 0);	/* white */
	xy[0] = wkx;
	xy[1] = wky;
	xy[2] = wkx + wkw - 1;
	xy[3] = wky + DISP_H - 1;
	vr_recfl(handle, xy);
	tx = wkx + wkw - 12 - 8 * (WORD) 0;
	vst_color(handle, 1);
	{
		WORD len = 0;
		while (display[len])
			len++;
		v_gtext(handle, wkx + wkw - 10 - 8 * len,
			wky + DISP_H - 8, display);
	}
	graf_mouse(M_ON, (LPVOID) 0);
}

static VOID
paint_buttons(VOID)
{
	WORD r, c;
	WORD cw, ch;
	WORD xy[4];
	BYTE label[2];

	cw = wkw / COLS;
	ch = (wkh - DISP_H) / ROWS;
	label[1] = 0;
	graf_mouse(M_OFF, (LPVOID) 0);
	vsl_color(handle, 1);	/* black outlines */
	vsf_interior(handle, 0);	/* hollow */
	vst_color(handle, 1);
	for (r = 0; r < ROWS; r++) {
		for (c = 0; c < COLS; c++) {
			xy[0] = wkx + c * cw + 1;
			xy[1] = wky + DISP_H + r * ch + 1;
			xy[2] = wkx + c * cw + cw - 2;
			xy[3] = wky + DISP_H + r * ch + ch - 2;
			{
				WORD box[10];
				box[0] = xy[0];
				box[1] = xy[1];
				box[2] = xy[2];
				box[3] = xy[1];
				box[4] = xy[2];
				box[5] = xy[3];
				box[6] = xy[0];
				box[7] = xy[3];
				box[8] = xy[0];
				box[9] = xy[1];
				v_pline(handle, 5, box);
			}
			label[0] = face[r][c];
			v_gtext(handle, xy[0] + (cw / 2) - 4,
				xy[1] + (ch / 2) + 4, label);
		}
	}
	graf_mouse(M_ON, (LPVOID) 0);
}

static VOID
paint_all(VOID)
{
	WORD xy[4];

	wind_update(1);
	graf_mouse(M_OFF, (LPVOID) 0);
	vswr_mode(handle, 1);
	vsf_interior(handle, 1);
	vsf_color(handle, 0);
	xy[0] = wkx;
	xy[1] = wky;
	xy[2] = wkx + wkw - 1;
	xy[3] = wky + wkh - 1;
	vr_recfl(handle, xy);
	graf_mouse(M_ON, (LPVOID) 0);
	paint_buttons();
	paint_display();
	wind_update(0);
}

static VOID
apply_pending(VOID)
{
	switch (pending) {
	case '+':
		accum += shown;
		break;
	case '-':
		accum -= shown;
		break;
	case '*':
		accum *= shown;
		break;
	case '/':
		if (shown)
			accum /= shown;
		break;
	default:
		accum = shown;
		break;
	}
}

static VOID
press(BYTE key)
{
	if (key >= '0' && key <= '9') {
		if (fresh) {
			shown = 0;
			fresh = 0;
		}
		shown = shown * 10 + (long) (key - '0');
	} else if (key == 'C') {
		accum = 0;
		shown = 0;
		pending = 0;
		fresh = 1;
	} else if (key == '=') {
		apply_pending();
		shown = accum;
		pending = 0;
		fresh = 1;
	} else {		/* + - * / */
		apply_pending();
		accum = accum;
		shown = accum;
		pending = key;
		fresh = 1;
	}
	paint_display();
}

/* map a click to a button face, 0 if outside the grid */
static BYTE
hit_button(WORD mx, WORD my)
{
	WORD cw, ch, c, r;

	if (my < wky + DISP_H)
		return 0;
	cw = wkw / COLS;
	ch = (wkh - DISP_H) / ROWS;
	c = (mx - wkx) / cw;
	r = (my - wky - DISP_H) / ch;
	if (c < 0 || c >= COLS || r < 0 || r >= ROWS)
		return 0;
	return face[r][c];
}

int
main(int argc, char **argv)
{
	UWORD ev, mx, my, mb, ks, kr, br;
	WORD i;

	(void) argc;
	(void) argv;
	(void) chdir("/lib/gemsys");
	if (!gem_client_install())
		return 1;
	if (appl_init((LPXBUF) 0) < 0)
		return 1;

	for (i = 0; i < 10; i++)
		work_in[i] = 1;
	work_in[10] = 2;
	v_opnvwk(work_in, &handle, work_out);

	accum = 0;
	shown = 0;
	pending = 0;
	fresh = 1;

	wh = wind_create(NAME | CLOSER | MOVER | SIZER | FULLER,
		CALC_X, CALC_Y, CALC_W, CALC_H);
	if (wh < 0) {
		v_clsvwk(handle);
		appl_exit();
		return 1;
	}
	wind_set(wh, WF_NAME, FP_OFF(title), FP_SEG(title), 0, 0);
	wind_open(wh, CALC_X, CALC_Y, CALC_W, CALC_H);
	wind_get(wh, WF_WORKXYWH, &wkx, &wky, &wkw, &wkh);
	paint_all();

	for (;;) {
		ev = evnt_multi(MU_MESAG | MU_BUTTON,
			1, 1, 1,
			0, 0, 0, 0, 0,
			0, 0, 0, 0, 0,
			(LPVOID) msg, 0, 0, &mx, &my, &mb, &ks, &kr, &br);
		if (ev & MU_MESAG) {
			if (msg[0] == WM_CLOSED)
				break;
			if (msg[0] == WM_REDRAW) {
				wind_get(wh, WF_WORKXYWH,
					&wkx, &wky, &wkw, &wkh);
				paint_all();
			} else if (msg[0] == WM_TOPPED) {
				wind_set(wh, WF_TOP, 0, 0, 0, 0);
			} else if (msg[0] == WM_MOVED || msg[0] == WM_SIZED) {
				wind_set(wh, WF_CURRXYWH,
					msg[4], msg[5], msg[6], msg[7]);
				wind_get(wh, WF_WORKXYWH,
					&wkx, &wky, &wkw, &wkh);
				paint_all();
			} else if (msg[0] == WM_FULLED) {
				WORD cx, cy, cw, ch, fx, fy, fw, fh;
				WORD px, py, pw, ph;

				wind_get(wh, WF_CURRXYWH, &cx, &cy, &cw, &ch);
				wind_get(wh, WF_FULLXYWH, &fx, &fy, &fw, &fh);
				if (cw == fw && ch == fh) {
					wind_get(wh, WF_PREVXYWH,
						&px, &py, &pw, &ph);
					wind_set(wh, WF_CURRXYWH,
						px, py, pw, ph);
				} else {
					wind_set(wh, WF_CURRXYWH,
						fx, fy, fw, fh);
				}
				wind_get(wh, WF_WORKXYWH,
					&wkx, &wky, &wkw, &wkh);
				paint_all();
			}
		}
		if (ev & MU_BUTTON) {
			BYTE key = hit_button((WORD) mx, (WORD) my);
			WORD rx, ry, rb, rk;

			if (key)
				press(key);
			/* evnt_multi returns with the button still down, so wait for release */
			evnt_button(1, 1, 0, &rx, &ry, &rb, &rk);
		}
	}

	wind_close(wh);
	wind_delete(wh);
	v_clsvwk(handle);
	appl_exit();
	return 0;
}
