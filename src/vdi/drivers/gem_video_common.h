/*
 * gem_video_common.h - shared state for the PC display drivers
 *
 * Copyright 1999 Caldera Thin Clients, Inc.
 * Copyright 1987 Digital Research, Inc.
 */

#ifndef ELKS_GEM_VIDEO_COMMON_H
#define ELKS_GEM_VIDEO_COMMON_H

#include "gem_pcvideo.h"

/* screen memory segments */
#define GEM_VIDEO_SEG_PLANAR	0xa000
#define GEM_VIDEO_SEG_CGA	0xb800
#define GEM_VIDEO_SEG_HERCULES	0xb000

/* scan-line bytes on the 1-bit adapters */
#define GEM_VIDEO_BYTES_CGA	80
#define GEM_VIDEO_BYTES_HERCULES	90

/* an unaligned 16-pixel cursor touches three video bytes per row, so the save area keeps 3 bytes x 16 rows per EGA/VGA plane */
#define GEM_CURSOR_WIDTH	16
#define GEM_CURSOR_HEIGHT	16
#define GEM_CURSOR_ROW_BYTES	3
#define GEM_CURSOR_PLANE_SAVE_BYTES	192
#define GEM_CURSOR_MASK_BYTES	48

/* a far pointer seen as its two 16-bit halves */
typedef union gem_video_address {
	volatile unsigned char __far *pointer;
	struct {
		unsigned short offset;
		unsigned short segment;
	} words;
} GEM_VIDEO_ADDRESS;

typedef char gem_video_far_pointer_must_be_four_bytes
	[(sizeof(GEM_VIDEO_ADDRESS) == 4) ? 1 : -1];

/* adapter gem_pc_open() picked */
extern GEM_VDI_UWORD gem_video_adapter;

/* bit 7 is the leftmost pixel in a video byte */
extern const GEM_VDI_UBYTE gem_video_bit_mask[8];

/* saved screen bytes under the cursor, plus its prepared masks */
extern GEM_VDI_UBYTE gem_cursor_save[GEM_CURSOR_PLANE_SAVE_BYTES];
extern GEM_VDI_UBYTE gem_cursor_masks[GEM_CURSOR_MASK_BYTES];
extern GEM_VDI_UBYTE gem_cursor_images[GEM_CURSOR_MASK_BYTES];
extern GEM_VDI_COORD gem_cursor_save_left;
extern GEM_VDI_COORD gem_cursor_save_top;
extern GEM_VDI_UWORD gem_cursor_save_first_byte;
extern GEM_VDI_UWORD gem_cursor_save_rows;
extern GEM_VDI_UWORD gem_cursor_save_bytes;

static volatile unsigned char __far *
gem_video_pointer(GEM_VDI_UWORD segment, GEM_VDI_UWORD offset)
{
	GEM_VIDEO_ADDRESS address;

	address.words.offset = offset;
	address.words.segment = segment;
	return address.pointer;
}

#define gem_pixel_mask(x) \
	(gem_video_bit_mask[(GEM_VDI_UWORD) (x) & 7])

/* clip the cursor and fill in gem_cursor_save_* state, zero when its off screen or the wrong size */
GEM_VDI_WORD gem_cursor_prepare(GEM_VDI_SCREEN *screen, GEM_VDI_COORD x,
	GEM_VDI_COORD y, const GEM_VDI_CURSOR *cursor);

#endif				/* ELKS_GEM_VIDEO_COMMON_H */
