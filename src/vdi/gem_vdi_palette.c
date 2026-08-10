/*
 * gem_vdi_palette.c - the resident VDI's color mapping and palette.
 *
 * VDI clients give colors as plain 0..1000 intensities, all sixteen requested
 * triples are kept as 16-bit words and the mapped VGA DAC entry is programmed
 * whenever one changes
 */

#include "gem_vdi_palette.h"
#include "drivers/gem_pcvideo.h"

/* the GEM/3 palette that shipped with the PALETTE desk accessory */
static WORD gem_vdi_palette[16][3] = {
	{ 1000, 1000, 1000 }, { 0, 0, 0 },
	{ 1000, 0, 0 }, { 0, 1000, 0 },
	{ 0, 0, 1000 }, { 0, 1000, 1000 },
	{ 1000, 1000, 0 }, { 1000, 0, 1000 },
	{ 666, 666, 666 }, { 333, 333, 333 },
	{ 333, 0, 0 }, { 0, 333, 0 },
	{ 0, 0, 333 }, { 0, 333, 333 },
	{ 333, 333, 0 }, { 333, 0, 333 }
};

/*
 * each entry is ceil(level * 1000 / 63) for levels one through 63, walking the
 * table gives floor(value * 63 / 1000), one six-bit VGA DAC value
 */
static const UWORD gem_vdi_dac_thresholds[63] = {
	16, 32, 48, 64, 80, 96, 112, 127,
	143, 159, 175, 191, 207, 223, 239, 254,
	270, 286, 302, 318, 334, 350, 366, 381,
	397, 413, 429, 445, 461, 477, 493, 508,
	524, 540, 556, 572, 588, 604, 620, 635,
	651, 667, 683, 699, 715, 731, 747, 762,
	778, 794, 810, 826, 842, 858, 874, 889,
	905, 921, 937, 953, 969, 985, 1000
};

GEM_VDI_COLOR
gem_vdi_resident_color(WORD logical)
{
	switch ((UWORD) logical & 15U) {
	case 0:		/* white */
		return 15;
	case 1:		/* black */
		return 0;
	case 2:		/* red */
		return 4;
	case 3:		/* green */
		return 2;
	case 4:		/* blue */
		return 1;
	case 5:		/* cyan */
		return 3;
	case 6:		/* yellow */
		return 14;
	case 7:		/* magenta */
		return 5;
	case 8:
		return 7;
	case 9:
		return 8;
	case 10:
		return 12;
	case 11:
		return 10;
	case 12:
		return 9;
	case 13:
		return 11;
	case 14:
		return 6;
	case 15:
		return 13;
	default:
		return 0;
	}
}

static WORD * __attribute__((optimize("Os")))
	gem_vdi_palette_entry(UWORD index)
{
	WORD *entry;

	/* index is already checked below sixteen */
	entry = &gem_vdi_palette[0][0];
	while (index--) {
		entry++;
		entry++;
		entry++;
	}
	return entry;
}

static GEM_VDI_UBYTE __attribute__((optimize("Os")))
	gem_vdi_palette_dac(UWORD value)
{
	const UWORD *threshold;
	GEM_VDI_UBYTE level;

	threshold = gem_vdi_dac_thresholds;
	level = 0;
	while (level < 63U && value >= *threshold++)
		level++;
	return level;
}

static WORD __attribute__((optimize("Os")))
	gem_vdi_palette_component(UWORD input)
{
	WORD value;

	value = (WORD) input;
	if (value < 0)
		return 0;
	if (value > 1000)
		return 1000;
	return value;
}

static VOID __attribute__((optimize("Os")))
	gem_vdi_palette_apply(UWORD logical, WORD *entry)
{
	gem_pc_video_set_palette(gem_vdi_resident_color((WORD) logical),
		gem_vdi_palette_dac((UWORD) entry[0]),
		gem_vdi_palette_dac((UWORD) entry[1]),
		gem_vdi_palette_dac((UWORD) entry[2]));
}

VOID __attribute__((optimize("Os")))
	gem_vdi_palette_set(UWORD index, const UWORD *rgb)
{
	WORD *entry;
	WORD *first;
	UWORD count;

	entry = gem_vdi_palette_entry(index);
	first = entry;
	count = 3;
	while (count--)
		*entry++ = gem_vdi_palette_component(*rgb++);
	gem_vdi_palette_apply(index, first);
}

VOID __attribute__((optimize("Os")))
	gem_vdi_palette_get(UWORD index, UWORD *rgb)
{
	WORD *entry;
	UWORD count;

	entry = gem_vdi_palette_entry(index);
	count = 3;
	while (count--)
		*rgb++ = (UWORD) *entry++;
}

VOID __attribute__((optimize("Os")))
	gem_vdi_palette_apply_all(VOID)
{
	WORD *entry;
	UWORD logical;

	entry = &gem_vdi_palette[0][0];
	logical = 0;
	while (logical < 16U) {
		gem_vdi_palette_apply(logical, entry);
		entry++;
		entry++;
		entry++;
		logical++;
	}
}
