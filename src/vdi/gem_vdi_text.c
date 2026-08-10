/*
 * gem_vdi_text.c - draws text and handles fonts
 *
 * picks the face, size and style, pulls each character out of the font and
 * draws it with bold/italic/underline/scaling/rotation, and loads .FNT fonts
 * off disk
 */

#include "gem_vdi_text.h"

#include "gem_resident_memory.h"

#define GEM_VDI_TEXT_CELL_BITS	(GEM_VDI_TEXT_CELL_BYTES * 8)

/*
 * the far draw helpers change DS, so save it and put it back after every call
 */
#define GEM_VDI_TEXT_SAVE_DS(segment) \
	__asm__ volatile ("movw %%ds,%0" : "=r" (segment) : : "memory")
#define GEM_VDI_TEXT_LOAD_DS(segment) \
	__asm__ volatile ("movw %0,%%ds" : : "r" (segment) : "memory")
#define GEM_VDI_TEXT_GLYPH_BYTES ((GEM_VDI_FONT_MAX_CELL / 8) + 1)

static WORD gem_vdi_text_justify_step(const GEM_VDI_TEXT_JUSTIFY *justify,
	UWORD code, WORD *word_extra, WORD *char_extra);

/* the working font header, scaling may have changed it */
static GEM_VDI_FONT gem_vdi_text_head;
/* the font this header came from */
static GEM_VDI_FONT *gem_vdi_text_font;

/* the font the client asked for: face, size, style */
static WORD gem_vdi_text_rq_font = 1;
static WORD gem_vdi_text_rq_size = 10;
static WORD gem_vdi_text_rq_attr;
static WORD gem_vdi_text_rq_type = TRUE;	/* size is in points */
static WORD gem_vdi_text_double;

static UWORD gem_vdi_text_special;
static WORD gem_vdi_text_rot_case;
static WORD gem_vdi_text_h_align;
static WORD gem_vdi_text_v_align;

static WORD gem_vdi_text_chr_ht;	/* full cell height */
static WORD gem_vdi_text_actdely;	/* cell height after scaling */
static WORD gem_vdi_text_char_del;	/* extra width bold and italic add */
static WORD gem_vdi_text_weight;	/* how many pixels bold smears */
static WORD gem_vdi_text_l_off;	/* italic left offset */
static WORD gem_vdi_text_r_off;	/* italic right offset */
static WORD gem_vdi_text_mono;	/* set when the font is monospaced */

static UWORD gem_vdi_text_dda_inc;	/* scaling step */
static WORD gem_vdi_text_scale_up;	/* set when scaling up not down */

/* size of the last measured string */
static WORD gem_vdi_text_extent_width;
static WORD gem_vdi_text_extent_height;

/* where the last drawn string wants its underline */
static WORD gem_vdi_text_under_x1;
static WORD gem_vdi_text_under_y1;
static WORD gem_vdi_text_under_x2;
static WORD gem_vdi_text_under_y2;
static WORD gem_vdi_text_under_on;

static GEM_VDI_UBYTE gem_vdi_text_glyph_rows
	[GEM_VDI_FONT_MAX_ROWS * GEM_VDI_TEXT_GLYPH_BYTES];
static GEM_VDI_UBYTE gem_vdi_text_form
	[GEM_VDI_TEXT_CELL_BYTES * GEM_VDI_TEXT_CELL_ROWS];
static GEM_VDI_UBYTE gem_vdi_text_spare
	[GEM_VDI_TEXT_CELL_BYTES * GEM_VDI_TEXT_CELL_ROWS];

/*
 * work out the scaling fraction, scaling up gives one step per source step
 * plus one more on each carry, scaling down gives one only on a carry
 */
static UWORD
gem_vdi_text_clc_dda(UWORD actual, UWORD requested)
{
	unsigned long numerator;

	if (!actual)
		return 0;
	if (requested > actual) {
		gem_vdi_text_scale_up = 1;
		requested = (UWORD) (requested - actual);
		if (requested >= actual)
			return 0xffffU;	/* two times or more */
	} else {
		gem_vdi_text_scale_up = 0;
		if (!requested)
			requested = 1;
		if (requested >= actual)
			return 0xffffU;
	}
	numerator = (unsigned long) requested;
	numerator <<= 16;
	return (UWORD) (numerator / (unsigned long) actual);
}

static UWORD
gem_vdi_text_act_siz(UWORD size)
{
	UWORD accumulator;
	UWORD previous;
	UWORD count;

	if (gem_vdi_text_dda_inc == 0xffffU)
		return (UWORD) (size << 1);
	if (!size)
		return 0;
	accumulator = 32767U;
	count = 0;
	while (size--) {
		previous = accumulator;
		accumulator = (UWORD) (accumulator + gem_vdi_text_dda_inc);
		if (accumulator < previous)
			count++;
		if (gem_vdi_text_scale_up)
			count++;
	}
	if (!gem_vdi_text_scale_up && !count)
		count = 1;
	return count;
}

/* take a fresh working copy of the chosen font */
static VOID
gem_vdi_text_copy_head(VOID)
{
	if (gem_vdi_text_font)
		gem_vdi_text_head = *gem_vdi_text_font;
}

/* rewrite the working header for a scaled size */
static VOID
gem_vdi_text_make_header(VOID)
{
	UWORD value;

	gem_vdi_text_copy_head();
	gem_vdi_text_head.point = (WORD) (gem_vdi_text_head.point << 1);
	value = gem_vdi_text_act_siz(gem_vdi_text_head.top);
	if (gem_vdi_text_dda_inc == 0xffffU)
		value++;
	else if (value == gem_vdi_text_act_siz((UWORD)
			(gem_vdi_text_head.top + 1U)))
		value--;
	gem_vdi_text_head.top = value;
	gem_vdi_text_head.ascent =
		(UWORD) (gem_vdi_text_act_siz(gem_vdi_text_head.ascent) + 1U);
	gem_vdi_text_head.half =
		(UWORD) (gem_vdi_text_act_siz(gem_vdi_text_head.half) + 1U);
	gem_vdi_text_head.descent =
		gem_vdi_text_act_siz(gem_vdi_text_head.descent);
	gem_vdi_text_head.bottom =
		gem_vdi_text_act_siz(gem_vdi_text_head.bottom);
	gem_vdi_text_head.max_char_width =
		gem_vdi_text_act_siz(gem_vdi_text_head.max_char_width);
	gem_vdi_text_head.max_cell_width =
		gem_vdi_text_act_siz(gem_vdi_text_head.max_cell_width);
	gem_vdi_text_head.left_offset =
		gem_vdi_text_act_siz(gem_vdi_text_head.left_offset);
	gem_vdi_text_head.right_offset =
		gem_vdi_text_act_siz(gem_vdi_text_head.right_offset);
	gem_vdi_text_head.thicken =
		gem_vdi_text_act_siz(gem_vdi_text_head.thicken);
	gem_vdi_text_head.ul_size =
		gem_vdi_text_act_siz(gem_vdi_text_head.ul_size);
	gem_vdi_text_actdely =
		(WORD) gem_vdi_text_act_siz(gem_vdi_text_head.form_height);
	gem_vdi_text_special |= GEM_VDI_STYLE_SCALE;
}

/*
 * find the closest size in one face, prefer an exact match, then a doubled
 * one, then the nearest size below
 */
static GEM_VDI_FONT *
gem_vdi_text_sel_size(GEM_VDI_FONT *first)
{
	GEM_VDI_FONT *font;
	GEM_VDI_FONT *size_head;
	GEM_VDI_FONT *close;
	WORD face;
	WORD point;
	WORD current;
	WORD this_size;
	WORD close_size;

	face = (WORD) (first->font_id & 0x00ffU);

	point = first->point;
	size_head = first;
	for (font = first; font && (WORD) (font->font_id & 0x00ffU) == face;
		font = gem_vdi_font_next(font)) {
		current = font->point;
		this_size = gem_vdi_text_rq_type ? current
			: (WORD) (font->top + 1U);
		if (point != current) {
			point = current;
			size_head = font;
		}
		if (this_size == gem_vdi_text_rq_size)
			return size_head;
		if (this_size > gem_vdi_text_rq_size)
			break;
	}

	point = first->point;
	size_head = first;
	for (font = first; font && (WORD) (font->font_id & 0x00ffU) == face;
		font = gem_vdi_font_next(font)) {
		current = font->point;
		this_size = gem_vdi_text_rq_type ? current
			: (WORD) (font->top + 1U);
		if (point != current) {
			point = current;
			size_head = font;
		}
		this_size = (WORD) (this_size << 1);
		if (this_size == gem_vdi_text_rq_size) {
			gem_vdi_text_double = TRUE;
			return size_head;
		}
		if (this_size > gem_vdi_text_rq_size)
			break;
	}

	close_size = gem_vdi_text_rq_type ? first->point
		: (WORD) (first->top + 1U);
	close = first;
	point = first->point;
	size_head = first;
	this_size = close_size;
	for (font = first; font && (WORD) (font->font_id & 0x00ffU) == face;
		font = gem_vdi_font_next(font)) {
		current = font->point;
		this_size = gem_vdi_text_rq_type ? current
			: (WORD) (font->top + 1U);
		if (point != current) {
			point = current;
			size_head = font;
		}
		if (this_size < gem_vdi_text_rq_size
			&& gem_vdi_text_rq_size - this_size
			<= gem_vdi_text_rq_size - close_size) {
			close = size_head;
			close_size = this_size;
			gem_vdi_text_double = FALSE;
		}
		this_size = (WORD) (this_size << 1);
		if (this_size < gem_vdi_text_rq_size) {
			if (gem_vdi_text_rq_size - this_size
				< gem_vdi_text_rq_size - close_size) {
				close = size_head;
				close_size = this_size;
			}
			gem_vdi_text_double = TRUE;
		}
	}
	if ((WORD) (this_size << 1) < gem_vdi_text_rq_size)
		gem_vdi_text_double = TRUE;
	return close;
}

/*
 * find a real bold/italic font in this size if there is one, else the nearest,
 * the leftover style bits get faked in when drawing
 */
static GEM_VDI_FONT *
gem_vdi_text_sel_effect(GEM_VDI_FONT *first)
{
	GEM_VDI_FONT *font;
	GEM_VDI_FONT *normal;
	GEM_VDI_FONT *bold;
	GEM_VDI_FONT *italic;
	WORD face;
	WORD point;
	WORD find;
	WORD effect;

	face = (WORD) (first->font_id & 0x00ffU);
	point = first->point;
	find = gem_vdi_text_rq_attr
		& (GEM_VDI_STYLE_SKEW | GEM_VDI_STYLE_THICKEN);
	normal = bold = italic = (GEM_VDI_FONT *) 0;
	gem_vdi_text_special &= 0xfff0U;
	gem_vdi_text_special |= (UWORD) gem_vdi_text_rq_attr;

	for (font = first; font && (WORD) (font->font_id & 0x00ffU) == face
		&& font->point == point; font = gem_vdi_font_next(font)) {
		effect = (WORD) ((font->font_id >> 8) & 0x00ffU);
		if (effect == find) {
			gem_vdi_text_special &= (UWORD) ~find;
			return font;
		}
		if (!effect)
			normal = font;
		else if (effect == GEM_VDI_STYLE_THICKEN)
			bold = font;
		else if (effect == GEM_VDI_STYLE_SKEW)
			italic = font;
	}

	if (find == (GEM_VDI_STYLE_SKEW | GEM_VDI_STYLE_THICKEN)) {
		if (italic) {
			gem_vdi_text_special &= (UWORD) ~GEM_VDI_STYLE_SKEW;
			return italic;
		}
		if (bold) {
			gem_vdi_text_special &= (UWORD) ~GEM_VDI_STYLE_THICKEN;
			return bold;
		}
	}
	return normal ? normal : first;
}

static VOID
gem_vdi_text_sel_font(VOID)
{
	GEM_VDI_FONT *font;

	for (font = gem_vdi_font_first();
		font
		&& (WORD) (font->font_id & 0x00ffU) != gem_vdi_text_rq_font;
		font = gem_vdi_font_next(font));
	if (!font)
		font = gem_vdi_font_first();
	if (!font)
		return;

	gem_vdi_text_double = FALSE;
	font = gem_vdi_text_sel_size(font);
	gem_vdi_text_font = gem_vdi_text_sel_effect(font);

	gem_vdi_text_special &= (UWORD) ~GEM_VDI_STYLE_SCALE;
	gem_vdi_text_copy_head();
	gem_vdi_text_actdely = (WORD) gem_vdi_text_head.form_height;
	if (gem_vdi_text_double) {
		gem_vdi_text_dda_inc = 0xffffU;
		gem_vdi_text_scale_up = 1;
		gem_vdi_text_make_header();
	} else if (!gem_vdi_text_rq_type && (WORD) (gem_vdi_text_head.top + 1U)
		!= gem_vdi_text_rq_size) {
		gem_vdi_text_dda_inc = gem_vdi_text_clc_dda(
			(UWORD) (gem_vdi_text_head.top + 1U),
			(UWORD) gem_vdi_text_rq_size);
		gem_vdi_text_make_header();
	}

	gem_vdi_text_chr_ht = (WORD) (gem_vdi_text_head.top + 1U
		+ gem_vdi_text_head.bottom);
	gem_vdi_text_char_del = 0;
	gem_vdi_text_weight = 0;
	gem_vdi_text_l_off = 0;
	gem_vdi_text_r_off = 0;
	if (gem_vdi_text_special & GEM_VDI_STYLE_THICKEN) {
		gem_vdi_text_weight = (WORD) gem_vdi_text_head.thicken;
		gem_vdi_text_char_del = gem_vdi_text_weight;
	}
	if (gem_vdi_text_special & GEM_VDI_STYLE_SKEW) {
		gem_vdi_text_l_off = (WORD) gem_vdi_text_head.left_offset;
		gem_vdi_text_r_off = (WORD) gem_vdi_text_head.right_offset;
		gem_vdi_text_char_del = (WORD) (gem_vdi_text_char_del
			+ gem_vdi_text_l_off + gem_vdi_text_r_off);
	}
	gem_vdi_text_mono = (gem_vdi_text_head.max_cell_width == 8U)
		? (WORD) (gem_vdi_text_head.flags & GEM_VDI_FONT_MONOSPACE)
		: FALSE;
}

VOID
gem_vdi_text_init(UWORD rom_segment, UWORD rom_offset, UWORD rom_rows)
{
	gem_vdi_font_reset(rom_segment, rom_offset, rom_rows);
	gem_vdi_text_font = gem_vdi_font_first();
	gem_vdi_text_special = 0;
	gem_vdi_text_rot_case = 0;
	gem_vdi_text_h_align = 0;
	gem_vdi_text_v_align = 0;
	gem_vdi_text_rq_font = 1;
	gem_vdi_text_rq_size = 10;
	gem_vdi_text_rq_attr = 0;
	gem_vdi_text_rq_type = TRUE;
	gem_vdi_text_double = FALSE;
	gem_vdi_text_dda_inc = 0;
	gem_vdi_text_scale_up = 0;
	gem_vdi_text_copy_head();
	gem_vdi_text_actdely = (WORD) gem_vdi_text_head.form_height;
	gem_vdi_text_chr_ht = (WORD) (gem_vdi_text_head.top
		+ gem_vdi_text_head.bottom + 1U);
	gem_vdi_text_char_del = 0;
	gem_vdi_text_weight = 0;
	gem_vdi_text_l_off = 0;
	gem_vdi_text_r_off = 0;
	gem_vdi_text_mono = (gem_vdi_text_head.max_cell_width == 8U)
		? (WORD) (gem_vdi_text_head.flags & GEM_VDI_FONT_MONOSPACE)
		: FALSE;
}

static VOID
gem_vdi_text_size_reply(UWORD *ptsout)
{
	if (!ptsout)
		return;
	ptsout[0] = gem_vdi_text_head.max_char_width;
	ptsout[1] = (UWORD) (gem_vdi_text_head.top + 1U);
	ptsout[2] = gem_vdi_text_head.max_cell_width;
	ptsout[3] = (UWORD) gem_vdi_text_chr_ht;
}

VOID
gem_vdi_text_set_height(WORD pixels, UWORD *ptsout)
{
	gem_vdi_text_rq_size = pixels ? pixels : 1;
	gem_vdi_text_rq_type = FALSE;
	gem_vdi_text_sel_font();
	gem_vdi_text_size_reply(ptsout);
}

WORD
gem_vdi_text_set_point(WORD points, UWORD *ptsout)
{
	gem_vdi_text_rq_size = points;
	gem_vdi_text_rq_type = TRUE;
	gem_vdi_text_sel_font();
	gem_vdi_text_size_reply(ptsout);
	return gem_vdi_text_head.point;
}

WORD
gem_vdi_text_set_font(WORD face)
{
	gem_vdi_text_rq_font = face;
	gem_vdi_text_sel_font();
	return (WORD) (gem_vdi_text_head.font_id & 0x00ffU);
}

WORD
gem_vdi_text_set_effects(WORD bits)
{
	/* the PC drivers do four of the six style bits */
	gem_vdi_text_rq_attr = bits & 0x000f;
	gem_vdi_text_sel_font();
	return gem_vdi_text_rq_attr;
}

WORD
gem_vdi_text_set_rotation(WORD tenths)
{
	while (tenths < 0)
		tenths += 3600;
	while (tenths >= 3600)
		tenths -= 3600;
	gem_vdi_text_rot_case = (WORD) ((tenths + 450) / 900);
	if (gem_vdi_text_rot_case > 3)
		gem_vdi_text_rot_case = 0;
	gem_vdi_text_special = (UWORD) ((gem_vdi_text_special
			& ~GEM_VDI_STYLE_ROTATE)
		| (UWORD) (gem_vdi_text_rot_case << 6));
	return (WORD) (gem_vdi_text_rot_case * 900);
}

VOID
gem_vdi_text_set_alignment(WORD horizontal, WORD vertical,
	WORD *out_horizontal, WORD *out_vertical)
{
	if (horizontal < 0 || horizontal > 2)
		horizontal = 0;
	if (vertical < 0 || vertical > 5)
		vertical = 0;
	gem_vdi_text_h_align = horizontal;
	gem_vdi_text_v_align = vertical;
	if (out_horizontal)
		*out_horizontal = horizontal;
	if (out_vertical)
		*out_vertical = vertical;
}

VOID
gem_vdi_text_attributes(WORD color, WORD write_mode, UWORD *intout,
	UWORD *ptsout)
{
	if (intout) {
		intout[0] = gem_vdi_text_head.font_id;
		intout[1] = (UWORD) color;
		intout[2] = (UWORD) (900 * ((gem_vdi_text_special
					& GEM_VDI_STYLE_ROTATE) >> 6));
		intout[3] = (UWORD) gem_vdi_text_h_align;
		intout[4] = (UWORD) gem_vdi_text_v_align;
		intout[5] = (UWORD) write_mode;
	}
	gem_vdi_text_size_reply(ptsout);
}

/* use a space for any code the font dont have */
static UWORD
gem_vdi_text_code(UWORD character)
{
	if (character >= gem_vdi_text_head.first_ade
		&& character <= gem_vdi_text_head.last_ade)
		return character;
	if (' ' >= gem_vdi_text_head.first_ade
		&& ' ' <= gem_vdi_text_head.last_ade)
		return (UWORD) ' ';
	return gem_vdi_text_head.first_ade;
}

static WORD
gem_vdi_text_measure(const UWORD *characters, UWORD count)
{
	WORD width;
	WORD left;
	WORD right;
	UWORD index;
	UWORD code;

	width = 0;
	for (index = 0; index < count; index++) {
		code = gem_vdi_text_code(characters[index]);
		width = (WORD) (width
			+ gem_vdi_font_char_width(gem_vdi_text_font, code));
		gem_vdi_font_char_offsets(gem_vdi_text_font, code, &left,
			&right);
		width = (WORD) (width - left - right);
	}
	if (gem_vdi_text_special & GEM_VDI_STYLE_SCALE)
		width = (WORD) gem_vdi_text_act_siz((UWORD) width);
	if ((gem_vdi_text_special & GEM_VDI_STYLE_THICKEN)
		&& !(gem_vdi_text_head.flags & GEM_VDI_FONT_MONOSPACE))
		width = (WORD) (width + (WORD) count * gem_vdi_text_weight);
	return width;
}

VOID
gem_vdi_text_extent(const UWORD *characters, UWORD count, UWORD *ptsout)
{
	UWORD index;

	gem_vdi_text_extent_width = count
		? gem_vdi_text_measure(characters, count) : 0;
	gem_vdi_text_extent_height = gem_vdi_text_chr_ht;
	if (!ptsout)
		return;
	for (index = 0; index < 8U; index++)
		ptsout[index] = 0;
	switch (gem_vdi_text_rot_case) {
	case 0:
		ptsout[2] = ptsout[4] = (UWORD) gem_vdi_text_extent_width;
		ptsout[5] = ptsout[7] = (UWORD) gem_vdi_text_extent_height;
		break;
	case 1:
		ptsout[0] = ptsout[2] = (UWORD) gem_vdi_text_extent_height;
		ptsout[3] = ptsout[5] = (UWORD) gem_vdi_text_extent_width;
		break;
	case 2:
		ptsout[0] = ptsout[6] = (UWORD) gem_vdi_text_extent_width;
		ptsout[1] = ptsout[3] = (UWORD) gem_vdi_text_extent_height;
		break;
	default:
		ptsout[4] = ptsout[6] = (UWORD) gem_vdi_text_extent_height;
		ptsout[1] = ptsout[7] = (UWORD) gem_vdi_text_extent_width;
		break;
	}
}

WORD
gem_vdi_text_char_width(WORD character, UWORD *ptsout)
{
	UWORD width;
	WORD left;
	WORD right;

	if (ptsout) {
		ptsout[0] = 0;
		ptsout[1] = 0;
		ptsout[2] = 0;
		ptsout[3] = 0;
		ptsout[4] = 0;
		ptsout[5] = 0;
	}
	if ((UWORD) character < gem_vdi_text_head.first_ade
		|| (UWORD) character > gem_vdi_text_head.last_ade)
		return -1;
	width = gem_vdi_font_char_width(gem_vdi_text_font, (UWORD) character);
	if (gem_vdi_text_special & GEM_VDI_STYLE_SCALE)
		width = gem_vdi_text_act_siz(width);
	gem_vdi_font_char_offsets(gem_vdi_text_font, (UWORD) character,
		&left, &right);
	if (ptsout) {
		ptsout[0] = width;
		ptsout[2] = (UWORD) left;
		ptsout[4] = (UWORD) right;
	}
	return character;
}

VOID
gem_vdi_text_fontinfo(UWORD *intout, UWORD *ptsout)
{
	UWORD index;

	if (intout) {
		intout[0] = 32;
		intout[1] = 255;
	}
	if (!ptsout)
		return;
	for (index = 0; index < 10U; index++)
		ptsout[index] = 0;
	ptsout[0] = gem_vdi_text_head.max_cell_width;
	ptsout[1] = gem_vdi_text_head.bottom;
	ptsout[2] = (gem_vdi_text_head.flags & GEM_VDI_FONT_MONOSPACE)
		? 0U : (UWORD) gem_vdi_text_weight;
	ptsout[3] = gem_vdi_text_head.descent;
	ptsout[4] = (UWORD) gem_vdi_text_l_off;
	ptsout[5] = gem_vdi_text_head.half;
	ptsout[6] = (UWORD) gem_vdi_text_r_off;
	ptsout[7] = gem_vdi_text_head.ascent;
	ptsout[9] = gem_vdi_text_head.top;
}

WORD
gem_vdi_text_name(UWORD index, UWORD *intout)
{
	const BYTE *name;
	UWORD face;
	UWORD position;

	face = 0;
	name = gem_vdi_font_face_name(index, &face);
	if (!name) {
		name = gem_vdi_font_face_name(1, &face);
		if (!name)
			return 0;
	}
	if (intout) {
		intout[0] = face;
		for (position = 0; position < GEM_VDI_FONT_NAME; position++)
			intout[position + 1U] = (UWORD)
				(UBYTE) name[position];
	}
	return (WORD) face;
}

WORD
gem_vdi_text_load_fonts(VOID)
{
	WORD added;

	added = gem_vdi_font_load_all();
	/* the chain moved under the working header, reselect on it */
	gem_vdi_text_sel_font();
	return added;
}

VOID
gem_vdi_text_unload_fonts(VOID)
{
	gem_vdi_font_unload_all();
	gem_vdi_text_rq_font = 1;
	gem_vdi_text_sel_font();
}

VOID
gem_vdi_text_cell(UWORD *width, UWORD *height)
{
	if (width)
		*width = gem_vdi_text_head.max_cell_width;
	if (height)
		*height = (UWORD) gem_vdi_text_chr_ht;
}

UWORD
gem_vdi_text_face_count(VOID)
{
	return gem_vdi_font_face_count();
}

UWORD
gem_vdi_text_size_count(VOID)
{
	return gem_vdi_font_size_count();
}

WORD
gem_vdi_text_is_plain(VOID)
{
	return !gem_vdi_text_special && gem_vdi_text_mono
		&& gem_vdi_text_font && gem_vdi_text_font->rom_rows == 16U;
}

/* cell bit accessors, bit 7 of byte zero is the leftmost pixel */
static WORD
gem_vdi_text_cell_get(const GEM_VDI_UBYTE *cell, UWORD stride, UWORD column,
	UWORD row)
{
	return (cell[row * stride + (column >> 3)]
		& (GEM_VDI_UBYTE) (0x80U >> (column & 7U))) != 0;
}

static VOID
gem_vdi_text_cell_set(GEM_VDI_UBYTE *cell, UWORD stride, UWORD column,
	UWORD row)
{
	cell[row * stride + (column >> 3)] |=
		(GEM_VDI_UBYTE) (0x80U >> (column & 7U));
}

static VOID
gem_vdi_text_cell_clear(GEM_VDI_UBYTE *cell, UWORD bytes)
{
	while (bytes--)
		*cell++ = 0;
}

/*
 * bold: smear each row right by one pixel, weight times over, carrying across
 * byte boundaries
 */
static VOID
gem_vdi_text_bold(GEM_VDI_UBYTE *cell, UWORD stride, UWORD rows, UWORD weight)
{
	UWORD pass;
	UWORD row;
	UWORD index;
	UWORD carry;
	UWORD value;

	for (pass = 0; pass < weight; pass++)
		for (row = 0; row < rows; row++) {
			carry = 0;
			for (index = 0; index < stride; index++) {
				value = cell[row * stride + index];
				cell[row * stride + index] = (GEM_VDI_UBYTE)
					(value | (value >> 1) | (carry << 7));
				carry = value & 1U;
			}
		}
}

/* grey out: dither with 0xaa and 0x55 on alternate rows */
static VOID
gem_vdi_text_grey(GEM_VDI_UBYTE *cell, UWORD stride, UWORD rows)
{
	UWORD row;
	UWORD index;
	GEM_VDI_UBYTE mask;

	mask = 0xaa;
	for (row = 0; row < rows; row++) {
		for (index = 0; index < stride; index++)
			cell[row * stride + index] &= mask;
		mask = (GEM_VDI_UBYTE) ~mask;
	}
}

/*
 * italic: slide each row right a bit more toward the top, so the cell leans
 * forward
 */
static VOID
gem_vdi_text_italic(GEM_VDI_UBYTE *cell, UWORD stride, UWORD width, UWORD rows)
{
	UWORD row;
	UWORD shift;
	UWORD column;
	UWORD limit;

	limit = (UWORD) (stride * 8U);
	for (row = 0; row < rows; row++) {
		shift = (UWORD) ((rows - 1U - row) >> 1);
		if (!shift)
			continue;
		column = (UWORD) (width + shift);
		if (column > limit)
			column = limit;
		while (column--) {
			if (column < shift)
				break;
			if (gem_vdi_text_cell_get(cell, stride,
					(UWORD) (column - shift), row))
				gem_vdi_text_cell_set(cell, stride, column,
					row);
			else
				cell[row * stride + (column >> 3)] &=
					(GEM_VDI_UBYTE) ~(0x80U
					>> (column & 7U));
		}
		for (column = 0; column < shift && column < limit; column++)
			cell[row * stride + (column >> 3)] &=
				(GEM_VDI_UBYTE) ~(0x80U >> (column & 7U));
	}
}

/*
 * pull the glyph out of the font into the cell buffer, scaling both axes when
 * the size was scaled
 */
static UWORD
gem_vdi_text_compose(UWORD code, UWORD stride, UWORD *rows)
{
	UWORD source_width;
	UWORD source_rows;
	UWORD width;
	UWORD accumulator;
	UWORD previous;
	UWORD row;
	UWORD column;
	UWORD out_row;
	UWORD out_column;
	UWORD copies;
	UWORD limit;

	source_width = gem_vdi_font_glyph(gem_vdi_text_font, code,
		gem_vdi_text_glyph_rows, GEM_VDI_TEXT_GLYPH_BYTES);
	if (!source_width)
		return 0;
	source_rows = gem_vdi_text_font->form_height;
	limit = (UWORD) (stride * 8U);

	gem_vdi_text_cell_clear(gem_vdi_text_form,
		(UWORD) (stride * GEM_VDI_TEXT_CELL_ROWS));

	if (!(gem_vdi_text_special & GEM_VDI_STYLE_SCALE)) {
		width = source_width;
		if (width > limit)
			width = limit;
		*rows = source_rows;
		if (*rows > GEM_VDI_TEXT_CELL_ROWS)
			*rows = GEM_VDI_TEXT_CELL_ROWS;
		for (row = 0; row < *rows; row++)
			for (column = 0; column < width; column++)
				if (gem_vdi_text_glyph_rows[row
						* GEM_VDI_TEXT_GLYPH_BYTES
						+ (column >> 3)]
					& (0x80U >> (column & 7U)))
					gem_vdi_text_cell_set(gem_vdi_text_form,
						stride, column, row);
		return width;
	}

	/* scale the rows, then the columns in each row */
	out_row = 0;
	accumulator = 32767U;
	for (row = 0; row < source_rows
		&& out_row < GEM_VDI_TEXT_CELL_ROWS; row++) {
		previous = accumulator;
		accumulator = (UWORD) (accumulator + gem_vdi_text_dda_inc);
		copies = 0;
		if (gem_vdi_text_dda_inc == 0xffffU)
			copies = 2;
		else {
			if (accumulator < previous)
				copies++;
			if (gem_vdi_text_scale_up)
				copies++;
		}
		while (copies-- && out_row < GEM_VDI_TEXT_CELL_ROWS) {
			out_column = 0;
			{
				UWORD x_accumulator;
				UWORD x_previous;
				UWORD x_copies;

				x_accumulator = 32767U;
				for (column = 0; column < source_width
					&& out_column < limit; column++) {
					x_previous = x_accumulator;
					x_accumulator = (UWORD)
						(x_accumulator
						+ gem_vdi_text_dda_inc);
					x_copies = 0;
					if (gem_vdi_text_dda_inc == 0xffffU)
						x_copies = 2;
					else {
						if (x_accumulator < x_previous)
							x_copies++;
						if (gem_vdi_text_scale_up)
							x_copies++;
					}
					while (x_copies-- && out_column < limit) {
						if (gem_vdi_text_glyph_rows[row
								*
								GEM_VDI_TEXT_GLYPH_BYTES
								+ (column >> 3)]
							& (0x80U >> (column &
									7U)))
							gem_vdi_text_cell_set
								(gem_vdi_text_form,
								stride,
								out_column,
								out_row);
						out_column++;
					}
				}
			}
			out_row++;
		}
	}
	*rows = out_row;
	width = gem_vdi_text_act_siz(source_width);
	if (width > limit)
		width = limit;
	return width;
}

/* rotate the cell by a quarter turn */
static VOID
gem_vdi_text_rotate(UWORD stride, UWORD width, UWORD rows,
	UWORD *out_stride, UWORD *out_width, UWORD *out_rows)
{
	UWORD destination_stride;
	UWORD row;
	UWORD column;
	UWORD destination_column;
	UWORD destination_row;

	if (!gem_vdi_text_rot_case) {
		*out_stride = stride;
		*out_width = width;
		*out_rows = rows;
		return;
	}
	if (gem_vdi_text_rot_case == 2) {
		destination_stride = stride;
		*out_width = width;
		*out_rows = rows;
	} else {
		destination_stride = (UWORD) ((rows + 7U) >> 3);
		if (destination_stride > GEM_VDI_TEXT_CELL_BYTES)
			destination_stride = GEM_VDI_TEXT_CELL_BYTES;
		*out_width = rows;
		*out_rows = width;
	}
	if (*out_rows > GEM_VDI_TEXT_CELL_ROWS)
		*out_rows = GEM_VDI_TEXT_CELL_ROWS;
	if (*out_width > (UWORD) (destination_stride * 8U))
		*out_width = (UWORD) (destination_stride * 8U);
	gem_vdi_text_cell_clear(gem_vdi_text_spare,
		(UWORD) (destination_stride * GEM_VDI_TEXT_CELL_ROWS));

	for (row = 0; row < rows; row++)
		for (column = 0; column < width; column++) {
			if (!gem_vdi_text_cell_get(gem_vdi_text_form, stride,
					column, row))
				continue;
			switch (gem_vdi_text_rot_case) {
			case 1:
				destination_column = row;
				destination_row = (UWORD) (width - 1U - column);
				break;
			case 2:
				destination_column = (UWORD) (width - 1U
					- column);
				destination_row = (UWORD) (rows - 1U - row);
				break;
			default:
				destination_column = (UWORD) (rows - 1U - row);
				destination_row = column;
				break;
			}
			if (destination_column >= *out_width
				|| destination_row >= *out_rows)
				continue;
			gem_vdi_text_cell_set(gem_vdi_text_spare,
				destination_stride, destination_column,
				destination_row);
		}
	for (row = 0; row < (UWORD) (destination_stride
			* GEM_VDI_TEXT_CELL_ROWS); row++)
		gem_vdi_text_form[row] = gem_vdi_text_spare[row];
	*out_stride = destination_stride;
}

/*
 * where each character would land, adding up each one's width plus its share
 * of the justify padding and writing the running total into ptsout
 */
UWORD
gem_vdi_text_justified_offsets(const UWORD *characters, UWORD count,
	const GEM_VDI_TEXT_JUSTIFY *justify, UWORD *ptsout, UWORD points)
{
	WORD word_extra;
	WORD char_extra;
	WORD running;
	WORD advance;
	WORD left;
	WORD right;
	UWORD index;
	UWORD code;
	UWORD along_y;
	UWORD negative;

	if (!characters || !ptsout || !gem_vdi_text_font)
		return 0;
	if (count > points)
		count = points;
	word_extra = justify ? justify->word_extra : 0;
	char_extra = justify ? justify->char_extra : 0;
	along_y = (gem_vdi_text_rot_case & 1U) != 0;
	negative = gem_vdi_text_rot_case == 1U || gem_vdi_text_rot_case == 2U;
	running = 0;
	for (index = 0; index < count; index++) {
		ptsout[index + index] = 0;
		ptsout[index + index + 1U] = 0;
		ptsout[index + index + along_y] = (UWORD) running;
		code = gem_vdi_text_code(characters[index]);
		advance = (WORD) gem_vdi_font_char_width(gem_vdi_text_font,
			code);
		if (gem_vdi_text_special & GEM_VDI_STYLE_SCALE)
			advance = (WORD) gem_vdi_text_act_siz((UWORD) advance);
		gem_vdi_font_char_offsets(gem_vdi_text_font, code, &left,
			&right);
		advance = (WORD) (advance - left - right);
		if ((gem_vdi_text_special & GEM_VDI_STYLE_THICKEN)
			&& !(gem_vdi_text_head.flags & GEM_VDI_FONT_MONOSPACE))
			advance = (WORD) (advance + gem_vdi_text_weight);
		advance = (WORD) (advance + gem_vdi_text_justify_step(justify,
				code, &word_extra, &char_extra));
		if (advance < 1)
			advance = 1;
		running = negative ? (WORD) (running - advance)
			: (WORD) (running + advance);
	}
	return count;
}

WORD
gem_vdi_text_underline(WORD *x1, WORD *y1, WORD *x2, WORD *y2)
{
	if (!gem_vdi_text_under_on)
		return FALSE;
	if (x1)
		*x1 = gem_vdi_text_under_x1;
	if (y1)
		*y1 = gem_vdi_text_under_y1;
	if (x2)
		*x2 = gem_vdi_text_under_x2;
	if (y2)
		*y2 = gem_vdi_text_under_y2;
	return TRUE;
}

WORD
gem_vdi_text_string_width(const UWORD *characters, UWORD count)
{
	if (!characters || !count)
		return 0;
	return gem_vdi_text_measure(characters, count);
}

/*
 * one character's share of the justify padding, every gap gets the whole-pixel
 * part and the first few get one extra
 */
static WORD
gem_vdi_text_justify_step(const GEM_VDI_TEXT_JUSTIFY *justify, UWORD code,
	WORD *word_extra, WORD *char_extra)
{
	WORD step;

	if (!justify)
		return 0;
	if (code == (UWORD) ' ') {
		step = justify->word_delta;
		if (*word_extra > 0) {
			step = (WORD) (step + justify->sign);
			(*word_extra)--;
		}
		return step;
	}
	step = justify->char_delta;
	if (*char_extra > 0) {
		step = (WORD) (step + justify->sign);
		(*char_extra)--;
	}
	return step;
}

VOID
gem_vdi_text_draw(GEM_VDI_SCREEN *screen, WORD x, WORD y,
	const UWORD *characters, UWORD count,
	const GEM_VDI_TEXT_JUSTIFY *justify)
{
	WORD word_extra;
	WORD char_extra;
	UWORD owner_segment;
	WORD destination_x;
	WORD destination_y;
	WORD delta_h;
	WORD delta_v;
	WORD rdel1;
	WORD rdel2;
	WORD divisor;
	WORD width;
	UWORD index;
	UWORD code;
	UWORD stride;
	UWORD cell_width;
	UWORD cell_rows;
	UWORD draw_stride;
	UWORD draw_width;
	UWORD draw_rows;
	WORD advance;
	WORD left;
	WORD right;

	gem_vdi_text_under_on = FALSE;
	if (!screen || !characters || !count || !gem_vdi_text_font)
		return;
	word_extra = justify ? justify->word_extra : 0;
	char_extra = justify ? justify->char_extra : 0;
	GEM_VDI_TEXT_SAVE_DS(owner_segment);

	/* horizontal alignment needs the extent up front */
	if (gem_vdi_text_h_align) {
		width = gem_vdi_text_measure(characters, count);
		delta_h = (gem_vdi_text_h_align == 1) ? (WORD) (width >> 1)
			: width;
	} else {
		width = 0;
		delta_h = 0;
	}

	divisor = 1;
	switch (gem_vdi_text_v_align) {
	case 1:
		delta_v = (WORD) gem_vdi_text_head.half;
		if (gem_vdi_text_head.top)
			divisor = (WORD) gem_vdi_text_head.top;
		delta_h = (WORD) (delta_h + gem_vdi_text_l_off
			+ ((WORD) gem_vdi_text_head.half
				* gem_vdi_text_r_off) / divisor);
		break;
	case 2:
		delta_v = (WORD) gem_vdi_text_head.ascent;
		if (gem_vdi_text_head.top)
			divisor = (WORD) gem_vdi_text_head.top;
		delta_h = (WORD) (delta_h + gem_vdi_text_l_off
			+ ((WORD) gem_vdi_text_head.ascent
				* gem_vdi_text_r_off) / divisor);
		break;
	case 3:
		delta_v = (WORD) -(WORD) gem_vdi_text_head.bottom;
		if (gem_vdi_text_head.bottom)
			divisor = (WORD) gem_vdi_text_head.bottom;
		delta_h = (WORD) (delta_h
			- (((WORD) gem_vdi_text_head.bottom
					- (WORD) gem_vdi_text_head.descent)
				* gem_vdi_text_l_off) / divisor);
		break;
	case 4:
		delta_v = (WORD) -(WORD) gem_vdi_text_head.descent;
		break;
	case 5:
		delta_v = (WORD) gem_vdi_text_head.top;
		delta_h = (WORD) (delta_h + gem_vdi_text_l_off
			+ gem_vdi_text_r_off);
		break;
	default:
		delta_v = 0;
		delta_h = (WORD) (delta_h + gem_vdi_text_l_off);
		break;
	}

	rdel1 = (WORD) ((WORD) gem_vdi_text_head.top - delta_v);
	rdel2 = (WORD) ((gem_vdi_text_actdely
			- (WORD) gem_vdi_text_head.top - 1) + delta_v);
	switch (gem_vdi_text_rot_case) {
	case 1:
		destination_x = (WORD) (x - rdel1);
		destination_y = (WORD) (y + delta_h);
		break;
	case 2:
		destination_x = (WORD) (x + delta_h);
		destination_y = (WORD) (y - rdel2);
		break;
	case 3:
		destination_x = (WORD) (x - rdel2);
		destination_y = (WORD) (y - delta_h);
		break;
	default:
		destination_x = (WORD) (x - delta_h);
		destination_y = (WORD) (y - rdel1);
		break;
	}

	/* the underline is one filled rectangle along the whole string, not
	 * per character */
	if (gem_vdi_text_special & GEM_VDI_STYLE_UNDER) {
		if (!width)
			width = gem_vdi_text_measure(characters, count);
		switch (gem_vdi_text_rot_case) {
		case 1:
			gem_vdi_text_under_x1 = (WORD) (destination_x
				+ gem_vdi_text_head.top + 1);
			gem_vdi_text_under_x2 = (WORD)
				(gem_vdi_text_under_x1
				+ gem_vdi_text_head.ul_size - 1);
			gem_vdi_text_under_y2 = (WORD) (destination_y
				- gem_vdi_text_l_off);
			gem_vdi_text_under_y1 = (WORD)
				(gem_vdi_text_under_y2 - width);
			break;
		case 2:
			gem_vdi_text_under_x2 = (WORD) (destination_x
				- gem_vdi_text_l_off);
			gem_vdi_text_under_x1 = (WORD)
				(gem_vdi_text_under_x2 - width);
			gem_vdi_text_under_y2 = (WORD) (destination_y
				+ gem_vdi_text_actdely
				- gem_vdi_text_head.top - 1);
			gem_vdi_text_under_y1 = (WORD)
				(gem_vdi_text_under_y2
				- gem_vdi_text_head.ul_size + 1);
			break;
		case 3:
			gem_vdi_text_under_x2 = (WORD) (destination_x
				+ gem_vdi_text_actdely
				- gem_vdi_text_head.top - 1);
			gem_vdi_text_under_x1 = (WORD)
				(gem_vdi_text_under_x2
				- gem_vdi_text_head.ul_size + 1);
			gem_vdi_text_under_y1 = (WORD) (destination_y
				+ gem_vdi_text_l_off);
			gem_vdi_text_under_y2 = (WORD)
				(gem_vdi_text_under_y1 + width);
			break;
		default:
			gem_vdi_text_under_x1 = (WORD) (destination_x
				+ gem_vdi_text_l_off);
			gem_vdi_text_under_x2 = (WORD)
				(gem_vdi_text_under_x1 + width);
			gem_vdi_text_under_y1 = (WORD) (destination_y
				+ gem_vdi_text_head.top + 1);
			gem_vdi_text_under_y2 = (WORD)
				(gem_vdi_text_under_y1
				+ gem_vdi_text_head.ul_size - 1);
			break;
		}
		gem_vdi_text_under_on = TRUE;
	}

	/* plain unrotated system text has two fast paths: one blit for a whole
	 * run, or a glyph at a time otherwise */
	if (gem_vdi_text_is_plain() && !gem_vdi_text_rot_case) {
		for (index = 0; index < count; index++)
			if (characters[index] < 32U)
				break;
		if (index == count && !justify
			&& gem_vdi_text_run(screen, destination_x,
				destination_y,
				(const GEM_VDI_UBYTE *) characters, count, 2U,
				gem_vdi_text_font->data_segment,
				gem_vdi_text_font->dat_offset)) {
			GEM_VDI_TEXT_LOAD_DS(owner_segment);
			return;
		}
		GEM_VDI_TEXT_LOAD_DS(owner_segment);
		for (index = 0; index < count; index++) {
			code = gem_vdi_text_code(characters[index]);
			if (!gem_vdi_font_glyph(gem_vdi_text_font, code,
					gem_vdi_text_glyph_rows, 1U))
				continue;
			gem_vdi_glyph(screen, destination_x, destination_y, 8,
				(WORD) gem_vdi_text_font->form_height,
				gem_vdi_text_glyph_rows, 0x80);
			GEM_VDI_TEXT_LOAD_DS(owner_segment);
			advance =
				(WORD) (8 + gem_vdi_text_justify_step(justify,
					code, &word_extra, &char_extra));
			if (advance < 1)
				advance = 1;
			destination_x = (WORD) (destination_x + advance);
		}
		return;
	}

	/* one cell buffer row holds the glyph plus its effect padding */
	stride = (UWORD) ((gem_vdi_text_head.max_cell_width
			+ gem_vdi_text_char_del + 7) >> 3);
	if (!stride)
		stride = 1;
	if (stride > GEM_VDI_TEXT_CELL_BYTES)
		stride = GEM_VDI_TEXT_CELL_BYTES;

	for (index = 0; index < count; index++) {
		code = gem_vdi_text_code(characters[index]);
		cell_width = gem_vdi_text_compose(code, stride, &cell_rows);
		if (!cell_width)
			continue;

		if (gem_vdi_text_special & GEM_VDI_STYLE_SKEW) {
			gem_vdi_text_italic(gem_vdi_text_form, stride,
				cell_width, cell_rows);
			cell_width = (UWORD) (cell_width
				+ gem_vdi_text_l_off + gem_vdi_text_r_off);
		}
		if (gem_vdi_text_special & GEM_VDI_STYLE_THICKEN) {
			gem_vdi_text_bold(gem_vdi_text_form, stride,
				cell_rows, (UWORD) gem_vdi_text_weight);
			if (!gem_vdi_text_mono)
				cell_width = (UWORD) (cell_width
					+ gem_vdi_text_weight);
		}
		if (gem_vdi_text_special & GEM_VDI_STYLE_LIGHT)
			gem_vdi_text_grey(gem_vdi_text_form, stride, cell_rows);
		if (cell_width > (UWORD) (stride * 8U))
			cell_width = (UWORD) (stride * 8U);

		gem_vdi_text_rotate(stride, cell_width, cell_rows,
			&draw_stride, &draw_width, &draw_rows);

		switch (gem_vdi_text_rot_case) {
		case 1:
			gem_vdi_form(screen, destination_x,
				(WORD) (destination_y - (WORD) draw_rows + 1),
				(WORD) draw_width, (WORD) draw_rows,
				gem_vdi_text_form, draw_stride);
			break;
		case 2:
			gem_vdi_form(screen,
				(WORD) (destination_x - (WORD) draw_width + 1),
				destination_y, (WORD) draw_width,
				(WORD) draw_rows, gem_vdi_text_form,
				draw_stride);
			break;
		default:
			gem_vdi_form(screen, destination_x, destination_y,
				(WORD) draw_width, (WORD) draw_rows,
				gem_vdi_text_form, draw_stride);
			break;
		}
		GEM_VDI_TEXT_LOAD_DS(owner_segment);

		/* advance by the cell, minus this glyph's own kerning */
		gem_vdi_font_char_offsets(gem_vdi_text_font, code, &left,
			&right);
		advance = (WORD) (cell_width - left - right
			+ gem_vdi_text_justify_step(justify, code,
				&word_extra, &char_extra));
		if (advance < 1)
			advance = 1;
		switch (gem_vdi_text_rot_case) {
		case 1:
			destination_y = (WORD) (destination_y - advance);
			break;
		case 2:
			destination_x = (WORD) (destination_x - advance);
			break;
		case 3:
			destination_y = (WORD) (destination_y + advance);
			break;
		default:
			destination_x = (WORD) (destination_x + advance);
			break;
		}
	}
}
