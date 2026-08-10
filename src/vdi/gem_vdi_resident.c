/*
 * gem_vdi_resident.c - original-array VDI dispatcher for resident ELKS GEM
 *
 * clients pass one packed five-pointer VDIPB at DS:DX, the 20-byte record and
 * each declared array are checked against the client DS, the WORD arrays are
 * copied as-is, and the classic operation numbers go to the native PC video
 * core, eight fixed virtual workstations keep per-owner state, the chosen one
 * is loaded into the one physical adapter before drawing
 */

#include "gem_resident_memory.h"
#include "gem_vdi_palette.h"
#include "gem_vdi_internal.h"
#include "gem_system_resource.h"
#include "gem_vdi_text.h"
#include "gem_vdi_sound.h"
#include "vdi.h"
#include "drivers/gem_pcvideo.h"

static GEM_VDI_RESIDENT_WORKSTATION
	gem_vdi_workstations[GEM_VDI_RESIDENT_WORKSTATIONS];
GEM_VDI_SCREEN *gem_vdi_screen;
static UWORD gem_vdi_open_count;

/* the classic VDI array sizes */
UWORD gem_vdi_control[GEM_VDI_CONTROL_WORDS];
UWORD gem_vdi_intin[GEM_VDI_INTIN_WORDS];
UWORD gem_vdi_ptsin[GEM_VDI_PTSIN_WORDS];
UWORD gem_vdi_intout[GEM_VDI_INTOUT_WORDS];
UWORD gem_vdi_ptsout[GEM_VDI_PTSOUT_WORDS];

static BYTE gem_vdi_resident_text_buffer[GEM_VDI_RESIDENT_TEXT_MAX];
GEM_VDI_UBYTE gem_vdi_system_glyph[GEM_VDI_SYSTEM_GLYPH_BYTES];
UWORD gem_vdi_font_segment;
UWORD gem_vdi_font_offset;
UWORD gem_vdi_font_rows = 16U;

VOID
gem_vdi_clear_words(UWORD *words, UWORD count)
{
	while (count--)
		*words++ = 0;
}

/* shared checks for VS_COLOR and VQ_COLOR, the opcode picks the direction */
static WORD __attribute__((optimize("Os")))
	gem_vdi_palette_request(VOID)
{
	UWORD index;
	WORD setting;

	setting = gem_vdi_control[0] == GEM_VDI_OP_SET_COLOR;
	if (gem_vdi_control[3] < (setting ? 4U : 2U)
		|| gem_vdi_intin[0] >= 16U)
		return FALSE;
	index = gem_vdi_intin[0];
	if (setting) {
		gem_vdi_palette_set(index, &gem_vdi_intin[1]);
	} else {
		gem_vdi_intout[0] = index;
		gem_vdi_palette_get(index, &gem_vdi_intout[1]);
		gem_vdi_control[4] = 4;
	}
	return TRUE;
}

GEM_VDI_WORD __attribute__((optimize("Os")))
	gem_vdi_resident_mode(WORD mode)
{
	switch (mode) {
	case GEM_VDI_MD_TRANS:
		return GEM_VDI_OR;
	case GEM_VDI_MD_XOR:
		return GEM_VDI_XOR;
	case GEM_VDI_MD_ERASE:
		return GEM_VDI_AND;
	case GEM_VDI_MD_REPLACE:
	default:
		return GEM_VDI_REPLACE;
	}
}

static GEM_VDI_RESIDENT_WORKSTATION *
gem_vdi_find_workstation(WORD application, WORD handle)
{
	GEM_VDI_RESIDENT_WORKSTATION *workstation;
	UWORD remaining;

	workstation = gem_vdi_workstations;
	remaining = GEM_VDI_RESIDENT_WORKSTATIONS;
	while (remaining--) {
		if (workstation->state == GEM_VDI_WS_OPEN
			&& workstation->application == application
			&& workstation->handle == handle)
			return workstation;
		workstation++;
	}
	return NULL;
}

/* which attribute class each GDP draws with */
static const UWORD gem_vdi_gdp_attribute[GEM_VDI_GDP_COUNT] = {
	3, 0, 3, 3, 3, 0, 3, 0, 3, 2
};

static GEM_VDI_RESIDENT_WORKSTATION * __attribute__((optimize("Os")))
	gem_vdi_allocate_workstation(WORD application)
{
	GEM_VDI_RESIDENT_WORKSTATION *workstation;
	UWORD handle;
	UWORD index;

	workstation = gem_vdi_workstations;
	handle = 1U;
	while (handle <= GEM_VDI_RESIDENT_WORKSTATIONS) {
		if (workstation->state == GEM_VDI_WS_FREE) {
			workstation->application = application;
			workstation->handle = (WORD) handle;
			workstation->write_mode = GEM_VDI_MD_REPLACE;
			workstation->line_type = 1;
			workstation->line_width = 1;
			workstation->line_color = GEM_VDI_BLACK;
			workstation->line_pattern = 0xffffU;
			workstation->line_begin = 0;
			workstation->line_end = 0;
			workstation->text_color = GEM_VDI_BLACK;
			workstation->text_height = GEM_VDI_CHAR_HEIGHT;
			workstation->marker_type = GEM_VDI_MARKER_STAR;
			workstation->marker_height = GEM_VDI_MARKER_HEIGHT;
			workstation->marker_color = GEM_VDI_BLACK;
			workstation->input_mode[0] = GEM_VDI_INPUT_REQUEST;
			workstation->input_mode[1] = GEM_VDI_INPUT_REQUEST;
			workstation->input_mode[2] = GEM_VDI_INPUT_REQUEST;
			workstation->input_mode[3] = GEM_VDI_INPUT_REQUEST;
			for (index = 0; index < 8U; index++)
				workstation->user_pattern[index] = 0xff;
			workstation->fill_interior = GEM_VDI_FIS_SOLID;
			workstation->fill_style = 7;
			workstation->fill_color = GEM_VDI_BLACK;
			workstation->fill_perimeter = 1;
			workstation->clip_on = FALSE;
			workstation->clip_x1 = 0;
			workstation->clip_y1 = 0;
			workstation->clip_x2 = gem_vdi_screen->xres - 1;
			workstation->clip_y2 = gem_vdi_screen->yres - 1;
			workstation->cursor_hides = 1U;
			workstation->state = GEM_VDI_WS_OPEN;
			return workstation;
		}
		workstation++;
		handle++;
	}
	return NULL;
}

VOID
gem_vdi_apply_workstation(GEM_VDI_RESIDENT_WORKSTATION *workstation,
	WORD logical_color)
{
	GEM_VDI_RECT clip;

	gem_vdi_set_mode(gem_vdi_resident_mode(workstation->write_mode));
	gem_vdi_set_foreground(gem_vdi_screen,
		gem_vdi_resident_color(logical_color));
	gem_vdi_set_background(gem_vdi_screen,
		gem_vdi_resident_color(GEM_VDI_WHITE));
	if (!workstation->clip_on) {
		gem_vdi_set_clip(gem_vdi_screen, 0, NULL);
		return;
	}
	clip.x = workstation->clip_x1;
	clip.y = workstation->clip_y1;
	clip.width = workstation->clip_x2 - workstation->clip_x1 + 1;
	clip.height = workstation->clip_y2 - workstation->clip_y1 + 1;
	gem_vdi_set_clip(gem_vdi_screen, 1, &clip);
}

WORD
gem_vdi_set_one_output(WORD value)
{
	gem_vdi_intout[0] = (UWORD) value;
	gem_vdi_control[4] = 1;
	return TRUE;
}

/*
 * the workstation info reply: forty-five integer words plus six point pairs,
 * both open workstation and vq_extnd with a zero flag answer with it
 */
static VOID __attribute__((optimize("Os")))
	gem_vdi_fill_work_out(const GEM_VDI_RESIDENT_WORKSTATION *workstation)
{
	UWORD character_width;
	UWORD character_height;
	UWORD index;

	gem_vdi_clear_words(gem_vdi_intout, GEM_VDI_INTOUT_WORDS);
	(void) workstation;
	gem_vdi_intout[0] = (UWORD) (gem_vdi_screen->xres - 1);
	gem_vdi_intout[1] = (UWORD) (gem_vdi_screen->yres - 1);
	gem_vdi_intout[2] = 1;
	gem_vdi_intout[3] = 1;
	gem_vdi_intout[4] = 1;
	/* character heights, then font faces */
	gem_vdi_intout[5] = gem_vdi_text_size_count();
	gem_vdi_intout[6] = GEM_VDI_LINE_TYPES;
	gem_vdi_intout[7] = 1;
	gem_vdi_intout[8] = GEM_VDI_MARKER_TYPES;
	gem_vdi_intout[9] = GEM_VDI_MARKER_SIZES;
	gem_vdi_intout[10] = gem_vdi_text_face_count();
	/* eight pattern and twelve hatch styles served, higher requests clamp */
	gem_vdi_intout[11] = 8;
	gem_vdi_intout[12] = 12;
	gem_vdi_intout[13] = 16;
	/* how many GDPs there are, their ids, then the attribute class each
	 * draws with (0 polyline, 1 polymarker, 2 text, 3 fill area) */
	gem_vdi_intout[14] = GEM_VDI_GDP_COUNT;
	for (index = 0; index < GEM_VDI_GDP_COUNT; index++) {
		gem_vdi_intout[15U + index] = (UWORD) (index + 1U);
		gem_vdi_intout[25U + index] = gem_vdi_gdp_attribute[index];
	}
	/* colour, text rotation, polygon fill, cell array, palette size, then
	 * how many of each input device and its type (two is output plus
	 * input) */
	gem_vdi_intout[35] = (gem_vdi_screen->colors > 2U) ? 1U : 0U;
	gem_vdi_intout[36] = 1;
	gem_vdi_intout[37] = 1;
	gem_vdi_intout[38] = 0;
	gem_vdi_intout[39] = gem_vdi_screen->colors;
	gem_vdi_intout[40] = 2;
	gem_vdi_intout[41] = 1;
	gem_vdi_intout[42] = 1;
	gem_vdi_intout[43] = 1;
	gem_vdi_intout[44] = 2;
	gem_vdi_text_cell(&character_width, &character_height);
	gem_vdi_ptsout[0] = character_width;
	gem_vdi_ptsout[1] = character_height;
	gem_vdi_ptsout[2] = character_width;
	gem_vdi_ptsout[3] = character_height;
	/* line widths, then the marker cell range */
	gem_vdi_ptsout[4] = 1;
	gem_vdi_ptsout[5] = 0;
	gem_vdi_ptsout[6] = 1;
	gem_vdi_ptsout[7] = 0;
	gem_vdi_ptsout[8] = GEM_VDI_MARKER_WIDTH;
	gem_vdi_ptsout[9] = GEM_VDI_MARKER_HEIGHT;
	gem_vdi_ptsout[10] = GEM_VDI_MARKER_WIDTH * GEM_VDI_MARKER_SIZES;
	gem_vdi_ptsout[11] = GEM_VDI_MARKER_HEIGHT * GEM_VDI_MARKER_SIZES;
}

static WORD __attribute__((optimize("Os")))
	gem_vdi_open_workstation(WORD application)
{
	GEM_VDI_RESIDENT_WORKSTATION *workstation;

	if (!gem_vdi_resident_startup())
		return FALSE;
	workstation = gem_vdi_allocate_workstation(application);
	if (!workstation)
		return FALSE;
	gem_vdi_open_count++;
	gem_vdi_control[6] = (UWORD) workstation->handle;
	gem_vdi_fill_work_out(workstation);
	gem_vdi_control[4] = GEM_VDI_INTOUT_WORDS;
	gem_vdi_control[2] = GEM_VDI_MAX_OUTPUT_POINTS;
	gem_vdi_flush(gem_vdi_screen);
	return TRUE;
}

static WORD __attribute__((optimize("Os")))
	gem_vdi_close_workstation(GEM_VDI_RESIDENT_WORKSTATION *workstation)
{
	if (!workstation)
		return FALSE;
	workstation->state = GEM_VDI_WS_FREE;
	workstation->application = -1;
	workstation->handle = 0;
	if (gem_vdi_open_count)
		gem_vdi_open_count--;
	return TRUE;
}

static WORD
gem_vdi_dispatch(const struct gemtrap_request *request, WORD application)
{
	GEM_VDI_RESIDENT_WORKSTATION *workstation;
	GEM_VDI_RECT screen_rect;
	GEM_VDI_COORD *line;
	GEM_VDI_COORD x1;
	GEM_VDI_COORD y1;
	GEM_VDI_COORD x2;
	GEM_VDI_COORD y2;
	GEM_VDI_UWORD character;
	GEM_VDI_UWORD modifiers;
	GEM_VDI_UWORD scan_code;
	GEM_VDI_WORD buttons;
	UWORD owner_segment;
	UWORD remaining;
	UWORD index;
	UWORD width;
	WORD opcode;
	WORD result;

	opcode = (WORD) gem_vdi_control[0];
	if (opcode == GEM_VDI_OP_OPEN || opcode == GEM_VDI_OP_OPEN_VIRTUAL)
		return gem_vdi_open_workstation(application);
	workstation = gem_vdi_find_workstation(application,
		(WORD) gem_vdi_control[6]);
	if (!workstation || !gem_vdi_screen)
		return FALSE;

	switch (opcode) {
	case GEM_VDI_OP_CLOSE:
	case GEM_VDI_OP_CLOSE_VIRTUAL:
		return gem_vdi_close_workstation(workstation);
	case GEM_VDI_OP_CLEAR:
		gem_vdi_apply_workstation(workstation, GEM_VDI_WHITE);
		gem_vdi_fill_rect(gem_vdi_screen, 0, 0, gem_vdi_screen->xres,
			gem_vdi_screen->yres);
		return TRUE;
	case GEM_VDI_OP_UPDATE:
		gem_vdi_flush(gem_vdi_screen);
		return TRUE;
	case GEM_VDI_OP_ESCAPE:
		if (gem_vdi_control[5] == GEM_VDI_ESCAPE_SOUND) {
			if (gem_vdi_control[3] < 2)
				return FALSE;
			return gem_vdi_sound_play((WORD) gem_vdi_intin[0],
				(WORD) gem_vdi_intin[1]);
		}
		if (gem_vdi_control[5] == GEM_VDI_ESCAPE_MUTE) {
			if (!gem_vdi_control[3])
				return FALSE;
			if ((WORD) gem_vdi_intin[0] == 0) {
				gem_vdi_sound_set_enabled(FALSE);
				(void) gem_vdi_sound_stop();
			} else if ((WORD) gem_vdi_intin[0] == 1) {
				gem_vdi_sound_set_enabled(TRUE);
			}
			gem_vdi_intout[0] = gem_vdi_sound_is_enabled()? 1U : 0U;
			gem_vdi_control[4] = 1;
			return TRUE;
		}
		return FALSE;
	case GEM_VDI_OP_PLINE:
		if (gem_vdi_control[1] < 2)
			return FALSE;
		gem_vdi_apply_workstation(workstation, workstation->line_color);
		line = (GEM_VDI_COORD *) gem_vdi_ptsin;
		x1 = *line++;
		y1 = *line++;
		remaining = gem_vdi_control[1] - 1U;
		__asm__ volatile ("movw %%ds,%0":"=r" (owner_segment)
			::"memory");
		while (remaining--) {
			x2 = *line++;
			y2 = *line++;
			if (workstation->line_type == GEM_VDI_USER_LINE)
				gem_vdi_pattern_line(gem_vdi_screen, x1, y1,
					x2, y2, workstation->line_pattern,
					TRUE);
			else
				gem_vdi_line(gem_vdi_screen, x1, y1, x2, y2,
					TRUE);
			__asm__ volatile ("movw %0,%%ds"::"r" (owner_segment)
				:"memory");
			x1 = x2;
			y1 = y2;
		}
		return TRUE;
	case GEM_VDI_OP_GTEXT:
		return gem_vdi_draw_text(workstation, gem_vdi_intin,
			gem_vdi_control[3], (const GEM_VDI_TEXT_JUSTIFY *) 0);
	case GEM_VDI_OP_PMARKER:
		return gem_vdi_draw_markers(workstation);
	case GEM_VDI_OP_FILLAREA:
		if (!gem_vdi_control[1])
			return FALSE;
		return gem_vdi_fill_area(workstation);
	case GEM_VDI_OP_GDP:
		return gem_vdi_dispatch_gdp(workstation);
	case GEM_VDI_OP_TEXT_HEIGHT:
		gem_vdi_text_set_height((WORD) gem_vdi_ptsin[1],
			gem_vdi_ptsout);
		workstation->text_height = (WORD) gem_vdi_ptsout[3];
		gem_vdi_control[2] = 2;
		return TRUE;
	case GEM_VDI_OP_TEXT_POINT:
		gem_vdi_intout[0] = (UWORD) gem_vdi_text_set_point(
			(WORD) gem_vdi_intin[0], gem_vdi_ptsout);
		workstation->text_height = (WORD) gem_vdi_ptsout[3];
		gem_vdi_control[4] = 1;
		gem_vdi_control[2] = 2;
		return TRUE;
	case GEM_VDI_OP_TEXT_FONT:
		return gem_vdi_set_one_output(gem_vdi_text_set_font((WORD)
				gem_vdi_intin[0]));
	case GEM_VDI_OP_TEXT_EFFECTS:
		return gem_vdi_set_one_output(gem_vdi_text_set_effects((WORD)
				gem_vdi_intin[0]));
	case GEM_VDI_OP_TEXT_ROTATION:
		return gem_vdi_set_one_output(gem_vdi_text_set_rotation((WORD)
				gem_vdi_intin[0]));
	case GEM_VDI_OP_TEXT_ALIGN:{
			WORD horizontal;
			WORD vertical;

			gem_vdi_text_set_alignment((WORD) gem_vdi_intin[0],
				(WORD) gem_vdi_intin[1], &horizontal,
				&vertical);
			gem_vdi_intout[0] = (UWORD) horizontal;
			gem_vdi_intout[1] = (UWORD) vertical;
			gem_vdi_control[4] = 2;
			return TRUE;
		}
	case GEM_VDI_OP_TEXT_EXTENT:
		/* VQT_EXTENT: corners go out counterclockwise from the origin */
		gem_vdi_text_extent(gem_vdi_intin, gem_vdi_control[3],
			gem_vdi_ptsout);
		gem_vdi_control[2] = 4;
		return TRUE;
	case GEM_VDI_OP_TEXT_WIDTH:
		gem_vdi_intout[0] = (UWORD) gem_vdi_text_char_width(
			(WORD) gem_vdi_intin[0], gem_vdi_ptsout);
		gem_vdi_control[4] = 1;
		gem_vdi_control[2] = 3;
		return TRUE;
	case GEM_VDI_OP_TEXT_FONTINFO:
		gem_vdi_text_fontinfo(gem_vdi_intout, gem_vdi_ptsout);
		gem_vdi_control[4] = 2;
		gem_vdi_control[2] = 5;
		return TRUE;
	case GEM_VDI_OP_LOAD_FONTS:
		return gem_vdi_set_one_output(gem_vdi_text_load_fonts());
	case GEM_VDI_OP_UNLOAD_FONTS:
		gem_vdi_text_unload_fonts();
		return TRUE;
	case GEM_VDI_OP_SET_COLOR:
		return gem_vdi_palette_request();
	case GEM_VDI_OP_LINE_TYPE:
		workstation->line_type = (WORD) gem_vdi_intin[0];
		return gem_vdi_set_one_output(workstation->line_type);
	case GEM_VDI_OP_LINE_WIDTH:
		workstation->line_width = (WORD) gem_vdi_ptsin[0];
		if (workstation->line_width < 1)
			workstation->line_width = 1;
		gem_vdi_ptsout[0] = (UWORD) workstation->line_width;
		gem_vdi_ptsout[1] = 0;
		gem_vdi_control[2] = 1;
		return TRUE;
	case GEM_VDI_OP_LINE_ENDS:
		/* only the square end is drawn, arrow and round ends are just
		 * recorded so the query call reports back what was asked for */
		workstation->line_begin = (WORD) gem_vdi_intin[0];
		if (workstation->line_begin < GEM_VDI_END_SQUARE
			|| workstation->line_begin > GEM_VDI_END_ROUND)
			workstation->line_begin = GEM_VDI_END_SQUARE;
		workstation->line_end = (WORD) gem_vdi_intin[1];
		if (workstation->line_end < GEM_VDI_END_SQUARE
			|| workstation->line_end > GEM_VDI_END_ROUND)
			workstation->line_end = GEM_VDI_END_SQUARE;
		gem_vdi_intout[0] = (UWORD) workstation->line_begin;
		gem_vdi_intout[1] = (UWORD) workstation->line_end;
		gem_vdi_control[4] = 2;
		return TRUE;
	case GEM_VDI_OP_MARKER_TYPE:
		workstation->marker_type = (WORD) gem_vdi_intin[0];
		if (workstation->marker_type < 1
			|| workstation->marker_type > GEM_VDI_MARKER_TYPES)
			workstation->marker_type = GEM_VDI_MARKER_DOT;
		return gem_vdi_set_one_output(workstation->marker_type);
	case GEM_VDI_OP_MARKER_HEIGHT:{
			/* clamp the request to the marker cell range, then round
			 * to the nearest whole multiple of the cell */
			WORD height;
			WORD scale;

			height = (WORD) gem_vdi_ptsin[1];
			if (height < GEM_VDI_MARKER_HEIGHT)
				height = GEM_VDI_MARKER_HEIGHT;
			else if (height > GEM_VDI_MARKER_MAX_HEIGHT)
				height = GEM_VDI_MARKER_MAX_HEIGHT;
			workstation->marker_height = height;
			scale = (WORD) ((height + GEM_VDI_MARKER_HEIGHT / 2)
				/ GEM_VDI_MARKER_HEIGHT);
			if (scale < 1)
				scale = 1;
			gem_vdi_ptsout[0] =
				(UWORD) (scale * GEM_VDI_MARKER_WIDTH);
			gem_vdi_ptsout[1] =
				(UWORD) (scale * GEM_VDI_MARKER_HEIGHT);
			gem_vdi_control[2] = 1;
			return TRUE;
		}
	case GEM_VDI_OP_MARKER_COLOR:
		workstation->marker_color = (WORD) gem_vdi_intin[0] & 15;
		return gem_vdi_set_one_output(workstation->marker_color);
	case GEM_VDI_OP_LINE_ATTR:
		/* VQL_ATTR: type, colour, mode, both end styles, width */
		gem_vdi_intout[0] = (UWORD) workstation->line_type;
		gem_vdi_intout[1] = (UWORD) workstation->line_color;
		gem_vdi_intout[2] = (UWORD) workstation->write_mode;
		gem_vdi_intout[3] = (UWORD) workstation->line_begin;
		gem_vdi_intout[4] = (UWORD) workstation->line_end;
		gem_vdi_ptsout[0] = (UWORD) workstation->line_width;
		gem_vdi_ptsout[1] = 0;
		gem_vdi_control[4] = 5;
		gem_vdi_control[2] = 1;
		return TRUE;
	case GEM_VDI_OP_MARKER_ATTR:{
			/* VQM_ATTR: type, colour, mode, then the scaled cell */
			WORD scale;

			scale = (WORD) ((workstation->marker_height
					+
					GEM_VDI_MARKER_HEIGHT / 2) /
				GEM_VDI_MARKER_HEIGHT);
			if (scale < 1)
				scale = 1;
			gem_vdi_intout[0] = (UWORD) workstation->marker_type;
			gem_vdi_intout[1] = (UWORD) workstation->marker_color;
			gem_vdi_intout[2] = (UWORD) workstation->write_mode;
			gem_vdi_ptsout[0] =
				(UWORD) (scale * GEM_VDI_MARKER_WIDTH);
			gem_vdi_ptsout[1] =
				(UWORD) (scale * GEM_VDI_MARKER_HEIGHT);
			gem_vdi_control[4] = 3;
			gem_vdi_control[2] = 1;
			return TRUE;
		}
	case GEM_VDI_OP_FILL_ATTR:
		/* VQF_ATTR: interior, colour, style, mode, perimeter */
		gem_vdi_intout[0] = (UWORD) workstation->fill_interior;
		gem_vdi_intout[1] = (UWORD) workstation->fill_color;
		gem_vdi_intout[2] = (UWORD) workstation->fill_style;
		gem_vdi_intout[3] = (UWORD) workstation->write_mode;
		gem_vdi_intout[4] = (UWORD) workstation->fill_perimeter;
		gem_vdi_control[4] = 5;
		return TRUE;
	case GEM_VDI_OP_USER_PATTERN:{
			/* the request is sixteen words but we tile 8 by 8, so
			 * only the top-left corner of the pattern is kept */
			UWORD row;

			if (gem_vdi_control[3] < 16U)
				return FALSE;
			for (row = 0; row < 8U; row++)
				workstation->user_pattern[row] = (GEM_VDI_UBYTE)
					(gem_vdi_intin[row] >> 8);
			return TRUE;
		}
	case GEM_VDI_OP_INPUT_MODE:{
			/* device one to four, request or sample */
			WORD device;
			WORD mode;

			device = (WORD) gem_vdi_intin[0];
			mode = (WORD) gem_vdi_intin[1];
			if (device < 1 || device > GEM_VDI_INPUT_DEVICES
				|| (mode != GEM_VDI_INPUT_REQUEST
					&& mode != GEM_VDI_INPUT_SAMPLE))
				return gem_vdi_set_one_output(0);
			workstation->input_mode[device - 1] = mode;
			return gem_vdi_set_one_output(mode);
		}
	case GEM_VDI_OP_QUERY_INPUT_MODE:{
			WORD device;

			device = (WORD) gem_vdi_intin[0];
			if (device < 1 || device > GEM_VDI_INPUT_DEVICES)
				return gem_vdi_set_one_output(0);
			return gem_vdi_set_one_output(workstation->input_mode
				[device - 1]);
		}
		/* locator, choice and string input, one resident serves every client
		 * so we cant block, all three just answer from the current sample and
		 * report nothing pending when there's nothing */
	case GEM_VDI_OP_LOCATOR:{
			GEM_VDI_RESIDENT_INPUT input;

			if (!gem_vdi_resident_poll_input(&input))
				return FALSE;
			gem_vdi_ptsout[0] = (UWORD) input.mouse_x;
			gem_vdi_ptsout[1] = (UWORD) input.mouse_y;
			gem_vdi_intout[0] = input.key_ready
				? (UWORD) (input.key_code & 0x00ffU)
				: (UWORD) (input.mouse_buttons ? 32U : 0U);
			gem_vdi_control[4] = 1;
			gem_vdi_control[2] = 1;
			return TRUE;
		}
	case GEM_VDI_OP_CHOICE:{
			GEM_VDI_RESIDENT_INPUT input;

			if (!gem_vdi_resident_poll_input(&input))
				return FALSE;
			gem_vdi_intout[0] = input.key_ready
				? (UWORD) (input.key_code & 0x00ffU) : 0U;
			gem_vdi_control[4] = 1;
			return TRUE;
		}
	case GEM_VDI_OP_STRING:{
			GEM_VDI_RESIDENT_INPUT input;

			if (!gem_vdi_resident_poll_input(&input))
				return FALSE;
			if (input.key_ready) {
				gem_vdi_intout[0] = (UWORD)
					(input.key_code & 0x00ffU);
				gem_vdi_control[4] = 1;
			} else
				gem_vdi_control[4] = 0;
			return TRUE;
		}
		/* these hand the driver a routine to call from an interrupt, the client
		 * is a separate ELKS process so no vector can be installed, each reports
		 * a null previous vector and the timer one still gives the BIOS tick
		 * period */
	case GEM_VDI_OP_TIMER_VECTOR:
		gem_vdi_intout[0] = 55;
		gem_vdi_control[4] = 1;
		gem_vdi_control[9] = 0;
		gem_vdi_control[10] = 0;
		return TRUE;
	case GEM_VDI_OP_BUTTON_VECTOR:
	case GEM_VDI_OP_MOTION_VECTOR:
	case GEM_VDI_OP_CURSOR_VECTOR:
		gem_vdi_control[9] = 0;
		gem_vdi_control[10] = 0;
		gem_vdi_control[4] = 0;
		return TRUE;
	case GEM_VDI_OP_LINE_COLOR:
		workstation->line_color = (WORD) gem_vdi_intin[0] & 15;
		return gem_vdi_set_one_output(workstation->line_color);
	case GEM_VDI_OP_TEXT_COLOR:
		workstation->text_color = (WORD) gem_vdi_intin[0] & 15;
		return gem_vdi_set_one_output(workstation->text_color);
	case GEM_VDI_OP_FILL_INTERIOR:
		/* an interior outside hollow..user defaults to hollow */
		workstation->fill_interior = (WORD) gem_vdi_intin[0];
		if (workstation->fill_interior < GEM_VDI_FIS_HOLLOW
			|| workstation->fill_interior > GEM_VDI_FIS_USER)
			workstation->fill_interior = GEM_VDI_FIS_HOLLOW;
		return gem_vdi_set_one_output(workstation->fill_interior);
	case GEM_VDI_OP_FILL_STYLE:
		/* classic ranges: 1-24 for FIS_PATTERN, 1-12 for FIS_HATCH */
		workstation->fill_style = (WORD) gem_vdi_intin[0];
		if (workstation->fill_style < 1)
			workstation->fill_style = 1;
		if (workstation->fill_interior == GEM_VDI_FIS_HATCH) {
			if (workstation->fill_style > 12)
				workstation->fill_style = 12;
		} else if (workstation->fill_style > 24)
			workstation->fill_style = 24;
		return gem_vdi_set_one_output(workstation->fill_style);
	case GEM_VDI_OP_FILL_COLOR:
		workstation->fill_color = (WORD) gem_vdi_intin[0] & 15;
		return gem_vdi_set_one_output(workstation->fill_color);
	case GEM_VDI_OP_FILL_PERIMETER:
		/* any nonzero value turns the outline on */
		workstation->fill_perimeter = gem_vdi_intin[0] ? 1 : 0;
		return gem_vdi_set_one_output(workstation->fill_perimeter);
	case GEM_VDI_OP_QUERY_COLOR:
		/* both flags get the stored requested intensities, on VGA the
		 * realized color is off by at most one six-bit DAC step */
		return gem_vdi_palette_request();
	case GEM_VDI_OP_WRITE_MODE:
		workstation->write_mode = (WORD) gem_vdi_intin[0];
		if (workstation->write_mode < GEM_VDI_MD_REPLACE
			|| workstation->write_mode > GEM_VDI_MD_ERASE)
			workstation->write_mode = GEM_VDI_MD_REPLACE;
		return gem_vdi_set_one_output(workstation->write_mode);
	case GEM_VDI_OP_TEXT_ATTR:
		gem_vdi_text_attributes(workstation->text_color,
			workstation->write_mode, gem_vdi_intout,
			gem_vdi_ptsout);
		gem_vdi_control[4] = 6;
		gem_vdi_control[2] = 2;
		return TRUE;
	case GEM_VDI_OP_EXTENDED:
		/* a zero flag repeats the open reply, a one returns the extended
		 * capability record */
		if (!gem_vdi_intin[0]) {
			gem_vdi_fill_work_out(workstation);
			gem_vdi_control[4] = GEM_VDI_INTOUT_WORDS;
			gem_vdi_control[2] = GEM_VDI_MAX_OUTPUT_POINTS;
			return TRUE;
		}
		gem_vdi_clear_words(gem_vdi_intout, GEM_VDI_INTOUT_WORDS);
		gem_vdi_intout[0] = 4;	/* alpha and graphics controller */
		gem_vdi_intout[1] = 1;	/* background colours */
		gem_vdi_intout[2] = GEM_VDI_STYLE_MASK;
		gem_vdi_intout[3] = 0;	/* rasters do not scale */
		gem_vdi_intout[4] = gem_vdi_screen->planes;
		gem_vdi_intout[5] = 0;	/* no video lookup table */
		gem_vdi_intout[6] = 50;	/* performance factor */
		gem_vdi_intout[7] = 0;	/* no contour fill */
		gem_vdi_intout[8] = 1;	/* right-angle text rotation */
		gem_vdi_intout[9] = 4;	/* writing modes */
		gem_vdi_intout[10] = GEM_VDI_INPUT_SAMPLE;
		gem_vdi_intout[11] = 1;	/* text alignment is honoured */
		gem_vdi_intout[12] = 0;	/* no inking */
		gem_vdi_intout[13] = 0;	/* no rubber banding */
		gem_vdi_intout[14] = GEM_VDI_MAX_INPUT_POINTS;
		gem_vdi_intout[15] = GEM_VDI_INTIN_WORDS;
		gem_vdi_intout[16] = 3;	/* mouse buttons */
		gem_vdi_intout[17] = 0;	/* wide-line styles */
		gem_vdi_intout[18] = 0;	/* wide-line writing modes */
		gem_vdi_ptsout[0] = (UWORD) workstation->clip_x1;
		gem_vdi_ptsout[1] = (UWORD) workstation->clip_y1;
		gem_vdi_ptsout[2] = (UWORD) workstation->clip_x2;
		gem_vdi_ptsout[3] = (UWORD) workstation->clip_y2;
		gem_vdi_control[4] = GEM_VDI_INTOUT_WORDS;
		gem_vdi_control[2] = GEM_VDI_MAX_OUTPUT_POINTS;
		return TRUE;
	case GEM_VDI_OP_COPY_FORM:
	case GEM_VDI_OP_COPY_TRANSPARENT:
		return gem_vdi_screen_copy(workstation, request);
	case GEM_VDI_OP_TRANSFORM:
		return gem_vdi_transform_form(request);
	case GEM_VDI_OP_CURSOR_FORM:
		return gem_vdi_resident_set_form();
	case GEM_VDI_OP_USER_LINE:
		if (!gem_vdi_control[3])
			return FALSE;
		workstation->line_pattern = gem_vdi_intin[0];
		return TRUE;
	case GEM_VDI_OP_FILL_RECT:
		/* VR_RECFL fills only, it never draws a perimeter */
		gem_vdi_fill_output(workstation, FALSE);
		return TRUE;
	case GEM_VDI_OP_SHOW_CURSOR:
		if (gem_vdi_intin[0])
			workstation->cursor_hides = 1U;
		if (workstation->cursor_hides)
			workstation->cursor_hides--;
		if (!workstation->cursor_hides)
			gem_vdi_show_cursor(gem_vdi_screen);
		return TRUE;
	case GEM_VDI_OP_HIDE_CURSOR:
		if (!workstation->cursor_hides)
			gem_vdi_hide_cursor(gem_vdi_screen);
		if (workstation->cursor_hides != 0xffffU)
			workstation->cursor_hides++;
		return TRUE;
	case GEM_VDI_OP_QUERY_MOUSE:
		x1 = 0;
		y1 = 0;
		buttons = 0;
		result = gem_vdi_read_mouse(&x1, &y1, &buttons);
		gem_vdi_intout[0] = (UWORD) buttons;
		gem_vdi_ptsout[0] = (UWORD) x1;
		gem_vdi_ptsout[1] = (UWORD) y1;
		gem_vdi_control[4] = 1;
		gem_vdi_control[2] = 1;
		return result >= 0 ? TRUE : FALSE;
	case GEM_VDI_OP_QUERY_KEY:
		character = 0;
		modifiers = 0;
		scan_code = 0;
		(void) gem_vdi_read_keyboard(&character, &modifiers,
			&scan_code);
		gem_vdi_intout[0] = modifiers;
		gem_vdi_control[4] = 1;
		return TRUE;
	case GEM_VDI_OP_CLIP:
		workstation->clip_on = gem_vdi_intin[0] ? TRUE : FALSE;
		if (workstation->clip_on) {
			workstation->clip_x1 = (WORD) gem_vdi_ptsin[0];
			workstation->clip_y1 = (WORD) gem_vdi_ptsin[1];
			workstation->clip_x2 = (WORD) gem_vdi_ptsin[2];
			workstation->clip_y2 = (WORD) gem_vdi_ptsin[3];
			if (workstation->clip_x1 < 0)
				workstation->clip_x1 = 0;
			if (workstation->clip_y1 < 0)
				workstation->clip_y1 = 0;
			if (workstation->clip_x2 >= gem_vdi_screen->xres)
				workstation->clip_x2 = gem_vdi_screen->xres - 1;
			if (workstation->clip_y2 >= gem_vdi_screen->yres)
				workstation->clip_y2 = gem_vdi_screen->yres - 1;
			if (workstation->clip_x2 < workstation->clip_x1
				|| workstation->clip_y2 < workstation->clip_y1)
				workstation->clip_on = FALSE;
		}
		return TRUE;
	case GEM_VDI_OP_TEXT_JUSTIFIED:{
			/* VQT_JUSTIFIED takes V_JUSTIFIED's args and answers with where
			 * each character would land instead of drawing it */
			GEM_VDI_TEXT_JUSTIFY justify;
			const UWORD *characters;
			UWORD count;

			if (gem_vdi_control[3] < 3U)
				return FALSE;
			characters = gem_vdi_intin + 2;
			count = (UWORD) (gem_vdi_control[3] - 2U);
			gem_vdi_text_justify(characters, count,
				gem_vdi_intin[0], gem_vdi_intin[1],
				(WORD) gem_vdi_ptsin[2], &justify);
			gem_vdi_control[2] =
				gem_vdi_text_justified_offsets(characters,
				count, &justify, gem_vdi_ptsout,
				GEM_VDI_PTSOUT_WORDS / 2U);
			return TRUE;
		}
	case GEM_VDI_OP_TEXT_NAME:
		(void) gem_vdi_text_name(gem_vdi_intin[0], gem_vdi_intout);
		gem_vdi_control[4] = 33;
		return TRUE;
	default:
		(void) screen_rect;
		return FALSE;
	}
}

WORD __attribute__((optimize("Os")))
	gem_vdi_resident_startup(VOID)
{
	GEM_VDI_SCREEN *screen;
	GEM_VDI_COORD mouse_x;
	GEM_VDI_COORD mouse_y;
	GEM_VDI_WORD mouse_buttons;
	UWORD owner_segment;

	/* safe to repeat, the screen geometry exists before anything opens */
	if (gem_vdi_screen)
		return TRUE;
	__asm__ volatile ("movw %%ds,%0":"=r" (owner_segment)
		::"memory");
	screen = gem_vdi_open();
	__asm__ volatile ("movw %0,%%ds"::"r" (owner_segment):"memory");
	if (!screen)
		return FALSE;
	gem_vdi_screen = screen;
	/* use the GEM system font, not the adapter ROM font (CGA and Hercules
	 * have none, and none matches the GEM furniture shapes), the cell is
	 * sized to the screen: 8x8 on 200-line CGA, 8x14 on 348/350-line EGA and
	 * Hercules, 8x16 on 480-line VGA */
	if (gem_vdi_screen->yres <= 200)
		gem_vdi_font_rows = 8U;
	else if (gem_vdi_screen->yres <= 350)
		gem_vdi_font_rows = 14U;
	else
		gem_vdi_font_rows = 16U;
	/* load the system font into its own far segment, a missing font file is
	 * fatal (no ROM-font fallback) so bring the display back and fail the
	 * open, the server reports it and exits */
	if (!gem_vdi_sysfont_load(gem_vdi_font_rows, &gem_vdi_font_segment,
			&gem_vdi_font_offset)) {
		gem_vdi_close(gem_vdi_screen);
		gem_vdi_screen = (GEM_VDI_SCREEN *) 0;
		return FALSE;
	}
	/* bind the font chain to that segment, the system font is the head of
	 * the chain and the default face, any *.FNT gets linked after it */
	gem_vdi_text_init(gem_vdi_font_segment, gem_vdi_font_offset,
		gem_vdi_font_rows);
	gem_vdi_cursor_hot_x = 0;
	gem_vdi_cursor_hot_y = 0;
	/* a BIOS mode set resets the adapter palette, reapply the GEM/3 colors */
	gem_vdi_palette_apply_all();

	/* a BIOS mode set also clears the screen to black, original GEM opens
	 * onto white and the AES menu bar is a hollow object, so fill white
	 * now */
	gem_vdi_set_mode(GEM_VDI_REPLACE);
	gem_vdi_set_foreground(gem_vdi_screen,
		gem_vdi_resident_color(GEM_VDI_WHITE));
	gem_vdi_set_clip(gem_vdi_screen, 0, NULL);
	gem_vdi_fill_rect(gem_vdi_screen, 0, 0, gem_vdi_screen->xres,
		gem_vdi_screen->yres);

	/* the display opens with one cursor hide outstanding, the arrow shape
	 * cant be read until GEM.RSC is loaded, so the caller balances that hide
	 * through gem_vdi_resident_default_mouse() once it is */
	(void) mouse_x;
	(void) mouse_y;
	(void) mouse_buttons;
	/* compiled far-video accesses may leave DS changed, put it back */
	__asm__ volatile ("movw %0,%%ds"::"r" (owner_segment):"memory");
	return TRUE;
}

WORD __attribute__((optimize("Os")))
	gem_vdi_resident_get_metrics(UWORD *width, UWORD *height,
	UWORD *character_width, UWORD *character_height)
{
	if (!gem_vdi_screen || !width || !height || !character_width
		|| !character_height)
		return FALSE;

	/* pixel counts, not maximum coordinates */
	*width = (UWORD) gem_vdi_screen->xres;
	*height = (UWORD) gem_vdi_screen->yres;
	*character_width = GEM_VDI_CHAR_WIDTH;
	*character_height = GEM_VDI_CHAR_HEIGHT;
	return TRUE;
}

GEM_VDI_SCREEN *
gem_vdi_resident_screen(VOID)
{
	/* this pointer never leaves the process, resident gemaes owns it */
	return gem_vdi_screen;
}

WORD __far __attribute__((far_section, noinline,
		section(".fartext.gemvdi_resident_text")))
	gem_vdi_resident_text(GEM_BINDINGS_POINTER_SLOT text,
	UWORD max_characters, WORD x, WORD y, WORD color)
{
	const GEM_VDI_UBYTE *glyph;
	UWORD index;
	UWORD owner_segment;

	if (!gem_vdi_screen || !text.hi || !max_characters
		|| max_characters > GEM_VDI_RESIDENT_TEXT_MAX
		|| !gem_resident_memory_range(text.lo, max_characters, 0xffffU))
		return FALSE;
	gem_resident_memory_from(text.hi, text.lo,
		gem_vdi_resident_text_buffer, max_characters);

	/* called in-process, not from a client parameter block, y is the top of
	 * the 16-pixel system-font cell */
	gem_vdi_set_mode(GEM_VDI_REPLACE);
	gem_vdi_set_foreground(gem_vdi_screen, gem_vdi_resident_color(color));
	index = 0;
	while (index < max_characters && gem_vdi_resident_text_buffer[index])
		index++;
	if (index && gem_vdi_font_segment && gem_vdi_font_rows == 16U
		&& !gem_vdi_text_has_furniture(
			(const GEM_VDI_UBYTE *) gem_vdi_resident_text_buffer,
			index, 1U)
		&& gem_vdi_text_run(gem_vdi_screen, x, y,
			(const GEM_VDI_UBYTE *) gem_vdi_resident_text_buffer,
			index, 1U, gem_vdi_font_segment, gem_vdi_font_offset))
		return TRUE;
	index = 0;
	__asm__ volatile ("movw %%ds,%0":"=r" (owner_segment)
		::"memory");
	while (index < max_characters && gem_vdi_resident_text_buffer[index]) {
		if (x > gem_vdi_screen->xres - GEM_VDI_GLYPH_WIDTH)
			break;
		glyph = gem_vdi_system_glyph_rows(
			(UBYTE) gem_vdi_resident_text_buffer[index]);
		gem_vdi_glyph(gem_vdi_screen, x, y, GEM_VDI_CHAR_WIDTH,
			GEM_VDI_CHAR_HEIGHT, glyph, 0x80);
		__asm__ volatile ("movw %0,%%ds"::"r" (owner_segment)
			:"memory");
		x += GEM_VDI_GLYPH_ADVANCE;
		index++;
	}
	return TRUE;
}

WORD
gem_vdi_resident_poll_input(GEM_VDI_RESIDENT_INPUT *input)
{
	GEM_VDI_UWORD character;
	GEM_VDI_UWORD modifiers;
	GEM_VDI_UWORD scan_code;
	GEM_VDI_COORD mouse_x;
	GEM_VDI_COORD mouse_y;
	GEM_VDI_WORD buttons;
	GEM_VDI_WORD mouse_status;
	GEM_VDI_WORD key_status;

	if (!gem_vdi_screen || !input)
		return FALSE;

	character = 0;
	modifiers = 0;
	scan_code = 0;
	mouse_x = 0;
	mouse_y = 0;
	buttons = 0;
	mouse_status = gem_vdi_read_mouse(&mouse_x, &mouse_y, &buttons);
	key_status = gem_vdi_read_keyboard(&character, &modifiers, &scan_code);
	if (mouse_status < 0 || key_status == GEM_VDI_KEY_ERROR)
		return FALSE;
	if (mouse_status > 0)
		gem_vdi_move_cursor(mouse_x - gem_vdi_cursor_hot_x,
			mouse_y - gem_vdi_cursor_hot_y);

	input->mouse_x = mouse_x;
	input->mouse_y = mouse_y;
	input->mouse_buttons = (UWORD) buttons;
	input->key_state = modifiers;
	/* GEM wants the BIOS scan code in the high byte, ASCII in the low */
	input->key_code = (UWORD) (((scan_code & 0x00ffU) << 8)
		| (character & 0x00ffU));
	input->key_ready = key_status == GEM_VDI_KEY_PRESS;
	input->changed = mouse_status > 0 || input->key_ready;
	return TRUE;
}

WORD
gem_vdi_resident_request(struct gemtrap_request *request, WORD application)
{
	GEM_BINDINGS_VDIPB parameter_block;
	UWORD owner_segment;
	UWORD integer_input_bytes;
	UWORD point_input_bytes;
	UWORD integer_output_bytes;
	UWORD point_output_bytes;
	WORD result;

	if (!request || application < 0
		|| request->cx != GEM_VDI_RESIDENT_SELECTOR
		|| !request->ds
		|| !gem_resident_memory_range(request->dx,
			sizeof(parameter_block), request->data_limit))
		return -1;
	gem_resident_memory_from(request->ds, request->dx, &parameter_block,
		sizeof(parameter_block));

	/* the classic VDIPB is five four-byte slots, never a C pointer */
	if (sizeof(parameter_block) != 20
		|| !gem_resident_memory_pointer(request, parameter_block.contrl,
			GEM_VDI_CONTROL_WORDS * 2U))
		return -1;
	gem_resident_memory_from(request->ds, parameter_block.contrl.lo,
		gem_vdi_control, GEM_VDI_CONTROL_WORDS * 2U);

	if (!gem_resident_memory_word_bytes(gem_vdi_control[3],
			GEM_VDI_INTIN_WORDS, &integer_input_bytes)
		|| !gem_resident_memory_point_bytes(gem_vdi_control[1],
			GEM_VDI_MAX_INPUT_POINTS, &point_input_bytes)
		|| !gem_resident_memory_pointer(request, parameter_block.intin,
			integer_input_bytes)
		|| !gem_resident_memory_pointer(request, parameter_block.ptsin,
			point_input_bytes))
		return -1;

	gem_vdi_clear_words(gem_vdi_intin, GEM_VDI_INTIN_WORDS);
	gem_vdi_clear_words(gem_vdi_ptsin, GEM_VDI_PTSIN_WORDS);
	gem_vdi_clear_words(gem_vdi_intout, GEM_VDI_INTOUT_WORDS);
	gem_vdi_clear_words(gem_vdi_ptsout, GEM_VDI_PTSOUT_WORDS);
	if (integer_input_bytes)
		gem_resident_memory_from(request->ds, parameter_block.intin.lo,
			gem_vdi_intin, integer_input_bytes);
	if (point_input_bytes)
		gem_resident_memory_from(request->ds, parameter_block.ptsin.lo,
			gem_vdi_ptsin, point_input_bytes);

	/* output counts are set here, stale caller values never escape */
	gem_vdi_control[2] = 0;
	gem_vdi_control[4] = 0;
	__asm__ volatile ("movw %%ds,%0":"=r" (owner_segment)
		::"memory");
	result = gem_vdi_dispatch(request, application);
	/* far-video accesses may leave DS changed, put the resident data
	 * segment back, SS may be a separate stack segment so no reload
	 * from SS */
	__asm__ volatile ("movw %0,%%ds"::"r" (owner_segment):"memory");

	if (!gem_resident_memory_word_bytes(gem_vdi_control[4],
			GEM_VDI_INTOUT_WORDS, &integer_output_bytes)
		|| !gem_resident_memory_point_bytes(gem_vdi_control[2],
			GEM_VDI_MAX_OUTPUT_POINTS, &point_output_bytes)
		|| !gem_resident_memory_pointer(request, parameter_block.intout,
			integer_output_bytes)
		|| !gem_resident_memory_pointer(request, parameter_block.ptsout,
			point_output_bytes))
		return -1;

	gem_resident_memory_to(gem_vdi_control, request->ds,
		parameter_block.contrl.lo, GEM_VDI_CONTROL_WORDS * 2U);
	if (integer_output_bytes)
		gem_resident_memory_to(gem_vdi_intout, request->ds,
			parameter_block.intout.lo, integer_output_bytes);
	if (point_output_bytes)
		gem_resident_memory_to(gem_vdi_ptsout, request->ds,
			parameter_block.ptsout.lo, point_output_bytes);
	return result;
}

VOID __attribute__((optimize("Os")))
	gem_vdi_resident_release(WORD application)
{
	GEM_VDI_RESIDENT_WORKSTATION *workstation;
	UWORD remaining;

	workstation = gem_vdi_workstations;
	remaining = GEM_VDI_RESIDENT_WORKSTATIONS;
	while (remaining--) {
		if (workstation->state == GEM_VDI_WS_OPEN
			&& workstation->application == application)
			(void) gem_vdi_close_workstation(workstation);
		workstation++;
	}
}

VOID __attribute__((optimize("Os")))
	gem_vdi_resident_shutdown(VOID)
{
	GEM_VDI_RESIDENT_WORKSTATION *workstation;
	UWORD remaining;

	workstation = gem_vdi_workstations;
	remaining = GEM_VDI_RESIDENT_WORKSTATIONS;
	while (remaining--) {
		workstation->state = GEM_VDI_WS_FREE;
		workstation->application = -1;
		workstation->handle = 0;
		workstation++;
	}
	gem_vdi_open_count = 0;
	(void) gem_vdi_sound_stop();
	gem_vdi_sound_set_enabled(TRUE);
	gem_vdi_text_unload_fonts();
	gem_vdi_font_segment = 0;
	gem_vdi_font_offset = 0;
	if (gem_vdi_screen) {
		gem_vdi_close(gem_vdi_screen);
		gem_vdi_screen = NULL;
	}
}

/*
 * release the adapter and input while a full-screen child runs,
 * gem_vdi_close() brings back text mode and the cooked tty state, resume
 * reruns the physical open
 */
WORD __attribute__((optimize("Os")))
	gem_vdi_resident_suspend(VOID)
{
	if (!gem_vdi_screen)
		return FALSE;
	gem_vdi_close(gem_vdi_screen);
	gem_vdi_screen = NULL;
	return TRUE;
}

WORD __attribute__((optimize("Os")))
	gem_vdi_resident_resume(VOID)
{
	return gem_vdi_resident_startup();
}
