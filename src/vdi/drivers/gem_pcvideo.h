/*
 * gem_pcvideo.h - PC display driver interface
 *
 * Copyright 1999 Caldera Thin Clients, Inc.
 * Copyright 1987 Digital Research, Inc.
 *
 * screen memory is an explicit segment and offset pair, no 32-bit
 * address is formed in C
 */

#ifndef ELKS_GEM_PCVIDEO_H
#define ELKS_GEM_PCVIDEO_H

#include "vdi.h"

typedef struct gem_vdi_driver {
	const char *name;
	GEM_VDI_WORD (*open)(GEM_VDI_SCREEN *screen);
	void (*close)(GEM_VDI_SCREEN *screen);
	void (*write_pixel)(GEM_VDI_COORD x, GEM_VDI_COORD y,
		GEM_VDI_COLOR color, GEM_VDI_WORD mode);
	GEM_VDI_COLOR (*read_pixel)(GEM_VDI_COORD x, GEM_VDI_COORD y);
	void (*horizontal_line)(GEM_VDI_COORD x1, GEM_VDI_COORD x2,
		GEM_VDI_COORD y, GEM_VDI_COLOR color, GEM_VDI_WORD mode);
	void (*vertical_line)(GEM_VDI_COORD x, GEM_VDI_COORD y1,
		GEM_VDI_COORD y2, GEM_VDI_COLOR color, GEM_VDI_WORD mode);
	void (*fill_rect)(GEM_VDI_COORD x1, GEM_VDI_COORD y1,
		GEM_VDI_COORD x2, GEM_VDI_COORD y2,
		GEM_VDI_COLOR color, GEM_VDI_WORD mode);
	/* draw a clipped one-bit form */
	void (*bitmap_replace)(GEM_VDI_COORD x1, GEM_VDI_COORD y1,
		GEM_VDI_COORD x2, GEM_VDI_COORD y2,
		const GEM_VDI_BITS *bits, GEM_VDI_UWORD source_x,
		GEM_VDI_UWORD words_per_row, GEM_VDI_COLOR foreground,
		GEM_VDI_COLOR background, GEM_VDI_WORD use_background);
	/* draw one clipped glyph */
	void (*glyph_replace)(GEM_VDI_COORD x1, GEM_VDI_COORD y1,
		GEM_VDI_COORD x2, GEM_VDI_COORD y2,
		const GEM_VDI_UBYTE *rows, GEM_VDI_UWORD source_x,
		GEM_VDI_UBYTE source_left_bit, GEM_VDI_COLOR foreground);
	/* draw an aligned 8x16 string, or return zero */
	GEM_VDI_WORD (*text_replace)(GEM_VDI_COORD x, GEM_VDI_COORD y,
		const GEM_VDI_UBYTE *characters, GEM_VDI_UWORD count,
		GEM_VDI_UWORD stride, GEM_VDI_UWORD font_segment,
		GEM_VDI_UWORD font_offset, GEM_VDI_COLOR foreground);
	/* fill with a screen-aligned 8x8 pattern, bit 7 lines up with x mod 8 == 0 */
	void (*fill_pattern)(GEM_VDI_COORD x1, GEM_VDI_COORD y1,
		GEM_VDI_COORD x2, GEM_VDI_COORD y2,
		GEM_VDI_COLOR foreground, GEM_VDI_COLOR background,
		const GEM_VDI_UBYTE *pattern);
	/* save under and draw a 16x16 cursor */
	GEM_VDI_WORD (*draw_cursor)(GEM_VDI_SCREEN *screen,
		GEM_VDI_COORD x, GEM_VDI_COORD y, const GEM_VDI_CURSOR *cursor);
	void (*restore_cursor)(void);
} GEM_VDI_DRIVER;

extern const GEM_VDI_DRIVER gem_pc_video_driver;

/* copy aligned screen bytes */
GEM_VDI_WORD gem_pc_screen_blit(GEM_VDI_COORD dst_x,
	GEM_VDI_COORD dst_y, GEM_VDI_COORD width, GEM_VDI_COORD height,
	GEM_VDI_COORD src_x, GEM_VDI_COORD src_y);

/* gem_bios_video_detect() results */
#define GEM_VIDEO_VGA	0
#define GEM_VIDEO_EGA	1
#define GEM_VIDEO_CGA	2
#define GEM_VIDEO_HERCULES	3
#define GEM_VIDEO_PEGA	4

/* planar rows are 80 bytes */
#define GEM_PC_PLANAR_ROW_BYTES	80U
#define GEM_PC_PLANAR_LAST_VISIBLE_OFFSET	38399U
#define GEM_PC_PLANAR_NEXT_ROW(offset) \
	((GEM_VDI_UWORD) ((GEM_VDI_UWORD) (offset) \
		+ GEM_PC_PLANAR_ROW_BYTES))

/* BIOS helpers, in gem_pcvideo_asm.S */
GEM_VDI_UWORD gem_bios_video_detect(void);
GEM_VDI_UWORD gem_bios_video_get_mode(void);
void gem_bios_video_set_mode(GEM_VDI_UWORD mode);
unsigned long gem_bios_font_address(GEM_VDI_UWORD selector);

GEM_VDI_WORD gem_pc_video_font(GEM_VDI_UWORD wanted_rows,
	GEM_VDI_UWORD *rows, GEM_VDI_UWORD *segment,
	GEM_VDI_UWORD *offset, GEM_VDI_UWORD *upper_segment,
	GEM_VDI_UWORD *upper_offset);

/* program one six-bit VGA DAC entry */
void gem_pc_video_set_palette(GEM_VDI_COLOR index,
	GEM_VDI_UBYTE red, GEM_VDI_UBYTE green, GEM_VDI_UBYTE blue);

/* scanline address helpers, in gem_pcvideo_asm.S */
GEM_VDI_UWORD gem_pc_planar_offset(GEM_VDI_COORD x, GEM_VDI_COORD y);
GEM_VDI_UWORD gem_pc_cga_offset(GEM_VDI_COORD x, GEM_VDI_COORD y);
GEM_VDI_UWORD gem_pc_hercules_offset(GEM_VDI_COORD x, GEM_VDI_COORD y);
GEM_VDI_UWORD gem_pc_byte_column(GEM_VDI_COORD x);

/* copy bytes inside one video segment */
void gem_pc_video_copy_bytes(GEM_VDI_UWORD segment,
	GEM_VDI_UWORD source_offset, GEM_VDI_UWORD destination_offset,
	GEM_VDI_UWORD count, GEM_VDI_WORD reverse);

/* fill bytes across planar rows */
void gem_pc_planar_fill_rows(GEM_VDI_UWORD offset,
	GEM_VDI_UWORD row_bytes, GEM_VDI_UWORD rows, GEM_VDI_WORD read_latches);

/* draw one aligned 8-pixel glyph */
void gem_pc_planar_glyph_rows(GEM_VDI_UWORD offset,
	const GEM_VDI_UBYTE *rows, GEM_VDI_UWORD row_count);

/* draw an aligned 8x16 string */
void gem_pc_planar_text_run(GEM_VDI_UWORD offset,
	const GEM_VDI_UBYTE *characters, GEM_VDI_UWORD count,
	GEM_VDI_UWORD stride, GEM_VDI_UWORD font_segment,
	GEM_VDI_UWORD font_offset);

#endif				/* ELKS_GEM_PCVIDEO_H */
