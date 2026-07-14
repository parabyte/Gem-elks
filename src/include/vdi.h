/*
 * Native GEM VDI interface for ELKS.
 *
 * Copyright (c) 2026 elks-gem contributors
 *
 * The public surface in this file is deliberately small and uses only
 * 8-bit and 16-bit values.  The implementation follows the raster layout
 * and mode setup
 * used by the GPL-released Digital Research/FreeGEM PC display drivers.
 *
 * Coordinates are signed 16-bit pixel positions.  Colors are unsigned
 * 16-bit GEM palette indexes, although the PC drivers consume only the low
 * four bits.  Monochrome drivers saturate palette index zero to black and
 * every nonzero index to white.  No fixed-point values cross this interface.
 */

#ifndef ELKS_GEM_VDI_H
#define ELKS_GEM_VDI_H

/*
 * The ELKS ia16 ABI defines int as one 16-bit 8086 word.  Using int here
 * keeps the VDI arrays representation-compatible with the original GEM
 * WORD arrays without a copy or an aliasing cast at every AES call.
 */
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

/*
 * GEM mouse forms are always 16 by 16 pixels.  Each word is one scanline;
 * bit 15 is the leftmost pixel.  The mask selects touched pixels and the
 * image selects foreground or background for those pixels.
 */
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

/* Raster operations supported by the original GEM screen drivers. */
#define GEM_VDI_REPLACE	0
#define GEM_VDI_XOR	1
#define GEM_VDI_OR	2
#define GEM_VDI_AND	3
#define GEM_VDI_CLEAR	4

/* Arc types used by gem_vdi_arc(). */
#define GEM_VDI_ARC_OUTLINE	0
#define GEM_VDI_ARC_PIE		1

/* Mouse button bits match the GEM AES button-state word. */
#define GEM_VDI_BUTTON_LEFT	0x0001
#define GEM_VDI_BUTTON_RIGHT	0x0002
#define GEM_VDI_BUTTON_MIDDLE	0x0004

/* Keyboard modifiers match the four GEM AES K_* bits after translation. */
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
	const GEM_VDI_RECT *rect);

void gem_vdi_point(GEM_VDI_SCREEN *screen, GEM_VDI_COORD x,
	GEM_VDI_COORD y);
void gem_vdi_line(GEM_VDI_SCREEN *screen, GEM_VDI_COORD x1,
	GEM_VDI_COORD y1, GEM_VDI_COORD x2, GEM_VDI_COORD y2,
	GEM_VDI_WORD draw_last);

/*
 * Draw one line through a classic 16-bit GEM line-style mask.  Bit 15 is
 * tested for the first point and the mask rotates toward bit zero once per
 * Bresenham step.  The phase restarts at bit 15 for each call, matching the
 * original driver's user-defined-line operation.  Pattern 0xffff selects
 * every point; pattern zero selects none.  No scaled or fractional value
 * crosses this interface.
 */
void gem_vdi_pattern_line(GEM_VDI_SCREEN *screen, GEM_VDI_COORD x1,
	GEM_VDI_COORD y1, GEM_VDI_COORD x2, GEM_VDI_COORD y2,
	GEM_VDI_UWORD pattern, GEM_VDI_WORD draw_last);
void gem_vdi_rect(GEM_VDI_SCREEN *screen, GEM_VDI_COORD x,
	GEM_VDI_COORD y, GEM_VDI_COORD width, GEM_VDI_COORD height);
void gem_vdi_fill_rect(GEM_VDI_SCREEN *screen, GEM_VDI_COORD x,
	GEM_VDI_COORD y, GEM_VDI_COORD width, GEM_VDI_COORD height);

/*
 * Fill a rectangle from an eight-row, screen-aligned monochrome pattern.
 * Bit 7 is the pixel whose absolute x coordinate is a multiple of eight.
 * A set bit selects the current foreground and a clear bit selects the
 * current background.  The operation always replaces both colors; it is the
 * byte-oriented fast path used for original GEM two-color desktop patterns.
 */
void gem_vdi_fill_pattern(GEM_VDI_SCREEN *screen, GEM_VDI_COORD x,
	GEM_VDI_COORD y, GEM_VDI_COORD width, GEM_VDI_COORD height,
	const GEM_VDI_UBYTE *pattern);

/*
 * Draw a native GEM one-bit form.  Source rows contain (width + 15) / 16
 * native 8086 words; bit 15 is the leftmost pixel in each word.  A set bit
 * selects the current foreground.  A clear bit selects the background when
 * use-background is enabled and is transparent otherwise.  The PC driver
 * consumes these original words directly without a converted bitmap or heap
 * allocation.
 */
void gem_vdi_bitmap(GEM_VDI_SCREEN *screen, GEM_VDI_COORD x,
	GEM_VDI_COORD y, GEM_VDI_COORD width, GEM_VDI_COORD height,
	const GEM_VDI_BITS *bits);

/*
 * Overlay a compact, byte-row glyph without a converted bitmap.  Every
 * source row is one byte and source_left_bit is the one-bit mask for the
 * glyph's leftmost pixel.  Source bits advance with one-bit right shifts.
 * Set bits replace video with the current foreground; clear bits are
 * transparent.  Width must not extend beyond bit zero from source_left_bit.
 */
void gem_vdi_glyph(GEM_VDI_SCREEN *screen, GEM_VDI_COORD x,
	GEM_VDI_COORD y, GEM_VDI_COORD width, GEM_VDI_COORD height,
	const GEM_VDI_UBYTE *rows, GEM_VDI_UBYTE source_left_bit);

/*
 * Try one native, fully visible 8-by-16 BIOS-font run.  CHARACTERS contains
 * COUNT low-byte codes separated by STRIDE bytes; the current uses are the
 * original VDI word array (stride two) and a resident byte string (stride
 * one).  FONT_SEGMENT:FONT_OFFSET remains an explicit real-mode address pair.
 * The function returns zero without drawing when alignment, clipping, font,
 * or driver support is unsuitable, so the original glyph loop remains the
 * exact fallback.  Pixel and byte scales are one and all arithmetic is 16-bit.
 */
GEM_VDI_WORD gem_vdi_text_run(GEM_VDI_SCREEN *screen, GEM_VDI_COORD x,
	GEM_VDI_COORD y, const GEM_VDI_UBYTE *characters,
	GEM_VDI_UWORD count, GEM_VDI_UWORD stride,
	GEM_VDI_UWORD font_segment, GEM_VDI_UWORD font_offset);
void gem_vdi_fill_polygon(GEM_VDI_SCREEN *screen, GEM_VDI_WORD count,
	const GEM_VDI_WORD *xy);
void gem_vdi_area(GEM_VDI_SCREEN *screen, GEM_VDI_COORD x,
	GEM_VDI_COORD y, GEM_VDI_COORD width, GEM_VDI_COORD height,
	const GEM_VDI_WORD *colors);
void gem_vdi_ellipse(GEM_VDI_SCREEN *screen, GEM_VDI_COORD x,
	GEM_VDI_COORD y, GEM_VDI_COORD rx, GEM_VDI_COORD ry,
	GEM_VDI_WORD fill);
void gem_vdi_arc(GEM_VDI_SCREEN *screen, GEM_VDI_COORD x,
	GEM_VDI_COORD y, GEM_VDI_COORD rx, GEM_VDI_COORD ry,
	GEM_VDI_WORD start_tenths, GEM_VDI_WORD end_tenths,
	GEM_VDI_WORD type);
void gem_vdi_blit(GEM_VDI_SCREEN *screen, GEM_VDI_COORD dst_x,
	GEM_VDI_COORD dst_y, GEM_VDI_COORD width, GEM_VDI_COORD height,
	GEM_VDI_COORD src_x, GEM_VDI_COORD src_y, GEM_VDI_WORD mode);
GEM_VDI_COLOR gem_vdi_read_pixel(GEM_VDI_SCREEN *screen,
	GEM_VDI_COORD x, GEM_VDI_COORD y);
void gem_vdi_read_area(GEM_VDI_SCREEN *screen, GEM_VDI_COORD x,
	GEM_VDI_COORD y, GEM_VDI_COORD width, GEM_VDI_COORD height,
	GEM_VDI_COLOR *colors);

GEM_VDI_WORD gem_vdi_open_input(GEM_VDI_SCREEN *screen);
void gem_vdi_close_input(void);
GEM_VDI_WORD gem_vdi_read_mouse(GEM_VDI_COORD *x, GEM_VDI_COORD *y,
	GEM_VDI_WORD *buttons);
GEM_VDI_WORD gem_vdi_read_keyboard(GEM_VDI_UWORD *character,
	GEM_VDI_UWORD *modifiers, GEM_VDI_UWORD *scan_code);

/* Low 16 bits of the PC BIOS 18.2 Hz tick counter; subtraction wraps. */
GEM_VDI_UWORD gem_vdi_clock_ticks(void);

/*
 * Read the same PC/XT BIOS counter as explicit high and low 16-bit halves.
 * The wire value is an unscaled count of about 18.2 ticks per second.  The
 * helper performs no wide C conversion; callers compare, carry, borrow, and
 * saturate the halves explicitly when a time interval crosses 65535 ticks.
 */
void gem_vdi_clock_words(GEM_VDI_UWORD *high, GEM_VDI_UWORD *low);

void gem_vdi_set_cursor(const GEM_VDI_CURSOR *cursor);
void gem_vdi_move_cursor(GEM_VDI_COORD x, GEM_VDI_COORD y);
void gem_vdi_show_cursor(GEM_VDI_SCREEN *screen);
void gem_vdi_hide_cursor(GEM_VDI_SCREEN *screen);

#endif /* ELKS_GEM_VDI_H */
