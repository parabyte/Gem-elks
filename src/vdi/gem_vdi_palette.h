/*
 * gem_vdi_palette.h - the resident VDI's color mapping and palette.
 *
 * maps a classic GEM logical color index to the PC adapter's palette index,
 * and keeps the sixteen requested 0..1000 triples for VS_COLOR and VQ_COLOR
 */

#ifndef ELKS_GEM_VDI_PALETTE_H
#define ELKS_GEM_VDI_PALETTE_H

#include "aes.h"
#include "vdi.h"

/* map a classic GEM logical color (0..15) to the native palette index */
GEM_VDI_COLOR gem_vdi_resident_color(WORD logical);

/*
 * store one requested triple (three 0..1000 words, clamped) and program the
 * mapped VGA DAC entry, index must be below sixteen
 */
VOID gem_vdi_palette_set(UWORD index, const UWORD *rgb);

/* read back the stored triple for VQ_COLOR, index must be below sixteen */
VOID gem_vdi_palette_get(UWORD index, UWORD *rgb);

/* program all sixteen entries, used right after a VGA mode set */
VOID gem_vdi_palette_apply_all(VOID);

#endif				/* ELKS_GEM_VDI_PALETTE_H */
