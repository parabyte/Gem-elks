/*
 * gemview.c - small HTML browser for GEM on ELKS
 *
 * a small web browser, the work is split across three files:
 *   gemview_net.c   fill g_html[] from a URL (HTTP/1.0 GET)
 *   gemview_html.c  tokenise g_html[] into wrapped display lines
 *   gemview.c       draw the visible lines and run the window
 */

#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "gemview.h"

WORD gem_client_install(VOID);

static WORD handle;		/* VDI workstation handle */
static WORD work_in[11];
static WORD work_out[57];
static WORD wh;			/* window handle */
static WORD wkx, wky, wkw, wkh;	/* work area */
static WORD msg[8];

/* shared page state, declared in gemview.h */
char g_html[HTML_MAX];
WORD g_htmllen;
char g_line[MAX_LINES][LINE_MAX];
BYTE g_style[MAX_LINES];
WORD g_nlines;			/* number of display lines built */
WORD g_top;			/* first visible line (scroll pos) */
WORD g_cols;			/* wrap width in characters */
char g_title[64] = " GEM Browser ";
char g_url[URL_MAX + 1] = "http://";	/* URL entry field */
WORD g_urllen = 7;
char g_href[MAX_HREF][HREF_MAX];
WORD g_nhref;
WORD g_line_href[MAX_LINES];	/* href index for the line, or -1 */
char base_host[128];
WORD base_port = 80;
char base_dir[URL_MAX + 1] = "/";	/* directory part, ends in '/' */
struct field g_fields[MAX_FIELDS];
WORD g_nfields;
WORD g_focus = -1;		/* -1 = URL bar, else field index */
char form_action[URL_MAX + 1] = "";

static char g_status[80] = "Type a URL and press Return.";

/* back history stack */
#define HIST_MAX	6
#define HIST_LEN	100
static char g_current[URL_MAX + 1] = "";	/* URL of the shown page */
static char g_hist[HIST_MAX][HIST_LEN];
static WORD g_histn;
static WORD going_back;		/* suppress a history push */

/* -- drawing ----------------------------------------------------------- */

/* content band, the work area below the URL toolbar */
static WORD
content_y(void)
{
	return wky + TBAR_H;
}

static WORD
content_h(void)
{
	WORD h = wkh - TBAR_H;
	return h < 0 ? 0 : h;
}

static WORD
visible_rows(void)
{
	WORD r = content_h() / LINE_H;
	return r < 1 ? 1 : r;
}

static VOID
draw_toolbar(void)
{
	WORD xy[4], box[10];
	WORD bx = wkx + 3, bw = 26;	/* back button */
	WORD fx = bx + bw + 3, fy = wky + 3;
	WORD fw = wkw - (bw + 9), fh = TBAR_H - 6;
	WORD maxch, len, cx;
	char *show;

	vswr_mode(handle, 1);
	vsf_interior(handle, 1);
	vsf_color(handle, 0);
	xy[0] = wkx;
	xy[1] = wky;
	xy[2] = wkx + wkw - 1;
	xy[3] = wky + TBAR_H - 1;
	vr_recfl(handle, xy);

	vsl_color(handle, 1);	/* back button */
	box[0] = bx;
	box[1] = fy;
	box[2] = bx + bw - 1;
	box[3] = fy;
	box[4] = bx + bw - 1;
	box[5] = fy + fh - 1;
	box[6] = bx;
	box[7] = fy + fh - 1;
	box[8] = bx;
	box[9] = fy;
	v_pline(handle, 5, box);
	vst_color(handle, 1);
	v_gtext(handle, bx + 5, fy + fh - 4, "<-");

	box[0] = fx;
	box[1] = fy;
	box[2] = fx + fw - 1;
	box[3] = fy;		/* URL field */
	box[4] = fx + fw - 1;
	box[5] = fy + fh - 1;
	box[6] = fx;
	box[7] = fy + fh - 1;
	box[8] = fx;
	box[9] = fy;
	v_pline(handle, 5, box);

	maxch = (fw - 8) / CELL_W;
	if (maxch < 1)
		maxch = 1;
	len = g_urllen;
	show = g_url;
	if (len > maxch) {
		show = g_url + (len - maxch);
		len = maxch;
	}
	vst_color(handle, 1);
	v_gtext(handle, fx + 4, fy + fh - 4, show);
	if (g_focus == -1) {	/* caret when URL bar has focus */
		cx = fx + 4 + len * CELL_W;
		xy[0] = cx;
		xy[1] = fy + 2;
		xy[2] = cx;
		xy[3] = fy + fh - 3;
		v_pline(handle, 2, xy);
	}

	xy[0] = wkx;
	xy[1] = wky + TBAR_H - 1;	/* separator */
	xy[2] = wkx + wkw - 1;
	xy[3] = wky + TBAR_H - 1;
	v_pline(handle, 2, xy);
}

static VOID
draw_line(WORD row, WORD scr_y)
{
	WORD idx = g_top + row;
	char *s;
	BYTE st;
	WORD len, tx;

	if (idx >= g_nlines)
		return;
	if (g_style[idx] == 0xff)	/* blank separator line */
		return;
	s = g_line[idx];
	st = g_style[idx];
	len = (WORD) strlen(s);
	if (!len)
		return;
	tx = wkx + 2;
	vst_color(handle, 1);
	v_gtext(handle, tx, scr_y + CELL_H - 3, s);
	if (st & ST_BOLD)	/* fake bold, print again 1px right */
		v_gtext(handle, tx + 1, scr_y + CELL_H - 3, s);
	if (st & ST_ULINE) {	/* underline links and headings */
		WORD xy[4];
		xy[0] = tx;
		xy[1] = scr_y + CELL_H - 1;
		xy[2] = tx + len * CELL_W;
		xy[3] = scr_y + CELL_H - 1;
		vsl_color(handle, 1);
		v_pline(handle, 2, xy);
	}
}

static VOID
draw_one_field(WORD i)
{
	struct field *f = &g_fields[i];
	WORD row = f->line - g_top;
	WORD rows = visible_rows();
	WORD x, y, w2, box[10], xy[4];

	if (row < 0 || row >= rows)
		return;
	x = wkx + 2 + f->col * CELL_W;
	y = content_y() + row * LINE_H;
	w2 = f->width * CELL_W;

	vswr_mode(handle, 1);
	vsf_interior(handle, 1);
	vsf_color(handle, 0);	/* wipe the widget cell white */
	xy[0] = x;
	xy[1] = y;
	xy[2] = x + w2 - 1;
	xy[3] = y + CELL_H - 1;
	vr_recfl(handle, xy);
	vsl_color(handle, 1);
	box[0] = x;
	box[1] = y;
	box[2] = x + w2 - 1;
	box[3] = y;
	box[4] = x + w2 - 1;
	box[5] = y + CELL_H - 1;
	box[6] = x;
	box[7] = y + CELL_H - 1;
	box[8] = x;
	box[9] = y;
	v_pline(handle, 5, box);
	vst_color(handle, 1);
	if (f->is_submit) {
		v_gtext(handle, x + 2, y + CELL_H - 3, f->value);
	} else {
		char *show = f->value;
		WORD len = (WORD) strlen(f->value);
		WORD maxch = (w2 - 4) / CELL_W;
		WORD cx;

		if (maxch < 1)
			maxch = 1;
		if (len > maxch) {
			show = f->value + (len - maxch);
			len = maxch;
		}
		v_gtext(handle, x + 3, y + CELL_H - 3, show);
		if (g_focus == i) {	/* caret when this field has focus */
			cx = x + 3 + len * CELL_W;
			xy[0] = cx;
			xy[1] = y + 2;
			xy[2] = cx;
			xy[3] = y + CELL_H - 2;
			v_pline(handle, 2, xy);
		}
	}
}

static VOID
draw_fields(void)
{
	WORD i;
	for (i = 0; i < g_nfields; i++)
		draw_one_field(i);
}

static VOID
redraw_field(WORD i)
{
	wind_update(1);
	graf_mouse(M_OFF, (LPVOID) 0);
	draw_one_field(i);
	graf_mouse(M_ON, (LPVOID) 0);
	wind_update(0);
}

static VOID
paint(void)
{
	WORD xy[4], rows, r, cy;

	wind_update(1);
	graf_mouse(M_OFF, (LPVOID) 0);
	draw_toolbar();
	vswr_mode(handle, 1);
	vsf_interior(handle, 1);
	vsf_color(handle, 0);	/* white page background */
	cy = content_y();
	xy[0] = wkx;
	xy[1] = cy;
	xy[2] = wkx + wkw - 1;
	xy[3] = wky + wkh - 1;
	vr_recfl(handle, xy);

	rows = visible_rows();
	for (r = 0; r < rows; r++)
		draw_line(r, cy + r * LINE_H);
	draw_fields();

	graf_mouse(M_ON, (LPVOID) 0);
	wind_update(0);
}

static VOID
paint_toolbar(void)
{
	wind_update(1);
	graf_mouse(M_OFF, (LPVOID) 0);
	draw_toolbar();
	graf_mouse(M_ON, (LPVOID) 0);
	wind_update(0);
}

/* keep g_top in range and move the slider to match */
static VOID
sync_slider(void)
{
	WORD rows = visible_rows();
	WORD maxtop = g_nlines - rows;
	WORD pos, siz;

	if (maxtop < 0)
		maxtop = 0;
	if (g_top > maxtop)
		g_top = maxtop;
	if (g_top < 0)
		g_top = 0;

	if (g_nlines <= rows) {
		pos = 0;
		siz = 1000;
	} else {
		pos = (WORD) (((long) g_top * 1000) / maxtop);
		siz = (WORD) (((long) rows * 1000) / g_nlines);
		if (siz < 1)
			siz = 1;
	}
	wind_set(wh, WF_VSLIDE, pos, 0, 0, 0);
	wind_set(wh, 16 /* WF_VSLSIZ */ , siz, 0, 0, 0);
}

static VOID
scroll_to(WORD newtop)
{
	WORD rows = visible_rows();
	WORD maxtop = g_nlines - rows;

	if (maxtop < 0)
		maxtop = 0;
	if (newtop > maxtop)
		newtop = maxtop;
	if (newtop < 0)
		newtop = 0;
	if (newtop == g_top)
		return;
	g_top = newtop;
	sync_slider();
	paint();
}

/* -- built-in pages --------------------------------------------------- */

static const char g_home[] =
	"<html><head><title>GEM Browser</title></head><body>"
	"<h1>GEM Browser on ELKS</h1>"
	"<p>A minimal web browser whose tokenise, flow-layout and paint "
	"pipeline and its scrollable window follow <b>HighWire</b>, adapted "
	"to 16-bit.</p>"
	"<h2>Using it</h2>"
	"<ul>"
	"<li>Type a URL in the bar above and press Return to fetch a page "
	"over ELKS TCP</li>"
	"<li>Use the scroll arrows and slider to read long pages</li>"
	"<li>Plain HTTP only - there is no TLS, so https:// sites will not "
	"load</li>"
	"</ul>"
	"<hr>"
	"<p>It renders headings, paragraphs with word wrap, bold text, "
	"<a href=x>links</a> (underlined), lists and rules.  Entities like "
	"&amp;, &lt;, &gt; and &#65; are decoded; scripts and styles are "
	"skipped.</p>" "</body></html>";

static VOID
load_home(void)
{
	g_htmllen = (WORD) strlen(g_home);
	memcpy(g_html, g_home, g_htmllen);
	g_html[g_htmllen] = 0;
}

/* small error page for build_layout() */
static VOID
msg_page(const char *heading, const char *body)
{
	strcpy(g_html, "<html><body><h1>");
	strcat(g_html, heading);
	strcat(g_html, "</h1><p>");
	strcat(g_html, body);
	strcat(g_html, "</p></body></html>");
	g_htmllen = (WORD) strlen(g_html);
}

/* -- navigation ------------------------------------------------------- */

/* fetch and show g_url, follow http:// redirects */
static VOID
load_url(void)
{
	WORD st, hop;
	const char *q;

	g_url[g_urllen] = 0;
	/* no '.', ':' or '/' after http:// means its a search */
	q = g_url;
	if (!strncmp(q, "http://", 7))
		q += 7;
	if (*q && !strchr(q, '.') && !strchr(q, ':') && !strchr(q, '/'))
		make_search_url(q);

	/* push the page were leaving onto the back stack */
	if (!going_back && g_current[0]) {
		if (g_histn >= HIST_MAX) {
			memmove(g_hist[0], g_hist[1],
				(HIST_MAX - 1) * HIST_LEN);
			g_histn--;
		}
		strncpy(g_hist[g_histn], g_current, HIST_LEN - 1);
		g_hist[g_histn][HIST_LEN - 1] = 0;
		g_histn++;
	}

	for (hop = 0; hop < 4; hop++) {
		g_url[g_urllen] = 0;
		if (!parse_url(g_url)) {
			msg_page("Bad URL", "Enter an address such as "
				"http://example.com/");
			break;
		}
		set_base();
		st = net_fetch();
		if (st < 0) {
			msg_page("Cannot connect",
				"No route to the host.  Check networking, "
				"and note only plain http:// works.");
			break;
		}
		if ((st == 301 || st == 302 || st == 303 || st == 307) &&
			net_location[0]) {
			if (ci_prefix(net_location, "https", 5)) {
				msg_page("HTTPS not supported",
					"This site redirects to https://, which "
					"needs TLS.  Try a plain http:// site.");
				break;
			}
			strncpy(g_url, net_location, URL_MAX);
			g_url[URL_MAX] = 0;
			g_urllen = (WORD) strlen(g_url);
			continue;	/* follow the redirect */
		}
		if (st == 0)
			msg_page("No response", "The server sent no reply.");
		break;
	}
	g_top = 0;
	build_layout();
	sync_slider();
	paint();
	wind_set(wh, WF_NAME, FP_OFF(g_title), FP_SEG(g_title), 0, 0);
	strncpy(g_current, g_url, URL_MAX);
	g_current[URL_MAX] = 0;
	going_back = 0;
}

static VOID
go_back(void)
{
	if (g_histn == 0)
		return;
	g_histn--;
	strncpy(g_url, g_hist[g_histn], URL_MAX);
	g_url[URL_MAX] = 0;
	g_urllen = (WORD) strlen(g_url);
	going_back = 1;
	load_url();
}

/* resolve a link href and load it */
static VOID
navigate(const char *href)
{
	if (!href[0] || href[0] == '#')
		return;
	if (ci_prefix(href, "https://", 8)) {
		msg_page("HTTPS not supported",
			"That link needs TLS (https://).");
		g_top = 0;
		build_layout();
		sync_slider();
		paint();
		wind_set(wh, WF_NAME, FP_OFF(g_title), FP_SEG(g_title), 0, 0);
		return;
	}
	resolve_url(href, g_url);
	g_urllen = (WORD) strlen(g_url);
	if (g_urllen > URL_MAX)
		g_urllen = URL_MAX;
	load_url();
}

/* field at screen point (mx,my), or -1 */
static WORD
field_at(WORD mx, WORD my)
{
	WORD i, rows = visible_rows();

	for (i = 0; i < g_nfields; i++) {
		struct field *f = &g_fields[i];
		WORD row = f->line - g_top;
		WORD x, y, w2;

		if (row < 0 || row >= rows)
			continue;
		x = wkx + 2 + f->col * CELL_W;
		y = content_y() + row * LINE_H;
		w2 = f->width * CELL_W;
		if (mx >= x && mx < x + w2 && my >= y && my < y + CELL_H)
			return i;
	}
	return -1;
}

/* build action?name=value&... from the fields and load it */
static VOID
submit_form(void)
{
	char query[URL_MAX + 1];
	WORD i, n = 0, first = 1;

	for (i = 0; i < g_nfields; i++) {
		struct field *f = &g_fields[i];
		const char *s;

		if (f->is_submit || !f->name[0])
			continue;
		if (n > (WORD) sizeof(query) - 90)
			break;
		if (!first)
			query[n++] = '&';
		first = 0;
		for (s = f->name; *s && n < (WORD) sizeof(query) - 4; s++)
			query[n++] = *s;
		query[n++] = '=';
		for (s = f->value; *s && n < (WORD) sizeof(query) - 4; s++) {
			char c = *s;
			if (c == ' ')
				query[n++] = '+';
			else if ((c >= 'a' && c <= 'z') || (c >= 'A'
					&& c <= 'Z') || (c >= '0' && c <= '9'))
				query[n++] = c;
		}
	}
	query[n] = 0;

	if (!resolve_url(form_action, g_url)) {
		msg_page("HTTPS not supported",
			"This form submits to https://, which needs TLS.");
		g_top = 0;
		build_layout();
		sync_slider();
		paint();
		wind_set(wh, WF_NAME, FP_OFF(g_title), FP_SEG(g_title), 0, 0);
		return;
	}
	if (query[0]) {
		strncat(g_url, strchr(g_url, '?') ? "&" : "?",
			URL_MAX - strlen(g_url));
		strncat(g_url, query, URL_MAX - strlen(g_url));
	}
	g_urllen = (WORD) strlen(g_url);
	if (g_urllen > URL_MAX)
		g_urllen = URL_MAX;
	load_url();
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

	load_home();

	wh = wind_create(NAME | CLOSER | MOVER | SIZER | FULLER |
		UPARROW | DNARROW | VSLIDE, 24, 24, 560, 340);
	if (wh < 0) {
		v_clsvwk(handle);
		appl_exit();
		return 1;
	}
	wind_set(wh, WF_NAME, FP_OFF(g_title), FP_SEG(g_title), 0, 0);
	wind_open(wh, 24, 24, 560, 340);
	wind_get(wh, WF_WORKXYWH, &wkx, &wky, &wkw, &wkh);

	g_cols = wkw / CELL_W - 1;
	if (g_cols > LINE_MAX - 1)
		g_cols = LINE_MAX - 1;
	if (g_cols < 8)
		g_cols = 8;
	g_top = 0;
	build_layout();
	sync_slider();
	paint();

	for (;;) {
		ev = evnt_multi(MU_MESAG | MU_KEYBD | MU_BUTTON,
			1, 1, 1, 0, 0, 0, 0, 0,
			0, 0, 0, 0, 0, (LPVOID) msg, 0, 0,
			&mx, &my, &mb, &ks, &kr, &br);
		if (ev & MU_BUTTON) {
			if ((WORD) my < content_y()) {	/* toolbar click */
				if ((WORD) mx >= wkx + 3 &&
					(WORD) mx < wkx + 3 + 26) {
					go_back();	/* back button */
				} else if (g_focus != -1) {
					g_focus = -1;
					paint();	/* focus URL bar */
				}
			} else {
				WORD fi = field_at((WORD) mx, (WORD) my);
				if (fi >= 0) {	/* form widget */
					if (g_fields[fi].is_submit)
						submit_form();
					else if (g_focus != fi) {
						g_focus = fi;
						paint();
					}
				} else {	/* link line */
					WORD ln = g_top +
						((WORD) my -
						content_y()) / LINE_H;
					if (ln >= 0 && ln < g_nlines
						&& g_line_href[ln] >= 0)
						navigate(g_href[g_line_href
								[ln]]);
				}
			}
		}
		if ((ev & MU_KEYBD) && g_focus >= 0 && g_focus < g_nfields &&
			!g_fields[g_focus].is_submit) {
			/* typing goes to the focused form field */
			struct field *f = &g_fields[g_focus];
			WORD ascii = kr & 0xff;
			WORD vl = (WORD) strlen(f->value);
			if (ascii == 13) {
				submit_form();
			} else if (ascii == 8 || ascii == 127) {
				if (vl > 0) {
					f->value[vl - 1] = 0;
					redraw_field(g_focus);
				}
			} else if (ascii >= 32 && ascii < 127) {
				if (vl < FLD_VAL - 1) {
					f->value[vl] = (char) ascii;
					f->value[vl + 1] = 0;
					redraw_field(g_focus);
				}
			}
		} else if (ev & MU_KEYBD) {	/* editing the URL bar */
			WORD ascii = kr & 0xff;
			if (ascii == 13) {
				load_url();
			} else if (ascii == 8 || ascii == 127) {
				if (g_urllen > 0) {
					g_url[--g_urllen] = 0;
					paint_toolbar();
				}
			} else if (ascii >= 32 && ascii < 127) {
				if (g_urllen < URL_MAX) {
					g_url[g_urllen++] = (char) ascii;
					g_url[g_urllen] = 0;
					paint_toolbar();
				}
			}
		}
		if (!(ev & MU_MESAG))
			continue;
		if (msg[0] == WM_CLOSED)
			break;
		switch (msg[0]) {
		case WM_REDRAW:
			wind_get(wh, WF_WORKXYWH, &wkx, &wky, &wkw, &wkh);
			paint();
			break;
		case WM_TOPPED:
			wind_set(wh, WF_TOP, 0, 0, 0, 0);
			break;
		case WM_MOVED:
			wind_set(wh, WF_CURRXYWH, msg[4], msg[5], msg[6],
				msg[7]);
			wind_get(wh, WF_WORKXYWH, &wkx, &wky, &wkw, &wkh);
			break;
		case WM_SIZED:
			wind_set(wh, WF_CURRXYWH, msg[4], msg[5], msg[6],
				msg[7]);
			wind_get(wh, WF_WORKXYWH, &wkx, &wky, &wkw, &wkh);
			g_cols = wkw / CELL_W - 1;
			if (g_cols > LINE_MAX - 1)
				g_cols = LINE_MAX - 1;
			if (g_cols < 8)
				g_cols = 8;
			build_layout();
			g_top = 0;
			sync_slider();
			paint();
			break;
		case WM_FULLED:{
				WORD cx, cy, cw, ch, fx, fy, fw, fh, px, py, pw,
					ph;
				wind_get(wh, WF_CURRXYWH, &cx, &cy, &cw, &ch);
				wind_get(wh, WF_FULLXYWH, &fx, &fy, &fw, &fh);
				if (cw == fw && ch == fh) {
					wind_get(wh, WF_PREVXYWH, &px, &py, &pw,
						&ph);
					wind_set(wh, WF_CURRXYWH, px, py, pw,
						ph);
				} else {
					wind_set(wh, WF_CURRXYWH, fx, fy, fw,
						fh);
				}
				wind_get(wh, WF_WORKXYWH, &wkx, &wky, &wkw,
					&wkh);
				g_cols = wkw / CELL_W - 1;
				if (g_cols > LINE_MAX - 1)
					g_cols = LINE_MAX - 1;
				if (g_cols < 8)
					g_cols = 8;
				build_layout();
				g_top = 0;
				sync_slider();
				paint();
				break;
			}
		case WM_ARROWED:
			switch (msg[4]) {
			case WA_UPLINE:
				scroll_to(g_top - 1);
				break;
			case WA_DNLINE:
				scroll_to(g_top + 1);
				break;
			case WA_UPPAGE:
				scroll_to(g_top - visible_rows());
				break;
			case WA_DNPAGE:
				scroll_to(g_top + visible_rows());
				break;
			}
			break;
		case WM_VSLID:{
				WORD rows = visible_rows();
				WORD maxtop = g_nlines - rows;
				if (maxtop < 0)
					maxtop = 0;
				scroll_to((WORD) (((long) msg[4] * maxtop) /
						1000));
				break;
			}
		default:
			break;
		}
	}

	wind_close(wh);
	wind_delete(wh);
	v_clsvwk(handle);
	appl_exit();
	return 0;
}
