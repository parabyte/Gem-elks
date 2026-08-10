/*
 * gem_vdi_output.c - the resident VDI's drawing operations
 *
 * the fill, text, marker and GDP drawing the dispatcher hands off to, plus the
 * tables they read - the pattern and hatch rows and the six marker shapes
 */

#include "gem_vdi_internal.h"

#include "gem_vdi_font.h"
#include "gem_vdi_palette.h"
#include "drivers/gem_pcvideo.h"

/*
 * dither patterns for FIS_PATTERN styles one through six, row zero is hollow,
 * row seven is solid
 */
static const GEM_VDI_UBYTE gem_vdi_fill_patterns[8][8] = {
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	{ 0x00, 0x44, 0x00, 0x11, 0x00, 0x44, 0x00, 0x11 },
	{ 0x00, 0x55, 0x00, 0x55, 0x00, 0x55, 0x00, 0x55 },
	{ 0x88, 0x55, 0x22, 0x55, 0x88, 0x55, 0x22, 0x55 },
	{ 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55 },
	{ 0xaa, 0xdd, 0xaa, 0x77, 0xaa, 0xdd, 0xaa, 0x77 },
	{ 0xaa, 0xff, 0xaa, 0xff, 0xaa, 0xff, 0xaa, 0xff },
	{ 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff }
};

/*
 * hatch patterns for FIS_HATCH styles one through twelve, the wide hatches
 * were 16 pixels, folded here onto the 8-pixel grid we tile with
 */
static const GEM_VDI_UBYTE gem_vdi_fill_hatches[12][8] = {
	{ 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88 },	/* vertical */
	{ 0xff, 0x00, 0x00, 0x00, 0xff, 0x00, 0x00, 0x00 },	/* horizontal */
	{ 0x88, 0x11, 0x22, 0x44, 0x88, 0x11, 0x22, 0x44 },	/* plus 45 */
	{ 0x88, 0x44, 0x22, 0x11, 0x88, 0x44, 0x22, 0x11 },	/* minus 45 */
	{ 0xff, 0x88, 0x88, 0x88, 0xff, 0x88, 0x88, 0x88 },	/* cross */
	{ 0x88, 0x55, 0x22, 0x55, 0x88, 0x55, 0x22, 0x55 },	/* x-cross */
	{ 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80 },	/* wide vertical */
	{ 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },	/* wide horizontal */
	{ 0x80, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40 },	/* wide plus 45 */
	{ 0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01 },	/* wide minus 45 */
	{ 0xff, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80 },	/* wide cross */
	{ 0x80, 0x41, 0x22, 0x14, 0x08, 0x14, 0x22, 0x41 }	/* wide x-cross */
};

/*
 * the six marker shapes, each entry is a count of polylines, then for each one
 * a point count and that many signed x, y offsets from the marker centre
 */
static const WORD gem_vdi_marker_dot[] = { 1, 2, 0, 0, 0, 0 };

static const WORD gem_vdi_marker_plus[] = {
	2, 2, 0, -3, 0, 3, 2, -4, 0, 4, 0
};

static const WORD gem_vdi_marker_star[] = {
	3, 2, 0, -3, 0, 3, 2, 3, 2, -3, -2, 2, 3, -2, -3, 2
};

static const WORD gem_vdi_marker_square[] = {
	1, 5, -4, -3, 4, -3, 4, 3, -4, 3, -4, -3
};

static const WORD gem_vdi_marker_cross[] = {
	2, 2, -4, -3, 4, 3, 2, -4, 3, 4, -3
};

static const WORD gem_vdi_marker_diamond[] = {
	1, 5, -4, 0, 0, -3, 4, 0, 0, 3, -4, 0
};

static const WORD *const gem_vdi_marker_shapes[GEM_VDI_MARKER_TYPES] = {
	gem_vdi_marker_dot,
	gem_vdi_marker_plus,
	gem_vdi_marker_star,
	gem_vdi_marker_square,
	gem_vdi_marker_cross,
	gem_vdi_marker_diamond
};

/* user-defined interiors use the stored pattern */
const GEM_VDI_UBYTE *
gem_vdi_fill_rows(const GEM_VDI_RESIDENT_WORKSTATION *workstation)
{
	WORD style;

	style = workstation->fill_style;
	if (style < 1)
		style = 1;
	switch (workstation->fill_interior) {
	case GEM_VDI_FIS_PATTERN:
		if (style > 8)
			return gem_vdi_fill_patterns[4];
		if (style > 6)
			return NULL;
		return gem_vdi_fill_patterns[style];
	case GEM_VDI_FIS_HATCH:
		if (style > 12)
			style = 12;
		return gem_vdi_fill_hatches[style - 1];
	case GEM_VDI_FIS_USER:
		return workstation->user_pattern;
	default:
		return NULL;
	}
}

VOID
gem_vdi_fill_output(GEM_VDI_RESIDENT_WORKSTATION *workstation,
	WORD honor_perimeter)
{
	GEM_VDI_COORD x1;
	GEM_VDI_COORD y1;
	GEM_VDI_COORD x2;
	GEM_VDI_COORD y2;
	GEM_VDI_COORD width;
	GEM_VDI_COORD height;
	const GEM_VDI_UBYTE *pattern;
	UWORD owner_segment;
	WORD perimeter;

	x1 = (WORD) gem_vdi_ptsin[0];
	y1 = (WORD) gem_vdi_ptsin[1];
	x2 = (WORD) gem_vdi_ptsin[2];
	y2 = (WORD) gem_vdi_ptsin[3];
	if (x2 < x1 || y2 < y1)
		return;
	width = x2 - x1 + 1;
	height = y2 - y1 + 1;
	perimeter = honor_perimeter && workstation->fill_perimeter;
	__asm__ volatile ("movw %%ds,%0":"=r" (owner_segment)
		::"memory");
	if (workstation->fill_interior == GEM_VDI_FIS_HOLLOW) {
		/* hollow is the all-zero pattern: replace clears to white,
		 * transparent and XOR draw nothing, reverse transparent covers
		 * the box in the fill color */
		if (workstation->write_mode == GEM_VDI_MD_REPLACE
			|| workstation->write_mode == GEM_VDI_MD_ERASE) {
			gem_vdi_apply_workstation(workstation,
				workstation->write_mode == GEM_VDI_MD_ERASE
				? workstation->fill_color : GEM_VDI_WHITE);
			gem_vdi_set_mode(GEM_VDI_REPLACE);
			gem_vdi_fill_rect(gem_vdi_screen, x1, y1, width,
				height);
			__asm__ volatile ("movw %0,%%ds"::"r"
				(owner_segment):"memory");
		}
	} else {
		pattern = gem_vdi_fill_rows(workstation);
		gem_vdi_apply_workstation(workstation, workstation->fill_color);
		if (!pattern)
			gem_vdi_fill_rect(gem_vdi_screen, x1, y1, width,
				height);
		else
			gem_vdi_fill_pattern(gem_vdi_screen, x1, y1, width,
				height, pattern);
		__asm__ volatile ("movw %0,%%ds"::"r" (owner_segment):"memory");
	}
	if (!perimeter)
		return;

	/* draw the outline in the fill color */
	gem_vdi_apply_workstation(workstation, workstation->fill_color);
	gem_vdi_rect(gem_vdi_screen, x1, y1, width, height);
	__asm__ volatile ("movw %0,%%ds"::"r" (owner_segment):"memory");
}

/*
 * fill a polygon, then if the perimeter is on, outline it in the fill color
 * closed back to the first point
 */
WORD
gem_vdi_fill_area(GEM_VDI_RESIDENT_WORKSTATION *workstation)
{
	const GEM_VDI_UBYTE *pattern;
	GEM_VDI_COORD *line;
	GEM_VDI_COORD first_x;
	GEM_VDI_COORD first_y;
	GEM_VDI_COORD x1;
	GEM_VDI_COORD y1;
	GEM_VDI_COORD x2;
	GEM_VDI_COORD y2;
	UWORD owner_segment;
	UWORD remaining;
	WORD points;

	points = (WORD) gem_vdi_control[1];
	__asm__ volatile ("movw %%ds,%0":"=r" (owner_segment)
		::"memory");
	if (workstation->fill_interior == GEM_VDI_FIS_HOLLOW) {
		/* same hollow writing-mode rules as the bar path */
		if (workstation->write_mode == GEM_VDI_MD_REPLACE
			|| workstation->write_mode == GEM_VDI_MD_ERASE) {
			gem_vdi_apply_workstation(workstation,
				workstation->write_mode == GEM_VDI_MD_ERASE
				? workstation->fill_color : GEM_VDI_WHITE);
			gem_vdi_set_mode(GEM_VDI_REPLACE);
			gem_vdi_fill_polygon(gem_vdi_screen, points,
				(WORD *) gem_vdi_ptsin);
			__asm__ volatile ("movw %0,%%ds"::"r"
				(owner_segment):"memory");
		}
	} else {
		pattern = gem_vdi_fill_rows(workstation);
		gem_vdi_apply_workstation(workstation, workstation->fill_color);
		gem_vdi_fill_polygon_pattern(gem_vdi_screen, points,
			(WORD *) gem_vdi_ptsin, pattern);
		__asm__ volatile ("movw %0,%%ds"::"r" (owner_segment):"memory");
	}
	if (!workstation->fill_perimeter)
		return TRUE;

	gem_vdi_apply_workstation(workstation, workstation->fill_color);
	line = (GEM_VDI_COORD *) gem_vdi_ptsin;
	first_x = *line++;
	first_y = *line++;
	x1 = first_x;
	y1 = first_y;
	remaining = (UWORD) points - 1U;
	while (remaining--) {
		x2 = *line++;
		y2 = *line++;
		gem_vdi_line(gem_vdi_screen, x1, y1, x2, y2, TRUE);
		__asm__ volatile ("movw %0,%%ds"::"r" (owner_segment)
			:"memory");
		x1 = x2;
		y1 = y2;
	}
	gem_vdi_line(gem_vdi_screen, x1, y1, first_x, first_y, TRUE);
	__asm__ volatile ("movw %0,%%ds"::"r" (owner_segment):"memory");
	return TRUE;
}

const GEM_VDI_UBYTE *
gem_vdi_system_glyph_rows(WORD character)
{
	UWORD glyph_offset;
	UWORD row;

	/* the system font is one byte per row per glyph, gem_vdi_font_rows rows
	 * to a cell (8 on CGA, 14 on EGA and Hercules, 16 on VGA), every code is
	 * a glyph, pad short cells so callers always get a full
	 * GEM_VDI_SYSTEM_GLYPH_BYTES-row glyph */
	if (!gem_vdi_font_segment)
		return NULL;
	glyph_offset = (UWORD) (((UWORD) character & 0x00ffU)
		* gem_vdi_font_rows);
	glyph_offset = (UWORD) (glyph_offset + gem_vdi_font_offset);
	gem_resident_memory_from(gem_vdi_font_segment, glyph_offset,
		gem_vdi_system_glyph, gem_vdi_font_rows);
	for (row = gem_vdi_font_rows; row < GEM_VDI_SYSTEM_GLYPH_BYTES; row++)
		gem_vdi_system_glyph[row] = 0;
	return gem_vdi_system_glyph;
}

/*
 * any code below 32 cant use the fast string path, so flag it for the
 * per-glyph path
 */
WORD
gem_vdi_text_has_furniture(const GEM_VDI_UBYTE *characters, UWORD count,
	UWORD stride)
{
	while (count--) {
		if (*characters < 32U)
			return TRUE;
		characters += stride;
	}
	return FALSE;
}

WORD __far __attribute__((far_section, noinline,
		section(".fartext.gemvdi_draw_text")))
	gem_vdi_draw_text(GEM_VDI_RESIDENT_WORKSTATION *workstation,
	const UWORD *characters, UWORD count,
	const GEM_VDI_TEXT_JUSTIFY *justify)
{
	UWORD owner_segment;
	WORD x1;
	WORD y1;
	WORD x2;
	WORD y2;

	if (!count)
		return TRUE;
	gem_vdi_apply_workstation(workstation, workstation->text_color);
	__asm__ volatile ("movw %%ds,%0":"=r" (owner_segment)::"memory");
	gem_vdi_text_draw(gem_vdi_screen, (WORD) gem_vdi_ptsin[0],
		(WORD) gem_vdi_ptsin[1], characters, count, justify);
	__asm__ volatile ("movw %0,%%ds"::"r" (owner_segment):"memory");

	/* the underline is one line under the whole string, not per character */
	if (gem_vdi_text_underline(&x1, &y1, &x2, &y2)) {
		if (x2 < x1) {
			WORD swap;

			swap = x1;
			x1 = x2;
			x2 = swap;
		}
		if (y2 < y1) {
			WORD swap;

			swap = y1;
			y1 = y2;
			y2 = swap;
		}
		gem_vdi_fill_rect(gem_vdi_screen, x1, y1,
			(WORD) (x2 - x1 + 1), (WORD) (y2 - y1 + 1));
		__asm__ volatile ("movw %0,%%ds"::"r" (owner_segment)
			:"memory");
	}
	return TRUE;
}

/*
 * draw each marker as its shape's polylines, scaled by the marker height and
 * centred on the point, in the marker colour with a solid line
 */
WORD
gem_vdi_draw_markers(GEM_VDI_RESIDENT_WORKSTATION *workstation)
{
	const GEM_VDI_COORD *points;
	UWORD owner_segment;
	UWORD count;
	WORD polylines;
	WORD vertices;
	WORD scale;
	WORD center_x;
	WORD center_y;
	WORD x1;
	WORD y1;
	WORD x2;
	WORD y2;
	WORD type;

	count = gem_vdi_control[1];
	if (!count)
		return FALSE;
	type = workstation->marker_type;
	if (type < 1 || type > GEM_VDI_MARKER_TYPES)
		type = GEM_VDI_MARKER_DOT;
	scale = (WORD) ((workstation->marker_height
			+ GEM_VDI_MARKER_HEIGHT / 2) / GEM_VDI_MARKER_HEIGHT);
	if (scale < 1)
		scale = 1;
	gem_vdi_apply_workstation(workstation, workstation->marker_color);
	points = (const GEM_VDI_COORD *) gem_vdi_ptsin;
	__asm__ volatile ("movw %%ds,%0":"=r" (owner_segment)::"memory");
	while (count--) {
		const WORD *segment;
		WORD index;

		center_x = *points++;
		center_y = *points++;
		segment = gem_vdi_marker_shapes[type - 1];
		polylines = *segment++;
		while (polylines--) {
			vertices = *segment++;
			if (vertices < 2)
				break;
			x1 = (WORD) (center_x + scale * segment[0]);
			y1 = (WORD) (center_y + scale * segment[1]);
			for (index = 1; index < vertices; index++) {
				x2 = (WORD) (center_x
					+ scale * segment[index * 2]);
				y2 = (WORD) (center_y
					+ scale * segment[index * 2 + 1]);
				gem_vdi_line(gem_vdi_screen, x1, y1, x2, y2,
					TRUE);
				__asm__ volatile ("movw %0,%%ds"::"r"
					(owner_segment):"memory");
				x1 = x2;
				y1 = y2;
			}
			segment += vertices * 2;
		}
	}
	return TRUE;
}

/*
 * work out the justify padding: whole pixels to every gap, then the remainder
 * one pixel at a time from the left
 */
VOID
gem_vdi_text_justify(const UWORD *characters, UWORD count, UWORD word_flag,
	UWORD char_flag, WORD length, GEM_VDI_TEXT_JUSTIFY *justify)
{
	UWORD index;
	WORD spaces;
	WORD slack;

	justify->word_delta = 0;
	justify->word_extra = 0;
	justify->char_delta = 0;
	justify->char_extra = 0;
	slack = (WORD) (length - gem_vdi_text_string_width(characters, count));
	justify->sign = (slack < 0) ? -1 : 1;
	if (slack < 0)
		slack = (WORD) -slack;
	spaces = 0;
	for (index = 0; index < count; index++)
		if (characters[index] == (UWORD) ' ')
			spaces++;
	if (word_flag && spaces) {
		justify->word_delta = (WORD) ((slack / spaces)
			* justify->sign);
		justify->word_extra = (WORD) (slack % spaces);
	} else if (char_flag && count) {
		justify->char_delta = (WORD) ((slack / (WORD) count)
			* justify->sign);
		justify->char_extra = (WORD) (slack % (WORD) count);
	}
}

WORD
gem_vdi_dispatch_gdp(GEM_VDI_RESIDENT_WORKSTATION *workstation)
{
	WORD subfunction;
	WORD x1;
	WORD y1;
	WORD x2;
	WORD y2;
	WORD rx;
	WORD ry;

	subfunction = (WORD) gem_vdi_control[5];
	if (subfunction == GEM_VDI_GDP_JUSTIFIED) {
		/* the interword and intercharacter flags come before the
		 * string in intin, ptsin carries the position then the
		 * length the string has to fill */
		GEM_VDI_TEXT_JUSTIFY justify;
		const UWORD *characters;
		UWORD count;

		if (gem_vdi_control[3] < 3U)
			return FALSE;
		characters = gem_vdi_intin + 2;
		count = (UWORD) (gem_vdi_control[3] - 2U);
		gem_vdi_text_justify(characters, count, gem_vdi_intin[0],
			gem_vdi_intin[1], (WORD) gem_vdi_ptsin[2], &justify);
		return gem_vdi_draw_text(workstation, characters, count,
			&justify);
	}
	if (subfunction == GEM_VDI_GDP_BAR
		|| subfunction == GEM_VDI_GDP_FILLED_ROUNDED_BOX) {
		gem_vdi_fill_output(workstation, TRUE);
		return TRUE;
	}
	if (subfunction == GEM_VDI_GDP_ROUNDED_BOX) {
		x1 = (WORD) gem_vdi_ptsin[0];
		y1 = (WORD) gem_vdi_ptsin[1];
		x2 = (WORD) gem_vdi_ptsin[2];
		y2 = (WORD) gem_vdi_ptsin[3];
		if (x2 < x1 || y2 < y1)
			return FALSE;
		gem_vdi_apply_workstation(workstation, workstation->line_color);
		gem_vdi_rect(gem_vdi_screen, x1, y1, x2 - x1 + 1, y2 - y1 + 1);
		return TRUE;
	}
	if (gem_vdi_control[1] < 2)
		return FALSE;
	x1 = (WORD) gem_vdi_ptsin[0];
	y1 = (WORD) gem_vdi_ptsin[1];
	rx = (WORD) gem_vdi_ptsin[2];
	ry = gem_vdi_control[1] > 2 ? (WORD) gem_vdi_ptsin[3] : rx;
	gem_vdi_apply_workstation(workstation,
		(subfunction == GEM_VDI_GDP_PIE
			|| subfunction == GEM_VDI_GDP_CIRCLE
			|| subfunction == GEM_VDI_GDP_ELLIPSE
			|| subfunction == GEM_VDI_GDP_ELLIPTIC_PIE)
		? workstation->fill_color : workstation->line_color);
	if (subfunction == GEM_VDI_GDP_CIRCLE
		|| subfunction == GEM_VDI_GDP_ELLIPSE) {
		gem_vdi_ellipse(gem_vdi_screen, x1, y1, rx, ry, TRUE);
		return TRUE;
	}
	if (gem_vdi_control[3] < 2)
		return FALSE;
	x2 = (WORD) gem_vdi_intin[0];
	y2 = (WORD) gem_vdi_intin[1];
	gem_vdi_arc(gem_vdi_screen, x1, y1, rx, ry, x2, y2,
		(subfunction == GEM_VDI_GDP_PIE
			|| subfunction == GEM_VDI_GDP_ELLIPTIC_PIE)
		? GEM_VDI_ARC_PIE : GEM_VDI_ARC_OUTLINE);
	return TRUE;
}

WORD
gem_vdi_screen_copy(GEM_VDI_RESIDENT_WORKSTATION *workstation,
	const struct gemtrap_request *request)
{
	GEM_BINDINGS_POINTER_SLOT source_pointer;
	GEM_BINDINGS_POINTER_SLOT destination_pointer;
	GEM_VDI_RESIDENT_MFDB source;
	GEM_VDI_RESIDENT_MFDB destination;
	WORD source_is_screen;
	WORD destination_is_screen;
	WORD width;
	WORD height;
	WORD mode;

	source_pointer.lo = gem_vdi_control[7];
	source_pointer.hi = gem_vdi_control[8];
	destination_pointer.lo = gem_vdi_control[9];
	destination_pointer.hi = gem_vdi_control[10];
	source.memory.lo = 0;
	source.memory.hi = 0;
	destination.memory.lo = 0;
	destination.memory.hi = 0;
	if (source_pointer.lo || source_pointer.hi) {
		if (!gem_resident_memory_pointer(request, source_pointer,
				sizeof(source)))
			return FALSE;
		gem_resident_memory_from(request->ds, source_pointer.lo,
			&source, sizeof(source));
	}
	if (destination_pointer.lo || destination_pointer.hi) {
		if (!gem_resident_memory_pointer(request, destination_pointer,
				sizeof(destination)))
			return FALSE;
		gem_resident_memory_from(request->ds, destination_pointer.lo,
			&destination, sizeof(destination));
	}
	source_is_screen = !(source_pointer.lo || source_pointer.hi)
		|| !(source.memory.lo || source.memory.hi);
	destination_is_screen = !(destination_pointer.lo
		|| destination_pointer.hi)
		|| !(destination.memory.lo || destination.memory.hi);
	if (!source_is_screen || !destination_is_screen)
		return FALSE;
	width = (WORD) gem_vdi_ptsin[2] - (WORD) gem_vdi_ptsin[0] + 1;
	height = (WORD) gem_vdi_ptsin[3] - (WORD) gem_vdi_ptsin[1] + 1;
	if (width <= 0 || height <= 0)
		return FALSE;
	mode = (WORD) gem_vdi_intin[0];
	gem_vdi_apply_workstation(workstation, workstation->line_color);
	gem_vdi_blit(gem_vdi_screen, (WORD) gem_vdi_ptsin[4],
		(WORD) gem_vdi_ptsin[5], width, height,
		(WORD) gem_vdi_ptsin[0], (WORD) gem_vdi_ptsin[1], mode & 15);
	return TRUE;
}

/*
 * byte size of one classic MFDB, built by checked repeated adds, a form too
 * big for one real-mode offset window is rejected before any address is
 * touched
 */
static WORD __attribute__((optimize("Os")))
	gem_vdi_form_bytes(const GEM_VDI_RESIDENT_MFDB *form, UWORD *bytes)
{
	UWORD row_bytes;
	UWORD plane_bytes;
	UWORD total;
	UWORD remaining;

	if (!form || !bytes || form->width_pixels <= 0 || form->height <= 0
		|| form->width_words <= 0 || form->planes <= 0
		|| form->planes > 8 || (form->format != 0 && form->format != 1)
		|| (UWORD) form->width_words > 0x7fffU)
		return FALSE;
	row_bytes = (UWORD) form->width_words;
	__asm__ volatile ("shlw %0":"+r" (row_bytes)::"cc");

	plane_bytes = 0;
	remaining = (UWORD) form->height;
	while (remaining--) {
		if (row_bytes > (UWORD) (0xffffU - plane_bytes))
			return FALSE;
		plane_bytes += row_bytes;
	}
	total = 0;
	remaining = (UWORD) form->planes;
	while (remaining--) {
		if (plane_bytes > (UWORD) (0xffffU - total))
			return FALSE;
		total += plane_bytes;
	}
	if (!total)
		return FALSE;
	*bytes = total;
	return TRUE;
}

/*
 * copy one checked span between real-mode segments, an overlapping move within
 * one segment runs backward then CLD puts the direction flag back as the ABI
 * wants, DS and ES are saved around the transfer
 */
static VOID __attribute__((optimize("Os")))
	gem_vdi_far_move(GEM_BINDINGS_POINTER_SLOT source,
	GEM_BINDINGS_POINTER_SLOT destination, UWORD count)
{
	UWORD distance;

	if (!count || (source.lo == destination.lo
			&& source.hi == destination.hi))
		return;
	if (source.hi == destination.hi && destination.lo > source.lo) {
		distance = destination.lo - source.lo;
		if (distance < count) {
			source.lo += count - 1U;
			destination.lo += count - 1U;
			__asm__ volatile ("pushw %%ds\n\t"
				"pushw %%es\n\t"
				"movw %3,%%ds\n\t"
				"movw %4,%%es\n\t"
				"std\n\t"
				"rep movsb\n\t"
				"cld\n\t"
				"popw %%es\n\t"
				"popw %%ds":"+S" (source.lo),
				"+D"(destination.lo), "+c"(count)
				:"r"(source.hi), "r"(destination.hi)
				:"cc", "memory");
			return;
		}
	}
	__asm__ volatile ("pushw %%ds\n\t"
		"pushw %%es\n\t"
		"movw %3,%%ds\n\t"
		"movw %4,%%es\n\t"
		"cld\n\t"
		"rep movsb\n\t"
		"popw %%es\n\t"
		"popw %%ds":"+S" (source.lo), "+D"(destination.lo), "+c"(count)
		:"r"(source.hi), "r"(destination.hi)
		:"cc", "memory");
}

/*
 * opcode 110, standard and device forms use the same byte order here so the
 * transform is just a checked memmove
 */
WORD __attribute__((optimize("Os")))
	gem_vdi_transform_form(const struct gemtrap_request *request)
{
	GEM_BINDINGS_POINTER_SLOT source_pointer;
	GEM_BINDINGS_POINTER_SLOT destination_pointer;
	GEM_VDI_RESIDENT_MFDB source;
	GEM_VDI_RESIDENT_MFDB destination;
	UWORD source_bytes;

	source_pointer.lo = gem_vdi_control[7];
	source_pointer.hi = gem_vdi_control[8];
	destination_pointer.lo = gem_vdi_control[9];
	destination_pointer.hi = gem_vdi_control[10];
	if (!gem_resident_memory_pointer(request, source_pointer,
			sizeof(source))
		|| !gem_resident_memory_pointer(request, destination_pointer,
			sizeof(destination)))
		return FALSE;
	gem_resident_memory_from(request->ds, source_pointer.lo, &source,
		sizeof(source));
	gem_resident_memory_from(request->ds, destination_pointer.lo,
		&destination, sizeof(destination));
	if (source.width_pixels != destination.width_pixels
		|| source.height != destination.height
		|| source.width_words != destination.width_words
		|| source.planes != destination.planes
		|| (destination.format != 0 && destination.format != 1)
		|| !gem_vdi_form_bytes(&source, &source_bytes)
		|| !source.memory.hi || !destination.memory.hi
		|| source_bytes - 1U > (UWORD) (0xffffU - source.memory.lo)
		|| source_bytes - 1U
		> (UWORD) (0xffffU - destination.memory.lo))
		return FALSE;

	gem_vdi_far_move(source.memory, destination.memory, source_bytes);
	return TRUE;
}
