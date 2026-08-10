/*
 * gem_video_mono.h - shared 1-bit drawing engine for CGA and Hercules
 *
 * Copyright 1999 Caldera Thin Clients, Inc.
 * Copyright 1987 Digital Research, Inc.
 *
 * CGA and Hercules both keep the screen as one bit per pixel in plain
 * memory so they share the drawing code, only the segment and
 * scan-line layout differ per adapter
 */

#ifndef ELKS_GEM_VIDEO_MONO_H
#define ELKS_GEM_VIDEO_MONO_H

#include "gem_video_common.h"

/* which segment and scan-line layout the active adapter uses */
GEM_VDI_UWORD gem_mono_segment(void);
GEM_VDI_UWORD gem_mono_offset(GEM_VDI_COORD x, GEM_VDI_COORD y);

void gem_mono_write_pixel(GEM_VDI_COORD x, GEM_VDI_COORD y,
	GEM_VDI_COLOR color, GEM_VDI_WORD mode);
GEM_VDI_COLOR gem_mono_read_pixel(GEM_VDI_COORD x, GEM_VDI_COORD y);
void gem_mono_horizontal_line(GEM_VDI_COORD x1, GEM_VDI_COORD x2,
	GEM_VDI_COORD y, GEM_VDI_COLOR color, GEM_VDI_WORD mode);
void gem_mono_vertical_line(GEM_VDI_COORD x, GEM_VDI_COORD y1,
	GEM_VDI_COORD y2, GEM_VDI_COLOR color, GEM_VDI_WORD mode);
void gem_mono_fill_rect(GEM_VDI_COORD x1, GEM_VDI_COORD y1,
	GEM_VDI_COORD x2, GEM_VDI_COORD y2, GEM_VDI_COLOR color,
	GEM_VDI_WORD mode);
void gem_mono_fill_pattern(GEM_VDI_COORD x1, GEM_VDI_COORD y1,
	GEM_VDI_COORD x2, GEM_VDI_COORD y2, GEM_VDI_COLOR foreground,
	GEM_VDI_COLOR background, const GEM_VDI_UBYTE *pattern);
void gem_mono_bitmap_replace(GEM_VDI_COORD x1, GEM_VDI_COORD y1,
	GEM_VDI_COORD x2, GEM_VDI_COORD y2, const GEM_VDI_BITS *bits,
	GEM_VDI_UWORD source_x, GEM_VDI_UWORD words_per_row,
	GEM_VDI_COLOR foreground, GEM_VDI_COLOR background,
	GEM_VDI_WORD use_background);
void gem_mono_glyph_replace(GEM_VDI_COORD x1, GEM_VDI_COORD y1,
	GEM_VDI_COORD x2, GEM_VDI_COORD y2, const GEM_VDI_UBYTE *rows,
	GEM_VDI_UWORD source_x, GEM_VDI_UBYTE source_left_bit,
	GEM_VDI_COLOR foreground);

/* copy whole bytes, used by the screen-to-screen blit */
void gem_mono_copy_bytes(GEM_VDI_UWORD source_offset,
	GEM_VDI_UWORD destination_offset, GEM_VDI_UWORD count,
	GEM_VDI_WORD reverse);

void gem_mono_cursor_draw(const GEM_VDI_CURSOR *cursor);
void gem_mono_cursor_restore(void);

#endif				/* ELKS_GEM_VIDEO_MONO_H */
