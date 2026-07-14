/*
 * Internal interface for the original GEM PC display-driver port.
 *
 * The hardware methods use only 16-bit coordinates and palette indexes.
 * Screen memory is addressed through explicit segment and offset halves in
 * the implementation; no 32-bit linear address is formed by C code.
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
	/*
	 * Replace a clipped one-bit GEM form directly from its native words.
	 * Each source row contains words_per_row 16-bit words, and bit 15 is
	 * its word's leftmost pixel.  source_x selects the first visible source
	 * pixel after VDI clipping.  A clear source bit either selects background
	 * or leaves video untouched according to use_background.
	 */
	void (*bitmap_replace)(GEM_VDI_COORD x1, GEM_VDI_COORD y1,
		GEM_VDI_COORD x2, GEM_VDI_COORD y2,
		const GEM_VDI_BITS *bits, GEM_VDI_UWORD source_x,
		GEM_VDI_UWORD words_per_row, GEM_VDI_COLOR foreground,
		GEM_VDI_COLOR background, GEM_VDI_WORD use_background);
	/*
	 * Overlay one compact glyph directly from its native byte rows.  The
	 * source_left_bit argument identifies the glyph's leftmost bit in each
	 * row, and source_x is the number of columns removed by left clipping.
	 * Only set source bits replace video with foreground; clear bits remain
	 * transparent.  Rows already skipped by top clipping are omitted from
	 * the pointer passed by the VDI.
	 */
	void (*glyph_replace)(GEM_VDI_COORD x1, GEM_VDI_COORD y1,
		GEM_VDI_COORD x2, GEM_VDI_COORD y2,
		const GEM_VDI_UBYTE *rows, GEM_VDI_UWORD source_x,
		GEM_VDI_UBYTE source_left_bit, GEM_VDI_COLOR foreground);
	/*
	 * Overlay a complete aligned 8-by-16 BIOS-font run.  CHARACTERS is
	 * resident near data containing COUNT low-byte character codes separated
	 * by STRIDE bytes.  FONT_SEGMENT:FONT_OFFSET is the unchanged native BIOS
	 * font returned by INT 10h function 1130h.  A nonzero return means the
	 * driver consumed the run; zero asks the VDI to retain its glyph fallback.
	 */
	GEM_VDI_WORD (*text_replace)(GEM_VDI_COORD x, GEM_VDI_COORD y,
		const GEM_VDI_UBYTE *characters, GEM_VDI_UWORD count,
		GEM_VDI_UWORD stride, GEM_VDI_UWORD font_segment,
		GEM_VDI_UWORD font_offset, GEM_VDI_COLOR foreground);
	/*
	 * Replace a rectangle with a screen-aligned eight-by-eight pattern.
	 * The pointer addresses eight bytes; bit 7 corresponds to x modulo
	 * eight equal to zero.  Both colors are native driver palette indexes.
	 */
	void (*fill_pattern)(GEM_VDI_COORD x1, GEM_VDI_COORD y1,
		GEM_VDI_COORD x2, GEM_VDI_COORD y2,
		GEM_VDI_COLOR foreground, GEM_VDI_COLOR background,
		const GEM_VDI_UBYTE *pattern);
	/*
	 * Save the covered video bytes and draw a 16 by 16 GEM cursor without
	 * entering the ordinary per-pixel VDI path.  The PC driver keeps the
	 * save area in fixed near storage.  A nonzero return means that a
	 * matching restore_cursor call is required.
	 */
	GEM_VDI_WORD (*draw_cursor)(GEM_VDI_SCREEN *screen,
		GEM_VDI_COORD x, GEM_VDI_COORD y,
		const GEM_VDI_CURSOR *cursor);
	void (*restore_cursor)(void);
} GEM_VDI_DRIVER;

extern const GEM_VDI_DRIVER gem_pc_video_driver;

/*
 * Copy a physical-screen source rectangle with GEM's S_ONLY rule.  Equal
 * source and destination bit alignment is handled in native video bytes and
 * planes; other alignments return zero so the portable VDI fallback can run.
 */
GEM_VDI_WORD gem_pc_screen_blit(GEM_VDI_COORD dst_x,
	GEM_VDI_COORD dst_y, GEM_VDI_COORD width, GEM_VDI_COORD height,
	GEM_VDI_COORD src_x, GEM_VDI_COORD src_y);

/* Values returned by gem_bios_video_detect(). */
#define GEM_VIDEO_VGA	0
#define GEM_VIDEO_EGA	1
#define GEM_VIDEO_CGA	2
#define GEM_VIDEO_HERCULES	3

/*
 * Mode 12h stores exactly eighty bytes in each 640-pixel scan line.  The
 * largest visible byte is x=639, y=479, which is offset 38399.  Adding one
 * row to that final visible offset produces 38479, so even the harmless
 * one-past-row value formed after a completed loop cannot wrap a 16-bit
 * segment offset.  Callers must still clip coordinates to the physical
 * screen before forming the first offset.
 *
 * Keep the row advance as an expression instead of a C function: the
 * bitmap and cursor loops then compile to one 8088/8086 ADD instruction and
 * do not pay a medium-model far call for every scan line.
 */
#define GEM_PC_PLANAR_ROW_BYTES	80U
#define GEM_PC_PLANAR_LAST_VISIBLE_OFFSET	38399U
#define GEM_PC_PLANAR_NEXT_ROW(offset) \
	((GEM_VDI_UWORD) ((GEM_VDI_UWORD) (offset) \
		+ GEM_PC_PLANAR_ROW_BYTES))

/* 8086 BIOS helpers implemented in gem_pcvideo_asm.S. */
GEM_VDI_UWORD gem_bios_video_detect(void);
GEM_VDI_UWORD gem_bios_video_get_mode(void);
void gem_bios_video_set_mode(GEM_VDI_UWORD mode);

/*
 * Program one VGA DAC entry without changing the framebuffer index stored in
 * video memory.  The red, green, and blue inputs are native six-bit DAC
 * values in the inclusive range zero through 63; larger inputs saturate at
 * 63.  EGA, CGA, and Hercules adapters have no VGA DAC, so the routine is a
 * deliberate hardware no-op on those adapters while the resident VDI keeps
 * the requested logical palette for VQ_COLOR.  The port and component writes
 * are individual 8088/8086 byte I/O operations and need no temporary buffer.
 */
void gem_pc_video_set_palette(GEM_VDI_COLOR index,
	GEM_VDI_UBYTE red, GEM_VDI_UBYTE green, GEM_VDI_UBYTE blue);

/*
 * Return the BIOS ROM's native VGA 8 by 16 character table as an explicit
 * segment and offset pair.  Both outputs are unscaled 16-bit addresses; the
 * caller copies glyph bytes without forming a flat or wide pointer.
 */
void gem_bios_video_font_8x16(GEM_VDI_UWORD *segment,
	GEM_VDI_UWORD *offset);

/*
 * Scanline address helpers implemented with explicit 8086 single-bit shifts.
 * Keeping these in assembly prevents the size optimizer from recognizing the
 * shift/add expressions as multiplication and emitting a slow MUL instruction
 * in the pixel hot path.
 */
GEM_VDI_UWORD gem_pc_planar_offset(GEM_VDI_COORD x, GEM_VDI_COORD y);
GEM_VDI_UWORD gem_pc_cga_offset(GEM_VDI_COORD x, GEM_VDI_COORD y);
GEM_VDI_UWORD gem_pc_hercules_offset(GEM_VDI_COORD x, GEM_VDI_COORD y);
GEM_VDI_UWORD gem_pc_byte_column(GEM_VDI_COORD x);

/*
 * Copy count bytes inside one physical video segment.  The forward path uses
 * the original 8088/8086 REP MOVSB instruction; the reverse path walks down
 * explicitly so the direction flag is never exposed to an ELKS signal.
 * Offsets and count are unscaled 16-bit values.  Callers clip every transfer
 * to one scan line, so neither direction can wrap the segment.
 */
void gem_pc_video_copy_bytes(GEM_VDI_UWORD segment,
	GEM_VDI_UWORD source_offset, GEM_VDI_UWORD destination_offset,
	GEM_VDI_UWORD count, GEM_VDI_WORD reverse);

/*
 * Fill complete planar bytes in consecutive 80-byte EGA/VGA scan lines.
 * When read_latches is zero, prepared replace mode permits REP STOSB.  A
 * nonzero value performs one LODSB before each STOSB for XOR/OR/AND modes.
 * All scale factors are one byte or one row; rows and offsets are bounded by
 * the 640-pixel VGA geometry and cannot overflow a 16-bit segment offset.
 */
void gem_pc_planar_fill_rows(GEM_VDI_UWORD offset,
	GEM_VDI_UWORD row_bytes, GEM_VDI_UWORD rows,
	GEM_VDI_WORD read_latches);

/*
 * Overlay one byte-aligned native 8-pixel glyph in planar video memory.
 * ROWS addresses one unchanged BIOS-font byte per scan line.  OFFSET is the
 * first A000 video byte and ROW_COUNT is bounded by the sixteen-row system
 * font.  The helper advances the video offset by exactly eighty bytes after
 * each source byte.  It programs only graphics-controller bit mask register
 * eight; the C caller has already selected replace mode and the foreground
 * set/reset color.  All arguments and intermediate offsets are 16-bit, and
 * the final unused successor cannot exceed 38479.
 */
void gem_pc_planar_glyph_rows(GEM_VDI_UWORD offset,
	const GEM_VDI_UBYTE *rows, GEM_VDI_UWORD row_count);

/*
 * Overlay an aligned string directly from the native BIOS 8-by-16 table.
 * CHARACTERS is near resident data, STRIDE is exactly one or two bytes, and
 * COUNT is at most eighty.  The helper preserves every segment register and
 * uses only 8088/8086 instructions.  Each character advances one video byte;
 * each glyph row advances eighty video bytes.  Font-offset carry adds 1000h
 * to the explicit segment half so no wide or normalized pointer is formed.
 */
void gem_pc_planar_text_run(GEM_VDI_UWORD offset,
	const GEM_VDI_UBYTE *characters, GEM_VDI_UWORD count,
	GEM_VDI_UWORD stride, GEM_VDI_UWORD font_segment,
	GEM_VDI_UWORD font_offset);

#endif /* ELKS_GEM_PCVIDEO_H */
