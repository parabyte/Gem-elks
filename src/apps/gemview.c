/*
 * gemview.c - a minimal HTML browser for GEM on stock ELKS.
 *
 * The user asked to bring HighWire (the Atari GEM web browser) to this port.
 * A faithful port is impossible on the 8086 - HighWire assumes a 32-bit flat
 * address space, megabytes of RAM, CSS/JS/image engines and the MiNT network
 * stack.  What this program does instead is follow HighWire's *design*, scaled
 * to 16 bits: the same tokenise -> flow-layout -> paint pipeline and the same
 * browser-window UI (a scrollable content area with slider and arrows), but
 * rewritten with WORD-sized state and small fixed tables, rendering a practical
 * subset of HTML (headings, paragraphs, lists, rules, bold, links) with the
 * BIOS ROM font.  The document is fetched over ELKS TCP (see net_fetch()).
 *
 * Pipeline, mirroring HighWire (scanner.c -> DomBox/Paragrph -> render.c):
 *   net_fetch()    fill g_html[] from a URL over a TCP socket (HTTP/1.0 GET)
 *   build_layout() tokenise g_html[] and flow it into wrapped display lines
 *   paint()        draw the lines visible at the current scroll position
 */

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "aes.h"

WORD gem_client_install(VOID);

/* -- limits (sized to live inside the 8086 64 KiB data segment) -------- */
#define HTML_MAX	16384		/* raw HTML source buffer */
#define LINE_MAX	88		/* characters per display line */
#define MAX_LINES	240		/* display lines held for a page */
#define URL_MAX		200		/* URL entry field capacity */

#define CELL_W		8		/* BIOS ROM font cell width */
#define CELL_H		16		/* BIOS ROM font cell height */
#define LINE_H		14		/* vertical pitch between lines */
#define TBAR_H		22		/* URL toolbar band height */

/* per-line style bits */
#define ST_BOLD		0x01
#define ST_ULINE	0x02		/* links and headings */

static WORD handle;			/* VDI workstation handle */
static WORD work_in[11];
static WORD work_out[57];
static WORD wh;				/* window handle */
static WORD wkx, wky, wkw, wkh;		/* work area */
static WORD msg[8];

static char  g_html[HTML_MAX];
static WORD  g_htmllen;
static char  g_line[MAX_LINES][LINE_MAX];
static BYTE  g_style[MAX_LINES];
static WORD  g_nlines;			/* number of display lines built */
static WORD  g_top;			/* first visible line (scroll pos) */
static WORD  g_cols;			/* wrap width in characters */
static char  g_title[64] = " GEM Browser ";
static char  g_url[URL_MAX + 1] = "http://";	/* URL entry field */
static WORD  g_urllen = 7;
static char  g_status[80] = "Type a URL and press Return.";

/* Links: an href table plus, for each display line, which href it belongs to. */
#define MAX_HREF	40
#define HREF_MAX	100
static char  g_href[MAX_HREF][HREF_MAX];
static WORD  g_nhref;
static WORD  g_line_href[MAX_LINES];	/* href index for the line, or -1 */

/* Base of the page now shown, so relative links can be resolved. */
static char  base_host[128];
static WORD  base_port = 80;
static char  base_dir[URL_MAX + 1] = "/";	/* directory part, ends in '/' */

/* Form fields (text inputs and submit buttons) laid out inside the page. */
#define MAX_FIELDS	6
#define FLD_NAME	24
#define FLD_VAL		56
struct field {
	char name[FLD_NAME];
	char value[FLD_VAL];
	WORD line, col, width;	/* position in the display grid */
	BYTE is_submit;
};
static struct field g_fields[MAX_FIELDS];
static WORD  g_nfields;
static WORD  g_focus = -1;		/* -1 = URL bar, else field index */
static char  form_action[URL_MAX + 1] = "";

/* Back history: a stack of previously shown URLs. */
#define HIST_MAX	6
#define HIST_LEN	100
static char  g_current[URL_MAX + 1] = "";	/* URL of the shown page */
static char  g_hist[HIST_MAX][HIST_LEN];
static WORD  g_histn;
static WORD  going_back;			/* suppress a history push */

/* -- HTML entities ---------------------------------------------------- */

struct entity { const char *name; char ch; };
static const struct entity g_entities[] = {
	{ "amp", '&' }, { "lt", '<' }, { "gt", '>' }, { "quot", '"' },
	{ "apos", '\'' }, { "nbsp", ' ' }, { "copy", 'c' }, { "reg", 'r' },
	{ "mdash", '-' }, { "ndash", '-' }, { "hellip", '.' }, { "trade", 't' },
	{ (const char *) 0, 0 }
};

/*
 * Decode one &...; entity starting at *pp (which points at '&').  Writes the
 * decoded ASCII byte to *out and advances *pp past the entity.  Mirrors
 * HighWire scan_namedchar() but collapses anything non-ASCII to '?'.
 */
static VOID
decode_entity(const char **pp, char *out)
{
	const char *p = *pp + 1;	/* skip '&' */
	char name[10];
	WORD i;

	if (*p == '#') {		/* numeric &#NN; or &#xHH; */
		WORD val = 0;
		p++;
		if (*p == 'x' || *p == 'X') {
			p++;
			while ((*p >= '0' && *p <= '9') ||
			       (*p >= 'a' && *p <= 'f') ||
			       (*p >= 'A' && *p <= 'F')) {
				WORD d = (*p <= '9') ? *p - '0'
				       : ((*p | 0x20) - 'a' + 10);
				val = val * 16 + d;
				p++;
			}
		} else {
			while (*p >= '0' && *p <= '9')
				val = val * 10 + (*p++ - '0');
		}
		if (*p == ';')
			p++;
		*out = (val >= 32 && val < 127) ? (char) val : '?';
		*pp = p;
		return;
	}
	i = 0;
	while (i < 9 && ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z')))
		name[i++] = *p++;
	name[i] = 0;
	if (*p == ';')
		p++;
	for (i = 0; g_entities[i].name; i++) {
		if (!strcmp(name, g_entities[i].name)) {
			*out = g_entities[i].ch;
			*pp = p;
			return;
		}
	}
	*out = '&';			/* unknown - keep the ampersand */
	*pp = *pp + 1;
}

/* -- tag recognition (HighWire scan_tag, shrunk to what we lay out) ---- */

enum {
	T_OTHER = 0, T_P, T_BR, T_DIV, T_H, T_UL, T_OL, T_LI, T_HR,
	T_B, T_I, T_U, T_A, T_PRE, T_BQ, T_CENTER, T_TR, T_TABLE,
	T_TITLE, T_SCRIPT, T_STYLE, T_HEAD, T_BODY,
	T_FORM, T_INPUT, T_BUTTON, T_TEXTAREA, T_LINK_IGNORE
};

static WORD
tag_id(const char *name)
{
	if (!strcmp(name, "p"))			return T_P;
	if (!strcmp(name, "br"))		return T_BR;
	if (!strcmp(name, "div"))		return T_DIV;
	if (name[0] == 'h' && name[1] >= '1' && name[1] <= '6' && !name[2])
		return T_H;
	if (!strcmp(name, "ul"))		return T_UL;
	if (!strcmp(name, "ol"))		return T_OL;
	if (!strcmp(name, "li"))		return T_LI;
	if (!strcmp(name, "hr"))		return T_HR;
	if (!strcmp(name, "b") || !strcmp(name, "strong"))	return T_B;
	if (!strcmp(name, "i") || !strcmp(name, "em"))		return T_I;
	if (!strcmp(name, "u"))			return T_U;
	if (!strcmp(name, "a"))			return T_A;
	if (!strcmp(name, "pre"))		return T_PRE;
	if (!strcmp(name, "blockquote"))	return T_BQ;
	if (!strcmp(name, "center"))		return T_CENTER;
	if (!strcmp(name, "tr"))		return T_TR;
	if (!strcmp(name, "table"))		return T_TABLE;
	if (!strcmp(name, "title"))		return T_TITLE;
	if (!strcmp(name, "script"))		return T_SCRIPT;
	if (!strcmp(name, "style"))		return T_STYLE;
	if (!strcmp(name, "head"))		return T_HEAD;
	if (!strcmp(name, "body"))		return T_BODY;
	if (!strcmp(name, "form"))		return T_FORM;
	if (!strcmp(name, "input"))		return T_INPUT;
	if (!strcmp(name, "button"))		return T_BUTTON;
	if (!strcmp(name, "textarea"))		return T_TEXTAREA;
	return T_OTHER;
}

/* -- flow layout ------------------------------------------------------ */

static char lbuf[LINE_MAX];		/* line being built */
static WORD lcol;			/* current column */
static BYTE cur_style;			/* active style for new text */
static WORD blank_pending;		/* suppress leading blank lines */
static WORD cur_href_idx = -1;		/* href index of the active <a>, or -1 */
static WORD line_link = -1;		/* href index for the line being built */

static VOID
emit_line(void)
{
	if (g_nlines >= MAX_LINES)
		return;
	lbuf[lcol] = 0;
	strcpy(g_line[g_nlines], lbuf);
	g_style[g_nlines] = cur_style;
	g_line_href[g_nlines] = line_link;
	g_nlines++;
	lcol = 0;
	lbuf[0] = 0;
	line_link = -1;
}

/* Force a line break, flushing any partial line. */
static VOID
break_line(void)
{
	if (lcol > 0)
		emit_line();
}

/* Blank separator line, but never two in a row and not at the very top. */
static VOID
blank_line(void)
{
	break_line();
	if (g_nlines == 0 || g_style[g_nlines - 1] == 0xff)
		return;			/* already blank */
	if (g_nlines < MAX_LINES) {
		g_line[g_nlines][0] = 0;
		g_style[g_nlines] = 0xff;	/* 0xff marks a blank */
		g_nlines++;
	}
}

/* Append one word, wrapping to the next line when it will not fit. */
static VOID
put_word(const char *word, WORD len)
{
	if (len <= 0)
		return;
	if (len >= g_cols)
		len = g_cols - 1;
	if (lcol > 0 && lcol + 1 + len > g_cols)
		emit_line();
	if (lcol > 0)
		lbuf[lcol++] = ' ';
	if (lcol + len >= LINE_MAX)
		len = LINE_MAX - 1 - lcol;
	memcpy(lbuf + lcol, word, len);
	lcol += len;
	if (cur_href_idx >= 0)		/* this line carries a link */
		line_link = cur_href_idx;
}

/* Reserve `width` columns on the current line for a form widget and record it. */
static VOID
place_field(WORD width, WORD is_submit, const char *name, const char *value)
{
	struct field *f;
	WORD i;

	if (g_nfields >= MAX_FIELDS)
		return;
	if (width >= g_cols)
		width = g_cols - 1;
	if (width < 1)
		width = 1;
	if (lcol > 0 && lcol + 1 + width > g_cols)
		emit_line();
	if (lcol > 0)
		lbuf[lcol++] = ' ';
	if (lcol + width >= LINE_MAX)
		width = LINE_MAX - 1 - lcol;

	f = &g_fields[g_nfields++];
	strncpy(f->name, name, FLD_NAME - 1);  f->name[FLD_NAME - 1] = 0;
	strncpy(f->value, value, FLD_VAL - 1); f->value[FLD_VAL - 1] = 0;
	f->line = g_nlines;		/* line currently being built */
	f->col = lcol;
	f->width = width;
	f->is_submit = (BYTE) is_submit;

	for (i = 0; i < width; i++)	/* blank placeholder under the widget */
		lbuf[lcol++] = ' ';
}

/*
 * Copy the value of attribute `key` found in the tag attribute span [a,b) into
 * dst (bounded by cap).  Returns TRUE if the attribute was present.  Matches on
 * a word boundary so "type" is not found inside "prototype".
 */
static WORD
extract_attr(const char *a, const char *b, const char *key, char *dst, WORD cap)
{
	WORD klen = (WORD) strlen(key);
	char prev = ' ';

	dst[0] = 0;
	while (a + klen <= b) {
		WORD k, m = 1;

		if ((prev | 0x20) >= 'a' && (prev | 0x20) <= 'z') {
			prev = *a++;		/* still inside a word */
			continue;
		}
		for (k = 0; k < klen; k++)
			if ((a[k] | 0x20) != (key[k] | 0x20)) { m = 0; break; }
		if (m) {
			const char *v = a + klen;
			char quote = 0;
			WORD i = 0;

			while (v < b && (*v == ' ' || *v == '\t')) v++;
			if (v < b && *v == '=') {
				v++;
				while (v < b && (*v == ' ' || *v == '\t')) v++;
				if (v < b && (*v == '"' || *v == '\'')) {
					quote = *v; v++;
				}
				while (v < b && i < cap - 1) {
					if (quote) { if (*v == quote) break; }
					else if (*v == ' ' || *v == '\t' ||
						 *v == '>') break;
					dst[i++] = *v++;
				}
				dst[i] = 0;
				return 1;
			}
		}
		prev = *a++;
	}
	return 0;
}

/* Convenience wrapper: the href of an <a>. */
static WORD
extract_href(const char *a, const char *b, char *dst, WORD cap)
{
	return extract_attr(a, b, "href", dst, cap);
}

/*
 * Tokenise g_html[] and flow it into g_line[]/g_style[].  One streaming pass:
 * text is word-wrapped; block tags break the line; a small style stack is not
 * needed because we only track bold/underline depth counters.
 */
static VOID
build_layout(void)
{
	const char *p = g_html;
	const char *end = g_html + g_htmllen;
	char word[LINE_MAX];
	WORD wlen = 0;
	WORD bold = 0, uline = 0, pre = 0, skip = 0, in_title = 0;
	WORD tlen = 0;

	g_nlines = 0;
	lcol = 0;
	lbuf[0] = 0;
	cur_style = 0;
	blank_pending = 0;
	cur_href_idx = -1;
	line_link = -1;
	g_nhref = 0;
	g_nfields = 0;
	g_focus = -1;
	form_action[0] = 0;
	memset(g_line_href, 0xff, sizeof(g_line_href));	/* all -1 */
	strcpy(g_title, " GEM Browser ");	/* overwritten by <title> */

	while (p < end && *p) {
		if (*p == '<') {
			WORD closing = 0, id;
			char name[16];
			WORD n = 0;
			const char *q = p + 1;
			const char *attr_start;

			/* flush any pending word before the tag acts */
			if (wlen) { put_word(word, wlen); wlen = 0; }

			if (*q == '/') { closing = 1; q++; }
			if (*q == '!') {	/* comment / doctype */
				while (q < end && *q != '>') q++;
				p = (*q == '>') ? q + 1 : q;
				continue;
			}
			while (n < 15 && ((*q >= 'a' && *q <= 'z') ||
					  (*q >= 'A' && *q <= 'Z') ||
					  (*q >= '0' && *q <= '9'))) {
				char c = *q++;
				if (c >= 'A' && c <= 'Z') c += 32;
				name[n++] = c;
			}
			name[n] = 0;
			attr_start = q;
			while (q < end && *q != '>') q++;	/* attribute span */
			p = (*q == '>') ? q + 1 : q;
			id = tag_id(name);

			if (skip) {		/* inside script/style */
				if (closing && (id == T_SCRIPT || id == T_STYLE))
					skip = 0;
				continue;
			}
			switch (id) {
			case T_SCRIPT: case T_STYLE:
				if (!closing) skip = 1;
				break;
			case T_TITLE:
				in_title = !closing; tlen = 0;
				break;
			case T_B:
				bold += closing ? -1 : 1; if (bold < 0) bold = 0;
				break;
			case T_U:
				uline += closing ? -1 : 1; if (uline < 0) uline = 0;
				break;
			case T_A:
				if (!closing) {
					uline++;
					cur_href_idx = -1;
					if (g_nhref < MAX_HREF &&
					    extract_href(attr_start, q,
						g_href[g_nhref], HREF_MAX)) {
						cur_href_idx = g_nhref;
						g_nhref++;
					}
				} else {
					uline--; if (uline < 0) uline = 0;
					cur_href_idx = -1;
				}
				break;
			case T_H:
				bold += closing ? -1 : 1; if (bold < 0) bold = 0;
				blank_line();
				break;
			case T_P: case T_DIV: case T_UL: case T_OL:
			case T_BQ: case T_TABLE:
				blank_line();
				break;
			case T_BR: case T_TR:
				break_line();
				break;
			case T_HR:
				break_line();
				if (g_nlines < MAX_LINES) {
					WORD k = g_cols - 1;
					if (k > LINE_MAX - 1) k = LINE_MAX - 1;
					memset(g_line[g_nlines], '-', k);
					g_line[g_nlines][k] = 0;
					g_style[g_nlines] = 0;
					g_nlines++;
				}
				break;
			case T_LI:
				break_line();
				if (!closing)
					put_word("*", 1);	/* bullet */
				break;
			case T_PRE:
				pre = !closing; break_line();
				break;
			case T_CENTER:
				break_line();
				break;
			case T_FORM:
				if (!closing)
					extract_attr(attr_start, q, "action",
						form_action, URL_MAX);
				break;
			case T_INPUT:
				if (!closing) {
					char ty[16], nm[FLD_NAME], vl[FLD_VAL];
					WORD ti;

					extract_attr(attr_start, q, "type", ty,
						sizeof(ty));
					extract_attr(attr_start, q, "name", nm,
						sizeof(nm));
					extract_attr(attr_start, q, "value", vl,
						sizeof(vl));
					for (ti = 0; ty[ti]; ti++)
						if (ty[ti] >= 'A' && ty[ti] <= 'Z')
							ty[ti] += 32;
					if (wlen) { put_word(word, wlen); wlen = 0; }
					if (!strcmp(ty, "submit") ||
					    !strcmp(ty, "image") ||
					    !strcmp(ty, "button")) {
						char lb[FLD_VAL];
						lb[0] = '[';
						if (vl[0]) {
							strncpy(lb + 1, vl,
								FLD_VAL - 4);
							lb[FLD_VAL - 4] = 0;
						} else
							strcpy(lb + 1, " Go ");
						strcat(lb, "]");
						place_field((WORD) strlen(lb), 1,
							nm, lb);
					} else if (!strcmp(ty, "hidden") ||
						   !strcmp(ty, "checkbox") ||
						   !strcmp(ty, "radio")) {
						/* not rendered */
					} else {
						place_field(20, 0, nm, vl);
					}
				}
				break;
			default:
				break;
			}
			cur_style = (BYTE) ((bold ? ST_BOLD : 0) |
					    (uline ? ST_ULINE : 0));
			continue;
		}

		if (skip) {		/* drop <script>/<style> body text */
			p++;
			continue;
		}

		if (*p == '&') {
			char c;
			decode_entity(&p, &c);
			if (in_title) {
				if (tlen < (WORD) sizeof(g_title) - 3)
					g_title[1 + tlen++] = c;
				continue;
			}
			if (wlen < LINE_MAX - 1)
				word[wlen++] = c;
			continue;
		}

		if (pre) {			/* preformatted: keep layout */
			if (*p == '\n') {
				if (wlen) { put_word(word, wlen); wlen = 0; }
				break_line();
			} else if (*p == '\t') {
				if (wlen) { put_word(word, wlen); wlen = 0; }
			} else if (wlen < LINE_MAX - 1) {
				word[wlen++] = *p;
			}
			p++;
			continue;
		}

		if (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') {
			if (in_title) {
				if (tlen && g_title[tlen] != ' '
				    && tlen < (WORD) sizeof(g_title) - 3)
					g_title[1 + tlen++] = ' ';
			} else if (wlen) {
				put_word(word, wlen); wlen = 0;
			}
			p++;
			continue;
		}

		if (in_title) {
			if (tlen < (WORD) sizeof(g_title) - 3)
				g_title[1 + tlen++] = *p;
		} else if (wlen < LINE_MAX - 1) {
			word[wlen++] = *p;
		}
		p++;
	}
	if (wlen) put_word(word, wlen);
	break_line();
	if (tlen) {
		g_title[0] = ' ';
		g_title[1 + tlen] = ' ';
		g_title[2 + tlen] = 0;
	}
}

/* -- rendering (HighWire render.c, one visible band) ------------------ */

/* Content band = work area below the URL toolbar. */
static WORD content_y(void) { return wky + TBAR_H; }
static WORD content_h(void) { WORD h = wkh - TBAR_H; return h < 0 ? 0 : h; }

static WORD
visible_rows(void)
{
	WORD r = content_h() / LINE_H;
	return r < 1 ? 1 : r;
}

/* Draw the URL entry toolbar: a framed field with the URL and a caret. */
static VOID
draw_toolbar(void)
{
	WORD xy[4], box[10];
	WORD bx = wkx + 3, bw = 26;		/* Back button */
	WORD fx = bx + bw + 3, fy = wky + 3;
	WORD fw = wkw - (bw + 9), fh = TBAR_H - 6;
	WORD maxch, len, cx;
	char *show;

	vswr_mode(handle, 1);
	vsf_interior(handle, 1);
	vsf_color(handle, 0);
	xy[0] = wkx; xy[1] = wky; xy[2] = wkx + wkw - 1; xy[3] = wky + TBAR_H - 1;
	vr_recfl(handle, xy);

	vsl_color(handle, 1);			/* Back button */
	box[0] = bx; box[1] = fy; box[2] = bx + bw - 1; box[3] = fy;
	box[4] = bx + bw - 1; box[5] = fy + fh - 1;
	box[6] = bx; box[7] = fy + fh - 1; box[8] = bx; box[9] = fy;
	v_pline(handle, 5, box);
	vst_color(handle, 1);
	v_gtext(handle, bx + 5, fy + fh - 4, "<-");

	box[0] = fx; box[1] = fy; box[2] = fx + fw - 1; box[3] = fy;	/* URL field */
	box[4] = fx + fw - 1; box[5] = fy + fh - 1;
	box[6] = fx; box[7] = fy + fh - 1; box[8] = fx; box[9] = fy;
	v_pline(handle, 5, box);

	maxch = (fw - 8) / CELL_W;
	if (maxch < 1) maxch = 1;
	len = g_urllen;
	show = g_url;
	if (len > maxch) { show = g_url + (len - maxch); len = maxch; }
	vst_color(handle, 1);
	v_gtext(handle, fx + 4, fy + fh - 4, show);
	if (g_focus == -1) {			/* caret only when URL bar focused */
		cx = fx + 4 + len * CELL_W;
		xy[0] = cx; xy[1] = fy + 2; xy[2] = cx; xy[3] = fy + fh - 3;
		v_pline(handle, 2, xy);
	}

	xy[0] = wkx; xy[1] = wky + TBAR_H - 1;	/* separator */
	xy[2] = wkx + wkw - 1; xy[3] = wky + TBAR_H - 1;
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
	if (g_style[idx] == 0xff)		/* blank separator */
		return;
	s = g_line[idx];
	st = g_style[idx];
	len = (WORD) strlen(s);
	if (!len)
		return;
	tx = wkx + 2;
	vst_color(handle, 1);
	v_gtext(handle, tx, scr_y + CELL_H - 3, s);
	if (st & ST_BOLD)			/* faux-bold: overprint +1px */
		v_gtext(handle, tx + 1, scr_y + CELL_H - 3, s);
	if (st & ST_ULINE) {			/* underline links/headings */
		WORD xy[4];
		xy[0] = tx;
		xy[1] = scr_y + CELL_H - 1;
		xy[2] = tx + len * CELL_W;
		xy[3] = scr_y + CELL_H - 1;
		vsl_color(handle, 1);
		v_pline(handle, 2, xy);
	}
}

/* Draw one form widget (input box or submit button) if it is on screen. */
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
	vsf_color(handle, 0);			/* clear the widget cell */
	xy[0] = x; xy[1] = y; xy[2] = x + w2 - 1; xy[3] = y + CELL_H - 1;
	vr_recfl(handle, xy);
	vsl_color(handle, 1);
	box[0] = x; box[1] = y; box[2] = x + w2 - 1; box[3] = y;
	box[4] = x + w2 - 1; box[5] = y + CELL_H - 1;
	box[6] = x; box[7] = y + CELL_H - 1; box[8] = x; box[9] = y;
	v_pline(handle, 5, box);
	vst_color(handle, 1);
	if (f->is_submit) {
		v_gtext(handle, x + 2, y + CELL_H - 3, f->value);
	} else {
		char *show = f->value;
		WORD len = (WORD) strlen(f->value);
		WORD maxch = (w2 - 4) / CELL_W;
		WORD cx;

		if (maxch < 1) maxch = 1;
		if (len > maxch) { show = f->value + (len - maxch); len = maxch; }
		v_gtext(handle, x + 3, y + CELL_H - 3, show);
		if (g_focus == i) {		/* caret in the focused field */
			cx = x + 3 + len * CELL_W;
			xy[0] = cx; xy[1] = y + 2; xy[2] = cx; xy[3] = y + CELL_H - 2;
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

/* Redraw a single field in place (after a keystroke or focus change). */
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
	vsf_color(handle, 0);			/* white page */
	cy = content_y();
	xy[0] = wkx; xy[1] = cy;
	xy[2] = wkx + wkw - 1; xy[3] = wky + wkh - 1;
	vr_recfl(handle, xy);

	rows = visible_rows();
	for (r = 0; r < rows; r++)
		draw_line(r, cy + r * LINE_H);
	draw_fields();

	graf_mouse(M_ON, (LPVOID) 0);
	wind_update(0);
}

/* Redraw only the URL toolbar band (after a keystroke). */
static VOID
paint_toolbar(void)
{
	wind_update(1);
	graf_mouse(M_OFF, (LPVOID) 0);
	draw_toolbar();
	graf_mouse(M_ON, (LPVOID) 0);
	wind_update(0);
}

/* Keep g_top in range and push the slider to match. */
static VOID
sync_slider(void)
{
	WORD rows = visible_rows();
	WORD maxtop = g_nlines - rows;
	WORD pos, siz;

	if (maxtop < 0) maxtop = 0;
	if (g_top > maxtop) g_top = maxtop;
	if (g_top < 0) g_top = 0;

	if (g_nlines <= rows) {
		pos = 0; siz = 1000;
	} else {
		pos = (WORD) (((long) g_top * 1000) / maxtop);
		siz = (WORD) (((long) rows * 1000) / g_nlines);
		if (siz < 1) siz = 1;
	}
	wind_set(wh, WF_VSLIDE, pos, 0, 0, 0);
	wind_set(wh, 16 /* WF_VSLSIZ */, siz, 0, 0, 0);
}

static VOID
scroll_to(WORD newtop)
{
	WORD rows = visible_rows();
	WORD maxtop = g_nlines - rows;

	if (maxtop < 0) maxtop = 0;
	if (newtop > maxtop) newtop = maxtop;
	if (newtop < 0) newtop = 0;
	if (newtop == g_top)
		return;
	g_top = newtop;
	sync_slider();
	paint();
}

/* -- networking (HighWire http.c / inet.c, minimal HTTP/1.0 GET) ------- */

static char net_host[128];
static char net_path[URL_MAX + 1];
static WORD net_port;
static char net_location[URL_MAX + 1];	/* Location: from a 3xx redirect */

/* Case-insensitive test of whether s begins with the n-char prefix. */
static WORD
ci_prefix(const char *s, const char *pfx, WORD n)
{
	WORD i;
	for (i = 0; i < n; i++)
		if ((s[i] | 0x20) != (pfx[i] | 0x20))
			return 0;
	return 1;
}

/*
 * Split "http://host[:port]/path" (the http:// prefix is optional) into
 * net_host / net_port / net_path.  Returns FALSE on a malformed URL.
 */
static WORD
parse_url(const char *url)
{
	const char *p = url;
	WORD i;

	if (!strncmp(p, "http://", 7))
		p += 7;
	else if (!strncmp(p, "HTTP://", 7))
		p += 7;
	net_port = 80;
	i = 0;
	while (*p && *p != '/' && *p != ':' && i < (WORD) sizeof(net_host) - 1)
		net_host[i++] = *p++;
	net_host[i] = 0;
	if (i == 0)
		return FALSE;
	if (*p == ':') {
		p++;
		net_port = 0;
		while (*p >= '0' && *p <= '9')
			net_port = net_port * 10 + (*p++ - '0');
	}
	while (*p && *p != '/')		/* skip anything before the path */
		p++;
	if (*p != '/') {
		strcpy(net_path, "/");
	} else {
		i = 0;
		while (*p && i < (WORD) sizeof(net_path) - 1)
			net_path[i++] = *p++;
		net_path[i] = 0;
	}
	return TRUE;
}

/*
 * Decode an HTTP chunked-transfer body in place: each chunk is a hex size
 * line, the data, then CRLF, ending with a zero-size chunk.  Returns the
 * decoded length.  Some servers chunk even for HTTP/1.0, so we handle it.
 */
static WORD
decode_chunked(char *buf, WORD len)
{
	WORD in = 0, out = 0;

	while (in < len) {
		WORD sz = 0, any = 0;
		char c;

		while (in < len) {
			c = buf[in];
			if (c >= '0' && c <= '9') {
				sz = sz * 16 + (c - '0'); any = 1; in++;
			} else if ((c | 0x20) >= 'a' && (c | 0x20) <= 'f') {
				sz = sz * 16 + ((c | 0x20) - 'a' + 10);
				any = 1; in++;
			} else
				break;
		}
		while (in < len && buf[in] != '\n') in++;	/* end of size line */
		if (in < len) in++;
		if (!any || sz == 0)
			break;					/* final chunk */
		if ((WORD) (in + sz) > len)
			sz = len - in;
		memmove(buf + out, buf + in, sz);
		out += sz;
		in += sz;
		while (in < len && (buf[in] == '\r' || buf[in] == '\n')) in++;
	}
	buf[out] = 0;
	return out;
}

/*
 * Fetch net_host:net_port net_path into g_html[], strip the HTTP response
 * headers, and set g_htmllen to the body length.  Returns the HTTP status,
 * or a negative value on a socket error.  Uses HTTP/1.0 + Connection: close;
 * a chunked body (some servers send one anyway) is decoded afterwards.
 */
static WORD
net_fetch(void)
{
	struct sockaddr_in sin;
	ipaddr_t ip;
	char req[URL_MAX + 160];
	char *body;
	WORD s, status;
	WORD chunked = 0;
	unsigned got = 0;
	int r;

	ip = in_gethostbyname(net_host);
	if (!ip)
		return -1;
	s = socket(AF_INET, SOCK_STREAM, 0);
	if (s < 0)
		return -2;
	/*
	 * ELKS ktcp requires an explicit local bind (to allocate an ephemeral
	 * port) before connect(), and a zero-linger so close() sends RST -
	 * exactly what urlget does.
	 */
	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = 0;
	sin.sin_addr.s_addr = INADDR_ANY;
	if (bind(s, (struct sockaddr *) &sin, sizeof(sin)) < 0) {
		close(s);
		return -3;
	}
	{
		struct linger lg;
		lg.l_onoff = 1;
		lg.l_linger = 0;
		setsockopt(s, SOL_SOCKET, SO_LINGER, &lg, sizeof(lg));
	}
	sin.sin_family = AF_INET;
	sin.sin_port = htons(net_port);
	sin.sin_addr.s_addr = ip;
	if (in_connect(s, (struct sockaddr *) &sin, sizeof(sin), 15) < 0) {
		close(s);
		return -4;
	}
	strcpy(req, "GET ");
	strcat(req, net_path);
	strcat(req, " HTTP/1.0\r\nHost: ");
	strcat(req, net_host);
	strcat(req, "\r\nUser-Agent: ELKS-HighWire/0.1\r\n"
		    "Connection: close\r\n\r\n");
	if (write(s, req, strlen(req)) < 0) {
		close(s);
		return -4;
	}
	while (got < HTML_MAX - 1 &&
	       (r = read(s, g_html + got, HTML_MAX - 1 - got)) > 0)
		got += (unsigned) r;
	close(s);
	g_html[got] = 0;

	status = 0;			/* "HTTP/1.x NNN ..." */
	{
		char *sp = strchr(g_html, ' ');
		if (sp)
			status = (WORD) atoi(sp + 1);
	}
	body = strstr(g_html, "\r\n\r\n");	/* end of headers */
	if (body)
		body += 4;
	else if ((body = strstr(g_html, "\n\n")) != 0)
		body += 2;
	else
		body = g_html;

	/* Scan headers for a redirect target and chunked transfer-encoding. */
	net_location[0] = 0;
	{
		char *h = g_html;
		while (h < body) {
			if (ci_prefix(h, "location:", 9)) {
				char *v = h + 9;
				WORD i = 0;
				while (v < body && (*v == ' ' || *v == '\t')) v++;
				while (v < body && *v != '\r' && *v != '\n' &&
				       i < URL_MAX)
					net_location[i++] = *v++;
				net_location[i] = 0;
			} else if (ci_prefix(h, "transfer-encoding:", 18)) {
				char *v = h;
				while (v < body && *v != '\n') {
					if (ci_prefix(v, "chunked", 7)) {
						chunked = 1;
						break;
					}
					v++;
				}
			}
			while (h < body && *h != '\n') h++;
			if (h < body) h++;
		}
	}
	g_htmllen = (WORD) (got - (unsigned) (body - g_html));
	memmove(g_html, body, g_htmllen + 1);
	if (chunked)
		g_htmllen = decode_chunked(g_html, g_htmllen);
	return status;
}

/* -- content source --------------------------------------------------- */

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
	"skipped.</p>"
	"</body></html>";

static VOID
load_home(void)
{
	g_htmllen = (WORD) strlen(g_home);
	memcpy(g_html, g_home, g_htmllen);
	g_html[g_htmllen] = 0;
}

/* Build a tiny error/status page so build_layout() has something to show. */
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

/*
 * Rewrite g_url as a web search for the given words.  Google and the other
 * majors are HTTPS-only, which needs TLS we do not have, so the query is sent
 * to FrogFind - a search service built for vintage browsers that answers over
 * plain HTTP with simplified, link-carrying HTML.
 */
static VOID
make_search_url(const char *query)
{
	char enc[URL_MAX + 1];
	WORD i = 0;
	const char *s = query;

	while (*s && i < (WORD) sizeof(enc) - 1) {
		char c = *s++;
		if (c == ' ')
			enc[i++] = '+';
		else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
			 (c >= '0' && c <= '9'))
			enc[i++] = c;
		/* other characters dropped by this minimal encoder */
	}
	enc[i] = 0;
	strcpy(g_url, "http://frogfind.com/?q=");
	strncat(g_url, enc, URL_MAX - strlen(g_url));
	g_urllen = (WORD) strlen(g_url);
}

/* Remember host/port/directory of the shown page so relative links resolve. */
static VOID
set_base(void)
{
	WORD i, last = 0;

	strcpy(base_host, net_host);
	base_port = net_port;
	for (i = 0; net_path[i]; i++)
		if (net_path[i] == '/')
			last = i;
	memcpy(base_dir, net_path, last + 1);
	base_dir[last + 1] = 0;
	if (!base_dir[0]) { base_dir[0] = '/'; base_dir[1] = 0; }
}

/* Fetch and render g_url, following up to a few http:// redirects. */
static VOID
load_url(void)
{
	WORD st, hop;
	const char *q;

	g_url[g_urllen] = 0;
	/* Omnibox: no '.' after the optional http:// prefix means a search. */
	q = g_url;
	if (!strncmp(q, "http://", 7)) q += 7;
	if (*q && !strchr(q, '.') && !strchr(q, ':') && !strchr(q, '/'))
		make_search_url(q);

	/* Push the page we are leaving onto the Back stack (forward moves). */
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
			continue;		/* follow the redirect */
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
	strncpy(g_current, g_url, URL_MAX);	/* remember the shown page */
	g_current[URL_MAX] = 0;
	going_back = 0;
}

/* Go to the previous page on the Back stack. */
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

/* Resolve a possibly-relative href against the current page into dst. */
static WORD
resolve_url(const char *href, char *dst)
{
	if (ci_prefix(href, "https://", 8))
		return 0;			/* caller must handle: no TLS */
	if (ci_prefix(href, "http://", 7)) {
		strncpy(dst, href, URL_MAX);
		dst[URL_MAX] = 0;
		return 1;
	}
	strcpy(dst, "http://");
	strncat(dst, base_host, 100);
	if (base_port != 80) {			/* rare: non-default port */
		char pd[8];
		WORD pv = base_port, k = 0, j;
		char *e;
		while (pv && k < 6) { pd[k++] = (char)('0' + pv % 10); pv /= 10; }
		strcat(dst, ":");
		e = dst + strlen(dst);
		for (j = 0; j < k; j++) e[j] = pd[k - 1 - j];
		e[k] = 0;
	}
	if (!href[0] || href[0] == '?')		/* same directory */
		strncat(dst, base_dir, URL_MAX - strlen(dst));
	else if (href[0] == '/')
		strncat(dst, href, URL_MAX - strlen(dst));
	else {
		strncat(dst, base_dir, URL_MAX - strlen(dst));
		strncat(dst, href, URL_MAX - strlen(dst));
	}
	if (href[0] == '?')			/* preserve a query on same dir */
		strncat(dst, href, URL_MAX - strlen(dst));
	return 1;
}

/* Follow a link href (resolving relatives), then load. */
static VOID
navigate(const char *href)
{
	if (!href[0] || href[0] == '#')
		return;
	if (ci_prefix(href, "https://", 8)) {
		msg_page("HTTPS not supported",
			 "That link needs TLS (https://).");
		g_top = 0; build_layout(); sync_slider(); paint();
		wind_set(wh, WF_NAME, FP_OFF(g_title), FP_SEG(g_title), 0, 0);
		return;
	}
	resolve_url(href, g_url);
	g_urllen = (WORD) strlen(g_url);
	if (g_urllen > URL_MAX) g_urllen = URL_MAX;
	load_url();
}

/* Which field is at screen point (mx,my), or -1. */
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

/* Submit the form: build action?name=value&... from the text fields, load. */
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
			else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
				 (c >= '0' && c <= '9'))
				query[n++] = c;
		}
	}
	query[n] = 0;

	if (!resolve_url(form_action, g_url)) {
		msg_page("HTTPS not supported",
			 "This form submits to https://, which needs TLS.");
		g_top = 0; build_layout(); sync_slider(); paint();
		wind_set(wh, WF_NAME, FP_OFF(g_title), FP_SEG(g_title), 0, 0);
		return;
	}
	if (query[0]) {				/* append the query string */
		strncat(g_url, strchr(g_url, '?') ? "&" : "?",
			URL_MAX - strlen(g_url));
		strncat(g_url, query, URL_MAX - strlen(g_url));
	}
	g_urllen = (WORD) strlen(g_url);
	if (g_urllen > URL_MAX) g_urllen = URL_MAX;
	load_url();
}

int
main(int argc, char **argv)
{
	UWORD ev, mx, my, mb, ks, kr, br;
	WORD i;

	(void) argc; (void) argv;
	(void) chdir("/GEMAPPS/GEMSYS");
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
	if (g_cols > LINE_MAX - 1) g_cols = LINE_MAX - 1;
	if (g_cols < 8) g_cols = 8;
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
			if ((WORD) my < content_y()) {	/* toolbar */
				if ((WORD) mx >= wkx + 3 &&
				    (WORD) mx < wkx + 3 + 26) {
					go_back();	/* Back button */
				} else if (g_focus != -1) {
					g_focus = -1; paint();	/* focus URL */
				}
			} else {
				WORD fi = field_at((WORD) mx, (WORD) my);
				if (fi >= 0) {		/* a form widget */
					if (g_fields[fi].is_submit)
						submit_form();
					else if (g_focus != fi) {
						g_focus = fi; paint();
					}
				} else {		/* maybe a link */
					WORD ln = g_top +
						((WORD) my - content_y()) / LINE_H;
					if (ln >= 0 && ln < g_nlines &&
					    g_line_href[ln] >= 0)
						navigate(g_href[g_line_href[ln]]);
				}
			}
		}
		if ((ev & MU_KEYBD) && g_focus >= 0 && g_focus < g_nfields &&
		    !g_fields[g_focus].is_submit) {
			/* typing into a focused form field */
			struct field *f = &g_fields[g_focus];
			WORD ascii = kr & 0xff;
			WORD vl = (WORD) strlen(f->value);
			if (ascii == 13) {
				submit_form();
			} else if (ascii == 8 || ascii == 127) {
				if (vl > 0) { f->value[vl - 1] = 0;
					redraw_field(g_focus); }
			} else if (ascii >= 32 && ascii < 127) {
				if (vl < FLD_VAL - 1) {
					f->value[vl] = (char) ascii;
					f->value[vl + 1] = 0;
					redraw_field(g_focus);
				}
			}
		} else if (ev & MU_KEYBD) {	/* URL field editing */
			WORD ascii = kr & 0xff;
			if (ascii == 13) {		/* Return: fetch */
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
			wind_set(wh, WF_CURRXYWH, msg[4], msg[5], msg[6], msg[7]);
			wind_get(wh, WF_WORKXYWH, &wkx, &wky, &wkw, &wkh);
			break;
		case WM_SIZED:
			wind_set(wh, WF_CURRXYWH, msg[4], msg[5], msg[6], msg[7]);
			wind_get(wh, WF_WORKXYWH, &wkx, &wky, &wkw, &wkh);
			g_cols = wkw / CELL_W - 1;
			if (g_cols > LINE_MAX - 1) g_cols = LINE_MAX - 1;
			if (g_cols < 8) g_cols = 8;
			build_layout();
			g_top = 0;
			sync_slider();
			paint();
			break;
		case WM_FULLED: {
			WORD cx, cy, cw, ch, fx, fy, fw, fh, px, py, pw, ph;
			wind_get(wh, WF_CURRXYWH, &cx, &cy, &cw, &ch);
			wind_get(wh, WF_FULLXYWH, &fx, &fy, &fw, &fh);
			if (cw == fw && ch == fh) {
				wind_get(wh, WF_PREVXYWH, &px, &py, &pw, &ph);
				wind_set(wh, WF_CURRXYWH, px, py, pw, ph);
			} else {
				wind_set(wh, WF_CURRXYWH, fx, fy, fw, fh);
			}
			wind_get(wh, WF_WORKXYWH, &wkx, &wky, &wkw, &wkh);
			g_cols = wkw / CELL_W - 1;
			if (g_cols > LINE_MAX - 1) g_cols = LINE_MAX - 1;
			if (g_cols < 8) g_cols = 8;
			build_layout();
			g_top = 0;
			sync_slider();
			paint();
			break;
		}
		case WM_ARROWED:
			switch (msg[4]) {
			case WA_UPLINE: scroll_to(g_top - 1); break;
			case WA_DNLINE: scroll_to(g_top + 1); break;
			case WA_UPPAGE: scroll_to(g_top - visible_rows()); break;
			case WA_DNPAGE: scroll_to(g_top + visible_rows()); break;
			}
			break;
		case WM_VSLID: {
			WORD rows = visible_rows();
			WORD maxtop = g_nlines - rows;
			if (maxtop < 0) maxtop = 0;
			scroll_to((WORD) (((long) msg[4] * maxtop) / 1000));
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
