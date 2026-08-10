/*
 * gem_video_mono.c - shared 1-bit drawing engine for CGA and Hercules
 *
 * Copyright 1999 Caldera Thin Clients, Inc.
 * Copyright 1987 Digital Research, Inc.
 *
 * CGA and Hercules both store one bit per pixel with the MSB on the
 * left so one engine does both, only the segment and the (x, y) to
 * byte-offset mapping differ
 */

#include "gem_video_mono.h"

GEM_VDI_UWORD
gem_mono_offset(GEM_VDI_COORD x, GEM_VDI_COORD y)
{
	if (gem_video_adapter == GEM_VIDEO_HERCULES)
		return gem_pc_hercules_offset(x, y);
	return gem_pc_cga_offset(x, y);
}

GEM_VDI_UWORD
gem_mono_segment(void)
{
	if (gem_video_adapter == GEM_VIDEO_HERCULES)
		return GEM_VIDEO_SEG_HERCULES;
	return GEM_VIDEO_SEG_CGA;
}

/* apply the GEM writing mode to the masked bits of one video byte */
static void
gem_mono_apply(volatile unsigned char __far *byte, GEM_VDI_UBYTE mask,
	GEM_VDI_COLOR color, GEM_VDI_WORD mode)
{
	GEM_VDI_UBYTE old_value;
	GEM_VDI_UBYTE source;

	old_value = *byte;
	source = color ? mask : 0;
	switch (mode) {
	case GEM_VDI_XOR:
		old_value ^= source;
		break;
	case GEM_VDI_OR:
		old_value |= source;
		break;
	case GEM_VDI_AND:
		old_value &= (GEM_VDI_UBYTE) (source | (GEM_VDI_UBYTE) ~mask);
		break;
	case GEM_VDI_CLEAR:
		old_value &= (GEM_VDI_UBYTE) ~mask;
		break;
	case GEM_VDI_REPLACE:
	default:
		old_value = (GEM_VDI_UBYTE)
			((old_value & (GEM_VDI_UBYTE) ~mask) | source);
		break;
	}
	*byte = old_value;
}

void
gem_mono_write_pixel(GEM_VDI_COORD x, GEM_VDI_COORD y,
	GEM_VDI_COLOR color, GEM_VDI_WORD mode)
{
	volatile unsigned char __far *byte;

	byte = gem_video_pointer(gem_mono_segment(), gem_mono_offset(x, y));
	gem_mono_apply(byte, gem_pixel_mask(x), color, mode);
}

GEM_VDI_COLOR
gem_mono_read_pixel(GEM_VDI_COORD x, GEM_VDI_COORD y)
{
	volatile unsigned char __far *byte;

	byte = gem_video_pointer(gem_mono_segment(), gem_mono_offset(x, y));
	return (*byte & gem_pixel_mask(x)) ? 1 : 0;
}

void
gem_mono_horizontal_line(GEM_VDI_COORD x1, GEM_VDI_COORD x2,
	GEM_VDI_COORD y, GEM_VDI_COLOR color, GEM_VDI_WORD mode)
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
	byte = gem_video_pointer(gem_mono_segment(), gem_mono_offset(x1, y));

	if (first_byte == last_byte) {
		gem_mono_apply(byte, (GEM_VDI_UBYTE) (first_mask & last_mask),
			color, mode);
		return;
	}

	gem_mono_apply(byte++, first_mask, color, mode);
	first_byte++;
	while (first_byte < last_byte) {
		gem_mono_apply(byte++, 0xff, color, mode);
		first_byte++;
	}
	gem_mono_apply(byte, last_mask, color, mode);
}

void
gem_mono_vertical_line(GEM_VDI_COORD x, GEM_VDI_COORD y1,
	GEM_VDI_COORD y2, GEM_VDI_COLOR color, GEM_VDI_WORD mode)
{
	while (y1 <= y2) {
		gem_mono_write_pixel(x, y1, color, mode);
		y1++;
	}
}

void
gem_mono_fill_rect(GEM_VDI_COORD x1, GEM_VDI_COORD y1,
	GEM_VDI_COORD x2, GEM_VDI_COORD y2, GEM_VDI_COLOR color,
	GEM_VDI_WORD mode)
{
	while (y1 <= y2) {
		gem_mono_horizontal_line(x1, x2, y1, color, mode);
		y1++;
	}
}

void
gem_mono_fill_pattern(GEM_VDI_COORD x1, GEM_VDI_COORD y1,
	GEM_VDI_COORD x2, GEM_VDI_COORD y2, GEM_VDI_COLOR foreground,
	GEM_VDI_COLOR background, const GEM_VDI_UBYTE *pattern)
{
	volatile unsigned char __far *byte;
	GEM_VDI_COORD y;
	GEM_VDI_UBYTE source;
	GEM_VDI_UBYTE mask;
	GEM_VDI_UWORD first_byte;
	GEM_VDI_UWORD last_byte;
	GEM_VDI_UWORD current_byte;

	first_byte = (GEM_VDI_UWORD) x1 >> 3;
	last_byte = (GEM_VDI_UWORD) x2 >> 3;
	for (y = y1; y <= y2; y++) {
		/* a dark foreground inverts the row, equal colors mean solid */
		source = pattern[(GEM_VDI_UWORD) y & 7];
		if (!foreground)
			source = (GEM_VDI_UBYTE) ~source;
		if (!!foreground == !!background)
			source = foreground ? 0xff : 0x00;
		byte = gem_video_pointer(gem_mono_segment(),
			gem_mono_offset(x1, y));
		current_byte = first_byte;
		while (current_byte <= last_byte) {
			mask = 0xff;
			if (current_byte == first_byte)
				mask &= (GEM_VDI_UBYTE) (0xff >> (x1 & 7));
			if (current_byte == last_byte)
				mask &= (GEM_VDI_UBYTE)
					(0xff << (7 - (x2 & 7)));
			*byte = (GEM_VDI_UBYTE)
				((*byte & (GEM_VDI_UBYTE) ~mask)
				| (source & mask));
			byte++;
			current_byte++;
		}
	}
}

/* draw a one-bit GEM form, source words and screen share left-to-right bit order so each covered byte is built from the source and merged with one read-modify-write */
void
gem_mono_bitmap_replace(GEM_VDI_COORD x1, GEM_VDI_COORD y1,
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
	volatile GEM_VDI_UWORD skip;
	GEM_VDI_BITS source_bit;
	GEM_VDI_UBYTE screen_bit;
	GEM_VDI_UBYTE covered;
	GEM_VDI_UBYTE source;
	GEM_VDI_UBYTE affected;
	GEM_VDI_UBYTE output;
	GEM_VDI_UBYTE old_value;

	row_bits = bits;
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
		video = gem_video_pointer(gem_mono_segment(),
			gem_mono_offset(x1, y));
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
				if (foreground)
					output |= source;
				if (background)
					output |= (GEM_VDI_UBYTE)
						(covered & (GEM_VDI_UBYTE)
						~source);
			} else {
				affected = source;
				output = foreground ? source : 0;
			}
			if (affected) {
				old_value = *video;
				*video = (GEM_VDI_UBYTE)
					((old_value & (GEM_VDI_UBYTE) ~affected)
					| (output & affected));
			}
			video++;
		}
		row_bits += words_per_row;
	}
}

/* draw font rows, glyph mask built per touched byte and merged with one transparent replace so bits outside the glyph keep their value */
void
gem_mono_glyph_replace(GEM_VDI_COORD x1, GEM_VDI_COORD y1,
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
		video_offset = gem_mono_offset(x1, screen_y);
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
				video = gem_video_pointer(gem_mono_segment(),
					video_offset);
				gem_mono_apply(video, mask, foreground,
					GEM_VDI_REPLACE);
			}
			video_offset++;
		}
		rows++;
		screen_y++;
	}
}

/* draw the cursor, saving the covered screen bytes first */
void
gem_mono_cursor_draw(const GEM_VDI_CURSOR *cursor)
{
	volatile unsigned char __far *video;
	GEM_VDI_UBYTE *mask_bytes;
	GEM_VDI_UBYTE *image_bytes;
	GEM_VDI_COORD y;
	GEM_VDI_UWORD row;
	GEM_VDI_UWORD byte_count;
	GEM_VDI_UWORD saved_index;
	GEM_VDI_UBYTE mask;
	GEM_VDI_UBYTE image;
	GEM_VDI_UBYTE source;
	GEM_VDI_UBYTE original;

	saved_index = 0;
	mask_bytes = gem_cursor_masks;
	image_bytes = gem_cursor_images;
	y = gem_cursor_save_top;
	for (row = 0; row < gem_cursor_save_rows; row++) {
		video = gem_video_pointer(gem_mono_segment(),
			gem_mono_offset(gem_cursor_save_left, y));
		byte_count = gem_cursor_save_bytes;
		while (byte_count) {
			original = *video;
			/* store via the named global so it goes through SS */
			gem_cursor_save[saved_index] = original;
			saved_index++;
			mask = *mask_bytes++;
			image = *image_bytes++;
			if (mask) {
				source = 0;
				if (cursor->foreground)
					source |= image & mask;
				if (cursor->background)
					source |=
						(GEM_VDI_UBYTE) (~image & mask);
				*video = (GEM_VDI_UBYTE)
					((original & (GEM_VDI_UBYTE) ~mask)
					| source);
			}
			video++;
			byte_count--;
		}
		y++;
	}
}

void
gem_mono_cursor_restore(void)
{
	volatile unsigned char __far *video;
	GEM_VDI_UBYTE *saved;
	GEM_VDI_COORD y;
	GEM_VDI_UWORD row;
	GEM_VDI_UWORD byte_count;

	saved = gem_cursor_save;
	y = gem_cursor_save_top;
	for (row = 0; row < gem_cursor_save_rows; row++) {
		video = gem_video_pointer(gem_mono_segment(),
			gem_mono_offset(gem_cursor_save_left, y));
		byte_count = gem_cursor_save_bytes;
		while (byte_count) {
			*video++ = *saved++;
			byte_count--;
		}
		y++;
	}
}

/* CGA and Hercules video bytes are ordinary memory, just copy them */
void
gem_mono_copy_bytes(GEM_VDI_UWORD source_offset,
	GEM_VDI_UWORD destination_offset, GEM_VDI_UWORD count,
	GEM_VDI_WORD reverse)
{
	if (!count)
		return;
	gem_pc_video_copy_bytes(gem_mono_segment(), source_offset,
		destination_offset, count, reverse);
}
