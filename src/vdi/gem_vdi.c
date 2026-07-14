/*
 * Native GEM VDI raster core for ELKS.
 *
 * Copyright 1999 Caldera Thin Clients, Inc.
 * Copyright 1987 Digital Research, Inc.
 * Copyright 2026 elks-gem contributors
 *
 * Its public calls are the compact in-process form of GEM's VDI operations;
 * the PC
 * driver underneath is derived from the original GPL FreeGEM screen-driver
 * source.  Drawing state mirrors the original workstation state: one
 * foreground, one background, one writing mode, and one inclusive clip.
 *
 * Every scalar, counter, coordinate, color and intermediate is 8 or 16 bits.
 * Polygon edges use incremental Bresenham stepping rather than multiply or
 * divide.  Arc scaling uses an explicit two-word product and returns the
 * rounded high portion; no C long value or compiler wide-arithmetic helper is
 * used.  Coordinates are clipped before reaching a hardware driver, so all
 * video offsets remain within their documented 16-bit segment.
 */

#include "vdi.h"
#include "drivers/gem_pcvideo.h"

#define GEM_POLYGON_MAX_EDGES	32
#define GEM_ARC_STEP_TENTHS	50

typedef struct gem_polygon_edge {
	GEM_VDI_COORD y_min;
	GEM_VDI_COORD y_max;
	GEM_VDI_COORD x;
	GEM_VDI_UWORD dx;
	GEM_VDI_UWORD dy;
	GEM_VDI_UWORD error;
	GEM_VDI_WORD x_step;
} GEM_POLYGON_EDGE;

static GEM_VDI_SCREEN gem_screen;
static GEM_VDI_WORD gem_mode = GEM_VDI_REPLACE;
static GEM_VDI_COLOR gem_foreground;
static GEM_VDI_COLOR gem_background;
static GEM_VDI_WORD gem_use_background = 1;
static GEM_VDI_COORD gem_clip_x1;
static GEM_VDI_COORD gem_clip_y1;
static GEM_VDI_COORD gem_clip_x2;
static GEM_VDI_COORD gem_clip_y2;

static GEM_POLYGON_EDGE gem_polygon_edges[GEM_POLYGON_MAX_EDGES];
static GEM_VDI_COORD gem_polygon_x[GEM_POLYGON_MAX_EDGES];

static const GEM_VDI_CURSOR *gem_cursor;
static GEM_VDI_COORD gem_cursor_x;
static GEM_VDI_COORD gem_cursor_y;
static GEM_VDI_WORD gem_cursor_hidden = 1;
static GEM_VDI_WORD gem_cursor_drawn;

static GEM_VDI_WORD
gem_inside_clip(GEM_VDI_COORD x, GEM_VDI_COORD y)
{
	return x >= gem_clip_x1 && x <= gem_clip_x2
		&& y >= gem_clip_y1 && y <= gem_clip_y2;
}

static GEM_VDI_COLOR
gem_driver_color(GEM_VDI_SCREEN *screen, GEM_VDI_COLOR color)
{
	if (screen->colors == 2)
		return color ? 1 : 0;
	return color & 15;
}

static GEM_VDI_WORD
gem_rect_intersects_cursor(GEM_VDI_COORD x1, GEM_VDI_COORD y1,
	GEM_VDI_COORD x2, GEM_VDI_COORD y2)
{
	GEM_VDI_COORD cursor_x2;
	GEM_VDI_COORD cursor_y2;

	if (!gem_cursor_drawn || !gem_cursor)
		return 0;
	cursor_x2 = gem_cursor_x + gem_cursor->width - 1;
	cursor_y2 = gem_cursor_y + gem_cursor->height - 1;
	return x1 <= cursor_x2 && x2 >= gem_cursor_x
		&& y1 <= cursor_y2 && y2 >= gem_cursor_y;
}

static void
gem_cursor_remove(void)
{
	if (!gem_cursor_drawn || !gem_cursor)
		return;
	gem_screen.driver->restore_cursor();
	gem_cursor_drawn = 0;
}

static void
gem_cursor_draw(void)
{
	if (gem_cursor_hidden || gem_cursor_drawn || !gem_cursor)
		return;
	gem_cursor_drawn = gem_screen.driver->draw_cursor(&gem_screen,
		gem_cursor_x, gem_cursor_y, gem_cursor);
}

static GEM_VDI_WORD
gem_begin_draw(GEM_VDI_COORD x1, GEM_VDI_COORD y1,
	GEM_VDI_COORD x2, GEM_VDI_COORD y2)
{
	if (gem_rect_intersects_cursor(x1, y1, x2, y2)) {
		gem_cursor_remove();
		return 1;
	}
	return 0;
}

static void
gem_end_draw(GEM_VDI_WORD restore_cursor)
{
	if (restore_cursor)
		gem_cursor_draw();
}

static void
gem_raw_point(GEM_VDI_SCREEN *screen, GEM_VDI_COORD x, GEM_VDI_COORD y,
	GEM_VDI_COLOR color, GEM_VDI_WORD mode)
{
	if (gem_inside_clip(x, y))
		screen->driver->write_pixel(x, y, gem_driver_color(screen, color),
			mode);
}

GEM_VDI_SCREEN *
gem_vdi_open(void)
{
	if (!gem_pc_video_driver.open(&gem_screen))
		return 0;
	gem_clip_x1 = 0;
	gem_clip_y1 = 0;
	gem_clip_x2 = gem_screen.xres - 1;
	gem_clip_y2 = gem_screen.yres - 1;
	gem_mode = GEM_VDI_REPLACE;
	gem_foreground = 0;
	gem_background = 15;
	gem_use_background = 1;
	gem_cursor = 0;
	gem_cursor_hidden = 1;
	gem_cursor_drawn = 0;
	gem_vdi_open_input(&gem_screen);
	return &gem_screen;
}

void
gem_vdi_close(GEM_VDI_SCREEN *screen)
{
	gem_cursor_remove();
	gem_vdi_close_input();
	if (screen && screen->driver)
		screen->driver->close(screen);
}

void
gem_vdi_flush(GEM_VDI_SCREEN *screen)
{
	/* Direct PC video memory is visible immediately. */
	(void) screen;
}

GEM_VDI_WORD
gem_vdi_set_mode(GEM_VDI_WORD mode)
{
	GEM_VDI_WORD old_mode;

	old_mode = gem_mode;
	if (mode < GEM_VDI_REPLACE || mode > GEM_VDI_CLEAR)
		mode = GEM_VDI_REPLACE;
	gem_mode = mode;
	return old_mode;
}

GEM_VDI_COLOR
gem_vdi_set_foreground(GEM_VDI_SCREEN *screen, GEM_VDI_COLOR color)
{
	GEM_VDI_COLOR old_color;

	(void) screen;
	old_color = gem_foreground;
	gem_foreground = color & 15;
	return old_color;
}

GEM_VDI_COLOR
gem_vdi_set_background(GEM_VDI_SCREEN *screen, GEM_VDI_COLOR color)
{
	GEM_VDI_COLOR old_color;

	(void) screen;
	old_color = gem_background;
	gem_background = color & 15;
	return old_color;
}

GEM_VDI_WORD
gem_vdi_set_use_background(GEM_VDI_WORD enabled)
{
	GEM_VDI_WORD old_value;

	old_value = gem_use_background;
	gem_use_background = enabled ? 1 : 0;
	return old_value;
}

void
gem_vdi_set_clip(GEM_VDI_SCREEN *screen, GEM_VDI_WORD count,
	const GEM_VDI_RECT *rect)
{
	if (!count || !rect) {
		gem_clip_x1 = 0;
		gem_clip_y1 = 0;
		gem_clip_x2 = screen->xres - 1;
		gem_clip_y2 = screen->yres - 1;
		return;
	}
	gem_clip_x1 = rect->x;
	gem_clip_y1 = rect->y;
	gem_clip_x2 = rect->x + rect->width - 1;
	gem_clip_y2 = rect->y + rect->height - 1;
	if (gem_clip_x1 < 0)
		gem_clip_x1 = 0;
	if (gem_clip_y1 < 0)
		gem_clip_y1 = 0;
	if (gem_clip_x2 >= screen->xres)
		gem_clip_x2 = screen->xres - 1;
	if (gem_clip_y2 >= screen->yres)
		gem_clip_y2 = screen->yres - 1;
}

void
gem_vdi_point(GEM_VDI_SCREEN *screen, GEM_VDI_COORD x, GEM_VDI_COORD y)
{
	GEM_VDI_WORD restore_cursor;

	if (!gem_inside_clip(x, y))
		return;
	restore_cursor = gem_begin_draw(x, y, x, y);
	screen->driver->write_pixel(x, y, gem_driver_color(screen,
		gem_foreground), gem_mode);
	gem_end_draw(restore_cursor);
}

void
gem_vdi_line(GEM_VDI_SCREEN *screen, GEM_VDI_COORD x1,
	GEM_VDI_COORD y1, GEM_VDI_COORD x2, GEM_VDI_COORD y2,
	GEM_VDI_WORD draw_last)
{
	GEM_VDI_COORD bound_x1;
	GEM_VDI_COORD bound_y1;
	GEM_VDI_COORD bound_x2;
	GEM_VDI_COORD bound_y2;
	GEM_VDI_WORD restore_cursor;
	GEM_VDI_WORD sx;
	GEM_VDI_WORD sy;
	GEM_VDI_WORD error;
	GEM_VDI_WORD twice;
	GEM_VDI_UWORD dx;
	GEM_VDI_UWORD dy;

	bound_x1 = x1 < x2 ? x1 : x2;
	bound_y1 = y1 < y2 ? y1 : y2;
	bound_x2 = x1 > x2 ? x1 : x2;
	bound_y2 = y1 > y2 ? y1 : y2;
	restore_cursor = gem_begin_draw(bound_x1, bound_y1, bound_x2, bound_y2);

	if (y1 == y2 && y1 >= gem_clip_y1 && y1 <= gem_clip_y2) {
		if (x1 > x2) {
			GEM_VDI_COORD swap = x1;
			x1 = x2;
			x2 = swap;
		}
		if (!draw_last)
			x2--;
		if (x1 < gem_clip_x1)
			x1 = gem_clip_x1;
		if (x2 > gem_clip_x2)
			x2 = gem_clip_x2;
		if (x1 <= x2)
			screen->driver->horizontal_line(x1, x2, y1,
				gem_driver_color(screen, gem_foreground), gem_mode);
		gem_end_draw(restore_cursor);
		return;
	}
	if (x1 == x2 && x1 >= gem_clip_x1 && x1 <= gem_clip_x2) {
		if (y1 > y2) {
			GEM_VDI_COORD swap = y1;
			y1 = y2;
			y2 = swap;
		}
		if (!draw_last)
			y2--;
		if (y1 < gem_clip_y1)
			y1 = gem_clip_y1;
		if (y2 > gem_clip_y2)
			y2 = gem_clip_y2;
		if (y1 <= y2)
			screen->driver->vertical_line(x1, y1, y2,
				gem_driver_color(screen, gem_foreground), gem_mode);
		gem_end_draw(restore_cursor);
		return;
	}

	dx = x1 < x2 ? (GEM_VDI_UWORD) (x2 - x1)
		: (GEM_VDI_UWORD) (x1 - x2);
	dy = y1 < y2 ? (GEM_VDI_UWORD) (y2 - y1)
		: (GEM_VDI_UWORD) (y1 - y2);
	sx = x1 < x2 ? 1 : -1;
	sy = y1 < y2 ? 1 : -1;
	error = (GEM_VDI_WORD) dx - (GEM_VDI_WORD) dy;
	for (;;) {
		if (draw_last || x1 != x2 || y1 != y2)
			gem_raw_point(screen, x1, y1, gem_foreground, gem_mode);
		if (x1 == x2 && y1 == y2)
			break;
		twice = error << 1;
		if (twice > -(GEM_VDI_WORD) dy) {
			error -= (GEM_VDI_WORD) dy;
			x1 += sx;
		}
		if (twice < (GEM_VDI_WORD) dx) {
			error += (GEM_VDI_WORD) dx;
			y1 += sy;
		}
	}
	gem_end_draw(restore_cursor);
}

void
gem_vdi_pattern_line(GEM_VDI_SCREEN *screen, GEM_VDI_COORD x1,
	GEM_VDI_COORD y1, GEM_VDI_COORD x2, GEM_VDI_COORD y2,
	GEM_VDI_UWORD pattern, GEM_VDI_WORD draw_last)
{
	GEM_VDI_COORD bound_x1;
	GEM_VDI_COORD bound_y1;
	GEM_VDI_COORD bound_x2;
	GEM_VDI_COORD bound_y2;
	GEM_VDI_WORD restore_cursor;
	GEM_VDI_WORD sx;
	GEM_VDI_WORD sy;
	GEM_VDI_WORD error;
	GEM_VDI_WORD twice;
	GEM_VDI_UWORD dx;
	GEM_VDI_UWORD dy;
	GEM_VDI_UWORD mask;

	/*
	 * Keep the solid case on the adapter's scan-line fast path.  The Desktop
	 * restores 0xffff after drawing an XOR outline, so this is also the common
	 * path for ordinary window furniture and object borders.
	 */
	if (pattern == 0xffffU) {
		gem_vdi_line(screen, x1, y1, x2, y2, draw_last);
		return;
	}
	if (!pattern)
		return;

	bound_x1 = x1 < x2 ? x1 : x2;
	bound_y1 = y1 < y2 ? y1 : y2;
	bound_x2 = x1 > x2 ? x1 : x2;
	bound_y2 = y1 > y2 ? y1 : y2;
	restore_cursor = gem_begin_draw(bound_x1, bound_y1, bound_x2, bound_y2);

	dx = x1 < x2 ? (GEM_VDI_UWORD) (x2 - x1)
		: (GEM_VDI_UWORD) (x1 - x2);
	dy = y1 < y2 ? (GEM_VDI_UWORD) (y2 - y1)
		: (GEM_VDI_UWORD) (y1 - y2);
	sx = x1 < x2 ? 1 : -1;
	sy = y1 < y2 ? 1 : -1;
	error = (GEM_VDI_WORD) dx - (GEM_VDI_WORD) dy;
	mask = 0x8000U;
	for (;;) {
		if ((draw_last || x1 != x2 || y1 != y2) && (pattern & mask))
			gem_raw_point(screen, x1, y1, gem_foreground, gem_mode);
		if (x1 == x2 && y1 == y2)
			break;

		/*
		 * A single-bit 8086 shift advances the sixteen-pixel style phase.
		 * The explicit reload avoids a variable shift or a remainder helper.
		 */
		mask >>= 1;
		if (!mask)
			mask = 0x8000U;
		twice = error << 1;
		if (twice > -(GEM_VDI_WORD) dy) {
			error -= (GEM_VDI_WORD) dy;
			x1 += sx;
		}
		if (twice < (GEM_VDI_WORD) dx) {
			error += (GEM_VDI_WORD) dx;
			y1 += sy;
		}
	}
	gem_end_draw(restore_cursor);
}

void
gem_vdi_fill_rect(GEM_VDI_SCREEN *screen, GEM_VDI_COORD x,
	GEM_VDI_COORD y, GEM_VDI_COORD width, GEM_VDI_COORD height)
{
	GEM_VDI_COORD x2;
	GEM_VDI_COORD y2;
	GEM_VDI_WORD restore_cursor;

	if (width <= 0 || height <= 0)
		return;
	x2 = x + width - 1;
	y2 = y + height - 1;
	if (x < gem_clip_x1)
		x = gem_clip_x1;
	if (y < gem_clip_y1)
		y = gem_clip_y1;
	if (x2 > gem_clip_x2)
		x2 = gem_clip_x2;
	if (y2 > gem_clip_y2)
		y2 = gem_clip_y2;
	if (x > x2 || y > y2)
		return;
	restore_cursor = gem_begin_draw(x, y, x2, y2);
	screen->driver->fill_rect(x, y, x2, y2,
		gem_driver_color(screen, gem_foreground), gem_mode);
	gem_end_draw(restore_cursor);
}

void
gem_vdi_rect(GEM_VDI_SCREEN *screen, GEM_VDI_COORD x,
	GEM_VDI_COORD y, GEM_VDI_COORD width, GEM_VDI_COORD height)
{
	if (width <= 0 || height <= 0)
		return;
	gem_vdi_line(screen, x, y, x + width - 1, y, 1);
	if (height > 1)
		gem_vdi_line(screen, x, y + height - 1,
			x + width - 1, y + height - 1, 1);
	if (height > 2) {
		gem_vdi_line(screen, x, y + 1, x, y + height - 2, 1);
		if (width > 1)
			gem_vdi_line(screen, x + width - 1, y + 1,
				x + width - 1, y + height - 2, 1);
	}
}

void
gem_vdi_fill_pattern(GEM_VDI_SCREEN *screen, GEM_VDI_COORD x,
	GEM_VDI_COORD y, GEM_VDI_COORD width, GEM_VDI_COORD height,
	const GEM_VDI_UBYTE *pattern)
{
	GEM_VDI_COORD x2;
	GEM_VDI_COORD y2;
	GEM_VDI_WORD restore_cursor;

	if (!pattern || width <= 0 || height <= 0)
		return;
	x2 = x + width - 1;
	y2 = y + height - 1;
	if (x < gem_clip_x1)
		x = gem_clip_x1;
	if (y < gem_clip_y1)
		y = gem_clip_y1;
	if (x2 > gem_clip_x2)
		x2 = gem_clip_x2;
	if (y2 > gem_clip_y2)
		y2 = gem_clip_y2;
	if (x > x2 || y > y2)
		return;

	/*
	 * Cursor removal is done once for the complete rectangle.  The driver
	 * then works in video bytes, so no pixel callback, temporary bitmap or
	 * resource conversion is required.
	 */
	restore_cursor = gem_begin_draw(x, y, x2, y2);
	screen->driver->fill_pattern(x, y, x2, y2,
		gem_driver_color(screen, gem_foreground),
		gem_driver_color(screen, gem_background), pattern);
	gem_end_draw(restore_cursor);
}

void
gem_vdi_bitmap(GEM_VDI_SCREEN *screen, GEM_VDI_COORD x,
	GEM_VDI_COORD y, GEM_VDI_COORD width, GEM_VDI_COORD height,
	const GEM_VDI_BITS *bits)
{
	GEM_VDI_COORD row;
	GEM_VDI_COORD column;
	GEM_VDI_UWORD words_per_row;
	GEM_VDI_BITS word;
	GEM_VDI_BITS mask;
	GEM_VDI_COLOR color;
	GEM_VDI_WORD restore_cursor;
	GEM_VDI_COORD x1;
	GEM_VDI_COORD y1;
	GEM_VDI_COORD x2;
	GEM_VDI_COORD y2;
	GEM_VDI_UWORD source_x;
	volatile GEM_VDI_UWORD skip_rows;
	const GEM_VDI_BITS *row_bits;

	if (!bits || width <= 0 || height <= 0)
		return;
	words_per_row = (GEM_VDI_UWORD) (width + 15) >> 4;

	/*
	 * Replace-mode GEM forms have a native PC byte/plane path.  Clip here so
	 * the driver receives only valid screen coordinates, but retain source_x
	 * and the original row stride so it consumes the RSC/ICN words in place.
	 * Skipped source rows are reached with pointer increments, not a 16-bit
	 * multiplication.  Unsupported writing modes retain the exact historical
	 * pixel fallback below.
	 */
	x1 = x;
	y1 = y;
	x2 = x + width - 1;
	y2 = y + height - 1;
	if (gem_mode == GEM_VDI_REPLACE && screen->driver->bitmap_replace) {
		if (x1 < gem_clip_x1)
			x1 = gem_clip_x1;
		if (y1 < gem_clip_y1)
			y1 = gem_clip_y1;
		if (x2 > gem_clip_x2)
			x2 = gem_clip_x2;
		if (y2 > gem_clip_y2)
			y2 = gem_clip_y2;
		if (x1 > x2 || y1 > y2)
			return;

		source_x = (GEM_VDI_UWORD) (x1 - x);
		skip_rows = (GEM_VDI_UWORD) (y1 - y);
		row_bits = bits;
		while (skip_rows) {
			row_bits += words_per_row;
			skip_rows--;
		}
		restore_cursor = gem_begin_draw(x1, y1, x2, y2);
		screen->driver->bitmap_replace(x1, y1, x2, y2, row_bits,
			source_x, words_per_row,
			gem_driver_color(screen, gem_foreground),
			gem_driver_color(screen, gem_background),
			gem_use_background);
		gem_end_draw(restore_cursor);
		return;
	}

	restore_cursor = gem_begin_draw(x, y, x + width - 1, y + height - 1);
	row_bits = bits;
	for (row = 0; row < height; row++) {
		for (column = 0; column < width; column++) {
			word = row_bits[(GEM_VDI_UWORD) column >> 4];
			mask = (GEM_VDI_BITS) 0x8000 >> (column & 15);
			if (word & mask)
				color = gem_foreground;
			else if (gem_use_background)
				color = gem_background;
			else
				continue;
			gem_raw_point(screen, x + column, y + row, color, gem_mode);
		}
		row_bits += words_per_row;
	}
	gem_end_draw(restore_cursor);
}

void
gem_vdi_glyph(GEM_VDI_SCREEN *screen, GEM_VDI_COORD x,
	GEM_VDI_COORD y, GEM_VDI_COORD width, GEM_VDI_COORD height,
	const GEM_VDI_UBYTE *rows, GEM_VDI_UBYTE source_left_bit)
{
	GEM_VDI_COORD x1;
	GEM_VDI_COORD y1;
	GEM_VDI_COORD x2;
	GEM_VDI_COORD y2;
	GEM_VDI_COORD screen_x;
	GEM_VDI_COORD screen_y;
	GEM_VDI_UWORD source_x;
	volatile GEM_VDI_UWORD skip;
	GEM_VDI_UBYTE source_bit;
	const GEM_VDI_UBYTE *row_bits;
	GEM_VDI_WORD restore_cursor;

	/*
	 * One source row is exactly one byte.  A wider request would need a row
	 * stride and would no longer be the compact native-font path described
	 * by this interface.  Reject it before any coordinate or pointer work.
	 */
	if (!screen || !rows || !source_left_bit || width <= 0 || width > 8
	    || height <= 0)
		return;

	x1 = x;
	y1 = y;
	x2 = x + width - 1;
	y2 = y + height - 1;
	if (x1 < gem_clip_x1)
		x1 = gem_clip_x1;
	if (y1 < gem_clip_y1)
		y1 = gem_clip_y1;
	if (x2 > gem_clip_x2)
		x2 = gem_clip_x2;
	if (y2 > gem_clip_y2)
		y2 = gem_clip_y2;
	if (x1 > x2 || y1 > y2)
		return;

	/*
	 * Reach clipped rows and columns with bounded pointer increments and
	 * one-bit shifts.  No row multiplication, variable-count shift, or
	 * converted glyph buffer is needed on the 8088.
	 */
	row_bits = rows;
	skip = (GEM_VDI_UWORD) (y1 - y);
	while (skip) {
		row_bits++;
		skip--;
	}
	source_x = (GEM_VDI_UWORD) (x1 - x);
	restore_cursor = gem_begin_draw(x1, y1, x2, y2);
	if (gem_mode == GEM_VDI_REPLACE && screen->driver->glyph_replace) {
		screen->driver->glyph_replace(x1, y1, x2, y2, row_bits,
			source_x, source_left_bit,
			gem_driver_color(screen, gem_foreground));
		gem_end_draw(restore_cursor);
		return;
	}

	/* Preserve VDI writing-mode behavior for any non-AES diagnostic caller. */
	for (screen_y = y1; screen_y <= y2; screen_y++) {
		source_bit = source_left_bit;
		skip = source_x;
		while (skip) {
			source_bit >>= 1;
			skip--;
		}
		for (screen_x = x1; screen_x <= x2; screen_x++) {
			if (*row_bits & source_bit)
				screen->driver->write_pixel(screen_x, screen_y,
					gem_driver_color(screen, gem_foreground),
					gem_mode);
			source_bit >>= 1;
		}
		row_bits++;
	}
	gem_end_draw(restore_cursor);
}

GEM_VDI_WORD __far __attribute__((far_section, noinline,
	section(".fartext.gemvdi_text_run")))
gem_vdi_text_run(GEM_VDI_SCREEN *screen, GEM_VDI_COORD x,
	GEM_VDI_COORD y, const GEM_VDI_UBYTE *characters,
	GEM_VDI_UWORD count, GEM_VDI_UWORD stride,
	GEM_VDI_UWORD font_segment, GEM_VDI_UWORD font_offset)
{
	GEM_VDI_COORD x2;
	GEM_VDI_COORD y2;
	GEM_VDI_UWORD remaining;
	GEM_VDI_WORD restore_cursor;
	GEM_VDI_WORD result;

	/*
	 * The native PC run consumes one byte per character cell and one sixteen-
	 * byte glyph from the unchanged BIOS table.  Refuse every shape which
	 * would need source clipping or format conversion; gem_vdi_glyph() remains
	 * the exact fallback for those cases.  COUNT is bounded by the original
	 * eighty-character VDI intin array, and STRIDE names its byte or word form.
	 */
	if (!screen || !screen->driver || !screen->driver->text_replace
	    || !characters || !font_segment || !count || count > 80U
	    || (stride != 1U && stride != 2U) || x < 0 || y < 0
	    || gem_mode != GEM_VDI_REPLACE
	    || ((GEM_VDI_UWORD) x & 7U)
	    || x > screen->xres - 8 || y > screen->yres - 16)
		return 0;

	/*
	 * Form x + count*8 - 1 with bounded repeated addition.  At most eighty
	 * iterations are performed once per VDI string; no MUL, variable shift,
	 * overflow, rounding, or wider temporary can be introduced by the compiler.
	 */
	x2 = x;
	remaining = count;
	while (remaining--) {
		if (x2 > screen->xres - 8)
			return 0;
		x2 += 8;
	}
	x2--;
	y2 = y + 15;
	if (x < gem_clip_x1 || y < gem_clip_y1
	    || x2 > gem_clip_x2 || y2 > gem_clip_y2)
		return 0;

	/* One cursor save/restore and one logical-to-driver color map per string. */
	restore_cursor = gem_begin_draw(x, y, x2, y2);
	result = screen->driver->text_replace(x, y, characters, count, stride,
		font_segment, font_offset,
		gem_driver_color(screen, gem_foreground));
	gem_end_draw(restore_cursor);
	return result;
}

static GEM_VDI_UWORD
gem_abs_word(GEM_VDI_WORD value)
{
	/* Convert through the unsigned domain so -32768 becomes 32768 exactly. */
	return value < 0
		? (GEM_VDI_UWORD) (0u - (GEM_VDI_UWORD) value)
		: (GEM_VDI_UWORD) value;
}

void
gem_vdi_fill_polygon(GEM_VDI_SCREEN *screen, GEM_VDI_WORD count,
	const GEM_VDI_WORD *xy)
{
	GEM_VDI_WORD edge_count;
	GEM_VDI_WORD point;
	volatile GEM_VDI_WORD edge;
	GEM_VDI_WORD intersections;
	GEM_VDI_WORD i;
	GEM_VDI_COORD y;
	GEM_VDI_COORD min_y;
	GEM_VDI_COORD max_y;
	GEM_VDI_COORD x1;
	GEM_VDI_COORD y1;
	GEM_VDI_COORD x2;
	GEM_VDI_COORD y2;
	GEM_VDI_COORD first_x;
	GEM_VDI_COORD first_y;
	GEM_VDI_COORD next_x;
	GEM_VDI_COORD next_y;
	GEM_VDI_COORD swap;
	GEM_VDI_WORD restore_cursor;
	const GEM_VDI_WORD *coordinates;
	GEM_POLYGON_EDGE *edge_write;
	GEM_POLYGON_EDGE * volatile edge_read;
	GEM_VDI_COORD *intersection_write;

	if (!xy || count < 3)
		return;
	if (count > GEM_POLYGON_MAX_EDGES)
		count = GEM_POLYGON_MAX_EDGES;
	edge_count = 0;
	min_y = 32767;
	max_y = -32767;
	coordinates = xy;
	edge_write = gem_polygon_edges;
	first_x = *coordinates++;
	first_y = *coordinates++;
	x1 = first_x;
	y1 = first_y;
	for (point = 1; point <= count; point++) {
		if (point == count) {
			x2 = first_x;
			y2 = first_y;
		} else {
			x2 = *coordinates++;
			y2 = *coordinates++;
		}
		next_x = x2;
		next_y = y2;
		if (y1 == y2)
			goto next_edge;
		if (y2 < y1) {
			swap = x1;
			x1 = x2;
			x2 = swap;
			swap = y1;
			y1 = y2;
			y2 = swap;
		}
		/*
		 * GEM_POLYGON_EDGE is 14 bytes.  Advance a near pointer instead of
		 * indexing by edge_count, which otherwise makes ia16-gcc emit MUL for
		 * the 14-byte structure scale on every polygon edge.
		 */
		edge_write->y_min = y1;
		edge_write->y_max = y2 - 1;
		edge_write->x = x1;
		edge_write->dx = gem_abs_word(x2 - x1);
		edge_write->dy = (GEM_VDI_UWORD) (y2 - y1);
		edge_write->error = 0;
		edge_write->x_step = x2 >= x1 ? 1 : -1;
		if (y1 < min_y)
			min_y = y1;
		if (y2 - 1 > max_y)
			max_y = y2 - 1;
		edge_count++;
		edge_write++;

	next_edge:
		x1 = next_x;
		y1 = next_y;
	}
	if (!edge_count)
		return;
	restore_cursor = gem_begin_draw(gem_clip_x1, min_y,
		gem_clip_x2, max_y);
	for (y = min_y; y <= max_y; y++) {
		intersections = 0;
		edge_read = gem_polygon_edges;
		intersection_write = gem_polygon_x;
		for (edge = 0; edge < edge_count; edge++) {
			if (y >= edge_read->y_min && y <= edge_read->y_max) {
				*intersection_write++ = edge_read->x;
				intersections++;
			}
			edge_read++;
		}
		for (i = 1; i < intersections; i++) {
			x1 = gem_polygon_x[i];
			point = i;
			while (point > 0 && gem_polygon_x[point - 1] > x1) {
				gem_polygon_x[point] = gem_polygon_x[point - 1];
				point--;
			}
			gem_polygon_x[point] = x1;
		}
		if (y >= gem_clip_y1 && y <= gem_clip_y2) {
			for (i = 0; i + 1 < intersections; i += 2) {
				x1 = gem_polygon_x[i];
				x2 = gem_polygon_x[i + 1];
				if (x1 < gem_clip_x1)
					x1 = gem_clip_x1;
				if (x2 > gem_clip_x2)
					x2 = gem_clip_x2;
				if (x1 <= x2)
					screen->driver->horizontal_line(x1, x2, y,
						gem_driver_color(screen, gem_foreground),
						gem_mode);
			}
		}
		edge_read = gem_polygon_edges;
		for (edge = 0; edge < edge_count; edge++) {
			if (y >= edge_read->y_min && y <= edge_read->y_max) {
				edge_read->error += edge_read->dx;
				while (edge_read->error >= edge_read->dy) {
					edge_read->x += edge_read->x_step;
					edge_read->error -= edge_read->dy;
				}
			}
			edge_read++;
		}
	}
	gem_end_draw(restore_cursor);
}

void
gem_vdi_area(GEM_VDI_SCREEN *screen, GEM_VDI_COORD x,
	GEM_VDI_COORD y, GEM_VDI_COORD width, GEM_VDI_COORD height,
	const GEM_VDI_WORD *colors)
{
	GEM_VDI_COORD row;
	GEM_VDI_COORD column;
	GEM_VDI_WORD restore_cursor;
	const GEM_VDI_WORD *source;

	if (!colors || width <= 0 || height <= 0)
		return;
	restore_cursor = gem_begin_draw(x, y, x + width - 1, y + height - 1);
	source = colors;
	for (row = 0; row < height; row++)
		for (column = 0; column < width; column++)
			gem_raw_point(screen, x + column, y + row,
				*source++,
				GEM_VDI_REPLACE);
	gem_end_draw(restore_cursor);
}

/*
 * Multiply a 16-bit value by a Q7 factor without a C wide temporary.
 * The product is accumulated as explicit high and low 16-bit halves.  Adding
 * 64 before the final seven-bit shift implements round-to-nearest.  The
 * result saturates at 32767 if the explicit high half cannot fit the signed
 * coordinate domain; normal screen radii never approach that boundary.
 */
static GEM_VDI_COORD
gem_scale_q7(GEM_VDI_COORD value, GEM_VDI_UBYTE factor)
{
	GEM_VDI_UWORD product_low;
	GEM_VDI_UWORD product_high;
	GEM_VDI_UWORD add_low;
	GEM_VDI_UWORD add_high;
	GEM_VDI_UWORD old_low;
	GEM_VDI_UBYTE bits;

	if (value < 0)
		value = -value;
	product_low = 64;
	product_high = 0;
	add_low = (GEM_VDI_UWORD) value;
	add_high = 0;
	bits = factor;
	while (bits) {
		if (bits & 1) {
			old_low = product_low;
			product_low += add_low;
			product_high += add_high;
			if (product_low < old_low)
				product_high++;
		}
		add_high = (GEM_VDI_UWORD)
			((add_high << 1) | (add_low >> 15));
		add_low <<= 1;
		bits >>= 1;
	}
	if (product_high > 63)
		return 32767;
	return (GEM_VDI_COORD) ((product_low >> 7) | (product_high << 9));
}

static const GEM_VDI_UBYTE gem_sine_q7[19] = {
	0, 11, 22, 33, 43, 54, 64, 74, 82, 90,
	97, 104, 110, 115, 119, 123, 125, 127, 127
};

static GEM_VDI_WORD
gem_normalize_angle(GEM_VDI_WORD angle)
{
	while (angle < 0)
		angle += 3600;
	while (angle >= 3600)
		angle -= 3600;
	return angle;
}

static GEM_VDI_UBYTE
gem_quadrant_sine(GEM_VDI_WORD angle)
{
	GEM_VDI_WORD quadrant;
	GEM_VDI_WORD within;
	GEM_VDI_WORD index;

	angle = gem_normalize_angle(angle);
	quadrant = 0;
	while (angle >= 900) {
		angle -= 900;
		quadrant++;
	}
	within = angle;
	if (quadrant & 1)
		within = 900 - within;
	index = 0;
	while (within >= GEM_ARC_STEP_TENTHS && index < 18) {
		within -= GEM_ARC_STEP_TENTHS;
		index++;
	}
	return gem_sine_q7[index];
}

static GEM_VDI_COORD
gem_arc_x(GEM_VDI_COORD radius, GEM_VDI_WORD angle)
{
	GEM_VDI_COORD value;
	GEM_VDI_WORD normalized;

	normalized = gem_normalize_angle(angle);
	value = gem_scale_q7(radius, gem_quadrant_sine(normalized + 900));
	if (normalized >= 900 && normalized < 2700)
		value = -value;
	return value;
}

static GEM_VDI_COORD
gem_arc_y(GEM_VDI_COORD radius, GEM_VDI_WORD angle)
{
	GEM_VDI_COORD value;
	GEM_VDI_WORD normalized;

	normalized = gem_normalize_angle(angle);
	value = gem_scale_q7(radius, gem_quadrant_sine(normalized));
	if (normalized >= 1800)
		value = -value;
	/* Screen Y grows downward, opposite to GEM's mathematical Y. */
	return -value;
}

void
gem_vdi_arc(GEM_VDI_SCREEN *screen, GEM_VDI_COORD x,
	GEM_VDI_COORD y, GEM_VDI_COORD rx, GEM_VDI_COORD ry,
	GEM_VDI_WORD start_tenths, GEM_VDI_WORD end_tenths,
	GEM_VDI_WORD type)
{
	GEM_VDI_WORD angle;
	GEM_VDI_WORD span;
	GEM_VDI_COORD first_x;
	GEM_VDI_COORD first_y;
	GEM_VDI_COORD old_x;
	GEM_VDI_COORD old_y;
	GEM_VDI_COORD new_x;
	GEM_VDI_COORD new_y;

	if (rx < 0)
		rx = -rx;
	if (ry < 0)
		ry = -ry;
	start_tenths = gem_normalize_angle(start_tenths);
	end_tenths = gem_normalize_angle(end_tenths);
	span = end_tenths - start_tenths;
	if (span <= 0)
		span += 3600;
	angle = start_tenths;
	first_x = x + gem_arc_x(rx, angle);
	first_y = y + gem_arc_y(ry, angle);
	old_x = first_x;
	old_y = first_y;
	while (span > 0) {
		if (span < GEM_ARC_STEP_TENTHS)
			angle += span;
		else
			angle += GEM_ARC_STEP_TENTHS;
		new_x = x + gem_arc_x(rx, angle);
		new_y = y + gem_arc_y(ry, angle);
		if (type == GEM_VDI_ARC_PIE)
			gem_vdi_line(screen, x, y, new_x, new_y, 1);
		else
			gem_vdi_line(screen, old_x, old_y, new_x, new_y, 1);
		old_x = new_x;
		old_y = new_y;
		if (span < GEM_ARC_STEP_TENTHS)
			span = 0;
		else
			span -= GEM_ARC_STEP_TENTHS;
	}
	if (type == GEM_VDI_ARC_PIE) {
		gem_vdi_line(screen, x, y, first_x, first_y, 1);
		gem_vdi_line(screen, first_x, first_y, old_x, old_y, 1);
	}
}

void
gem_vdi_ellipse(GEM_VDI_SCREEN *screen, GEM_VDI_COORD x,
	GEM_VDI_COORD y, GEM_VDI_COORD rx, GEM_VDI_COORD ry,
	GEM_VDI_WORD fill)
{
	gem_vdi_arc(screen, x, y, rx, ry, 0, 3599,
		fill ? GEM_VDI_ARC_PIE : GEM_VDI_ARC_OUTLINE);
}

static GEM_VDI_COLOR
gem_blit_rule(GEM_VDI_COLOR source, GEM_VDI_COLOR destination,
	GEM_VDI_WORD rule)
{
	GEM_VDI_COLOR mask;

	mask = 15;
	source &= mask;
	destination &= mask;
	switch (rule & 15) {
	case 0: return 0;
	case 1: return source & destination;
	case 2: return source & (GEM_VDI_COLOR) ~destination & mask;
	case 3: return source;
	case 4: return destination & (GEM_VDI_COLOR) ~source & mask;
	case 5: return destination;
	case 6: return source ^ destination;
	case 7: return source | destination;
	case 8: return (GEM_VDI_COLOR) ~(source | destination) & mask;
	case 9: return (GEM_VDI_COLOR) ~(source ^ destination) & mask;
	case 10: return (GEM_VDI_COLOR) ~destination & mask;
	case 11: return source | ((GEM_VDI_COLOR) ~destination & mask);
	case 12: return (GEM_VDI_COLOR) ~source & mask;
	case 13: return ((GEM_VDI_COLOR) ~source & mask) | destination;
	case 14: return (GEM_VDI_COLOR) ~(source & destination) & mask;
	default: return mask;
	}
}

void
gem_vdi_blit(GEM_VDI_SCREEN *screen, GEM_VDI_COORD dst_x,
	GEM_VDI_COORD dst_y, GEM_VDI_COORD width, GEM_VDI_COORD height,
	GEM_VDI_COORD src_x, GEM_VDI_COORD src_y, GEM_VDI_WORD rule)
{
	GEM_VDI_COORD row;
	GEM_VDI_COORD column;
	GEM_VDI_COORD row_start;
	GEM_VDI_COORD row_end;
	GEM_VDI_COORD row_step;
	GEM_VDI_COORD column_start;
	GEM_VDI_COORD column_end;
	GEM_VDI_COORD column_step;
	GEM_VDI_COLOR source;
	GEM_VDI_COLOR destination;
	GEM_VDI_WORD restore_cursor;

	if (width <= 0 || height <= 0)
		return;
	restore_cursor = gem_begin_draw(dst_x, dst_y,
		dst_x + width - 1, dst_y + height - 1);
	/*
	 * S_ONLY is GEM Boolean raster rule three.  The native PC driver copies
	 * equal-alignment rectangles in video bytes (and all VGA planes at once),
	 * retaining overlap order without a temporary bitmap.  Bounds are tested
	 * with subtraction so a 16-bit coordinate sum cannot wrap through zero.
	 */
	if ((rule & 15) == 3
	    && src_x >= 0 && src_y >= 0
	    && dst_x >= gem_clip_x1 && dst_y >= gem_clip_y1
	    && src_x < screen->xres && src_y < screen->yres
	    && dst_x <= gem_clip_x2 && dst_y <= gem_clip_y2
	    && width <= screen->xres - src_x
	    && height <= screen->yres - src_y
	    && width <= gem_clip_x2 - dst_x + 1
	    && height <= gem_clip_y2 - dst_y + 1
	    && gem_pc_screen_blit(dst_x, dst_y, width, height,
				  src_x, src_y)) {
		gem_end_draw(restore_cursor);
		return;
	}
	if (dst_y > src_y) {
		row_start = height - 1;
		row_end = -1;
		row_step = -1;
	} else {
		row_start = 0;
		row_end = height;
		row_step = 1;
	}
	if (dst_y == src_y && dst_x > src_x) {
		column_start = width - 1;
		column_end = -1;
		column_step = -1;
	} else {
		column_start = 0;
		column_end = width;
		column_step = 1;
	}
	for (row = row_start; row != row_end; row += row_step) {
		for (column = column_start; column != column_end;
		     column += column_step) {
			if (!gem_inside_clip(dst_x + column, dst_y + row))
				continue;
			source = screen->driver->read_pixel(src_x + column,
				src_y + row);
			destination = screen->driver->read_pixel(dst_x + column,
				dst_y + row);
			screen->driver->write_pixel(dst_x + column, dst_y + row,
				gem_driver_color(screen,
					gem_blit_rule(source, destination, rule)),
				GEM_VDI_REPLACE);
		}
	}
	gem_end_draw(restore_cursor);
}

GEM_VDI_COLOR
gem_vdi_read_pixel(GEM_VDI_SCREEN *screen, GEM_VDI_COORD x,
	GEM_VDI_COORD y)
{
	GEM_VDI_WORD restore_cursor;
	GEM_VDI_COLOR color;

	if (x < 0 || y < 0 || x >= screen->xres || y >= screen->yres)
		return 0;
	restore_cursor = gem_begin_draw(x, y, x, y);
	color = screen->driver->read_pixel(x, y);
	gem_end_draw(restore_cursor);
	return color;
}

void
gem_vdi_read_area(GEM_VDI_SCREEN *screen, GEM_VDI_COORD x,
	GEM_VDI_COORD y, GEM_VDI_COORD width, GEM_VDI_COORD height,
	GEM_VDI_COLOR *colors)
{
	GEM_VDI_COORD row;
	GEM_VDI_COORD column;
	GEM_VDI_WORD restore_cursor;

	if (!colors || width <= 0 || height <= 0)
		return;
	restore_cursor = gem_begin_draw(x, y, x + width - 1, y + height - 1);
	for (row = 0; row < height; row++)
		for (column = 0; column < width; column++)
			*colors++ = screen->driver->read_pixel(x + column, y + row);
	gem_end_draw(restore_cursor);
}

void
gem_vdi_set_cursor(const GEM_VDI_CURSOR *cursor)
{
	GEM_VDI_WORD redraw;

	redraw = gem_cursor_drawn;
	gem_cursor_remove();
	gem_cursor = cursor;
	if (redraw)
		gem_cursor_draw();
}

void
gem_vdi_move_cursor(GEM_VDI_COORD x, GEM_VDI_COORD y)
{
	GEM_VDI_WORD redraw;

	/*
	 * A serial packet may change only the button bits.  The cursor form and
	 * coordinates are then already correct, so restoring, saving, and drawing
	 * the same 16 by 16 area would waste three complete planar passes.
	 */
	if (x == gem_cursor_x && y == gem_cursor_y)
		return;
	redraw = gem_cursor_drawn;
	gem_cursor_remove();
	gem_cursor_x = x;
	gem_cursor_y = y;
	if (redraw)
		gem_cursor_draw();
}

void
gem_vdi_show_cursor(GEM_VDI_SCREEN *screen)
{
	(void) screen;
	if (gem_cursor_hidden > 0)
		gem_cursor_hidden--;
	gem_cursor_draw();
}

void
gem_vdi_hide_cursor(GEM_VDI_SCREEN *screen)
{
	(void) screen;
	if (!gem_cursor_hidden)
		gem_cursor_remove();
	gem_cursor_hidden++;
}
