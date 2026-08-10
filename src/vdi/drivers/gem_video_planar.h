/*
 * gem_video_planar.h - planar EGA/VGA driver interface
 *
 * Copyright 1999 Caldera Thin Clients, Inc.
 * Copyright 1987 Digital Research, Inc.
 *
 * one driver covers both EGA and VGA, they differ only in BIOS mode
 * and row count
 */

#ifndef ELKS_GEM_VIDEO_PLANAR_H
#define ELKS_GEM_VIDEO_PLANAR_H

#include "gem_video_common.h"

void gem_planar_open(GEM_VDI_SCREEN *screen, GEM_VDI_UWORD adapter);

void gem_planar_write_pixel(GEM_VDI_COORD x, GEM_VDI_COORD y,
	GEM_VDI_COLOR color, GEM_VDI_WORD mode);
GEM_VDI_COLOR gem_planar_read_pixel(GEM_VDI_COORD x, GEM_VDI_COORD y);
void gem_planar_horizontal_line(GEM_VDI_COORD x1, GEM_VDI_COORD x2,
	GEM_VDI_COORD y, GEM_VDI_COLOR color, GEM_VDI_WORD mode);
void gem_planar_vertical_line(GEM_VDI_COORD x, GEM_VDI_COORD y1,
	GEM_VDI_COORD y2, GEM_VDI_COLOR color, GEM_VDI_WORD mode);
void gem_planar_fill_rect(GEM_VDI_COORD x1, GEM_VDI_COORD y1,
	GEM_VDI_COORD x2, GEM_VDI_COORD y2, GEM_VDI_COLOR color,
	GEM_VDI_WORD mode);
void gem_planar_fill_pattern(GEM_VDI_COORD x1, GEM_VDI_COORD y1,
	GEM_VDI_COORD x2, GEM_VDI_COORD y2, GEM_VDI_COLOR foreground,
	GEM_VDI_COLOR background, const GEM_VDI_UBYTE *pattern);
void gem_planar_bitmap_replace(GEM_VDI_COORD x1, GEM_VDI_COORD y1,
	GEM_VDI_COORD x2, GEM_VDI_COORD y2, const GEM_VDI_BITS *bits,
	GEM_VDI_UWORD source_x, GEM_VDI_UWORD words_per_row,
	GEM_VDI_COLOR foreground, GEM_VDI_COLOR background,
	GEM_VDI_WORD use_background);
void gem_planar_glyph_replace(GEM_VDI_COORD x1, GEM_VDI_COORD y1,
	GEM_VDI_COORD x2, GEM_VDI_COORD y2, const GEM_VDI_UBYTE *rows,
	GEM_VDI_UWORD source_x, GEM_VDI_UBYTE source_left_bit,
	GEM_VDI_COLOR foreground);

/* zero when the caller should fall back to per-glyph drawing */
GEM_VDI_WORD __far gem_planar_text_replace(GEM_VDI_COORD x,
	GEM_VDI_COORD y, const GEM_VDI_UBYTE *characters,
	GEM_VDI_UWORD count, GEM_VDI_UWORD stride,
	GEM_VDI_UWORD font_segment, GEM_VDI_UWORD font_offset,
	GEM_VDI_COLOR foreground);

void gem_planar_copy_bytes(GEM_VDI_UWORD source_offset,
	GEM_VDI_UWORD destination_offset, GEM_VDI_UWORD count,
	GEM_VDI_WORD reverse);

void gem_planar_cursor_draw(const GEM_VDI_CURSOR *cursor);
void gem_planar_cursor_restore(void);

#endif				/* ELKS_GEM_VIDEO_PLANAR_H */
