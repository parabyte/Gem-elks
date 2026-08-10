/*
 * gem_vdi_text.h - VDI text attributes, metrics and rasterising
 *
 * draws text and handles fonts: picks the face, size and style, pulls each
 * character out of the font and draws it with bold, italic, underline, scaling
 * and rotation
 */

#ifndef ELKS_GEM_VDI_TEXT_H
#define ELKS_GEM_VDI_TEXT_H

#include "gem_vdi_font.h"

/* widest and tallest composed cell. a 32-pixel cell doubled with skew and thicken padding still fits 64 across */
#define GEM_VDI_TEXT_CELL_BYTES	8
#define GEM_VDI_TEXT_CELL_ROWS	64

/* bind the chain to the adapter ROM font and pick the default face */
VOID gem_vdi_text_init(UWORD rom_segment, UWORD rom_offset, UWORD rom_rows);

/* vst_height: size given in pixels above the baseline */
VOID gem_vdi_text_set_height(WORD pixels, UWORD *ptsout);

/* vst_point: size given in points, returns the realised point size */
WORD gem_vdi_text_set_point(WORD points, UWORD *ptsout);

/* vst_font: returns the realised face id */
WORD gem_vdi_text_set_font(WORD face);

/* vst_effects: only THICKEN, LIGHT, SKEW and UNDER exist here */
WORD gem_vdi_text_set_effects(WORD bits);

/* vst_rotation: snapped to the nearest right angle, in tenths */
WORD gem_vdi_text_set_rotation(WORD tenths);

VOID gem_vdi_text_set_alignment(WORD horizontal, WORD vertical,
	WORD *out_horizontal, WORD *out_vertical);

/* vqt_attributes, vqt_extent, vqt_width, vqt_fontinfo, vqt_name */
VOID gem_vdi_text_attributes(WORD color, WORD write_mode, UWORD *intout,
	UWORD *ptsout);
VOID gem_vdi_text_extent(const UWORD *characters, UWORD count, UWORD *ptsout);
WORD gem_vdi_text_char_width(WORD character, UWORD *ptsout);
VOID gem_vdi_text_fontinfo(UWORD *intout, UWORD *ptsout);
WORD gem_vdi_text_name(UWORD index, UWORD *intout);

/* vst_load_fonts / vst_unload_fonts */
WORD gem_vdi_text_load_fonts(VOID);
VOID gem_vdi_text_unload_fonts(VOID);

/* cell metrics for vq_extnd and the AES GRAF_HANDLE reply */
VOID gem_vdi_text_cell(UWORD *width, UWORD *height);
UWORD gem_vdi_text_face_count(VOID);
UWORD gem_vdi_text_size_count(VOID);

/* nonzero when the selected font is the plain unstyled ROM face, so the caller may take its aligned whole-string fast path */
WORD gem_vdi_text_is_plain(VOID);

/* V_JUSTIFIED padding, kept the way the original spread it: a whole number of pixels per gap, then one extra pixel on the first EXTRA gaps, with SIGN carrying the direction when the string has to shrink not stretch */
typedef struct gem_vdi_text_justify {
	WORD word_delta;
	WORD word_extra;
	WORD char_delta;
	WORD char_extra;
	WORD sign;
} GEM_VDI_TEXT_JUSTIFY;

/* measure a string like vqt_extent does, without touching the saved extent. used to work out the v_justified padding */
WORD gem_vdi_text_string_width(const UWORD *characters, UWORD count);

/* draw one string. x and y are the alignment point in device coords. JUSTIFY is null for ordinary v_gtext */
VOID gem_vdi_text_draw(GEM_VDI_SCREEN *screen, WORD x, WORD y,
	const UWORD *characters, UWORD count,
	const GEM_VDI_TEXT_JUSTIFY *justify);

/* VQT_JUSTIFIED: the running offset of every character of a justified string, along the text direction. fills one point per character and returns how many were written */
UWORD gem_vdi_text_justified_offsets(const UWORD *characters, UWORD count,
	const GEM_VDI_TEXT_JUSTIFY *justify, UWORD *ptsout, UWORD points);

/* underline extent for the caller's own rectangle fill, returns zero when the current effects dont include UNDER */
WORD gem_vdi_text_underline(WORD *x1, WORD *y1, WORD *x2, WORD *y2);

#endif				/* ELKS_GEM_VDI_TEXT_H */
