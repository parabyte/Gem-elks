/*
 * gem_vdi_font.h - GEM font chain and .FNT loading for the ELKS VDI
 *
 * the record below is the GEM font header with the three data pointers spelled
 * as an 8086 segment plus three offsets, since loaded font data lives in its
 * own far segment here.
 *
 * the chain is ordered by face id, then point size inside a face, then style
 * inside a size, and font_id keeps the face in the low byte and style bits in
 * the high byte
 */

#ifndef ELKS_GEM_VDI_FONT_H
#define ELKS_GEM_VDI_FONT_H

#include "aes.h"
#include "vdi.h"

/* the font header flag bits */
#define GEM_VDI_FONT_DEFAULT	0x0001	/* default face and size */
#define GEM_VDI_FONT_HORZ_OFF	0x0002	/* left/right offset tables present */
#define GEM_VDI_FONT_STDFORM	0x0004	/* data is in motorola byte order */
#define GEM_VDI_FONT_MONOSPACE	0x0008	/* every cell is the same width */

/* vst_effects style bits */
#define GEM_VDI_STYLE_THICKEN	0x0001
#define GEM_VDI_STYLE_LIGHT	0x0002
#define GEM_VDI_STYLE_SKEW	0x0004
#define GEM_VDI_STYLE_UNDER	0x0008
#define GEM_VDI_STYLE_OUTLINE	0x0010
#define GEM_VDI_STYLE_SHADOW	0x0020
#define GEM_VDI_STYLE_ROTATE	0x00c0	/* rotation, shifted left six */
#define GEM_VDI_STYLE_SCALE	0x0100	/* the header was built by scaling */

/* the five style bits vst_effects itself accepts */
#define GEM_VDI_STYLE_MASK	0x001f

/* table sizes. a face keeps one name, every size and style of that face is a separate font record */
#define GEM_VDI_FONTS		24
#define GEM_VDI_FACES		8
#define GEM_VDI_FONT_NAME	32

/* widest and tallest cell the rasteriser will compose */
#define GEM_VDI_FONT_MAX_CELL	32
#define GEM_VDI_FONT_MAX_ROWS	32

/* the longest font directory the loader will hold */
#define GEM_VDI_FONT_DIRECTORY_MAX	48U

typedef struct gem_vdi_font {
	UWORD font_id;		/* face in the low byte, style in the high */
	WORD point;
	UWORD first_ade;
	UWORD last_ade;
	UWORD top;
	UWORD ascent;
	UWORD half;
	UWORD descent;
	UWORD bottom;
	UWORD max_char_width;
	UWORD max_cell_width;
	UWORD left_offset;	/* pixels the cell slants left when skewed */
	UWORD right_offset;	/* pixels it slants right */
	UWORD thicken;		/* pixels to smear for THICKEN */
	UWORD ul_size;		/* underline thickness */
	UWORD lighten;		/* mask anded in for LIGHT */
	UWORD skew;		/* mask that steps the SKEW shift */
	UWORD flags;
	UWORD form_width;	/* bytes across one row of the strip */
	UWORD form_height;	/* rows in the strip */

	/* data_segment is zero for a font thats not resident. the system font carries the resident's own segment and rom_rows nonzero (one byte per row per glyph), a loaded .FNT carries its own far segment and the three table offsets inside it */
	UWORD data_segment;
	UWORD hor_offset;
	UWORD off_offset;
	UWORD dat_offset;
	UWORD rom_rows;		/* nonzero: ROM layout, rom_rows per glyph */
	UWORD face_index;	/* which name in the face table */
	UWORD file_index;	/* which far allocation backs it, or 0xffff */
} GEM_VDI_FONT;

/* reset the chain to just the system font. rows is 16 on VGA, 14 on EGA and Hercules, 8 on CGA, the caller passes the segment and offset of the matching embedded system-font table */
VOID gem_vdi_font_reset(UWORD rom_segment, UWORD rom_offset, UWORD rom_rows);

/* the head of the chain, always the system font */
GEM_VDI_FONT *gem_vdi_font_first(VOID);

/* walk the chain in face, size, style order, null ends it */
GEM_VDI_FONT *gem_vdi_font_next(const GEM_VDI_FONT *font);

/* faces and sizes for the vq_extnd device table */
UWORD gem_vdi_font_face_count(VOID);
UWORD gem_vdi_font_size_count(VOID);

/* the 32-byte nul-padded face name for a one-based face index */
const BYTE *gem_vdi_font_face_name(UWORD index, UWORD *face_id);

/* where vst_load_fonts looks for *.FNT. the resident sets this from the AES's own search path instead of writing a directory here */
VOID gem_vdi_font_set_directory(const BYTE *path);

/* load every *.FNT in that directory not already in the chain. returns the number of extra faces, like vst_load_fonts does */
WORD gem_vdi_font_load_all(VOID);

/* drop every loaded font, leaving only the system font */
VOID gem_vdi_font_unload_all(VOID);

/* read one glyph's rows out of a font into rows[], which must hold GEM_VDI_FONT_MAX_ROWS entries of stride bytes. returns the glyph width in pixels, zero when the code isnt in the font. bits are left aligned in each row, bit 7 of rows[r * stride] is the leftmost pixel */
UWORD gem_vdi_font_glyph(const GEM_VDI_FONT *font, UWORD character,
	GEM_VDI_UBYTE *rows, UWORD stride);

/* cell advance for one character, before THICKEN and SKEW padding */
UWORD gem_vdi_font_char_width(const GEM_VDI_FONT *font, UWORD character);

/* left and right kerning for one character, zero without HORZ_OFF */
VOID gem_vdi_font_char_offsets(const GEM_VDI_FONT *font, UWORD character,
	WORD *left, WORD *right);

#endif				/* ELKS_GEM_VDI_FONT_H */
