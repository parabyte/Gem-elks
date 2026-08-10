/*
 * gem_pcvideo.c - top level of the PC display drivers
 *
 * Copyright 1999 Caldera Thin Clients, Inc.
 * Copyright 1987 Digital Research, Inc.
 *
 * top of the PC display drivers, detects the adapter, owns the ELKS
 * console graphics lock, and hands drawing to the per-adapter files
 */

#include <linuxmt/ntty.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

#include "gem_video_cga.h"
#include "gem_video_common.h"
#include "gem_video_hercules.h"
#include "gem_video_mono.h"
#include "gem_video_planar.h"

static GEM_VDI_UWORD saved_bios_mode;
static GEM_VDI_WORD console_locked;
static GEM_VDI_WORD gem_cursor_save_active;

/* on once the graphics lock is held and the keyboard is in raw scancode
 * mode, gem_input reads it to pick the raw decoder over the ansi one */
GEM_VDI_WORD gem_console_kraw;

static GEM_VDI_WORD
gem_pc_is_planar(void)
{
	return gem_video_adapter == GEM_VIDEO_VGA
		|| gem_video_adapter == GEM_VIDEO_EGA;
}

/* GEM_VIDEO=vga/ega/cga/herc overrides the BIOS probe */
static GEM_VDI_UWORD
gem_requested_adapter(void)
{
	const char *name;

	name = getenv("GEM_VIDEO");
	if (!name || !*name || !strcmp(name, "auto"))
		return gem_bios_video_detect();
	if (!strcmp(name, "vga"))
		return GEM_VIDEO_VGA;
	if (!strcmp(name, "ega"))
		return GEM_VIDEO_EGA;
	if (!strcmp(name, "cga"))
		return GEM_VIDEO_CGA;
	if (!strcmp(name, "herc") || !strcmp(name, "hercules"))
		return GEM_VIDEO_HERCULES;
	return gem_bios_video_detect();
}

static GEM_VDI_WORD
gem_pc_open(GEM_VDI_SCREEN *screen)
{
	/* a mode set throws away anything saved under an old cursor */
	gem_cursor_save_active = 0;
	if (ioctl(0, DCGET_GRAPH) != 0) {
		/* no adapter access without the console graphics lock, failing here lets AES keep the workstation suspended */
		console_locked = 0;
		return 0;
	}
	console_locked = 1;
	/* raw scancodes so the Amstrad mouse buttons (and key releases)
	 * reach us, a cooked tty keymaps them away */
	gem_console_kraw = (ioctl(0, DCSET_KRAW) == 0) ? 1 : 0;

	saved_bios_mode = gem_bios_video_get_mode();
	gem_video_adapter = gem_requested_adapter();

	screen->driver = &gem_pc_video_driver;
	if (gem_pc_is_planar())
		gem_planar_open(screen, gem_video_adapter);
	else if (gem_video_adapter == GEM_VIDEO_CGA)
		gem_cga_open(screen);
	else
		gem_hercules_open(screen);
	return 1;
}

static void
gem_pc_close(GEM_VDI_SCREEN *screen)
{
	(void) screen;
	if (gem_video_adapter == GEM_VIDEO_HERCULES)
		gem_hercules_close();
	else
		gem_bios_video_set_mode(saved_bios_mode);
	if (console_locked) {
		if (gem_console_kraw) {
			ioctl(0, DCREL_KRAW);
			gem_console_kraw = 0;
		}
		ioctl(0, DCREL_GRAPH);
		console_locked = 0;
	}
}

static void
gem_pc_write_pixel(GEM_VDI_COORD x, GEM_VDI_COORD y,
	GEM_VDI_COLOR color, GEM_VDI_WORD mode)
{
	if (gem_pc_is_planar())
		gem_planar_write_pixel(x, y, color, mode);
	else
		gem_mono_write_pixel(x, y, color, mode);
}

static GEM_VDI_COLOR
gem_pc_read_pixel(GEM_VDI_COORD x, GEM_VDI_COORD y)
{
	if (gem_pc_is_planar())
		return gem_planar_read_pixel(x, y);
	return gem_mono_read_pixel(x, y);
}

static void
gem_pc_horizontal_line(GEM_VDI_COORD x1, GEM_VDI_COORD x2,
	GEM_VDI_COORD y, GEM_VDI_COLOR color, GEM_VDI_WORD mode)
{
	if (gem_pc_is_planar())
		gem_planar_horizontal_line(x1, x2, y, color, mode);
	else
		gem_mono_horizontal_line(x1, x2, y, color, mode);
}

static void
gem_pc_vertical_line(GEM_VDI_COORD x, GEM_VDI_COORD y1,
	GEM_VDI_COORD y2, GEM_VDI_COLOR color, GEM_VDI_WORD mode)
{
	if (gem_pc_is_planar())
		gem_planar_vertical_line(x, y1, y2, color, mode);
	else
		gem_mono_vertical_line(x, y1, y2, color, mode);
}

static void
gem_pc_fill_rect(GEM_VDI_COORD x1, GEM_VDI_COORD y1,
	GEM_VDI_COORD x2, GEM_VDI_COORD y2, GEM_VDI_COLOR color,
	GEM_VDI_WORD mode)
{
	if (gem_pc_is_planar())
		gem_planar_fill_rect(x1, y1, x2, y2, color, mode);
	else
		gem_mono_fill_rect(x1, y1, x2, y2, color, mode);
}

static void
gem_pc_bitmap_replace(GEM_VDI_COORD x1, GEM_VDI_COORD y1,
	GEM_VDI_COORD x2, GEM_VDI_COORD y2, const GEM_VDI_BITS *bits,
	GEM_VDI_UWORD source_x, GEM_VDI_UWORD words_per_row,
	GEM_VDI_COLOR foreground, GEM_VDI_COLOR background,
	GEM_VDI_WORD use_background)
{
	if (gem_pc_is_planar())
		gem_planar_bitmap_replace(x1, y1, x2, y2, bits, source_x,
			words_per_row, foreground, background, use_background);
	else
		gem_mono_bitmap_replace(x1, y1, x2, y2, bits, source_x,
			words_per_row, foreground, background, use_background);
}

static void
gem_pc_glyph_replace(GEM_VDI_COORD x1, GEM_VDI_COORD y1,
	GEM_VDI_COORD x2, GEM_VDI_COORD y2, const GEM_VDI_UBYTE *rows,
	GEM_VDI_UWORD source_x, GEM_VDI_UBYTE source_left_bit,
	GEM_VDI_COLOR foreground)
{
	if (gem_pc_is_planar())
		gem_planar_glyph_replace(x1, y1, x2, y2, rows, source_x,
			source_left_bit, foreground);
	else
		gem_mono_glyph_replace(x1, y1, x2, y2, rows, source_x,
			source_left_bit, foreground);
}

static void
gem_pc_fill_pattern(GEM_VDI_COORD x1, GEM_VDI_COORD y1,
	GEM_VDI_COORD x2, GEM_VDI_COORD y2, GEM_VDI_COLOR foreground,
	GEM_VDI_COLOR background, const GEM_VDI_UBYTE *pattern)
{
	if (gem_pc_is_planar())
		gem_planar_fill_pattern(x1, y1, x2, y2, foreground,
			background, pattern);
	else
		gem_mono_fill_pattern(x1, y1, x2, y2, foreground,
			background, pattern);
}

static GEM_VDI_WORD
gem_pc_draw_cursor(GEM_VDI_SCREEN *screen, GEM_VDI_COORD x,
	GEM_VDI_COORD y, const GEM_VDI_CURSOR *cursor)
{
	/* one save area only, so a second draw is refused not overwriting the first */
	if (gem_cursor_save_active || !gem_cursor_prepare(screen, x, y, cursor))
		return 0;

	if (gem_pc_is_planar())
		gem_planar_cursor_draw(cursor);
	else
		gem_mono_cursor_draw(cursor);
	gem_cursor_save_active = 1;
	return 1;
}

static void
gem_pc_restore_cursor(void)
{
	if (!gem_cursor_save_active)
		return;
	if (gem_pc_is_planar())
		gem_planar_cursor_restore();
	else
		gem_mono_cursor_restore();
	gem_cursor_save_active = 0;
}

static void
gem_pc_copy_pixel(GEM_VDI_COORD destination_x,
	GEM_VDI_COORD destination_y, GEM_VDI_COORD source_x,
	GEM_VDI_COORD source_y)
{
	GEM_VDI_COLOR color;

	if (gem_pc_is_planar()) {
		color = gem_planar_read_pixel(source_x, source_y);
		gem_planar_write_pixel(destination_x, destination_y, color,
			GEM_VDI_REPLACE);
	} else {
		color = gem_mono_read_pixel(source_x, source_y);
		gem_mono_write_pixel(destination_x, destination_y, color,
			GEM_VDI_REPLACE);
	}
}

/* screen-to-screen copy, needs matching bit alignment or returns zero for the VDI fallback, ragged edges go pixel by pixel, whole bytes via byte copy, backwards when overlapping */
GEM_VDI_WORD
gem_pc_screen_blit(GEM_VDI_COORD dst_x, GEM_VDI_COORD dst_y,
	GEM_VDI_COORD width, GEM_VDI_COORD height, GEM_VDI_COORD src_x,
	GEM_VDI_COORD src_y)
{
	GEM_VDI_COORD row;
	GEM_VDI_COORD source_y;
	GEM_VDI_COORD destination_y;
	GEM_VDI_UWORD leading;
	GEM_VDI_UWORD remaining;
	GEM_VDI_UWORD full_bytes;
	GEM_VDI_UWORD full_pixels;
	GEM_VDI_UWORD boundary;
	GEM_VDI_UWORD index;
	GEM_VDI_UWORD rows;
	GEM_VDI_WORD reverse_rows;
	GEM_VDI_WORD reverse_columns;

	if (width <= 0 || height <= 0
		|| (((GEM_VDI_UWORD) src_x ^ (GEM_VDI_UWORD) dst_x) & 7U))
		return 0;
	if (src_x == dst_x && src_y == dst_y)
		return 1;

	/* pixels before the first whole destination byte */
	leading = 0;
	while (leading < (GEM_VDI_UWORD) width
		&& (((GEM_VDI_UWORD) dst_x + leading) & 7U))
		leading++;
	remaining = (GEM_VDI_UWORD) width - leading;
	full_bytes = remaining >> 3;
	full_pixels = full_bytes;
	full_pixels <<= 1;
	full_pixels <<= 1;
	full_pixels <<= 1;
	boundary = leading + full_pixels;

	reverse_rows = dst_y > src_y;
	reverse_columns = dst_x > src_x;
	row = reverse_rows ? height : 0;
	rows = (GEM_VDI_UWORD) height;
	while (rows--) {
		if (reverse_rows)
			row--;
		source_y = src_y + row;
		destination_y = dst_y + row;

		if (reverse_columns) {
			index = (GEM_VDI_UWORD) width;
			while (index > boundary) {
				index--;
				gem_pc_copy_pixel(dst_x + index, destination_y,
					src_x + index, source_y);
			}
			if (full_bytes) {
				if (gem_pc_is_planar())
					gem_planar_copy_bytes
						(gem_pc_planar_offset(src_x +
							leading, source_y),
						gem_pc_planar_offset(dst_x +
							leading, destination_y),
						full_bytes, 1);
				else
					gem_mono_copy_bytes(gem_mono_offset
						(src_x + leading, source_y),
						gem_mono_offset(dst_x + leading,
							destination_y),
						full_bytes, 1);
			}
			index = leading;
			while (index) {
				index--;
				gem_pc_copy_pixel(dst_x + index, destination_y,
					src_x + index, source_y);
			}
		} else {
			index = 0;
			while (index < leading) {
				gem_pc_copy_pixel(dst_x + index, destination_y,
					src_x + index, source_y);
				index++;
			}
			if (full_bytes) {
				if (gem_pc_is_planar())
					gem_planar_copy_bytes
						(gem_pc_planar_offset(src_x +
							leading, source_y),
						gem_pc_planar_offset(dst_x +
							leading, destination_y),
						full_bytes, 0);
				else
					gem_mono_copy_bytes(gem_mono_offset
						(src_x + leading, source_y),
						gem_mono_offset(dst_x + leading,
							destination_y),
						full_bytes, 0);
			}
			index = boundary;
			while (index < (GEM_VDI_UWORD) width) {
				gem_pc_copy_pixel(dst_x + index, destination_y,
					src_x + index, source_y);
				index++;
			}
		}
		if (!reverse_rows)
			row++;
	}
	return 1;
}

const GEM_VDI_DRIVER gem_pc_video_driver = {
	"FreeGEM PC display",
	gem_pc_open,
	gem_pc_close,
	gem_pc_write_pixel,
	gem_pc_read_pixel,
	gem_pc_horizontal_line,
	gem_pc_vertical_line,
	gem_pc_fill_rect,
	gem_pc_bitmap_replace,
	gem_pc_glyph_replace,
	gem_planar_text_replace,
	gem_pc_fill_pattern,
	gem_pc_draw_cursor,
	gem_pc_restore_cursor
};
