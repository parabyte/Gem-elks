/*
 * gem_video_common.c - shared helpers for the PC display drivers
 *
 * Copyright 1999 Caldera Thin Clients, Inc.
 * Copyright 1987 Digital Research, Inc.
 *
 * the bit-mask table and the cursor byte-mask builder shared by the PC
 * display drivers
 */

#include "gem_video_common.h"

GEM_VDI_UWORD gem_video_adapter;

const GEM_VDI_UBYTE gem_video_bit_mask[8] = {
	0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01
};

GEM_VDI_UBYTE gem_cursor_save[GEM_CURSOR_PLANE_SAVE_BYTES];
GEM_VDI_UBYTE gem_cursor_masks[GEM_CURSOR_MASK_BYTES];
GEM_VDI_UBYTE gem_cursor_images[GEM_CURSOR_MASK_BYTES];
GEM_VDI_COORD gem_cursor_save_left;
GEM_VDI_COORD gem_cursor_save_top;
GEM_VDI_UWORD gem_cursor_save_first_byte;
GEM_VDI_UWORD gem_cursor_save_rows;
GEM_VDI_UWORD gem_cursor_save_bytes;

/* line one cursor row up with the video bytes it lands in, source words keep GEM layout with bit 15 leftmost */
static void
gem_cursor_make_row(GEM_VDI_COORD cursor_x, GEM_VDI_COORD screen_width,
	GEM_VDI_BITS mask_bits, GEM_VDI_BITS image_bits,
	GEM_VDI_UBYTE *mask_bytes, GEM_VDI_UBYTE *image_bytes)
{
	GEM_VDI_COORD screen_x;
	GEM_VDI_UWORD column;
	volatile GEM_VDI_UWORD skip;
	GEM_VDI_UWORD byte_index;
	GEM_VDI_BITS source_bit;
	GEM_VDI_UBYTE screen_bit;

	mask_bytes[0] = 0;
	mask_bytes[1] = 0;
	mask_bytes[2] = 0;
	image_bytes[0] = 0;
	image_bytes[1] = 0;
	image_bytes[2] = 0;

	screen_x = cursor_x;
	column = 0;
	source_bit = 0x8000;
	if ((GEM_VDI_UWORD) screen_x & 0x8000) {
		/* cursor hangs off the left edge, skip the hidden part */
		column = (GEM_VDI_UWORD) -screen_x;
		screen_x = 0;
		skip = column;
		while (skip) {
			source_bit >>= 1;
			skip--;
		}
	}
	byte_index = 0;
	while (column < GEM_CURSOR_WIDTH && screen_x < screen_width) {
		if (mask_bits & source_bit) {
			screen_bit = gem_video_bit_mask[
				(GEM_VDI_UWORD) screen_x & 7];
			mask_bytes[byte_index] |= screen_bit;
			if (image_bits & source_bit)
				image_bytes[byte_index] |= screen_bit;
		}
		screen_x++;
		column++;
		source_bit >>= 1;
		/* advance only after the byte's last bit is used, testing before the pixel wrote a byte-aligned cursor past its row */
		if (!((GEM_VDI_UWORD) screen_x & 7))
			byte_index++;
	}
}

/* clip first, then build the byte masks once for all rows, positions outside -15 through the last screen pixel are refused so adding 15 cant overflow a signed 16-bit number */
GEM_VDI_WORD
gem_cursor_prepare(GEM_VDI_SCREEN *screen, GEM_VDI_COORD x,
	GEM_VDI_COORD y, const GEM_VDI_CURSOR *cursor)
{
	GEM_VDI_COORD right;
	GEM_VDI_COORD bottom;
	GEM_VDI_UWORD last_byte;
	GEM_VDI_UWORD source_row;
	GEM_VDI_UWORD row;
	GEM_VDI_UBYTE *mask_bytes;
	GEM_VDI_UBYTE *image_bytes;

	if (!screen || !cursor || cursor->width != GEM_CURSOR_WIDTH
		|| cursor->height != GEM_CURSOR_HEIGHT)
		return 0;
	if (x < -(GEM_CURSOR_WIDTH - 1)
		|| y < -(GEM_CURSOR_HEIGHT - 1)
		|| x >= screen->xres || y >= screen->yres)
		return 0;

	gem_cursor_save_left = x < 0 ? 0 : x;
	gem_cursor_save_top = y < 0 ? 0 : y;
	right = x + (GEM_CURSOR_WIDTH - 1);
	bottom = y + (GEM_CURSOR_HEIGHT - 1);
	if (right >= screen->xres)
		right = screen->xres - 1;
	if (bottom >= screen->yres)
		bottom = screen->yres - 1;

	gem_cursor_save_first_byte = gem_pc_byte_column(gem_cursor_save_left);
	last_byte = gem_pc_byte_column(right);
	gem_cursor_save_bytes = last_byte - gem_cursor_save_first_byte + 1;
	gem_cursor_save_rows =
		(GEM_VDI_UWORD) (bottom - gem_cursor_save_top + 1);
	if (gem_cursor_save_bytes > GEM_CURSOR_ROW_BYTES
		|| gem_cursor_save_rows > GEM_CURSOR_HEIGHT)
		return 0;

	source_row = (GEM_VDI_UWORD) (gem_cursor_save_top - y);
	mask_bytes = gem_cursor_masks;
	image_bytes = gem_cursor_images;
	for (row = 0; row < gem_cursor_save_rows; row++) {
		gem_cursor_make_row(x, screen->xres, cursor->mask[source_row],
			cursor->image[source_row], mask_bytes, image_bytes);
		mask_bytes += gem_cursor_save_bytes;
		image_bytes += gem_cursor_save_bytes;
		source_row++;
	}
	return 1;
}
