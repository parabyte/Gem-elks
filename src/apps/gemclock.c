/*
 * gemclock.c - GEM digital clock application for stock ELKS.
 *
 * A standalone GEM client: it links the client transport (gem_client.c) and
 * the original AES/VDI bindings, so it talks to the /bin/gem server over the
 * two transport pipes exactly like the Desktop.  The server hosts it as the
 * sole GEM client while it runs, so its window appears on the live screen.
 *
 * It opens one titled, movable, closable window (classic GEM 1 gadgets) and
 * redraws the current time once a second from an AES timer event.  Clicking
 * the close box ends the application and returns to the Desktop.
 */

#include <time.h>
#include <unistd.h>

#include "aes.h"

WORD gem_client_install(VOID);

#define CLOCK_X		80
#define CLOCK_Y		50
#define CLOCK_W		168
#define CLOCK_H		56

static WORD handle;			/* VDI workstation handle */
static WORD work_in[11];
static WORD work_out[57];
static WORD wh;				/* window handle */
static WORD wkx, wky, wkw, wkh;		/* work area */
static WORD msg[8];
static BYTE face[9];
static BYTE title[] = " Clock ";

static const BYTE digit_char[10] =
	{ '0', '1', '2', '3', '4', '5', '6', '7', '8', '9' };

/* Format HH:MM:SS into face[] without printf or wide target arithmetic. */
static VOID
build_face(VOID)
{
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
		hh = 0;
		mm = 0;
		ss = 0;
	}
	face[0] = digit_char[(hh / 10) % 10];
	face[1] = digit_char[hh % 10];
	face[2] = ':';
	face[3] = digit_char[(mm / 10) % 10];
	face[4] = digit_char[mm % 10];
	face[5] = ':';
	face[6] = digit_char[(ss / 10) % 10];
	face[7] = digit_char[ss % 10];
	face[8] = 0;
}

/* Repaint the work area: white background, current time in black. */
static VOID
paint(VOID)
{
	WORD xy[4];

	build_face();
	wind_update(1);
	graf_mouse(M_OFF, (LPVOID) 0);
	vswr_mode(handle, 1);		/* replace */
	vsf_interior(handle, 1);	/* solid */
	vsf_color(handle, 0);		/* white */
	xy[0] = wkx;
	xy[1] = wky;
	xy[2] = wkx + wkw - 1;
	xy[3] = wky + wkh - 1;
	vr_recfl(handle, xy);
	vst_color(handle, 1);		/* black */
	v_gtext(handle, wkx + (wkw / 2) - 32, wky + (wkh / 2) + 4, face);
	graf_mouse(M_ON, (LPVOID) 0);
	wind_update(0);
}

int
main(int argc, char **argv)
{
	UWORD ev, mx, my, mb, ks, kr, br;
	WORD i;

	(void) argc;
	(void) argv;
	(void) chdir("/GEMAPPS/GEMSYS");
	if (!gem_client_install())
		return 1;
	if (appl_init((LPXBUF) 0) < 0)
		return 1;

	for (i = 0; i < 10; i++)
		work_in[i] = 1;
	work_in[10] = 2;
	v_opnvwk(work_in, &handle, work_out);

	wh = wind_create(NAME | CLOSER | MOVER | SIZER | FULLER,
		CLOCK_X, CLOCK_Y, CLOCK_W, CLOCK_H);
	if (wh < 0) {
		v_clsvwk(handle);
		appl_exit();
		return 1;
	}
	wind_set(wh, WF_NAME, FP_OFF(title), FP_SEG(title), 0, 0);
	wind_open(wh, CLOCK_X, CLOCK_Y, CLOCK_W, CLOCK_H);
	wind_get(wh, WF_WORKXYWH, &wkx, &wky, &wkw, &wkh);
	paint();

	for (;;) {
		ev = evnt_multi(MU_MESAG | MU_TIMER,
			0, 0, 0,
			0, 0, 0, 0, 0,
			0, 0, 0, 0, 0,
			(LPVOID) msg, 1000, 0,
			&mx, &my, &mb, &ks, &kr, &br);
		if (ev & MU_MESAG) {
			if (msg[0] == WM_CLOSED)
				break;
			if (msg[0] == WM_REDRAW) {
				wind_get(wh, WF_WORKXYWH,
					&wkx, &wky, &wkw, &wkh);
				paint();
			} else if (msg[0] == WM_TOPPED) {
				wind_set(wh, WF_TOP, 0, 0, 0, 0);
			} else if (msg[0] == WM_MOVED || msg[0] == WM_SIZED) {
				wind_set(wh, WF_CURRXYWH,
					msg[4], msg[5], msg[6], msg[7]);
				wind_get(wh, WF_WORKXYWH,
					&wkx, &wky, &wkw, &wkh);
				paint();
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
				paint();
			}
		}
		if (ev & MU_TIMER)
			paint();
	}

	wind_close(wh);
	wind_delete(wh);
	v_clsvwk(handle);
	appl_exit();
	return 0;
}
