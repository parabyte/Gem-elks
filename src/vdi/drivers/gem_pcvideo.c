/*
 * Native IBM PC display drivers for ELKS GEM.
 *
 * Copyright 1999 Caldera Thin Clients, Inc.
 * Copyright 1987 Digital Research, Inc.
 * Copyright 2026 elks-gem contributors
 *
 * This is an ia16 C translation of the GPL-released FreeGEM PC display
 * drivers.  The mode selection, geometry, video segments, scanline
 * interleaving, planar register usage, and Hercules CRTC tables come from:
 *
 *   SDUNI9  - universal EGA/VGA driver (uni_drv.a86, uniregs.a86)
 *   SDCGA9  - 640x200 monochrome CGA driver (cga_drv.a86, cgaregs.a86)
 *   SDHRC9  - 720x348 Hercules driver (hrc_drv.a86, hercregs.a86)
 *
 * This file is the hardware raster boundary for the native GEM VDI; no
 * third-party window-server device contract sits between GEM and the PC.
 *
 * All arithmetic is 8-bit or 16-bit.  Physical video addresses cross the C
 * interface as an explicit segment:offset union.  Offset calculations stay
 * within one 64 KiB video segment: VGA/EGA reaches at most 38399, CGA 16383,
 * and Hercules 32767.  There is therefore no far-pointer normalization and
 * no compiler 32-bit address helper in a drawing hot path.
 */

#include <arch/io.h>
#include <linuxmt/ntty.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

#include "gem_pcvideo.h"

#define GEM_VIDEO_SEG_PLANAR	0xa000
#define GEM_VIDEO_SEG_CGA	0xb800
#define GEM_VIDEO_SEG_HERCULES	0xb000

#define GEM_VIDEO_BYTES_CGA	80
#define GEM_VIDEO_BYTES_HERCULES	90

/*
 * A 16-pixel cursor can touch three video bytes when its left edge is not
 * byte aligned.  The save store contains three bytes for each of sixteen
 * rows and each of four EGA/VGA planes.  This is the compact descendant of
 * the four-byte scan-line mousbuf in FreeGEM's egammre2.a86: clipping and
 * exact byte bounds let this port omit the unused fourth byte.  The masks
 * are prepared once in fixed near storage and are never converted into a
 * device-independent bitmap.
 */
#define GEM_CURSOR_WIDTH	16
#define GEM_CURSOR_HEIGHT	16
#define GEM_CURSOR_ROW_BYTES	3
#define GEM_CURSOR_PLANE_SAVE_BYTES	192
#define GEM_CURSOR_MASK_BYTES	48

typedef union gem_video_address {
	volatile unsigned char __far *pointer;
	struct {
		unsigned short offset;
		unsigned short segment;
	} words;
} GEM_VIDEO_ADDRESS;

/* A far pointer is exactly one 16-bit offset plus one 16-bit segment. */
typedef char gem_video_far_pointer_must_be_four_bytes
	[(sizeof(GEM_VIDEO_ADDRESS) == 4) ? 1 : -1];

static GEM_VDI_UWORD active_adapter;
static GEM_VDI_UWORD saved_bios_mode;
static GEM_VDI_WORD console_locked;

static GEM_VDI_UBYTE gem_cursor_save[GEM_CURSOR_PLANE_SAVE_BYTES];
static GEM_VDI_UBYTE gem_cursor_masks[GEM_CURSOR_MASK_BYTES];
static GEM_VDI_UBYTE gem_cursor_images[GEM_CURSOR_MASK_BYTES];
static GEM_VDI_COORD gem_cursor_save_left;
static GEM_VDI_COORD gem_cursor_save_top;
static GEM_VDI_UWORD gem_cursor_save_first_byte;
static GEM_VDI_UWORD gem_cursor_save_rows;
static GEM_VDI_UWORD gem_cursor_save_bytes;
static GEM_VDI_WORD gem_cursor_save_active;

static const GEM_VDI_UBYTE gem_video_bit_mask[8] = {
	0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01
};

/* Values for graphics-controller read-map-select register four. */
static const GEM_VDI_UWORD gem_planar_read_map[4] = {
	0x0004, 0x0104, 0x0204, 0x0304
};

/* Values for sequencer map-mask register two, one plane at a time. */
static const GEM_VDI_UWORD gem_planar_write_map[4] = {
	0x0102, 0x0202, 0x0402, 0x0802
};

static const GEM_VDI_UBYTE gem_planar_color_bit[4] = {
	0x01, 0x02, 0x04, 0x08
};

static volatile unsigned char __far *
gem_video_pointer(GEM_VDI_UWORD segment, GEM_VDI_UWORD offset)
{
	GEM_VIDEO_ADDRESS address;

	address.words.offset = offset;
	address.words.segment = segment;
	return address.pointer;
}

static GEM_VDI_UBYTE
gem_pixel_mask(GEM_VDI_COORD x)
{
	return gem_video_bit_mask[(GEM_VDI_UWORD) x & 7];
}

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

static void
gem_planar_prepare(GEM_VDI_COLOR color, GEM_VDI_WORD mode)
{
	if (mode == GEM_VDI_CLEAR) {
		color = 0;
		mode = GEM_VDI_REPLACE;
	}
	/*
	 * Cursor and native-bitmap paths deliberately leave set/reset disabled.
	 * Make the ordinary raster contract local to this preparation point: GC0
	 * supplies the requested color and GC1 enables that value on all four
	 * planes.  No ordinary caller therefore depends on a stale cursor color
	 * or on the register state left by an earlier operation.
	 */
	outw((GEM_VDI_UWORD) ((color & 15) << 8), 0x3ce);
	outw(0x0f01, 0x3ce);
	outw((GEM_VDI_UWORD) (3 | (gem_planar_rotate(mode) << 8)), 0x3ce);
}

static void
gem_planar_mask(GEM_VDI_UBYTE mask)
{
	outw((GEM_VDI_UWORD) (8 | ((GEM_VDI_UWORD) mask << 8)), 0x3ce);
}

static void
gem_planar_write_pixel(GEM_VDI_COORD x, GEM_VDI_COORD y,
	GEM_VDI_COLOR color, GEM_VDI_WORD mode)
{
	volatile unsigned char __far *byte;

	gem_planar_prepare(color, mode);
	gem_planar_mask(gem_pixel_mask(x));
	byte = gem_video_pointer(GEM_VIDEO_SEG_PLANAR,
		gem_pc_planar_offset(x, y));
	/* The read loads the EGA/VGA latches; the write applies set/reset. */
	*byte |= 1;
}

static GEM_VDI_COLOR
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
		/*
		 * Fixed tables avoid both variable-count shifts in this hot path.
		 * Each entry is the exact EGA/VGA register word or color bit for
		 * this plane, so no multiplication helper can enter an 8086 build.
		 */
		outw(gem_planar_read_map[plane], 0x3ce);
		if (*byte & mask)
			color |= gem_planar_color_bit[plane];
	}
	return color;
}

static void gem_planar_solid_span(GEM_VDI_COORD x1, GEM_VDI_COORD x2,
	GEM_VDI_COORD y);

static void
gem_planar_horizontal_line(GEM_VDI_COORD x1, GEM_VDI_COORD x2,
	GEM_VDI_COORD y, GEM_VDI_COLOR color, GEM_VDI_WORD mode)
{
	gem_planar_prepare(color, mode);
	gem_planar_solid_span(x1, x2, y);
}

static void
gem_planar_vertical_line(GEM_VDI_COORD x, GEM_VDI_COORD y1,
	GEM_VDI_COORD y2, GEM_VDI_COLOR color, GEM_VDI_WORD mode)
{
	GEM_VDI_UWORD offset;
	GEM_VDI_UWORD rows;

	gem_planar_prepare(color, mode);
	gem_planar_mask(gem_pixel_mask(x));
	offset = gem_pc_planar_offset(x, y1);
	rows = (GEM_VDI_UWORD) (y2 - y1) + 1U;

	/*
	 * ES alone addresses A000 video memory; DS is never changed.  Each ORB
	 * loads the VGA latches and writes the prepared set/reset color, then DI
	 * advances by the exact 80-byte scan-line stride.  The largest offset is
	 * 38399, so a clipped row count cannot wrap this 16-bit offset.
	 */
	__asm__ volatile ("pushw %%es\n\t"
			  "movw $0xa000,%%ax\n\t"
			  "movw %%ax,%%es\n\t"
			  "orw %%cx,%%cx\n\t"
			  "jz 2f\n"
			  "1:\n\t"
			  "orb $1,%%es:(%%di)\n\t"
			  "addw $80,%%di\n\t"
			  "loop 1b\n"
			  "2:\n\t"
			  "popw %%es"
			  : "+D" (offset), "+c" (rows)
			  :
			  : "ax", "cc", "memory");
}

static void
gem_planar_fill_rect(GEM_VDI_COORD x1, GEM_VDI_COORD y1,
	GEM_VDI_COORD x2, GEM_VDI_COORD y2, GEM_VDI_COLOR color,
	GEM_VDI_WORD mode)
{
	GEM_VDI_UWORD offset;
	GEM_VDI_UWORD row_bytes;
	GEM_VDI_UWORD rows;

	gem_planar_prepare(color, mode);

	/*
	 * GEM desktop, menu, and window interiors normally begin and end on byte
	 * boundaries.  Select the all-bit mask once for the complete rectangle,
	 * then perform only one latch read/write per video byte.  Row width is at
	 * most eighty bytes, so all counts and offsets remain unsigned 16-bit and
	 * no multiply, divide, allocation, or temporary bitmap is required.
	 */
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

/*
 * Write one solid scan-line span after set/reset and rotate mode have already
 * been selected.  Only the first and last video bytes need partial masks;
 * every middle byte is written with mask ff.  Thus a 640-pixel row uses 80
 * latch read/writes instead of 640 pixel calls and does not reload the VGA
 * graphics registers for each pixel.
 */
static void
gem_planar_solid_span(GEM_VDI_COORD x1, GEM_VDI_COORD x2,
	GEM_VDI_COORD y)
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

/*
 * Overlay one foreground pattern row after the background pass.  Since GEM
 * patterns are anchored to absolute screen coordinates, every aligned video
 * byte on a scan line receives the same pattern byte.  A single VGA bit-mask
 * register value therefore covers all middle bytes in the row.
 */
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

	mask = (GEM_VDI_UBYTE) (pattern
		& (GEM_VDI_UBYTE) (0xff >> (x1 & 7)));
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

static void
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

	/* First establish every clear pattern bit as the background color. */
	gem_planar_prepare(background, GEM_VDI_REPLACE);
	for (y = y1; y <= y2; y++)
		gem_planar_solid_span(x1, x2, y);

	/* Then replace only set pattern bits with the foreground color. */
	gem_planar_prepare(foreground, GEM_VDI_REPLACE);
	for (y = y1; y <= y2; y++)
		gem_planar_pattern_span(x1, x2, y,
			pattern[(GEM_VDI_UWORD) y & 7]);
}

static GEM_VDI_UWORD
gem_mono_offset(GEM_VDI_COORD x, GEM_VDI_COORD y)
{
	if (active_adapter == GEM_VIDEO_HERCULES)
		return gem_pc_hercules_offset(x, y);
	return gem_pc_cga_offset(x, y);
}

static GEM_VDI_UWORD
gem_mono_segment(void)
{
	if (active_adapter == GEM_VIDEO_HERCULES)
		return GEM_VIDEO_SEG_HERCULES;
	return GEM_VIDEO_SEG_CGA;
}

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

static void
gem_mono_write_pixel(GEM_VDI_COORD x, GEM_VDI_COORD y,
	GEM_VDI_COLOR color, GEM_VDI_WORD mode)
{
	volatile unsigned char __far *byte;

	byte = gem_video_pointer(gem_mono_segment(), gem_mono_offset(x, y));
	gem_mono_apply(byte, gem_pixel_mask(x), color, mode);
}

static GEM_VDI_COLOR
gem_mono_read_pixel(GEM_VDI_COORD x, GEM_VDI_COORD y)
{
	volatile unsigned char __far *byte;

	byte = gem_video_pointer(gem_mono_segment(), gem_mono_offset(x, y));
	return (*byte & gem_pixel_mask(x)) ? 1 : 0;
}

static void
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

static void
gem_mono_vertical_line(GEM_VDI_COORD x, GEM_VDI_COORD y1,
	GEM_VDI_COORD y2, GEM_VDI_COLOR color, GEM_VDI_WORD mode)
{
	while (y1 <= y2) {
		gem_mono_write_pixel(x, y1, color, mode);
		y1++;
	}
}

static void
gem_mono_fill_rect(GEM_VDI_COORD x1, GEM_VDI_COORD y1,
	GEM_VDI_COORD x2, GEM_VDI_COORD y2, GEM_VDI_COLOR color,
	GEM_VDI_WORD mode)
{
	while (y1 <= y2) {
		gem_mono_horizontal_line(x1, x2, y1, color, mode);
		y1++;
	}
}

static void
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

/*
 * Align one GEM cursor row to the actual PC video bytes it touches.  The
 * source words retain GEM's bit-15-at-the-left representation.  This small
 * CPU-only loop avoids every variable-count shift: each iteration advances
 * the source with the one-bit 8086 shift form, and the screen bit comes from
 * an eight-byte lookup table.  No video I/O occurs here.
 */
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
		/*
		 * Advance after consuming the last bit in a screen byte.  Testing
		 * before the pixel made a cursor whose left edge was already byte
		 * aligned start in output byte one instead of byte zero.  Apart from
		 * shifting the form eight pixels, that wrote past each two-byte row
		 * prepared for an aligned 16-pixel cursor.  The post-pixel test also
		 * handles a form clipped at screen x zero without a special case.
		 */
		if (!((GEM_VDI_UWORD) screen_x & 7))
			byte_index++;
	}
}

/*
 * Clip the cursor before any offset is formed, then prepare its byte-aligned
 * masks once for all four VGA/EGA planes.  Coordinates outside the range
 * -15 through the final screen coordinate are rejected before adding 15,
 * so the signed 16-bit additions cannot overflow.
 */
static GEM_VDI_WORD
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

/*
 * Select raw write-mode-zero access to individual EGA/VGA planes.  CPU data
 * replaces all eight bits of the selected plane: set/reset is disabled,
 * rotate/logical operation is replace, and the bit mask is ff.  The map mask
 * itself is selected once per plane by the caller.
 */
static void
gem_planar_cursor_direct_begin(void)
{
	/*
	 * FreeGEM's EGA_KLUG enables every write plane before it saves or draws
	 * the mouse.  Do the same before the first framebuffer read so no stale
	 * per-plane mask from a prior cursor pass can survive into this pass.
	 */
	outw(0x0f02, 0x3c4); /* Sequencer 2: writes may reach all four planes. */
	outw(0x0001, 0x3ce);	/* Graphics controller 1: disable set/reset. */
	outw(0x0003, 0x3ce);	/* Graphics controller 3: rotate 0, replace. */
	outw(0x0005, 0x3ce);	/* Graphics controller 5: write mode zero. */
	outw(0xff08, 0x3ce);	/* Graphics controller 8: all bits writable. */
}

/*
 * Leave a neutral raw-plane state matching FreeGEM's EGA_KLUG.  In
 * particular, do not enable set/reset while GC0 still contains an arbitrary
 * color from an older primitive.  gem_planar_prepare() establishes both GC0
 * and GC1 together when the next ordinary raster operation begins.
 */
static void
gem_planar_cursor_direct_end(void)
{
	outw(0x0f02, 0x3c4);	/* Sequencer 2: writes reach all four planes. */
	outw(0x0000, 0x3ce);	/* Graphics controller 0: neutral set/reset color. */
	outw(0x0001, 0x3ce);	/* Graphics controller 1: keep set/reset disabled. */
	outw(0x0003, 0x3ce);	/* Graphics controller 3: replace operation. */
	outw(0x0004, 0x3ce);	/* Graphics controller 4: read plane zero. */
	outw(0x0005, 0x3ce);	/* Graphics controller 5: mode zero. */
	outw(0xff08, 0x3ce);	/* Graphics controller 8: full bit mask. */
}

/*
 * Draw a clipped native GEM one-bit form into planar video memory.
 *
 * The source is never converted.  Each plane walks the original 16-bit
 * words, advancing its source mask with the cheap one-bit 8086 shift.  The
 * inner loop assembles exactly one destination video byte in registers and
 * then performs one read/modify/write for that plane.  Even an unaligned
 * icon therefore needs at most one video access per covered byte and plane,
 * rather than eight pixel calls and repeated graphics-controller setup.
 *
 * The four plane walks deliberately repeat the small CPU bit loop.  Keeping
 * no converted scan-line buffer saves near data and lets the original RSC or
 * ICN words remain the sole source representation on the target machine.
 */
static void
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
		/* CPU reads and writes address the same physical color plane. */
		outw(gem_planar_read_map[plane], 0x3ce);
		outw(gem_planar_write_map[plane], 0x3c4);
		plane_bit = gem_planar_color_bit[plane];
		row_bits = bits;
		/*
		 * VDI clipping guarantees 0 <= y1 <= y2 <= 479 and x1 is on
		 * the 640-pixel screen.  Form the first A000 byte offset once for
		 * this plane, then walk the fixed eighty-byte hardware stride.
		 * The largest visible offset is 38399.  The final loop advance can
		 * therefore form at most 38479, which is never dereferenced and
		 * cannot wrap the 16-bit segment offset.
		 */
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
			video = gem_video_pointer(GEM_VIDEO_SEG_PLANAR, video_offset);
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
							(covered & (GEM_VDI_UBYTE) ~source);
				} else {
					affected = source;
					output = (foreground & plane_bit) ? source : 0;
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
			video_offset = GEM_PC_PLANAR_NEXT_ROW(video_offset);
		}
	}
	gem_planar_cursor_direct_end();
}

/*
 * CGA and Hercules expose the same left-to-right bit order as a GEM form.
 * Build one covered byte directly from the native source word and merge it
 * into the adapter's interleaved scan line.  Color indexes have already
 * crossed the VDI monochrome boundary and are therefore exactly zero or one.
 */
static void
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
						(covered & (GEM_VDI_UBYTE) ~source);
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

/*
 * Overlay compact font rows with EGA/VGA set/reset hardware.
 *
 * Each source byte is consumed in place; source_left_bit identifies its
 * first visible glyph column before clipping.  One small shift loop aligns
 * the source with the first clipped screen column.  The inner loop then
 * composes one hardware bit mask for each video byte touched by the row.
 * Selecting the foreground once in set/reset makes one latched byte write
 * update all four color planes, so no plane loop and no per-pixel register
 * setup remains in the menu/text path.  Clear glyph bits never enter the
 * mask and therefore preserve the existing video byte exactly.
 */
static void
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

	/*
	 * GEM's native system font is eight pixels wide.  When its destination
	 * starts on a PC video-byte boundary, one source row is already the exact
	 * EGA/VGA bit mask for that row.  Consume it directly instead of walking
	 * eight individual source bits.  This is the normal menu, Desktop, and
	 * IRC text-grid case after layout alignment.
	 *
	 * The row offset has scale one video byte.  It begins in the inclusive
	 * 0..38399 visible range and advances by the fixed eighty-byte scan-line
	 * stride; the final unused successor is at most 38479 and cannot wrap a
	 * 16-bit word.  Clear source bits stay transparent because they never
	 * enter graphics-controller register eight.
	 */
	if (!source_x && source_left_bit == 0x80
	    && x2 - x1 == 7 && !((GEM_VDI_UWORD) x1 & 7U)) {
		row_count = (GEM_VDI_UWORD) (y2 - y1 + 1);
		video_offset = gem_pc_planar_offset(x1, y1);
		/*
		 * One far call now owns the complete row walk.  The helper leaves
		 * DS untouched, loads A000 into ES once, and emits register-eight
		 * plus one latch read/write only for nonblank native glyph rows.
		 * This removes two medium-model far calls and one segment reload
		 * from every visible scan line on an 8088/8086.
		 */
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
				/* Read loads the latches; set/reset replaces masked bits. */
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

/*
 * CGA and Hercules use the same left-to-right destination-byte masks.  Their
 * one-bit framebuffer needs only one transparent replace merge per touched
 * byte.  gem_mono_apply() preserves every bit outside the composed glyph
 * mask and maps the already-bounded foreground index to black or white.
 */
static void
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
	/*
	 * A planar scan line is exactly 80 bytes.  Cursor clipping guarantees the
	 * final row remains at or below offset 38399, so one 16-bit addition per
	 * row replaces repeated far offset calls without wrap or normalization.
	 */
	for (plane = 0; plane < 4; plane++) {
		/* One read-map register write replaces sixty-four pixel reads. */
		outw(gem_planar_read_map[plane], 0x3ce);
		video_offset = gem_pc_planar_offset(gem_cursor_save_left,
			gem_cursor_save_top);
		for (row = 0; row < gem_cursor_save_rows; row++) {
			video = gem_video_pointer(GEM_VIDEO_SEG_PLANAR,
				video_offset);
			byte_count = gem_cursor_save_bytes;
			while (byte_count) {
				/*
				 * Name the near destination directly.  GNU ia16 must load DS
				 * with A000 while dereferencing VIDEO.  A near-pointer store in
				 * the same expression was consequently emitted through that
				 * video DS instead of through SS, corrupting the framebuffer on
				 * every mouse move and leaving the save buffer unchanged.  The
				 * indexed global forces the required SS override while retaining
				 * a single byte read and single byte store on an 8088/8086.
				 */
				gem_cursor_save[saved_index] = *video++;
				saved_index++;
				byte_count--;
			}
			video_offset = GEM_PC_PLANAR_NEXT_ROW(video_offset);
		}
	}
}

static void
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
	/* Save under the same known raw-plane state used for the draw itself. */
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
					if ((cursor->foreground & 15) & plane_bit)
						source |= image & mask;
					if ((cursor->background & 15) & plane_bit)
						source |= (GEM_VDI_UBYTE)
							(~image & mask);
					*video = (GEM_VDI_UBYTE)
						((original & (GEM_VDI_UBYTE) ~mask)
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

static void
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

static void
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
			/* Keep the near save store out of the active video DS. */
			gem_cursor_save[saved_index] = original;
			saved_index++;
			mask = *mask_bytes++;
			image = *image_bytes++;
			if (mask) {
				source = 0;
				if (cursor->foreground)
					source |= image & mask;
				if (cursor->background)
					source |= (GEM_VDI_UBYTE) (~image & mask);
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

static void
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

static void
gem_hercules_crtc(const GEM_VDI_UBYTE *table, GEM_VDI_UBYTE mode)
{
	GEM_VDI_UWORD index;

	outb(mode, 0x3b8);
	for (index = 0; index < 12; index++) {
		outb((GEM_VDI_UBYTE) index, 0x3b4);
		outb(table[index], 0x3b5);
	}
	outb((GEM_VDI_UBYTE) (mode + 8), 0x3b8);
}

static void
gem_hercules_open(void)
{
	static const GEM_VDI_UBYTE graphics_crtc[12] = {
		0x35, 0x2d, 0x2e, 0x07, 0x5b, 0x02,
		0x57, 0x57, 0x02, 0x03, 0x00, 0x00
	};

	/* Permit graphics memory, then select graphics with video enabled. */
	outb(1, 0x3bf);
	gem_hercules_crtc(graphics_crtc, 0x02);
}

static void
gem_hercules_close(void)
{
	static const GEM_VDI_UBYTE text_crtc[12] = {
		0x61, 0x50, 0x52, 0x0f, 0x19, 0x06,
		0x19, 0x19, 0x02, 0x0d, 0x0b, 0x0c
	};

	outb(0, 0x3bf);
	gem_hercules_crtc(text_crtc, 0x20);
}

/*
 * Copy complete VGA/EGA bytes with write mode one.  Reading a source byte
 * fills all four VGA latches; the following destination write stores those
 * four latched plane bytes together.  Source and destination are in the same
 * 64 KiB A000 segment, and count is at most one 80-byte scanline.  Reverse
 * order preserves overlapping rectangles without a temporary row buffer.
 */
static void
gem_planar_copy_bytes(GEM_VDI_UWORD source_offset,
	GEM_VDI_UWORD destination_offset, GEM_VDI_UWORD count,
	GEM_VDI_WORD reverse)
{
	if (!count)
		return;

	/* Enable every plane, select write mode one, and retain read mode zero. */
	outw(0x0f02, 0x3c4);
	outw(0x0105, 0x3ce);
	gem_pc_video_copy_bytes(GEM_VIDEO_SEG_PLANAR, source_offset,
		destination_offset, count, reverse);
	/* Every ordinary drawing path expects VGA write mode zero. */
	outw(0x0005, 0x3ce);
}

/* Complete CGA/Hercules bytes are ordinary packed monochrome memory. */
static void
gem_mono_copy_bytes(GEM_VDI_UWORD source_offset,
	GEM_VDI_UWORD destination_offset, GEM_VDI_UWORD count,
	GEM_VDI_WORD reverse)
{
	if (!count)
		return;
	gem_pc_video_copy_bytes(gem_mono_segment(), source_offset,
		destination_offset, count, reverse);
}

/* Copy one partial-edge pixel with the adapter's native palette width. */
static void
gem_pc_copy_pixel(GEM_VDI_COORD destination_x,
	GEM_VDI_COORD destination_y, GEM_VDI_COORD source_x,
	GEM_VDI_COORD source_y)
{
	GEM_VDI_COLOR color;

	if (active_adapter == GEM_VIDEO_VGA || active_adapter == GEM_VIDEO_EGA) {
		color = gem_planar_read_pixel(source_x, source_y);
		gem_planar_write_pixel(destination_x, destination_y, color,
			GEM_VDI_REPLACE);
	} else {
		color = gem_mono_read_pixel(source_x, source_y);
		gem_mono_write_pixel(destination_x, destination_y, color,
			GEM_VDI_REPLACE);
	}
}

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

	/* Reach the first complete destination byte with at most seven pixels. */
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
			/* Right partial byte, complete bytes, then left partial. */
			index = (GEM_VDI_UWORD) width;
			while (index > boundary) {
				index--;
				gem_pc_copy_pixel(dst_x + index, destination_y,
					src_x + index, source_y);
			}
			if (full_bytes) {
				if (active_adapter == GEM_VIDEO_VGA
				    || active_adapter == GEM_VIDEO_EGA)
					gem_planar_copy_bytes(
						gem_pc_planar_offset(src_x + leading,
							source_y),
						gem_pc_planar_offset(dst_x + leading,
							destination_y),
						full_bytes, 1);
				else
					gem_mono_copy_bytes(
						gem_mono_offset(src_x + leading,
							source_y),
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
			/* Left partial byte, complete bytes, then right partial. */
			index = 0;
			while (index < leading) {
				gem_pc_copy_pixel(dst_x + index, destination_y,
					src_x + index, source_y);
				index++;
			}
			if (full_bytes) {
				if (active_adapter == GEM_VIDEO_VGA
				    || active_adapter == GEM_VIDEO_EGA)
					gem_planar_copy_bytes(
						gem_pc_planar_offset(src_x + leading,
							source_y),
						gem_pc_planar_offset(dst_x + leading,
							destination_y),
						full_bytes, 0);
				else
					gem_mono_copy_bytes(
						gem_mono_offset(src_x + leading,
							source_y),
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

void __attribute__((optimize("Os")))
gem_pc_video_set_palette(GEM_VDI_COLOR index, GEM_VDI_UBYTE red,
	GEM_VDI_UBYTE green, GEM_VDI_UBYTE blue)
{
	/*
	 * Only VGA mode 12h exposes the 256-entry, six-bit DAC selected through
	 * ports 3c8h and 3c9h.  The active resident display uses just sixteen
	 * indexes, but retain the complete byte-sized DAC selector at this hardware
	 * boundary.  Saturation is exact; there is no scale, rounding, multiply,
	 * divide, or wider compiler helper in this routine.
	 */
	if (active_adapter != GEM_VIDEO_VGA || index > 0x00ffU)
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

static GEM_VDI_WORD
gem_pc_open(GEM_VDI_SCREEN *screen)
{
	/* A mode set invalidates any bytes saved from an earlier workstation. */
	gem_cursor_save_active = 0;
	if (ioctl(0, DCGET_GRAPH) != 0) {
		/*
		 * Never program the adapter without owning ELKS's console graphics
		 * lock.  Returning failure lets AES keep its workstation suspended;
		 * the caller may retry after the kernel releases a dead owner.
		 */
		console_locked = 0;
		return 0;
	}
	console_locked = 1;

	saved_bios_mode = gem_bios_video_get_mode();
	active_adapter = gem_requested_adapter();

	screen->driver = &gem_pc_video_driver;
	if (active_adapter == GEM_VIDEO_VGA) {
		gem_bios_video_set_mode(0x12);
		screen->xres = 640;
		screen->yres = 480;
		screen->planes = 4;
		screen->colors = 16;
	} else if (active_adapter == GEM_VIDEO_EGA) {
		gem_bios_video_set_mode(0x10);
		screen->xres = 640;
		screen->yres = 350;
		screen->planes = 4;
		screen->colors = 16;
	} else if (active_adapter == GEM_VIDEO_CGA) {
		gem_bios_video_set_mode(0x06);
		screen->xres = 640;
		screen->yres = 200;
		screen->planes = 1;
		screen->colors = 2;
	} else {
		gem_hercules_open();
		screen->xres = 720;
		screen->yres = 348;
		screen->planes = 1;
		screen->colors = 2;
	}

	if (active_adapter == GEM_VIDEO_VGA || active_adapter == GEM_VIDEO_EGA) {
		/* Enable set/reset on every plane and select write/read mode zero. */
		outw(0x0f01, 0x3ce);
		outw(0x0003, 0x3ce);
		outw(0x0005, 0x3ce);
	}
	return 1;
}

static void
gem_pc_close(GEM_VDI_SCREEN *screen)
{
	(void) screen;
	if (active_adapter == GEM_VIDEO_HERCULES)
		gem_hercules_close();
	else
		gem_bios_video_set_mode(saved_bios_mode);
	if (console_locked) {
		ioctl(0, DCREL_GRAPH);
		console_locked = 0;
	}
}

static void
gem_pc_write_pixel(GEM_VDI_COORD x, GEM_VDI_COORD y,
	GEM_VDI_COLOR color, GEM_VDI_WORD mode)
{
	if (active_adapter == GEM_VIDEO_VGA || active_adapter == GEM_VIDEO_EGA)
		gem_planar_write_pixel(x, y, color, mode);
	else
		gem_mono_write_pixel(x, y, color, mode);
}

static GEM_VDI_COLOR
gem_pc_read_pixel(GEM_VDI_COORD x, GEM_VDI_COORD y)
{
	if (active_adapter == GEM_VIDEO_VGA || active_adapter == GEM_VIDEO_EGA)
		return gem_planar_read_pixel(x, y);
	return gem_mono_read_pixel(x, y);
}

static void
gem_pc_horizontal_line(GEM_VDI_COORD x1, GEM_VDI_COORD x2,
	GEM_VDI_COORD y, GEM_VDI_COLOR color, GEM_VDI_WORD mode)
{
	if (active_adapter == GEM_VIDEO_VGA || active_adapter == GEM_VIDEO_EGA)
		gem_planar_horizontal_line(x1, x2, y, color, mode);
	else
		gem_mono_horizontal_line(x1, x2, y, color, mode);
}

static void
gem_pc_vertical_line(GEM_VDI_COORD x, GEM_VDI_COORD y1,
	GEM_VDI_COORD y2, GEM_VDI_COLOR color, GEM_VDI_WORD mode)
{
	if (active_adapter == GEM_VIDEO_VGA || active_adapter == GEM_VIDEO_EGA)
		gem_planar_vertical_line(x, y1, y2, color, mode);
	else
		gem_mono_vertical_line(x, y1, y2, color, mode);
}

static void
gem_pc_fill_rect(GEM_VDI_COORD x1, GEM_VDI_COORD y1,
	GEM_VDI_COORD x2, GEM_VDI_COORD y2, GEM_VDI_COLOR color,
	GEM_VDI_WORD mode)
{
	if (active_adapter == GEM_VIDEO_VGA || active_adapter == GEM_VIDEO_EGA)
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
	if (active_adapter == GEM_VIDEO_VGA || active_adapter == GEM_VIDEO_EGA)
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
	if (active_adapter == GEM_VIDEO_VGA || active_adapter == GEM_VIDEO_EGA)
		gem_planar_glyph_replace(x1, y1, x2, y2, rows, source_x,
			source_left_bit, foreground);
	else
		gem_mono_glyph_replace(x1, y1, x2, y2, rows, source_x,
			source_left_bit, foreground);
}

static GEM_VDI_WORD __far __attribute__((far_section, noinline,
	section(".fartext.gem_pc_text_replace")))
gem_pc_text_replace(GEM_VDI_COORD x, GEM_VDI_COORD y,
	const GEM_VDI_UBYTE *characters, GEM_VDI_UWORD count,
	GEM_VDI_UWORD stride, GEM_VDI_UWORD font_segment,
	GEM_VDI_UWORD font_offset, GEM_VDI_COLOR foreground)
{
	GEM_VDI_UWORD video_offset;

	/*
	 * EGA and VGA share the original set/reset plus bit-mask contract.  Select
	 * it once for the complete string, compute the first byte offset once, and
	 * let the 8086 helper consume the unchanged BIOS glyphs directly.  CGA and
	 * Hercules retain gem_vdi_glyph(); their interleaved scan-line layouts do
	 * not match this fixed eighty-byte planar run.
	 */
	if (active_adapter != GEM_VIDEO_VGA && active_adapter != GEM_VIDEO_EGA)
		return 0;
	gem_planar_prepare(foreground, GEM_VDI_REPLACE);
	video_offset = gem_pc_planar_offset(x, y);
	gem_pc_planar_text_run(video_offset, characters, count, stride,
		font_segment, font_offset);
	return 1;
}

static void
gem_pc_fill_pattern(GEM_VDI_COORD x1, GEM_VDI_COORD y1,
	GEM_VDI_COORD x2, GEM_VDI_COORD y2, GEM_VDI_COLOR foreground,
	GEM_VDI_COLOR background, const GEM_VDI_UBYTE *pattern)
{
	if (active_adapter == GEM_VIDEO_VGA || active_adapter == GEM_VIDEO_EGA)
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
	/*
	 * The VDI removes a drawn cursor before requesting another one.  Refuse
	 * an unmatched draw instead of overwriting the sole fixed save area.
	 */
	if (gem_cursor_save_active || !gem_cursor_prepare(screen, x, y, cursor))
		return 0;

	if (active_adapter == GEM_VIDEO_VGA || active_adapter == GEM_VIDEO_EGA)
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
	if (active_adapter == GEM_VIDEO_VGA || active_adapter == GEM_VIDEO_EGA)
		gem_planar_cursor_restore();
	else
		gem_mono_cursor_restore();
	gem_cursor_save_active = 0;
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
	gem_pc_text_replace,
	gem_pc_fill_pattern,
	gem_pc_draw_cursor,
	gem_pc_restore_cursor
};
