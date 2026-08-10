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
	/* draw a clipped one-bit form, each source row is words_per_row words bit 15 leftmost, source_x is the first visible pixel, a clear bit paints background or leaves the screen, per use_background */
	void (*bitmap_replace)(GEM_VDI_COORD x1, GEM_VDI_COORD y1,
		GEM_VDI_COORD x2, GEM_VDI_COORD y2,
		const GEM_VDI_BITS *bits, GEM_VDI_UWORD source_x,
		GEM_VDI_UWORD words_per_row, GEM_VDI_COLOR foreground,
		GEM_VDI_COLOR background, GEM_VDI_WORD use_background);
	/* draw one glyph, source_left_bit is each row's leftmost bit, source_x the columns clipped left, clear bits stay transparent, rows clipped off the top already skipped */
	void (*glyph_replace)(GEM_VDI_COORD x1, GEM_VDI_COORD y1,
		GEM_VDI_COORD x2, GEM_VDI_COORD y2,
		const GEM_VDI_UBYTE *rows, GEM_VDI_UWORD source_x,
		GEM_VDI_UBYTE source_left_bit, GEM_VDI_COLOR foreground);
	/* draw a byte-aligned run of 8x16 system-font chars stride bytes apart, from font_segment:font_offset, zero return means fall back to single glyphs */
	GEM_VDI_WORD (*text_replace)(GEM_VDI_COORD x, GEM_VDI_COORD y,
		const GEM_VDI_UBYTE *characters, GEM_VDI_UWORD count,
		GEM_VDI_UWORD stride, GEM_VDI_UWORD font_segment,
		GEM_VDI_UWORD font_offset, GEM_VDI_COLOR foreground);
	/* fill with a screen-aligned 8x8 pattern, bit 7 lines up with x mod 8 == 0 */
	void (*fill_pattern)(GEM_VDI_COORD x1, GEM_VDI_COORD y1,
		GEM_VDI_COORD x2, GEM_VDI_COORD y2,
		GEM_VDI_COLOR foreground, GEM_VDI_COLOR background,
		const GEM_VDI_UBYTE *pattern);
	/* save the covered bytes and draw a 16x16 cursor, nonzero return means a restore_cursor is owed */
	GEM_VDI_WORD (*draw_cursor)(GEM_VDI_SCREEN *screen,
		GEM_VDI_COORD x, GEM_VDI_COORD y, const GEM_VDI_CURSOR *cursor);
	void (*restore_cursor)(void);
} GEM_VDI_DRIVER;

extern const GEM_VDI_DRIVER gem_pc_video_driver;

/* screen-to-screen copy, returns zero when bit alignments differ so the VDI fallback runs */
GEM_VDI_WORD gem_pc_screen_blit(GEM_VDI_COORD dst_x,
	GEM_VDI_COORD dst_y, GEM_VDI_COORD width, GEM_VDI_COORD height,
	GEM_VDI_COORD src_x, GEM_VDI_COORD src_y);

/* gem_bios_video_detect() results */
#define GEM_VIDEO_VGA	0
#define GEM_VIDEO_EGA	1
#define GEM_VIDEO_CGA	2
#define GEM_VIDEO_HERCULES	3

/* a planar scan line is 80 bytes, last visible byte in mode 12h is 38399 so clipped offsets never wrap 16 bits */
#define GEM_PC_PLANAR_ROW_BYTES	80U
#define GEM_PC_PLANAR_LAST_VISIBLE_OFFSET	38399U
#define GEM_PC_PLANAR_NEXT_ROW(offset) \
	((GEM_VDI_UWORD) ((GEM_VDI_UWORD) (offset) \
		+ GEM_PC_PLANAR_ROW_BYTES))

/* BIOS helpers, in gem_pcvideo_asm.S */
GEM_VDI_UWORD gem_bios_video_detect(void);
GEM_VDI_UWORD gem_bios_video_get_mode(void);
void gem_bios_video_set_mode(GEM_VDI_UWORD mode);

/* program one VGA DAC entry, red/green/blue are six-bit, EGA/CGA/Hercules have no DAC so its a no-op there */
void gem_pc_video_set_palette(GEM_VDI_COLOR index,
	GEM_VDI_UBYTE red, GEM_VDI_UBYTE green, GEM_VDI_UBYTE blue);

/* scanline address helpers, in gem_pcvideo_asm.S */
GEM_VDI_UWORD gem_pc_planar_offset(GEM_VDI_COORD x, GEM_VDI_COORD y);
GEM_VDI_UWORD gem_pc_cga_offset(GEM_VDI_COORD x, GEM_VDI_COORD y);
GEM_VDI_UWORD gem_pc_hercules_offset(GEM_VDI_COORD x, GEM_VDI_COORD y);
GEM_VDI_UWORD gem_pc_byte_column(GEM_VDI_COORD x);

/* copy count bytes inside one video segment, reverse copies walk down by hand so the direction flag is never left set for an ELKS signal to trip on */
void gem_pc_video_copy_bytes(GEM_VDI_UWORD segment,
	GEM_VDI_UWORD source_offset, GEM_VDI_UWORD destination_offset,
	GEM_VDI_UWORD count, GEM_VDI_WORD reverse);

/* fill whole bytes in consecutive 80-byte planar rows, nonzero read_latches does a LODSB before each STOSB so XOR/OR/AND modes see the old screen */
void gem_pc_planar_fill_rows(GEM_VDI_UWORD offset,
	GEM_VDI_UWORD row_bytes, GEM_VDI_UWORD rows, GEM_VDI_WORD read_latches);

/* draw one byte-aligned 8-wide glyph, one font byte per scan line, C caller already set replace mode and foreground color */
void gem_pc_planar_glyph_rows(GEM_VDI_UWORD offset,
	const GEM_VDI_UBYTE *rows, GEM_VDI_UWORD row_count);

/* draw an aligned string from the 8x16 system font, characters is near data, stride one or two bytes, count at most eighty */
void gem_pc_planar_text_run(GEM_VDI_UWORD offset,
	const GEM_VDI_UBYTE *characters, GEM_VDI_UWORD count,
	GEM_VDI_UWORD stride, GEM_VDI_UWORD font_segment,
	GEM_VDI_UWORD font_offset);

#endif				/* ELKS_GEM_PCVIDEO_H */
