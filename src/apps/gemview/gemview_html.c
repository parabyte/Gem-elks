/*
 * gemview_html.c - HTML tokeniser and flow layout
 *
 * one pass over g_html[]: decode entities, read tags, wrap words into
 * lines, and record the links and form fields
 */

#include <string.h>

#include "gemview.h"

/* -- HTML entities ---------------------------------------------------- */

struct entity {
	const char *name;
	char ch;
};
static const struct entity g_entities[] = {
	{ "amp", '&' }, { "lt", '<' }, { "gt", '>' }, { "quot", '"' },
	{ "apos", '\'' }, { "nbsp", ' ' }, { "copy", 'c' }, { "reg", 'r' },
	{ "mdash", '-' }, { "ndash", '-' }, { "hellip", '.' }, { "trade", 't' },
	{ (const char *) 0, 0 }
};

/*
 * decode one &...; entity at *pp into *out, advance *pp past it,
 * non-ASCII becomes '?'
 */
static VOID
decode_entity(const char **pp, char *out)
{
	const char *p = *pp + 1;
	char name[10];
	WORD i;

	if (*p == '#') {	/* numeric, &#NN; or &#xHH; */
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
	*out = '&';		/* unknown entity, keep the '&' */
	*pp = *pp + 1;
}

/* -- tag recognition --------------------------------------------------- */

enum {
	T_OTHER = 0, T_P, T_BR, T_DIV, T_H, T_UL, T_OL, T_LI, T_HR,
	T_B, T_I, T_U, T_A, T_PRE, T_BQ, T_CENTER, T_TR, T_TABLE,
	T_TITLE, T_SCRIPT, T_STYLE, T_HEAD, T_BODY,
	T_FORM, T_INPUT, T_BUTTON, T_TEXTAREA, T_LINK_IGNORE
};

static WORD
tag_id(const char *name)
{
	if (!strcmp(name, "p"))
		return T_P;
	if (!strcmp(name, "br"))
		return T_BR;
	if (!strcmp(name, "div"))
		return T_DIV;
	if (name[0] == 'h' && name[1] >= '1' && name[1] <= '6' && !name[2])
		return T_H;
	if (!strcmp(name, "ul"))
		return T_UL;
	if (!strcmp(name, "ol"))
		return T_OL;
	if (!strcmp(name, "li"))
		return T_LI;
	if (!strcmp(name, "hr"))
		return T_HR;
	if (!strcmp(name, "b") || !strcmp(name, "strong"))
		return T_B;
	if (!strcmp(name, "i") || !strcmp(name, "em"))
		return T_I;
	if (!strcmp(name, "u"))
		return T_U;
	if (!strcmp(name, "a"))
		return T_A;
	if (!strcmp(name, "pre"))
		return T_PRE;
	if (!strcmp(name, "blockquote"))
		return T_BQ;
	if (!strcmp(name, "center"))
		return T_CENTER;
	if (!strcmp(name, "tr"))
		return T_TR;
	if (!strcmp(name, "table"))
		return T_TABLE;
	if (!strcmp(name, "title"))
		return T_TITLE;
	if (!strcmp(name, "script"))
		return T_SCRIPT;
	if (!strcmp(name, "style"))
		return T_STYLE;
	if (!strcmp(name, "head"))
		return T_HEAD;
	if (!strcmp(name, "body"))
		return T_BODY;
	if (!strcmp(name, "form"))
		return T_FORM;
	if (!strcmp(name, "input"))
		return T_INPUT;
	if (!strcmp(name, "button"))
		return T_BUTTON;
	if (!strcmp(name, "textarea"))
		return T_TEXTAREA;
	return T_OTHER;
}

/* -- flow layout ------------------------------------------------------ */

static char lbuf[LINE_MAX];	/* line being built */
static WORD lcol;		/* current column */
static BYTE cur_style;		/* active style for new text */
static WORD blank_pending;	/* suppress leading blank lines */
static WORD cur_href_idx = -1;	/* href index of the active <a>, or -1 */
static WORD line_link = -1;	/* href index for the line being built */

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

static VOID
break_line(void)
{
	if (lcol > 0)
		emit_line();
}

/* blank separator line, never two in a row, none at the top */
static VOID
blank_line(void)
{
	break_line();
	if (g_nlines == 0 || g_style[g_nlines - 1] == 0xff)
		return;
	if (g_nlines < MAX_LINES) {
		g_line[g_nlines][0] = 0;
		g_style[g_nlines] = 0xff;	/* 0xff marks a blank line */
		g_nlines++;
	}
}

/* append one word, wrap when it wont fit */
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
	if (cur_href_idx >= 0)	/* line now carries a link */
		line_link = cur_href_idx;
}

/* reserve width columns for a form widget and record it */
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
	strncpy(f->name, name, FLD_NAME - 1);
	f->name[FLD_NAME - 1] = 0;
	strncpy(f->value, value, FLD_VAL - 1);
	f->value[FLD_VAL - 1] = 0;
	f->line = g_nlines;	/* line being built */
	f->col = lcol;
	f->width = width;
	f->is_submit = (BYTE) is_submit;

	for (i = 0; i < width; i++)	/* blank space under the widget */
		lbuf[lcol++] = ' ';
}

/*
 * copy attr key's value from the span [a,b) into dst, matches on a word
 * boundary so "type" isnt found inside "prototype"
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
			prev = *a++;	/* mid-word */
			continue;
		}
		for (k = 0; k < klen; k++)
			if ((a[k] | 0x20) != (key[k] | 0x20)) {
				m = 0;
				break;
			}
		if (m) {
			const char *v = a + klen;
			char quote = 0;
			WORD i = 0;

			while (v < b && (*v == ' ' || *v == '\t'))
				v++;
			if (v < b && *v == '=') {
				v++;
				while (v < b && (*v == ' ' || *v == '\t'))
					v++;
				if (v < b && (*v == '"' || *v == '\'')) {
					quote = *v;
					v++;
				}
				while (v < b && i < cap - 1) {
					if (quote) {
						if (*v == quote)
							break;
					} else if (*v == ' ' || *v == '\t' ||
						*v == '>')
						break;
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

static WORD
extract_href(const char *a, const char *b, char *dst, WORD cap)
{
	return extract_attr(a, b, "href", dst, cap);
}

/* tokenise g_html[] and flow it into g_line[]/g_style[] in one pass */
VOID
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
	strcpy(g_title, " GEM Browser ");	/* replaced by <title> */

	while (p < end && *p) {
		if (*p == '<') {
			WORD closing = 0, id;
			char name[16];
			WORD n = 0;
			const char *q = p + 1;
			const char *attr_start;

			/* flush the pending word before the tag acts */
			if (wlen) {
				put_word(word, wlen);
				wlen = 0;
			}

			if (*q == '/') {
				closing = 1;
				q++;
			}
			if (*q == '!') {	/* comment or doctype */
				while (q < end && *q != '>')
					q++;
				p = (*q == '>') ? q + 1 : q;
				continue;
			}
			while (n < 15 && ((*q >= 'a' && *q <= 'z') ||
					(*q >= 'A' && *q <= 'Z') ||
					(*q >= '0' && *q <= '9'))) {
				char c = *q++;
				if (c >= 'A' && c <= 'Z')
					c += 32;
				name[n++] = c;
			}
			name[n] = 0;
			attr_start = q;
			while (q < end && *q != '>')
				q++;	/* attribute span */
			p = (*q == '>') ? q + 1 : q;
			id = tag_id(name);

			if (skip) {	/* inside <script> or <style> */
				if (closing && (id == T_SCRIPT
						|| id == T_STYLE))
					skip = 0;
				continue;
			}
			switch (id) {
			case T_SCRIPT:
			case T_STYLE:
				if (!closing)
					skip = 1;
				break;
			case T_TITLE:
				in_title = !closing;
				tlen = 0;
				break;
			case T_B:
				bold += closing ? -1 : 1;
				if (bold < 0)
					bold = 0;
				break;
			case T_U:
				uline += closing ? -1 : 1;
				if (uline < 0)
					uline = 0;
				break;
			case T_A:
				if (!closing) {
					uline++;
					cur_href_idx = -1;
					if (g_nhref < MAX_HREF &&
						extract_href(attr_start, q,
							g_href[g_nhref],
							HREF_MAX)) {
						cur_href_idx = g_nhref;
						g_nhref++;
					}
				} else {
					uline--;
					if (uline < 0)
						uline = 0;
					cur_href_idx = -1;
				}
				break;
			case T_H:
				bold += closing ? -1 : 1;
				if (bold < 0)
					bold = 0;
				blank_line();
				break;
			case T_P:
			case T_DIV:
			case T_UL:
			case T_OL:
			case T_BQ:
			case T_TABLE:
				blank_line();
				break;
			case T_BR:
			case T_TR:
				break_line();
				break;
			case T_HR:
				break_line();
				if (g_nlines < MAX_LINES) {
					WORD k = g_cols - 1;
					if (k > LINE_MAX - 1)
						k = LINE_MAX - 1;
					memset(g_line[g_nlines], '-', k);
					g_line[g_nlines][k] = 0;
					g_style[g_nlines] = 0;
					g_nlines++;
				}
				break;
			case T_LI:
				break_line();
				if (!closing)
					put_word("*", 1);	/* list bullet */
				break;
			case T_PRE:
				pre = !closing;
				break_line();
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
						if (ty[ti] >= 'A'
							&& ty[ti] <= 'Z')
							ty[ti] += 32;
					if (wlen) {
						put_word(word, wlen);
						wlen = 0;
					}
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
						place_field((WORD) strlen(lb),
							1, nm, lb);
					} else if (!strcmp(ty, "hidden")
						|| !strcmp(ty, "checkbox")
						|| !strcmp(ty, "radio")) {
						/* not drawn at all */
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

		if (skip) {	/* script/style body text */
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

		if (pre) {	/* preformatted */
			if (*p == '\n') {
				if (wlen) {
					put_word(word, wlen);
					wlen = 0;
				}
				break_line();
			} else if (*p == '\t') {
				if (wlen) {
					put_word(word, wlen);
					wlen = 0;
				}
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
				put_word(word, wlen);
				wlen = 0;
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
	if (wlen)
		put_word(word, wlen);
	break_line();
	if (tlen) {
		g_title[0] = ' ';
		g_title[1 + tlen] = ' ';
		g_title[2 + tlen] = 0;
	}
}
