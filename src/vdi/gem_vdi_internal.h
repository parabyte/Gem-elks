/*
 * gem_vdi_internal.h - shared internals of the resident VDI
 *
 * the work is split across files: gem_vdi_resident.c dispatches and holds the
 * workstation records, gem_vdi_output.c draws, gem_vdi_mouse.c runs the cursor,
 * and what they share sits here
 *
 * nothing outside them should include this header, the public interface is
 * gem_vdi_resident.h
 */

#ifndef ELKS_GEM_VDI_INTERNAL_H
#define ELKS_GEM_VDI_INTERNAL_H

#include "gem_vdi_resident.h"

#include "gem_resident_memory.h"
#include "gem_vdi_text.h"

#define GEM_VDI_CONTROL_WORDS	11U
#define GEM_VDI_INTIN_WORDS	80U
#define GEM_VDI_PTSIN_WORDS	256U
#define GEM_VDI_INTOUT_WORDS	45U
#define GEM_VDI_PTSOUT_WORDS	12U

#define GEM_VDI_MAX_INPUT_POINTS	128U
#define GEM_VDI_MAX_OUTPUT_POINTS	6U

#define GEM_VDI_WS_FREE		0
#define GEM_VDI_WS_OPEN		1

#define GEM_VDI_OP_OPEN		1
#define GEM_VDI_OP_CLOSE	2
#define GEM_VDI_OP_CLEAR	3
#define GEM_VDI_OP_UPDATE	4
#define GEM_VDI_OP_ESCAPE	5
#define GEM_VDI_OP_PLINE	6
#define GEM_VDI_OP_PMARKER	7
#define GEM_VDI_OP_GTEXT	8
#define GEM_VDI_OP_FILLAREA	9
#define GEM_VDI_OP_GDP		11
#define GEM_VDI_OP_TEXT_HEIGHT	12
#define GEM_VDI_OP_TEXT_ROTATION	13
#define GEM_VDI_OP_SET_COLOR	14
#define GEM_VDI_OP_LINE_TYPE	15
#define GEM_VDI_OP_LINE_WIDTH	16
#define GEM_VDI_OP_LINE_COLOR	17
#define GEM_VDI_OP_MARKER_TYPE	18
#define GEM_VDI_OP_MARKER_HEIGHT	19
#define GEM_VDI_OP_MARKER_COLOR	20
#define GEM_VDI_OP_TEXT_FONT	21
#define GEM_VDI_OP_TEXT_COLOR	22
#define GEM_VDI_OP_FILL_INTERIOR	23
#define GEM_VDI_OP_FILL_STYLE	24
#define GEM_VDI_OP_FILL_COLOR	25
#define GEM_VDI_OP_QUERY_COLOR	26
#define GEM_VDI_OP_LOCATOR	28
#define GEM_VDI_OP_CHOICE	30
#define GEM_VDI_OP_STRING	31
#define GEM_VDI_OP_WRITE_MODE	32
#define GEM_VDI_OP_INPUT_MODE	33
#define GEM_VDI_OP_LINE_ATTR	35
#define GEM_VDI_OP_MARKER_ATTR	36
#define GEM_VDI_OP_FILL_ATTR	37
#define GEM_VDI_OP_TEXT_ATTR	38
#define GEM_VDI_OP_TEXT_ALIGN	39
#define GEM_VDI_OP_OPEN_VIRTUAL	100
#define GEM_VDI_OP_CLOSE_VIRTUAL	101
#define GEM_VDI_OP_EXTENDED	102
#define GEM_VDI_OP_FILL_PERIMETER	104
#define GEM_VDI_OP_TEXT_EFFECTS	106
#define GEM_VDI_OP_TEXT_POINT	107
#define GEM_VDI_OP_LINE_ENDS	108
#define GEM_VDI_OP_QUERY_INPUT_MODE	115
#define GEM_VDI_OP_TEXT_EXTENT	116
#define GEM_VDI_OP_TEXT_WIDTH	117
#define GEM_VDI_OP_TIMER_VECTOR	118
#define GEM_VDI_OP_LOAD_FONTS	119
#define GEM_VDI_OP_UNLOAD_FONTS	120
#define GEM_VDI_OP_COPY_FORM	109
#define GEM_VDI_OP_TRANSFORM	110
#define GEM_VDI_OP_CURSOR_FORM	111
#define GEM_VDI_OP_USER_PATTERN	112
#define GEM_VDI_OP_USER_LINE	113
#define GEM_VDI_OP_FILL_RECT	114
#define GEM_VDI_OP_COPY_TRANSPARENT 121
#define GEM_VDI_OP_SHOW_CURSOR	122
#define GEM_VDI_OP_HIDE_CURSOR	123
#define GEM_VDI_OP_QUERY_MOUSE	124
#define GEM_VDI_OP_BUTTON_VECTOR	125
#define GEM_VDI_OP_MOTION_VECTOR	126
#define GEM_VDI_OP_CURSOR_VECTOR	127
#define GEM_VDI_OP_QUERY_KEY	128
#define GEM_VDI_OP_CLIP		129
#define GEM_VDI_OP_TEXT_NAME	130
#define GEM_VDI_OP_TEXT_FONTINFO	131
#define GEM_VDI_OP_TEXT_JUSTIFIED	132

#define GEM_VDI_ESCAPE_SOUND	61
#define GEM_VDI_ESCAPE_MUTE	62

#define GEM_VDI_GDP_BAR		1
#define GEM_VDI_GDP_ARC		2
#define GEM_VDI_GDP_PIE		3
#define GEM_VDI_GDP_CIRCLE	4
#define GEM_VDI_GDP_ELLIPSE	5
#define GEM_VDI_GDP_ELLIPTIC_ARC 6
#define GEM_VDI_GDP_ELLIPTIC_PIE 7
#define GEM_VDI_GDP_ROUNDED_BOX	8
#define GEM_VDI_GDP_FILLED_ROUNDED_BOX 9
#define GEM_VDI_GDP_JUSTIFIED	10
#define GEM_VDI_GDP_COUNT	10U

/* classic GEM logical colors */
#define GEM_VDI_WHITE		0
#define GEM_VDI_BLACK		1
#define GEM_VDI_RED		2
#define GEM_VDI_GREEN		3
#define GEM_VDI_BLUE		4
#define GEM_VDI_CYAN		5
#define GEM_VDI_YELLOW		6
#define GEM_VDI_MAGENTA		7

/* classic writing modes are one-based, the native core is zero-based */
#define GEM_VDI_MD_REPLACE	1
#define GEM_VDI_MD_TRANS	2
#define GEM_VDI_MD_XOR		3
#define GEM_VDI_MD_ERASE	4

#define GEM_VDI_FIS_HOLLOW	0
#define GEM_VDI_FIS_SOLID	1
#define GEM_VDI_FIS_PATTERN	2
#define GEM_VDI_FIS_HATCH	3
#define GEM_VDI_FIS_USER	4

/* polymarker types, in the classic order the driver's table keeps */
#define GEM_VDI_MARKER_DOT	1
#define GEM_VDI_MARKER_PLUS	2
#define GEM_VDI_MARKER_STAR	3
#define GEM_VDI_MARKER_SQUARE	4
#define GEM_VDI_MARKER_CROSS	5
#define GEM_VDI_MARKER_DIAMOND	6
#define GEM_VDI_MARKER_TYPES	6
#define GEM_VDI_MARKER_SIZES	8
/* the marker shapes are drawn for a fifteen by eleven cell */
#define GEM_VDI_MARKER_WIDTH	15
#define GEM_VDI_MARKER_HEIGHT	11
#define GEM_VDI_MARKER_MAX_HEIGHT	88
#define GEM_VDI_LINE_TYPES	7

/* input device numbers and modes */
#define GEM_VDI_INPUT_DEVICES	4
#define GEM_VDI_INPUT_REQUEST	1
#define GEM_VDI_INPUT_SAMPLE	2

/* line end styles */
#define GEM_VDI_END_SQUARE	0
#define GEM_VDI_END_ARROW	1
#define GEM_VDI_END_ROUND	2

#define GEM_VDI_CHAR_WIDTH	8
#define GEM_VDI_CHAR_HEIGHT	16
#define GEM_VDI_GLYPH_WIDTH	5
#define GEM_VDI_GLYPH_ADVANCE	8
#define GEM_VDI_SYSTEM_GLYPH_BYTES 16U
#define GEM_VDI_USER_LINE	7

typedef struct gem_vdi_resident_workstation {
	WORD application;
	WORD handle;
	WORD write_mode;
	WORD line_type;
	WORD line_width;
	WORD line_color;
	UWORD line_pattern;
	WORD line_begin;
	WORD line_end;
	WORD text_color;
	WORD text_height;
	WORD marker_type;
	WORD marker_height;
	WORD marker_color;
	WORD fill_interior;
	WORD fill_style;
	WORD fill_color;
	WORD fill_perimeter;
	/* one input mode each for locator, valuator, choice and string */
	WORD input_mode[4];
	/* the client sends a 16 by 16 fill pattern but we tile 8 by 8, so only
	 * the top-left corner is kept */
	GEM_VDI_UBYTE user_pattern[8];
	WORD clip_on;
	WORD clip_x1;
	WORD clip_y1;
	WORD clip_x2;
	WORD clip_y2;
	UWORD cursor_hides;
	UBYTE state;
	UBYTE reserved;
} GEM_VDI_RESIDENT_WORKSTATION;

/*
 * classic GEM MFDB, the address is kept as an offset and segment pair so the
 * record is exactly 20 bytes in either ia16 data model
 */
typedef struct __attribute__((packed)) gem_vdi_resident_mfdb {
	GEM_BINDINGS_POINTER_SLOT memory;
	WORD width_pixels;
	WORD height;
	WORD width_words;
	WORD format;
	WORD planes;
	WORD reserved1;
	WORD reserved2;
	WORD reserved3;
} GEM_VDI_RESIDENT_MFDB;

typedef BYTE GEM_VDI_RESIDENT_MFDB_MUST_BE_20_BYTES
	[(sizeof(GEM_VDI_RESIDENT_MFDB) == 20) ? 1 : -1];

/* GEM MFORM: five words, then sixteen mask and sixteen image rows */
typedef struct __attribute__((packed)) gem_vdi_resident_mform {
	WORD hot_x;
	WORD hot_y;
	WORD planes;
	WORD foreground;
	WORD background;
	UWORD mask[16];
	UWORD image[16];
} GEM_VDI_RESIDENT_MFORM;

typedef BYTE GEM_VDI_RESIDENT_MFORM_MUST_BE_74_BYTES
	[(sizeof(GEM_VDI_RESIDENT_MFORM) == 74) ? 1 : -1];


/* --- the classic five arrays, filled by the dispatcher --- */

extern UWORD gem_vdi_control[GEM_VDI_CONTROL_WORDS];
extern UWORD gem_vdi_intin[GEM_VDI_INTIN_WORDS];
extern UWORD gem_vdi_ptsin[GEM_VDI_PTSIN_WORDS];
extern UWORD gem_vdi_intout[GEM_VDI_INTOUT_WORDS];
extern UWORD gem_vdi_ptsout[GEM_VDI_PTSOUT_WORDS];

/* --- the one physical workstation --- */

extern GEM_VDI_SCREEN *gem_vdi_screen;
extern UWORD gem_vdi_font_segment;
extern UWORD gem_vdi_font_offset;
extern UWORD gem_vdi_font_rows;
extern GEM_VDI_UBYTE gem_vdi_system_glyph[GEM_VDI_SYSTEM_GLYPH_BYTES];

/*
 * read the GEM system face for a cell height of 8, 14 or 16 rows off disk
 * (gem_vdi_sysfont.c) into its own far segment, returns its segment and offset,
 * FALSE means the file is missing or short
 */
WORD gem_vdi_sysfont_load(UWORD rows, UWORD *segment, UWORD *offset);

/* load one client workstation's attributes into the adapter */
VOID gem_vdi_apply_workstation(GEM_VDI_RESIDENT_WORKSTATION * workstation,
	WORD logical_color);
/* classic logical colour and writing mode to the native core's own */
GEM_VDI_COLOR gem_vdi_resident_color(WORD logical);
GEM_VDI_WORD gem_vdi_resident_mode(WORD classic);
WORD gem_vdi_set_one_output(WORD value);
VOID gem_vdi_clear_words(UWORD *words, UWORD count);

/* --- drawing, in gem_vdi_output.c --- */

WORD gem_vdi_fill_area(GEM_VDI_RESIDENT_WORKSTATION * workstation);
VOID gem_vdi_fill_output(GEM_VDI_RESIDENT_WORKSTATION * workstation,
	WORD honor_perimeter);
WORD gem_vdi_draw_text(GEM_VDI_RESIDENT_WORKSTATION * workstation,
	const UWORD *characters, UWORD count,
	const GEM_VDI_TEXT_JUSTIFY *justify);
WORD gem_vdi_draw_markers(GEM_VDI_RESIDENT_WORKSTATION * workstation);
WORD gem_vdi_dispatch_gdp(GEM_VDI_RESIDENT_WORKSTATION * workstation);
WORD gem_vdi_screen_copy(GEM_VDI_RESIDENT_WORKSTATION * workstation,
	const struct gemtrap_request *request);
WORD gem_vdi_transform_form(const struct gemtrap_request *request);
const GEM_VDI_UBYTE *gem_vdi_system_glyph_rows(WORD character);
WORD gem_vdi_text_has_furniture(const GEM_VDI_UBYTE *characters, UWORD count,
	UWORD stride);

/* --- the pointer, in gem_vdi_mouse.c --- */

VOID gem_vdi_resident_apply_cursor(const GEM_VDI_CURSOR *cursor);
WORD gem_vdi_resident_set_form(VOID);
extern WORD gem_vdi_cursor_hot_x;
extern WORD gem_vdi_cursor_hot_y;

#endif				/* ELKS_GEM_VDI_INTERNAL_H */
