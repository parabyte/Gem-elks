/*
 * vdi.h - native GEM VDI interface for ELKS
 *
 * follows the raster layout and mode setup of the GPL-released Digital
 * Research/FreeGEM PC display drivers. coords are signed 16-bit pixels, colors
 * are GEM palette indexes and the PC drivers use the low four bits. mono drivers
 * map index zero to black and every nonzero index to white
 */

#ifndef ELKS_GEM_VDI_H
#define ELKS_GEM_VDI_H

/* the v_* and vs* binding prototypes live in aes.h */
#include "aes.h"

/* int is one 16-bit word on ia16, so these match the original GEM WORD arrays */
typedef signed int GEM_VDI_COORD;
typedef signed int GEM_VDI_WORD;
typedef unsigned int GEM_VDI_UWORD;
typedef unsigned char GEM_VDI_UBYTE;
typedef GEM_VDI_UWORD GEM_VDI_COLOR;
typedef GEM_VDI_UWORD GEM_VDI_BITS;

typedef struct gem_vdi_rect {
	GEM_VDI_COORD x;
	GEM_VDI_COORD y;
	GEM_VDI_COORD width;
	GEM_VDI_COORD height;
} GEM_VDI_RECT;

typedef struct gem_vdi_point {
	GEM_VDI_COORD x;
	GEM_VDI_COORD y;
} GEM_VDI_POINT;

/* one 16x16 GEM mouse form */
typedef struct gem_vdi_cursor {
	GEM_VDI_WORD width;
	GEM_VDI_WORD height;
	GEM_VDI_WORD hot_x;
	GEM_VDI_WORD hot_y;
	GEM_VDI_COLOR foreground;
	GEM_VDI_COLOR background;
	GEM_VDI_BITS image[16];
	GEM_VDI_BITS mask[16];
} GEM_VDI_CURSOR;

struct gem_vdi_driver;

typedef struct gem_vdi_screen {
	GEM_VDI_COORD xres;
	GEM_VDI_COORD yres;
	GEM_VDI_UWORD planes;
	GEM_VDI_UWORD colors;
	const struct gem_vdi_driver *driver;
} GEM_VDI_SCREEN;

/* the raster ops the original GEM screen drivers support */
#define GEM_VDI_REPLACE	0
#define GEM_VDI_XOR	1
#define GEM_VDI_OR	2
#define GEM_VDI_AND	3
#define GEM_VDI_CLEAR	4

/* arc types used by gem_vdi_arc() */
#define GEM_VDI_ARC_OUTLINE	0
#define GEM_VDI_ARC_PIE		1

/* mouse button bits match the GEM AES button-state word */
#define GEM_VDI_BUTTON_LEFT	0x0001
#define GEM_VDI_BUTTON_RIGHT	0x0002
#define GEM_VDI_BUTTON_MIDDLE	0x0004

/* keyboard modifiers match the four GEM AES K_* bits after translation */
#define GEM_VDI_MOD_RSHIFT	0x0001
#define GEM_VDI_MOD_LSHIFT	0x0002
#define GEM_VDI_MOD_CTRL	0x0004
#define GEM_VDI_MOD_ALT		0x0008

#define GEM_VDI_KEY_NONE	0
#define GEM_VDI_KEY_PRESS	1
#define GEM_VDI_KEY_RELEASE	2
#define GEM_VDI_KEY_ERROR	(-1)

GEM_VDI_SCREEN *gem_vdi_open(void);
void gem_vdi_close(GEM_VDI_SCREEN *screen);
void gem_vdi_flush(GEM_VDI_SCREEN *screen);

GEM_VDI_WORD gem_vdi_set_mode(GEM_VDI_WORD mode);
GEM_VDI_COLOR gem_vdi_set_foreground(GEM_VDI_SCREEN *screen,
	GEM_VDI_COLOR color);
GEM_VDI_COLOR gem_vdi_set_background(GEM_VDI_SCREEN *screen,
	GEM_VDI_COLOR color);
GEM_VDI_WORD gem_vdi_set_use_background(GEM_VDI_WORD enabled);
void gem_vdi_set_clip(GEM_VDI_SCREEN *screen, GEM_VDI_WORD count,
	GEM_VDI_COORD x, GEM_VDI_COORD y, GEM_VDI_COORD width,
	GEM_VDI_COORD height);

void gem_vdi_point(GEM_VDI_SCREEN *screen, GEM_VDI_COORD x, GEM_VDI_COORD y);
void gem_vdi_line(GEM_VDI_SCREEN *screen, GEM_VDI_COORD x1,
	GEM_VDI_COORD y1, GEM_VDI_COORD x2, GEM_VDI_COORD y2,
	GEM_VDI_WORD draw_last);

/* draw a line through one 16-bit style mask */
void gem_vdi_pattern_line(GEM_VDI_SCREEN *screen, GEM_VDI_COORD x1,
	GEM_VDI_COORD y1, GEM_VDI_COORD x2, GEM_VDI_COORD y2,
	GEM_VDI_UWORD pattern, GEM_VDI_WORD draw_last);
void gem_vdi_rect(GEM_VDI_SCREEN *screen, GEM_VDI_COORD x,
	GEM_VDI_COORD y, GEM_VDI_COORD width, GEM_VDI_COORD height);
void gem_vdi_fill_rect(GEM_VDI_SCREEN *screen, GEM_VDI_COORD x,
	GEM_VDI_COORD y, GEM_VDI_COORD width, GEM_VDI_COORD height);

/* fill with an eight-row screen-aligned pattern */
void gem_vdi_fill_pattern(GEM_VDI_SCREEN *screen, GEM_VDI_COORD x,
	GEM_VDI_COORD y, GEM_VDI_COORD width, GEM_VDI_COORD height,
	const GEM_VDI_UBYTE *pattern);

/* draw one GEM one-bit form */
void gem_vdi_bitmap(GEM_VDI_SCREEN *screen, GEM_VDI_COORD x,
	GEM_VDI_COORD y, GEM_VDI_COORD width, GEM_VDI_COORD height,
	const GEM_VDI_BITS *bits);

/* overlay one transparent byte-per-row glyph */
void gem_vdi_glyph(GEM_VDI_SCREEN *screen, GEM_VDI_COORD x,
	GEM_VDI_COORD y, GEM_VDI_COORD width, GEM_VDI_COORD height,
	const GEM_VDI_UBYTE *rows, GEM_VDI_UBYTE source_left_bit);

/* overlay one transparent mono form */
void gem_vdi_form(GEM_VDI_SCREEN *screen, GEM_VDI_COORD x, GEM_VDI_COORD y,
	GEM_VDI_COORD width, GEM_VDI_COORD height,
	const GEM_VDI_UBYTE *rows, GEM_VDI_UWORD stride);

/* try the aligned 8x16 text path */
GEM_VDI_WORD gem_vdi_text_run(GEM_VDI_SCREEN *screen, GEM_VDI_COORD x,
	GEM_VDI_COORD y, const GEM_VDI_UBYTE *characters,
	GEM_VDI_UWORD count, GEM_VDI_UWORD stride,
	GEM_VDI_UWORD font_segment, GEM_VDI_UWORD font_offset);
void gem_vdi_fill_polygon(GEM_VDI_SCREEN *screen, GEM_VDI_WORD count,
	const GEM_VDI_WORD *xy);

/* fill a polygon with a screen-anchored pattern */
void gem_vdi_fill_polygon_pattern(GEM_VDI_SCREEN *screen,
	GEM_VDI_WORD count, const GEM_VDI_WORD *xy,
	const GEM_VDI_UBYTE *pattern);
void gem_vdi_ellipse(GEM_VDI_SCREEN *screen, GEM_VDI_COORD x,
	GEM_VDI_COORD y, GEM_VDI_COORD rx, GEM_VDI_COORD ry, GEM_VDI_WORD fill);
void gem_vdi_arc(GEM_VDI_SCREEN *screen, GEM_VDI_COORD x,
	GEM_VDI_COORD y, GEM_VDI_COORD rx, GEM_VDI_COORD ry,
	GEM_VDI_WORD start_tenths, GEM_VDI_WORD end_tenths, GEM_VDI_WORD type);
void gem_vdi_blit(GEM_VDI_SCREEN *screen, GEM_VDI_COORD dst_x,
	GEM_VDI_COORD dst_y, GEM_VDI_COORD width, GEM_VDI_COORD height,
	GEM_VDI_COORD src_x, GEM_VDI_COORD src_y, GEM_VDI_WORD mode);

GEM_VDI_WORD gem_vdi_open_input(GEM_VDI_SCREEN *screen);
void gem_vdi_close_input(void);
GEM_VDI_WORD gem_vdi_read_mouse(GEM_VDI_COORD *x, GEM_VDI_COORD *y,
	GEM_VDI_WORD *buttons);
GEM_VDI_WORD gem_vdi_read_keyboard(GEM_VDI_UWORD *character,
	GEM_VDI_UWORD *modifiers, GEM_VDI_UWORD *scan_code);

/* low 16 bits of the PC BIOS 18.2 Hz tick counter, subtraction wraps */
GEM_VDI_UWORD gem_vdi_clock_ticks(void);

/* the same BIOS counter as explicit high and low 16-bit halves */
void gem_vdi_clock_words(GEM_VDI_UWORD *high, GEM_VDI_UWORD *low);

void gem_vdi_set_cursor(const GEM_VDI_CURSOR *cursor);
void gem_vdi_move_cursor(GEM_VDI_COORD x, GEM_VDI_COORD y);
void gem_vdi_show_cursor(GEM_VDI_SCREEN *screen);
void gem_vdi_hide_cursor(GEM_VDI_SCREEN *screen);

#endif				/* ELKS_GEM_VDI_H */
