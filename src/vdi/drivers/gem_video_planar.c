/*
 * gem_video_planar.c - planar EGA/VGA display driver
 *
 * Copyright 1999 Caldera Thin Clients, Inc.
 * Copyright 1987 Digital Research, Inc.
 *
 * one driver for EGA and VGA, four one-bit planes at segment A000,
 * most drawing goes through set/reset hardware so one written byte
 * paints all four planes
 */

#include <arch/io.h>

#include "gem_video_planar.h"

/* read-map register (GR4) values per plane */
static const GEM_VDI_UWORD gem_planar_read_map[4] = {
	0x0004, 0x0104, 0x0204, 0x0304
};

/* map-mask register (SR2) values per plane */
static const GEM_VDI_UWORD gem_planar_write_map[4] = {
	0x0102, 0x0202, 0x0402, 0x0802
};

/* color-index bit per plane */
static const GEM_VDI_UBYTE gem_planar_color_bit[4] = {
	0x01, 0x02, 0x04, 0x08
};

static GEM_VDI_UBYTE
gem_planar_rotate(GEM_VDI_WORD mode)
{
	switch (mode) {
	case GEM_VDI_XOR:
		return 0x18;
	case GEM_VDI_OR:
		return 0x10;
	case GEM_VDI_AND:
		return 0x08;
	default:
		return 0x00;
	}
}

/* set color and write mode, the cursor code leaves these changed */
static void
gem_planar_prepare(GEM_VDI_COLOR color, GEM_VDI_WORD mode)
{
	if (mode == GEM_VDI_CLEAR) {
		color = 0;
		mode = GEM_VDI_REPLACE;
	}
	outw((GEM_VDI_UWORD) ((color & 15) << 8), 0x3ce);
	outw(0x0f01, 0x3ce);
	outw((GEM_VDI_UWORD) (3 | (gem_planar_rotate(mode) << 8)), 0x3ce);
}

/* bit-mask register (GR8) */
static void
gem_planar_mask(GEM_VDI_UBYTE mask)
{
	outw((GEM_VDI_UWORD) (8 | ((GEM_VDI_UWORD) mask << 8)), 0x3ce);
}

void
gem_planar_write_pixel(GEM_VDI_COORD x, GEM_VDI_COORD y,
	GEM_VDI_COLOR color, GEM_VDI_WORD mode)
{
	volatile unsigned char __far *byte;

	gem_planar_prepare(color, mode);
	gem_planar_mask(gem_pixel_mask(x));
	byte = gem_video_pointer(GEM_VIDEO_SEG_PLANAR,
		gem_pc_planar_offset(x, y));
	/* read loads the latches, write paints the color */
	*byte |= 1;
}

GEM_VDI_COLOR
gem_planar_read_pixel(GEM_VDI_COORD x, GEM_VDI_COORD y)
{
	volatile unsigned char __far *byte;
	GEM_VDI_COLOR color;
	GEM_VDI_UWORD plane;
	GEM_VDI_UBYTE mask;

	byte = gem_video_pointer(GEM_VIDEO_SEG_PLANAR,
		gem_pc_planar_offset(x, y));
	mask = gem_pixel_mask(x);
	color = 0;
	for (plane = 0; plane < 4; plane++) {
		outw(gem_planar_read_map[plane], 0x3ce);
		if (*byte & mask)
			color |= gem_planar_color_bit[plane];
	}
	return color;
}

static void gem_planar_solid_span(GEM_VDI_COORD x1, GEM_VDI_COORD x2,
	GEM_VDI_COORD y);

void
gem_planar_horizontal_line(GEM_VDI_COORD x1, GEM_VDI_COORD x2,
	GEM_VDI_COORD y, GEM_VDI_COLOR color, GEM_VDI_WORD mode)
{
	gem_planar_prepare(color, mode);
	gem_planar_solid_span(x1, x2, y);
}

void
gem_planar_vertical_line(GEM_VDI_COORD x, GEM_VDI_COORD y1,
	GEM_VDI_COORD y2, GEM_VDI_COLOR color, GEM_VDI_WORD mode)
{
	GEM_VDI_UWORD offset;
	GEM_VDI_UWORD rows;

	gem_planar_prepare(color, mode);
	gem_planar_mask(gem_pixel_mask(x));
	offset = gem_pc_planar_offset(x, y1);
	rows = (GEM_VDI_UWORD) (y2 - y1) + 1U;

	/* ORB loads the latches and writes the color, 80 bytes per row */
	__asm__ volatile ("pushw %%es\n\t"
		"movw $0xa000,%%ax\n\t"
		"movw %%ax,%%es\n\t"
		"orw %%cx,%%cx\n\t"
		"jz 2f\n"
		"1:\n\t"
		"orb $1,%%es:(%%di)\n\t"
		"addw $80,%%di\n\t"
		"loop 1b\n" "2:\n\t" "popw %%es":"+D" (offset), "+c"(rows)
		::"ax", "cc", "memory");
}

void
gem_planar_fill_rect(GEM_VDI_COORD x1, GEM_VDI_COORD y1,
	GEM_VDI_COORD x2, GEM_VDI_COORD y2, GEM_VDI_COLOR color,
	GEM_VDI_WORD mode)
{
	GEM_VDI_UWORD offset;
	GEM_VDI_UWORD row_bytes;
	GEM_VDI_UWORD rows;

	gem_planar_prepare(color, mode);

	/* byte-aligned rectangles fill whole rows in the assembly helper */
	if (!((GEM_VDI_UWORD) x1 & 7U)
		&& (((GEM_VDI_UWORD) x2 & 7U) == 7U)) {
		row_bytes = gem_pc_byte_column(x2)
			- gem_pc_byte_column(x1) + 1U;
		offset = gem_pc_planar_offset(x1, y1);
		rows = (GEM_VDI_UWORD) (y2 - y1) + 1U;
		gem_planar_mask(0xff);
		gem_pc_planar_fill_rows(offset, row_bytes, rows,
			mode != GEM_VDI_REPLACE && mode != GEM_VDI_CLEAR);
		return;
	}

	while (y1 <= y2) {
		gem_planar_solid_span(x1, x2, y1);
		y1++;
	}
}

/* paint one solid span, color and mode already set */
static void
gem_planar_solid_span(GEM_VDI_COORD x1, GEM_VDI_COORD x2, GEM_VDI_COORD y)
{
	volatile unsigned char __far *byte;
	GEM_VDI_UBYTE first_mask;
	GEM_VDI_UBYTE last_mask;
	GEM_VDI_UWORD first_byte;
	GEM_VDI_UWORD last_byte;

	first_byte = (GEM_VDI_UWORD) x1 >> 3;
	last_byte = (GEM_VDI_UWORD) x2 >> 3;
	first_mask = (GEM_VDI_UBYTE) (0xff >> (x1 & 7));
	last_mask = (GEM_VDI_UBYTE) (0xff << (7 - (x2 & 7)));
	byte = gem_video_pointer(GEM_VIDEO_SEG_PLANAR,
		gem_pc_planar_offset(x1, y));

	if (first_byte == last_byte) {
		gem_planar_mask((GEM_VDI_UBYTE) (first_mask & last_mask));
		*byte |= 1;
		return;
	}
	gem_planar_mask(first_mask);
	*byte++ |= 1;
	first_byte++;
	if (first_byte < last_byte) {
		gem_planar_mask(0xff);
		while (first_byte < last_byte) {
			*byte++ |= 1;
			first_byte++;
		}
	}
	gem_planar_mask(last_mask);
	*byte |= 1;
}

/* GEM patterns are anchored to the screen so one pattern byte covers every aligned byte on the row */
static void
gem_planar_pattern_span(GEM_VDI_COORD x1, GEM_VDI_COORD x2,
	GEM_VDI_COORD y, GEM_VDI_UBYTE pattern)
{
	volatile unsigned char __far *byte;
	GEM_VDI_UBYTE mask;
	GEM_VDI_UWORD first_byte;
	GEM_VDI_UWORD last_byte;

	if (!pattern)
		return;
	first_byte = (GEM_VDI_UWORD) x1 >> 3;
	last_byte = (GEM_VDI_UWORD) x2 >> 3;
	byte = gem_video_pointer(GEM_VIDEO_SEG_PLANAR,
		gem_pc_planar_offset(x1, y));

	if (first_byte == last_byte) {
		mask = (GEM_VDI_UBYTE) (pattern
			& (GEM_VDI_UBYTE) (0xff >> (x1 & 7))
			& (GEM_VDI_UBYTE) (0xff << (7 - (x2 & 7))));
		if (mask) {
			gem_planar_mask(mask);
			*byte |= 1;
		}
		return;
	}

	mask = (GEM_VDI_UBYTE) (pattern & (GEM_VDI_UBYTE) (0xff >> (x1 & 7)));
	if (mask) {
		gem_planar_mask(mask);
		*byte |= 1;
	}
	byte++;
	first_byte++;
	if (first_byte < last_byte) {
		gem_planar_mask(pattern);
		while (first_byte < last_byte) {
			*byte++ |= 1;
			first_byte++;
		}
	}
	mask = (GEM_VDI_UBYTE) (pattern
		& (GEM_VDI_UBYTE) (0xff << (7 - (x2 & 7))));
	if (mask) {
		gem_planar_mask(mask);
		*byte |= 1;
	}
}

void
gem_planar_fill_pattern(GEM_VDI_COORD x1, GEM_VDI_COORD y1,
	GEM_VDI_COORD x2, GEM_VDI_COORD y2, GEM_VDI_COLOR foreground,
	GEM_VDI_COLOR background, const GEM_VDI_UBYTE *pattern)
{
	GEM_VDI_COORD y;

	if (foreground == background) {
		gem_planar_prepare(foreground, GEM_VDI_REPLACE);
		for (y = y1; y <= y2; y++)
			gem_planar_solid_span(x1, x2, y);
		return;
	}

	gem_planar_prepare(background, GEM_VDI_REPLACE);
	for (y = y1; y <= y2; y++)
		gem_planar_solid_span(x1, x2, y);

	gem_planar_prepare(foreground, GEM_VDI_REPLACE);
	for (y = y1; y <= y2; y++)
		gem_planar_pattern_span(x1, x2, y,
			pattern[(GEM_VDI_UWORD) y & 7]);
}

/* put the VGA in plain write-mode-zero so the cursor code can read and write video bytes directly */
static void
gem_planar_cursor_direct_begin(void)
{
	outw(0x0f02, 0x3c4);	/* writes may reach all four planes */
	outw(0x0001, 0x3ce);	/* set/reset off */
	outw(0x0003, 0x3ce);	/* no rotate, replace */
	outw(0x0005, 0x3ce);	/* write mode zero */
	outw(0xff08, 0x3ce);	/* all bits writable */
}

/* leave a neutral state, gem_planar_prepare() sets color and set/reset together on the next ordinary call */
static void
gem_planar_cursor_direct_end(void)
{
	outw(0x0f02, 0x3c4);	/* writes reach all four planes */
	outw(0x0000, 0x3ce);	/* neutral set/reset color */
	outw(0x0001, 0x3ce);	/* set/reset stays off */
	outw(0x0003, 0x3ce);	/* replace */
	outw(0x0004, 0x3ce);	/* read plane zero */
	outw(0x0005, 0x3ce);	/* mode zero */
	outw(0xff08, 0x3ce);	/* full bit mask */
}

/* draw a clipped one-bit form straight from the source words, once per plane, so the RSC or ICN words stay the only copy of the image */
void
gem_planar_bitmap_replace(GEM_VDI_COORD x1, GEM_VDI_COORD y1,
	GEM_VDI_COORD x2, GEM_VDI_COORD y2, const GEM_VDI_BITS *bits,
	GEM_VDI_UWORD source_x, GEM_VDI_UWORD words_per_row,
	GEM_VDI_COLOR foreground, GEM_VDI_COLOR background,
	GEM_VDI_WORD use_background)
{
	volatile unsigned char __far *video;
	const GEM_VDI_BITS *row_bits;
	const GEM_VDI_BITS *source_word;
	GEM_VDI_COORD screen_x;
	GEM_VDI_COORD y;
	GEM_VDI_UWORD plane;
	volatile GEM_VDI_UWORD skip;
	GEM_VDI_BITS source_bit;
	GEM_VDI_UBYTE screen_bit;
	GEM_VDI_UBYTE covered;
	GEM_VDI_UBYTE source;
	GEM_VDI_UBYTE affected;
	GEM_VDI_UBYTE output;
	GEM_VDI_UBYTE old_value;
	GEM_VDI_UBYTE plane_bit;
	GEM_VDI_UWORD video_offset;

	gem_planar_cursor_direct_begin();
	for (plane = 0; plane < 4; plane++) {
		outw(gem_planar_read_map[plane], 0x3ce);
		outw(gem_planar_write_map[plane], 0x3c4);
		plane_bit = gem_planar_color_bit[plane];
		row_bits = bits;
		/* VDI already clipped so the offsets stay inside 16 bits */
		video_offset = gem_pc_planar_offset(x1, y1);
		for (y = y1; y <= y2; y++) {
			source_word = row_bits;
			source_bit = 0x8000;
			skip = source_x;
			while (skip >= 16) {
				source_word++;
				skip -= 16;
			}
			while (skip) {
				source_bit >>= 1;
				skip--;
			}

			screen_x = x1;
			video = gem_video_pointer(GEM_VIDEO_SEG_PLANAR,
				video_offset);
			while (screen_x <= x2) {
				covered = 0;
				source = 0;
				screen_bit = gem_video_bit_mask[
					(GEM_VDI_UWORD) screen_x & 7];
				while (screen_x <= x2 && screen_bit) {
					covered |= screen_bit;
					if (*source_word & source_bit)
						source |= screen_bit;
					screen_x++;
					screen_bit >>= 1;
					source_bit >>= 1;
					if (!source_bit) {
						source_bit = 0x8000;
						source_word++;
					}
				}

				if (use_background) {
					affected = covered;
					output = 0;
					if (foreground & plane_bit)
						output |= source;
					if (background & plane_bit)
						output |= (GEM_VDI_UBYTE)
							(covered &
							(GEM_VDI_UBYTE)
							~source);
				} else {
					affected = source;
					output = (foreground & plane_bit) ?
						source : 0;
				}
				if (affected) {
					old_value = *video;
					*video = (GEM_VDI_UBYTE)
						((old_value & (GEM_VDI_UBYTE)
							~affected)
						| (output & affected));
				}
				video++;
			}
			row_bits += words_per_row;
			video_offset = GEM_PC_PLANAR_NEXT_ROW(video_offset);
		}
	}
	gem_planar_cursor_direct_end();
}

/* draw font rows through set/reset, source_left_bit is the glyph's first visible column, clear glyph bits never enter the mask so pixels under them are untouched */
void
gem_planar_glyph_replace(GEM_VDI_COORD x1, GEM_VDI_COORD y1,
	GEM_VDI_COORD x2, GEM_VDI_COORD y2, const GEM_VDI_UBYTE *rows,
	GEM_VDI_UWORD source_x, GEM_VDI_UBYTE source_left_bit,
	GEM_VDI_COLOR foreground)
{
	volatile unsigned char __far *video;
	GEM_VDI_COORD screen_x;
	GEM_VDI_COORD screen_y;
	GEM_VDI_UWORD row_count;
	GEM_VDI_UWORD columns;
	volatile GEM_VDI_UWORD skip;
	GEM_VDI_UWORD video_offset;
	GEM_VDI_UBYTE source_bit;
	GEM_VDI_UBYTE screen_bit;
	GEM_VDI_UBYTE mask;

	gem_planar_prepare(foreground, GEM_VDI_REPLACE);

	/* an aligned unclipped 8-wide glyph row is already the bit mask, so the assembly helper takes the whole run */
	if (!source_x && source_left_bit == 0x80
		&& x2 - x1 == 7 && !((GEM_VDI_UWORD) x1 & 7U)) {
		row_count = (GEM_VDI_UWORD) (y2 - y1 + 1);
		video_offset = gem_pc_planar_offset(x1, y1);
		gem_pc_planar_glyph_rows(video_offset, rows, row_count);
		return;
	}

	screen_y = y1;
	row_count = (GEM_VDI_UWORD) (y2 - y1 + 1);
	while (row_count--) {
		source_bit = source_left_bit;
		skip = source_x;
		while (skip) {
			source_bit >>= 1;
			skip--;
		}
		screen_x = x1;
		columns = (GEM_VDI_UWORD) (x2 - x1 + 1);
		video_offset = gem_pc_planar_offset(x1, screen_y);
		while (columns) {
			mask = 0;
			screen_bit = gem_video_bit_mask[
				(GEM_VDI_UWORD) screen_x & 7];
			while (columns && screen_bit) {
				if (*rows & source_bit)
					mask |= screen_bit;
				screen_x++;
				columns--;
				screen_bit >>= 1;
				source_bit >>= 1;
			}
			if (mask) {
				gem_planar_mask(mask);
				video = gem_video_pointer(GEM_VIDEO_SEG_PLANAR,
					video_offset);
				*video |= 1;
			}
			video_offset++;
		}
		rows++;
		screen_y++;
	}
}

/* draw an aligned string straight from the 8x16 system font, zero on CGA and Hercules since their interleaved scan lines dont match the flat 80-byte planar layout so the VDI falls back to glyphs */
GEM_VDI_WORD __far __attribute__((far_section, noinline,
		section(".fartext.gem_planar_text_replace")))
	gem_planar_text_replace(GEM_VDI_COORD x, GEM_VDI_COORD y,
	const GEM_VDI_UBYTE *characters, GEM_VDI_UWORD count,
	GEM_VDI_UWORD stride, GEM_VDI_UWORD font_segment,
	GEM_VDI_UWORD font_offset, GEM_VDI_COLOR foreground)
{
	GEM_VDI_UWORD video_offset;

	if (gem_video_adapter != GEM_VIDEO_VGA
		&& gem_video_adapter != GEM_VIDEO_EGA)
		return 0;
	gem_planar_prepare(foreground, GEM_VDI_REPLACE);
	video_offset = gem_pc_planar_offset(x, y);
	gem_pc_planar_text_run(video_offset, characters, count, stride,
		font_segment, font_offset);
	return 1;
}

static void
gem_planar_cursor_save(void)
{
	volatile unsigned char __far *video;
	GEM_VDI_UWORD plane;
	GEM_VDI_UWORD row;
	GEM_VDI_UWORD byte_count;
	GEM_VDI_UWORD saved_index;
	GEM_VDI_UWORD video_offset;

	saved_index = 0;
	for (plane = 0; plane < 4; plane++) {
		outw(gem_planar_read_map[plane], 0x3ce);
		video_offset = gem_pc_planar_offset(gem_cursor_save_left,
			gem_cursor_save_top);
		for (row = 0; row < gem_cursor_save_rows; row++) {
			video = gem_video_pointer(GEM_VIDEO_SEG_PLANAR,
				video_offset);
			byte_count = gem_cursor_save_bytes;
			while (byte_count) {
				/* indexing the named global forces the SS override, a near-pointer store here once went through the video DS and trashed the framebuffer */
				gem_cursor_save[saved_index] = *video++;
				saved_index++;
				byte_count--;
			}
			video_offset = GEM_PC_PLANAR_NEXT_ROW(video_offset);
		}
	}
}

void
gem_planar_cursor_draw(const GEM_VDI_CURSOR *cursor)
{
	volatile unsigned char __far *video;
	GEM_VDI_UBYTE *saved;
	GEM_VDI_UBYTE *mask_bytes;
	GEM_VDI_UBYTE *image_bytes;
	GEM_VDI_UWORD plane;
	GEM_VDI_UWORD row;
	GEM_VDI_UWORD byte_count;
	GEM_VDI_UWORD video_offset;
	GEM_VDI_UBYTE mask;
	GEM_VDI_UBYTE image;
	GEM_VDI_UBYTE source;
	GEM_VDI_UBYTE original;
	GEM_VDI_UBYTE plane_bit;

	gem_planar_cursor_direct_begin();
	gem_planar_cursor_save();
	saved = gem_cursor_save;
	for (plane = 0; plane < 4; plane++) {
		outw(gem_planar_write_map[plane], 0x3c4);
		plane_bit = gem_planar_color_bit[plane];
		mask_bytes = gem_cursor_masks;
		image_bytes = gem_cursor_images;
		video_offset = gem_pc_planar_offset(gem_cursor_save_left,
			gem_cursor_save_top);
		for (row = 0; row < gem_cursor_save_rows; row++) {
			video = gem_video_pointer(GEM_VIDEO_SEG_PLANAR,
				video_offset);
			byte_count = gem_cursor_save_bytes;
			while (byte_count) {
				mask = *mask_bytes++;
				image = *image_bytes++;
				original = *saved++;
				if (mask) {
					source = 0;
					if ((cursor->foreground & 15) &
						plane_bit)
						source |= image & mask;
					if ((cursor->background & 15) &
						plane_bit)
						source |= (GEM_VDI_UBYTE)
							(~image & mask);
					*video = (GEM_VDI_UBYTE)
						((original & (GEM_VDI_UBYTE)
							~mask)
						| source);
				}
				video++;
				byte_count--;
			}
			video_offset = GEM_PC_PLANAR_NEXT_ROW(video_offset);
		}
	}
	gem_planar_cursor_direct_end();
}

void
gem_planar_cursor_restore(void)
{
	volatile unsigned char __far *video;
	GEM_VDI_UBYTE *saved;
	GEM_VDI_UWORD plane;
	GEM_VDI_UWORD row;
	GEM_VDI_UWORD byte_count;
	GEM_VDI_UWORD video_offset;

	gem_planar_cursor_direct_begin();
	saved = gem_cursor_save;
	for (plane = 0; plane < 4; plane++) {
		outw(gem_planar_write_map[plane], 0x3c4);
		video_offset = gem_pc_planar_offset(gem_cursor_save_left,
			gem_cursor_save_top);
		for (row = 0; row < gem_cursor_save_rows; row++) {
			video = gem_video_pointer(GEM_VIDEO_SEG_PLANAR,
				video_offset);
			byte_count = gem_cursor_save_bytes;
			while (byte_count) {
				*video++ = *saved++;
				byte_count--;
			}
			video_offset = GEM_PC_PLANAR_NEXT_ROW(video_offset);
		}
	}
	gem_planar_cursor_direct_end();
}

/*
 * Copy whole planar bytes with write mode one: one read loads all
 * four plane latches, one write puts them back.  Reverse order keeps
 * overlapping copies safe.
 */
void
gem_planar_copy_bytes(GEM_VDI_UWORD source_offset,
	GEM_VDI_UWORD destination_offset, GEM_VDI_UWORD count,
	GEM_VDI_WORD reverse)
{
	if (!count)
		return;

	/* all planes on, write mode one, read mode zero */
	outw(0x0f02, 0x3c4);
	outw(0x0105, 0x3ce);
	gem_pc_video_copy_bytes(GEM_VIDEO_SEG_PLANAR, source_offset,
		destination_offset, count, reverse);
	/* everything else expects write mode zero back */
	outw(0x0005, 0x3ce);
}

void __attribute__((optimize("Os")))
	gem_pc_video_set_palette(GEM_VDI_COLOR index, GEM_VDI_UBYTE red,
	GEM_VDI_UBYTE green, GEM_VDI_UBYTE blue)
{
	/* only VGA has the DAC at 3c8/3c9, elsewhere the resident VDI keeps the requested palette for VQ_COLOR */
	if (gem_video_adapter != GEM_VIDEO_VGA || index > 0x00ffU)
		return;
	if (red > 63U)
		red = 63U;
	if (green > 63U)
		green = 63U;
	if (blue > 63U)
		blue = 63U;
	outb((GEM_VDI_UBYTE) index, 0x3c8);
	outb(red, 0x3c9);
	outb(green, 0x3c9);
	outb(blue, 0x3c9);
}

void
gem_planar_open(GEM_VDI_SCREEN *screen, GEM_VDI_UWORD adapter)
{
	if (adapter == GEM_VIDEO_VGA) {
		gem_bios_video_set_mode(0x12);
		screen->xres = 640;
		screen->yres = 480;
	} else {
		gem_bios_video_set_mode(0x10);
		screen->xres = 640;
		screen->yres = 350;
	}
	screen->planes = 4;
	screen->colors = 16;

	/* set/reset on every plane, write and read mode zero */
	outw(0x0f01, 0x3ce);
	outw(0x0003, 0x3ce);
	outw(0x0005, 0x3ce);
}
