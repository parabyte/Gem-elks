/*
 * gemview.h - shared state of the GEM browser
 *
 * gemview_net.c fills g_html[] from a URL, gemview_html.c flows it into
 * display lines, gemview.c draws them and runs the window
 */

#ifndef GEMVIEW_H
#define GEMVIEW_H

#include "aes.h"

/* limits sized to fit the 64 KiB data segment */
#define HTML_MAX	16384	/* raw HTML source buffer */
#define LINE_MAX	88	/* characters per display line */
#define MAX_LINES	240	/* display lines held for a page */
#define URL_MAX		200	/* URL entry field capacity */

#define CELL_W		8	/* BIOS ROM font cell width */
#define CELL_H		16	/* BIOS ROM font cell height */
#define LINE_H		14	/* vertical pitch between lines */
#define TBAR_H		22	/* URL toolbar band height */

/* per-line style bits */
#define ST_BOLD		0x01
#define ST_ULINE	0x02	/* links and headings */

/* href table, plus which href each display line belongs to */
#define MAX_HREF	40
#define HREF_MAX	100

/* form fields, text inputs and submit buttons */
#define MAX_FIELDS	6
#define FLD_NAME	24
#define FLD_VAL		56

struct field {
	char name[FLD_NAME];
	char value[FLD_VAL];
	WORD line, col, width;	/* position in the display grid */
	BYTE is_submit;
};

/* page state, raw source, laid-out lines, links, form fields */
extern char g_html[HTML_MAX];
extern WORD g_htmllen;
extern char g_line[MAX_LINES][LINE_MAX];
extern BYTE g_style[MAX_LINES];
extern WORD g_nlines;		/* number of display lines built */
extern WORD g_top;		/* first visible line (scroll pos) */
extern WORD g_cols;		/* wrap width in characters */
extern char g_title[64];
extern char g_url[URL_MAX + 1];	/* URL entry field */
extern WORD g_urllen;
extern char g_href[MAX_HREF][HREF_MAX];
extern WORD g_nhref;
extern WORD g_line_href[MAX_LINES];	/* href index for the line, or -1 */
extern struct field g_fields[MAX_FIELDS];
extern WORD g_nfields;
extern WORD g_focus;		/* -1 = URL bar, else field index */
extern char form_action[URL_MAX + 1];

/* base of the shown page, for resolving relative links */
extern char base_host[128];
extern WORD base_port;
extern char base_dir[URL_MAX + 1];	/* directory part, ends in '/' */

/* gemview_html.c: flow g_html[] into g_line[]/g_style[] and friends */
VOID build_layout(void);

/* gemview_net.c: URLs and the HTTP fetch */
extern char net_location[URL_MAX + 1];	/* Location: header of a 3xx redirect */
WORD ci_prefix(const char *s, const char *pfx, WORD n);
WORD parse_url(const char *url);
WORD net_fetch(void);
VOID make_search_url(const char *query);
VOID set_base(void);
WORD resolve_url(const char *href, char *dst);

#endif				/* GEMVIEW_H */
