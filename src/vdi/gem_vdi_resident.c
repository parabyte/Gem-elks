/*
 * gem_vdi_resident.c - original-array VDI dispatcher for resident ELKS GEM.
 *
 * Digital Research VDI clients pass one packed five-pointer VDIPB at DS:DX.
 * This module validates that exact 20-byte record and every declared array
 * against the ELKS-pinned client DS, copies only the original WORD arrays,
 * and dispatches their classic operation numbers to the native GEM PC video
 * core.  There is no converted graphics protocol and no wrapper process.
 *
 * Eight fixed virtual workstations retain state independently for their AES
 * PD owners.  Before drawing, the selected state is restored into the one
 * physical PC adapter.  All sizes, colors, handles, coordinates, and loop
 * counters are 8 or 16 bits.  Multiplication and division are deliberately
 * absent from the 8088 hot path.
 */

#include "gem_resident_memory.h"
#include "gem_vdi_resident.h"
#include "vdi.h"
#include "drivers/gem_pcvideo.h"

#include <linuxmt/kd.h>
#include <sys/ioctl.h>

#define GEM_VDI_CONTROL_WORDS	11U
#define GEM_VDI_INTIN_WORDS	80U
#define GEM_VDI_PTSIN_WORDS	256U
#define GEM_VDI_INTOUT_WORDS	45U
#define GEM_VDI_PTSOUT_WORDS	12U

#define GEM_VDI_MAX_INPUT_POINTS	128U
#define GEM_VDI_MAX_OUTPUT_POINTS	6U

#define GEM_VDI_WS_FREE		0
#define GEM_VDI_WS_OPEN		1

#define GEM_VDI_OP_OPEN		1
#define GEM_VDI_OP_CLOSE	2
#define GEM_VDI_OP_CLEAR	3
#define GEM_VDI_OP_UPDATE	4
#define GEM_VDI_OP_ESCAPE	5
#define GEM_VDI_OP_PLINE	6
#define GEM_VDI_OP_GTEXT	8
#define GEM_VDI_OP_FILLAREA	9
#define GEM_VDI_OP_GDP		11
#define GEM_VDI_OP_TEXT_HEIGHT	12
#define GEM_VDI_OP_SET_COLOR	14
#define GEM_VDI_OP_LINE_TYPE	15
#define GEM_VDI_OP_LINE_WIDTH	16
#define GEM_VDI_OP_LINE_COLOR	17
#define GEM_VDI_OP_TEXT_COLOR	22
#define GEM_VDI_OP_FILL_INTERIOR	23
#define GEM_VDI_OP_FILL_STYLE	24
#define GEM_VDI_OP_FILL_COLOR	25
#define GEM_VDI_OP_QUERY_COLOR	26
#define GEM_VDI_OP_WRITE_MODE	32
#define GEM_VDI_OP_TEXT_ATTR	38
#define GEM_VDI_OP_OPEN_VIRTUAL	100
#define GEM_VDI_OP_CLOSE_VIRTUAL	101
#define GEM_VDI_OP_EXTENDED	102
#define GEM_VDI_OP_COPY_FORM	109
#define GEM_VDI_OP_TRANSFORM	110
#define GEM_VDI_OP_CURSOR_FORM	111
#define GEM_VDI_OP_USER_LINE	113
#define GEM_VDI_OP_FILL_RECT	114
#define GEM_VDI_OP_COPY_TRANSPARENT 121
#define GEM_VDI_OP_SHOW_CURSOR	122
#define GEM_VDI_OP_HIDE_CURSOR	123
#define GEM_VDI_OP_QUERY_MOUSE	124
#define GEM_VDI_OP_QUERY_KEY	128
#define GEM_VDI_OP_CLIP		129
#define GEM_VDI_OP_TEXT_NAME	130

#define GEM_VDI_ESCAPE_SOUND	61
#define GEM_VDI_ESCAPE_MUTE	62

#define GEM_VDI_GDP_BAR		1
#define GEM_VDI_GDP_ARC		2
#define GEM_VDI_GDP_PIE		3
#define GEM_VDI_GDP_CIRCLE	4
#define GEM_VDI_GDP_ELLIPSE	5
#define GEM_VDI_GDP_ELLIPTIC_ARC 6
#define GEM_VDI_GDP_ELLIPTIC_PIE 7
#define GEM_VDI_GDP_ROUNDED_BOX	8
#define GEM_VDI_GDP_FILLED_ROUNDED_BOX 9

/* Classic GEM logical colors. */
#define GEM_VDI_WHITE		0
#define GEM_VDI_BLACK		1
#define GEM_VDI_RED		2
#define GEM_VDI_GREEN		3
#define GEM_VDI_BLUE		4
#define GEM_VDI_CYAN		5
#define GEM_VDI_YELLOW		6
#define GEM_VDI_MAGENTA		7

/* Classic writing modes are one-based; the native core is zero-based. */
#define GEM_VDI_MD_REPLACE	1
#define GEM_VDI_MD_TRANS	2
#define GEM_VDI_MD_XOR		3
#define GEM_VDI_MD_ERASE	4

#define GEM_VDI_FIS_HOLLOW	0
#define GEM_VDI_FIS_SOLID	1

#define GEM_VDI_CHAR_WIDTH	8
#define GEM_VDI_CHAR_HEIGHT	16
#define GEM_VDI_GLYPH_WIDTH	5
#define GEM_VDI_GLYPH_HEIGHT	7
#define GEM_VDI_GLYPH_ADVANCE	8
#define GEM_VDI_SYSTEM_GLYPH_BYTES 16U
#define GEM_VDI_USER_LINE	7

typedef struct gem_vdi_resident_workstation {
	WORD application;
	WORD handle;
	WORD write_mode;
	WORD line_type;
	WORD line_width;
	WORD line_color;
	UWORD line_pattern;
	WORD text_color;
	WORD text_height;
	WORD fill_interior;
	WORD fill_style;
	WORD fill_color;
	WORD clip_on;
	WORD clip_x1;
	WORD clip_y1;
	WORD clip_x2;
	WORD clip_y2;
	UWORD cursor_hides;
	UBYTE state;
	UBYTE reserved;
} GEM_VDI_RESIDENT_WORKSTATION;

/*
 * Classic GEM's MFDB starts with a four-byte fd_addr offset:segment pair.
 * Keeping that pair explicit preserves the original 20-byte wire record in
 * either GNU ia16 data model and lets a far resident resource bitmap cross
 * INT EF without flattening or conversion.
 */
typedef struct __attribute__((packed)) gem_vdi_resident_mfdb {
	GEM_BINDINGS_POINTER_SLOT memory;
	WORD width_pixels;
	WORD height;
	WORD width_words;
	WORD format;
	WORD planes;
	WORD reserved1;
	WORD reserved2;
	WORD reserved3;
} GEM_VDI_RESIDENT_MFDB;

typedef BYTE GEM_VDI_RESIDENT_MFDB_MUST_BE_20_BYTES
	[(sizeof(GEM_VDI_RESIDENT_MFDB) == 20) ? 1 : -1];

/* Original GEM MFORM: five scalar words, sixteen mask and sixteen data rows. */
typedef struct __attribute__((packed)) gem_vdi_resident_mform {
	WORD hot_x;
	WORD hot_y;
	WORD planes;
	WORD foreground;
	WORD background;
	UWORD mask[16];
	UWORD image[16];
} GEM_VDI_RESIDENT_MFORM;

typedef BYTE GEM_VDI_RESIDENT_MFORM_MUST_BE_74_BYTES
	[(sizeof(GEM_VDI_RESIDENT_MFORM) == 74) ? 1 : -1];

static GEM_VDI_RESIDENT_WORKSTATION
	gem_vdi_workstations[GEM_VDI_RESIDENT_WORKSTATIONS];
static GEM_VDI_SCREEN *gem_vdi_screen;
static UWORD gem_vdi_open_count;

/* Original VDI local array bounds retained by the Pacific-derived binding. */
static UWORD gem_vdi_control[GEM_VDI_CONTROL_WORDS];
static UWORD gem_vdi_intin[GEM_VDI_INTIN_WORDS];
static UWORD gem_vdi_ptsin[GEM_VDI_PTSIN_WORDS];
static UWORD gem_vdi_intout[GEM_VDI_INTOUT_WORDS];
static UWORD gem_vdi_ptsout[GEM_VDI_PTSOUT_WORDS];

static GEM_VDI_CURSOR gem_vdi_cursor;
static GEM_VDI_RESIDENT_MFORM gem_vdi_mouse_form;
static BYTE gem_vdi_resident_text_buffer[GEM_VDI_RESIDENT_TEXT_MAX];
static GEM_VDI_UBYTE gem_vdi_system_glyph[GEM_VDI_SYSTEM_GLYPH_BYTES];
static UWORD gem_vdi_font_segment;
static UWORD gem_vdi_font_offset;
static UBYTE gem_vdi_sound_enabled = TRUE;
static WORD gem_vdi_cursor_hot_x;
static WORD gem_vdi_cursor_hot_y;

/*
 * Original VDI colors use an unscaled zero-through-1000 intensity at the
 * client boundary.  Keep all sixteen requested triples as 16-bit words.  The
 * initial table is the GEM/3 palette shipped with the original PALETTE desk
 * accessory; the resident owner applies it directly after a VGA mode set.
 */
static WORD gem_vdi_palette[16][3] = {
	{ 1000, 1000, 1000 }, {    0,    0,    0 },
	{ 1000,    0,    0 }, {    0, 1000,    0 },
	{    0,    0, 1000 }, {    0, 1000, 1000 },
	{ 1000, 1000,    0 }, { 1000,    0, 1000 },
	{  666,  666,  666 }, {  333,  333,  333 },
	{  333,    0,    0 }, {    0,  333,    0 },
	{    0,    0,  333 }, {    0,  333,  333 },
	{  333,  333,    0 }, {  333,    0,  333 }
};

/*
 * Thresholds implement floor(value * 63 / 1000) without asking an 8088 to
 * multiply or divide.  Each entry is ceil(level * 1000 / 63), for levels one
 * through 63.  A zero-through-1000 VDI value walks at most 63 near words and
 * returns one six-bit VGA DAC value.  This operation occurs only when a user
 * changes a palette entry, never in a drawing hot path.  Conversion truncates
 * toward zero, values above 1000 saturate before this function is called, and
 * the result cannot overflow one byte.
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

/*
 * Fixed GEM system forms.  Shape four is the flat hand; shapes five through
 * seven select the three historical cross variants, represented by their
 * common one-plane system cross here.  Rows remain original 16-bit GEM words
 * with bit 15 at the left; no runtime bitmap conversion or allocation occurs.
 */
static const GEM_VDI_CURSOR gem_vdi_arrow_cursor = {
	16, 16, 0, 0, 15, 0,
	{
		0x8000, 0xc000, 0xa000, 0x9000,
		0x8800, 0x8400, 0x8200, 0x8100,
		0x8080, 0x87c0, 0x8400, 0x9600,
		0xa200, 0xc300, 0x0200, 0x0300
	},
	{
		0x8000, 0xc000, 0xe000, 0xf000,
		0xf800, 0xfc00, 0xfe00, 0xff00,
		0xff80, 0xffc0, 0xfc00, 0xec00,
		0xcc00, 0x8600, 0x0600, 0x0300
	}
};

static const GEM_VDI_CURSOR gem_vdi_hourglass_cursor = {
	16, 16, 7, 7, 15, 0,
	{
		0x7ffe, 0x4002, 0x300c, 0x1818,
		0x0c30, 0x0660, 0x03c0, 0x0180,
		0x0180, 0x03c0, 0x0660, 0x0c30,
		0x1818, 0x300c, 0x4002, 0x7ffe
	},
	{
		0x7ffe, 0x4002, 0x2004, 0x1008,
		0x0810, 0x0420, 0x0240, 0x0180,
		0x0180, 0x0240, 0x0420, 0x0810,
		0x1008, 0x2004, 0x4002, 0x7ffe
	}
};

static const GEM_VDI_CURSOR gem_vdi_text_cursor = {
	16, 16, 7, 8, 15, 0,
	{
		0x1ff0, 0x0180, 0x0180, 0x0180,
		0x0180, 0x0180, 0x0180, 0x0180,
		0x0180, 0x0180, 0x0180, 0x0180,
		0x0180, 0x0180, 0x0180, 0x1ff0
	},
	{
		0x1ff0, 0x03c0, 0x03c0, 0x03c0,
		0x03c0, 0x03c0, 0x03c0, 0x03c0,
		0x03c0, 0x03c0, 0x03c0, 0x03c0,
		0x03c0, 0x03c0, 0x03c0, 0x1ff0
	}
};

static const GEM_VDI_CURSOR gem_vdi_hand_cursor = {
	16, 16, 5, 1, 15, 0,
	{
		0x0600, 0x0900, 0x0900, 0x0900,
		0x0f70, 0x1888, 0x2888, 0x2888,
		0x2888, 0x2888, 0x1888, 0x0888,
		0x0448, 0x0448, 0x03f0, 0x0000
	},
	{
		0x0f00, 0x0f00, 0x0f00, 0x0f00,
		0x1ff8, 0x3ffc, 0x7ffc, 0x7ffc,
		0x7ffc, 0x7ffc, 0x3ffc, 0x1ffc,
		0x0ffc, 0x0ff8, 0x07f0, 0x0000
	}
};

static const GEM_VDI_CURSOR gem_vdi_cross_cursor = {
	16, 16, 7, 7, 15, 0,
	{
		0x0180, 0x0180, 0x0180, 0x0180,
		0x0180, 0x0180, 0x0180, 0x7ffe,
		0x7ffe, 0x0180, 0x0180, 0x0180,
		0x0180, 0x0180, 0x0180, 0x0180
	},
	{
		0x03c0, 0x03c0, 0x03c0, 0x03c0,
		0x03c0, 0x03c0, 0xffff, 0xffff,
		0xffff, 0xffff, 0x03c0, 0x03c0,
		0x03c0, 0x03c0, 0x03c0, 0x03c0
	}
};

static const GEM_VDI_UBYTE gem_vdi_fill_patterns[8][8] = {
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	{ 0x80, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40 },
	{ 0x88, 0x44, 0x22, 0x11, 0x88, 0x44, 0x22, 0x11 },
	{ 0x88, 0x22, 0x88, 0x22, 0x88, 0x22, 0x88, 0x22 },
	{ 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55 },
	{ 0x77, 0xee, 0xdd, 0xbb, 0x77, 0xee, 0xdd, 0xbb },
	{ 0x77, 0xbb, 0xdd, 0xee, 0x77, 0xbb, 0xdd, 0xee },
	{ 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff }
};

static VOID
gem_vdi_clear_words(UWORD *words, UWORD count)
{
	while (count--)
		*words++ = 0;
}

static GEM_VDI_COLOR
gem_vdi_resident_color(WORD logical)
{
	switch ((UWORD) logical & 15U) {
	case GEM_VDI_WHITE:
		return 15;
	case GEM_VDI_BLACK:
		return 0;
	case GEM_VDI_RED:
		return 4;
	case GEM_VDI_GREEN:
		return 2;
	case GEM_VDI_BLUE:
		return 1;
	case GEM_VDI_CYAN:
		return 3;
	case GEM_VDI_YELLOW:
		return 14;
	case GEM_VDI_MAGENTA:
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

	/*
	 * Three-word row addressing is expressed as bounded pointer increments.
	 * This prevents a size optimizer from replacing index times six bytes with
	 * a slow 8088 MUL instruction.  Callers validate index below sixteen.
	 */
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

static VOID __attribute__((optimize("Os")))
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

/*
 * VS_COLOR and VQ_COLOR are setup/query operations, never drawing hot paths.
 * Keeping their shared validation and three-word copy in one size-optimized
 * helper leaves the main VDI switch compact without changing either classic
 * opcode.  The operation word itself selects the direction, so no extra far
 * call argument or duplicate wrapper is needed.
 */
static WORD __attribute__((optimize("Os")))
gem_vdi_palette_request(VOID)
{
	const UWORD *source;
	UWORD *destination;
	WORD *entry;
	WORD *first;
	UWORD count;
	UWORD index;
	WORD setting;

	setting = gem_vdi_control[0] == GEM_VDI_OP_SET_COLOR;
	if (gem_vdi_control[3] < (setting ? 4U : 2U)
	    || gem_vdi_intin[0] >= 16U)
		return FALSE;
	index = gem_vdi_intin[0];
	entry = gem_vdi_palette_entry(index);
	first = entry;
	count = 3;
	if (setting) {
		source = &gem_vdi_intin[1];
		while (count--)
			*entry++ = gem_vdi_palette_component(*source++);
		gem_vdi_palette_apply(index, first);
	} else {
		gem_vdi_intout[0] = index;
		destination = &gem_vdi_intout[1];
		while (count--)
			*destination++ = (UWORD) *entry++;
		gem_vdi_control[4] = 4;
	}
	return TRUE;
}

static GEM_VDI_WORD __attribute__((optimize("Os")))
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

static GEM_VDI_RESIDENT_WORKSTATION * __attribute__((optimize("Os")))
gem_vdi_allocate_workstation(WORD application)
{
	GEM_VDI_RESIDENT_WORKSTATION *workstation;
	UWORD handle;

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
			workstation->text_color = GEM_VDI_BLACK;
			workstation->text_height = GEM_VDI_CHAR_HEIGHT;
			workstation->fill_interior = GEM_VDI_FIS_SOLID;
			workstation->fill_style = 7;
			workstation->fill_color = GEM_VDI_BLACK;
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

static VOID
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

static WORD
gem_vdi_set_one_output(WORD value)
{
	gem_vdi_intout[0] = (UWORD) value;
	gem_vdi_control[4] = 1;
	return TRUE;
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

	/* Classic VDI work_out is 45 integer words plus six point pairs. */
	gem_vdi_intout[0] = (UWORD) (gem_vdi_screen->xres - 1);
	gem_vdi_intout[1] = (UWORD) (gem_vdi_screen->yres - 1);
	gem_vdi_intout[2] = 1;
	gem_vdi_intout[3] = 1;
	gem_vdi_intout[4] = 1;
	gem_vdi_intout[13] = 16;
	gem_vdi_intout[35] = 2;
	gem_vdi_intout[39] = 8;
	gem_vdi_ptsout[0] = GEM_VDI_CHAR_WIDTH;
	gem_vdi_ptsout[1] = GEM_VDI_CHAR_HEIGHT;
	gem_vdi_ptsout[2] = GEM_VDI_CHAR_WIDTH;
	gem_vdi_ptsout[3] = GEM_VDI_CHAR_HEIGHT;
	gem_vdi_ptsout[4] = 1;
	gem_vdi_ptsout[5] = 0;
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

static VOID
gem_vdi_fill_output(GEM_VDI_RESIDENT_WORKSTATION *workstation)
{
	GEM_VDI_COORD x1;
	GEM_VDI_COORD y1;
	GEM_VDI_COORD x2;
	GEM_VDI_COORD y2;
	GEM_VDI_COORD width;
	GEM_VDI_COORD height;
	const GEM_VDI_UBYTE *pattern;

	x1 = (WORD) gem_vdi_ptsin[0];
	y1 = (WORD) gem_vdi_ptsin[1];
	x2 = (WORD) gem_vdi_ptsin[2];
	y2 = (WORD) gem_vdi_ptsin[3];
	if (x2 < x1 || y2 < y1)
		return;
	width = x2 - x1 + 1;
	height = y2 - y1 + 1;
	if (workstation->fill_interior == GEM_VDI_FIS_HOLLOW)
		return;
	gem_vdi_apply_workstation(workstation, workstation->fill_color);
	if (workstation->fill_interior == GEM_VDI_FIS_SOLID
	    || workstation->fill_style == 7) {
		gem_vdi_fill_rect(gem_vdi_screen, x1, y1, width, height);
		return;
	}
	pattern = gem_vdi_fill_patterns[(UWORD) workstation->fill_style & 7U];
	gem_vdi_fill_pattern(gem_vdi_screen, x1, y1, width, height, pattern);
}

static const GEM_VDI_UBYTE *
gem_vdi_glyph_rows(WORD character)
{
	static const GEM_VDI_UBYTE blank[7] =
		{ 0, 0, 0, 0, 0, 0, 0 };
	static const GEM_VDI_UBYTE digits[10][7] = {
		{ 0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e },
		{ 0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e },
		{ 0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f },
		{ 0x1e, 0x01, 0x01, 0x0e, 0x01, 0x01, 0x1e },
		{ 0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02 },
		{ 0x1f, 0x10, 0x10, 0x1e, 0x01, 0x01, 0x1e },
		{ 0x0e, 0x10, 0x10, 0x1e, 0x11, 0x11, 0x0e },
		{ 0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 },
		{ 0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e },
		{ 0x0e, 0x11, 0x11, 0x0f, 0x01, 0x01, 0x0e }
	};
	static const GEM_VDI_UBYTE letters[26][7] = {
		{ 0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11 },
		{ 0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e },
		{ 0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e },
		{ 0x1e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1e },
		{ 0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f },
		{ 0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10 },
		{ 0x0e, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0f },
		{ 0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11 },
		{ 0x0e, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e },
		{ 0x07, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0c },
		{ 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 },
		{ 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f },
		{ 0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11 },
		{ 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 },
		{ 0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e },
		{ 0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10 },
		{ 0x0e, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0d },
		{ 0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11 },
		{ 0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e },
		{ 0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 },
		{ 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e },
		{ 0x11, 0x11, 0x11, 0x11, 0x11, 0x0a, 0x04 },
		{ 0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0a },
		{ 0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11 },
		{ 0x11, 0x11, 0x0a, 0x04, 0x04, 0x04, 0x04 },
		{ 0x1f, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1f }
	};
	static const GEM_VDI_UBYTE dot[7] =
		{ 0, 0, 0, 0, 0, 0x0c, 0x0c };
	static const GEM_VDI_UBYTE colon[7] =
		{ 0, 0x0c, 0x0c, 0, 0x0c, 0x0c, 0 };
	static const GEM_VDI_UBYTE slash[7] =
		{ 0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10 };
	static const GEM_VDI_UBYTE backslash[7] =
		{ 0x10, 0x08, 0x08, 0x04, 0x02, 0x02, 0x01 };
	static const GEM_VDI_UBYTE dash[7] =
		{ 0, 0, 0, 0x1f, 0, 0, 0 };
	static const GEM_VDI_UBYTE underscore[7] =
		{ 0, 0, 0, 0, 0, 0, 0x1f };
	const GEM_VDI_UBYTE *glyph;
	UWORD index;

	if (character >= 'a' && character <= 'z')
		character -= 'a' - 'A';
	if (character >= 'A' && character <= 'Z') {
		glyph = &letters[0][0];
		index = (UWORD) (character - 'A');
		while (index--)
			glyph += GEM_VDI_GLYPH_HEIGHT;
		return glyph;
	}
	if (character >= '0' && character <= '9') {
		glyph = &digits[0][0];
		index = (UWORD) (character - '0');
		while (index--)
			glyph += GEM_VDI_GLYPH_HEIGHT;
		return glyph;
	}
	switch (character) {
	case '.':
		return dot;
	case ':':
		return colon;
	case '/':
		return slash;
	case '\\':
		return backslash;
	case '-':
		return dash;
	case '_':
		return underscore;
	default:
		return blank;
	}
}

static const GEM_VDI_UBYTE *
gem_vdi_system_glyph_rows(WORD character)
{
	const GEM_VDI_UBYTE *source;
	UWORD glyph_offset;
	UWORD glyph_segment;
	UWORD index;
	UWORD destination;

	/*
	 * VGA BIOS function 1130h exposes the same native 8 by 16 PC glyph cells
	 * used by the original screen driver.  Locate character times sixteen with
	 * four bounded word doublings; no multiply, wide pointer, or conversion is
	 * involved.  A carry in the offset advances the real-mode segment by 1000h,
	 * which preserves the same physical byte address modulo the 20-bit bus.
	 */
	if (gem_vdi_font_segment) {
		glyph_offset = (UWORD) character & 0x00ffU;
		glyph_offset += glyph_offset;
		glyph_offset += glyph_offset;
		glyph_offset += glyph_offset;
		glyph_offset += glyph_offset;
		glyph_segment = gem_vdi_font_segment;
		glyph_offset += gem_vdi_font_offset;
		if (glyph_offset < gem_vdi_font_offset)
			glyph_segment += 0x1000U;
		gem_resident_memory_from(glyph_segment, glyph_offset,
			gem_vdi_system_glyph, GEM_VDI_SYSTEM_GLYPH_BYTES);
		return gem_vdi_system_glyph;
	}

	/*
	 * A pre-VGA BIOS may not return selector six.  Keep an exact 8 by 16 cell
	 * by placing the compact original fallback between one blank top and one
	 * blank bottom row, doubling each of its seven source rows vertically.
	 */
	source = gem_vdi_glyph_rows(character);
	destination = 0;
	gem_vdi_system_glyph[destination++] = 0;
	index = 0;
	while (index < GEM_VDI_GLYPH_HEIGHT) {
		gem_vdi_system_glyph[destination++] = source[index];
		gem_vdi_system_glyph[destination++] = source[index];
		index++;
	}
	gem_vdi_system_glyph[destination] = 0;
	return gem_vdi_system_glyph;
}

static WORD __far __attribute__((far_section, noinline,
	section(".fartext.gemvdi_draw_text")))
gem_vdi_draw_text(GEM_VDI_RESIDENT_WORKSTATION *workstation)
{
	const GEM_VDI_UBYTE *glyph;
	UWORD owner_segment;
	WORD x;
	WORD y;
	WORD character;
	UWORD count;
	UWORD *text;

	if (!gem_vdi_control[3])
		return TRUE;
	gem_vdi_apply_workstation(workstation, workstation->text_color);
	x = (WORD) gem_vdi_ptsin[0];
	y = (WORD) gem_vdi_ptsin[1] - GEM_VDI_CHAR_HEIGHT;
	text = gem_vdi_intin;
	count = gem_vdi_control[3];

	/*
	 * The ordinary VDI intin representation keeps one character in each
	 * little-endian word.  Its low bytes are therefore a stride-two view of
	 * the unchanged request array.  A fully visible aligned run reaches the
	 * PC driver once, where the original BIOS glyphs are consumed directly;
	 * no converted font or line bitmap is allocated.  Clipped, unaligned,
	 * non-planar, and pre-VGA cases retain the exact glyph loop below.
	 */
	if (gem_vdi_font_segment
	    && gem_vdi_text_run(gem_vdi_screen, x, y,
		(const GEM_VDI_UBYTE *) text, count, 2U,
		gem_vdi_font_segment, gem_vdi_font_offset))
		return TRUE;
	__asm__ volatile ("movw %%ds,%0" : "=r" (owner_segment)
		: : "memory");
	while (count--) {
		character = (WORD) *text++;
		glyph = gem_vdi_system_glyph_rows(character);
		gem_vdi_glyph(gem_vdi_screen, x, y, GEM_VDI_CHAR_WIDTH,
			GEM_VDI_CHAR_HEIGHT, glyph, 0x80);
		__asm__ volatile ("movw %0,%%ds" : : "r" (owner_segment)
			: "memory");
		x += GEM_VDI_GLYPH_ADVANCE;
	}
	return TRUE;
}

static WORD
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
	if (subfunction == GEM_VDI_GDP_BAR
	    || subfunction == GEM_VDI_GDP_FILLED_ROUNDED_BOX) {
		gem_vdi_fill_output(workstation);
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
		gem_vdi_rect(gem_vdi_screen, x1, y1, x2 - x1 + 1,
			y2 - y1 + 1);
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

static WORD
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
		(WORD) gem_vdi_ptsin[0], (WORD) gem_vdi_ptsin[1],
		mode & 15);
	return TRUE;
}

/*
 * Calculate the exact byte extent of one classic MFDB without a C multiply.
 * One scan line contains width_words times two bytes.  Repeated checked adds
 * then form the height and plane products.  This is a cold resource/setup
 * path; keeping MUL and compiler helper calls out is more valuable on an 8088
 * than replacing the bounded loops with a wider arithmetic interface.  The
 * local GNU size attribute affects only this cold helper and preserves text
 * space for the resident owner; the 8086 code-generation gate still rejects
 * any size optimization which recreates MUL or DIV.
 *
 * Every successful result is in the inclusive range 1..65535.  There is no
 * rounding or saturation.  A form whose exact extent cannot fit in one
 * real-mode offset window is rejected before an address is touched.
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
	__asm__ volatile ("shlw %0" : "+r" (row_bytes) : : "cc");

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
 * Copy one validated memory-form span between exact real-mode segments.
 * Forward REP MOVSB handles disjoint forms.  An overlapping move within one
 * segment runs backward, then restores the ABI-required forward direction.
 * DS and ES are saved around the transfer, so resident near data and the
 * caller's stack assumptions are unchanged when GNU C resumes.
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
				  "popw %%ds"
				  : "+S" (source.lo), "+D" (destination.lo),
				    "+c" (count)
				  : "r" (source.hi), "r" (destination.hi)
				  : "cc", "memory");
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
			  "popw %%ds"
			  : "+S" (source.lo), "+D" (destination.lo),
			    "+c" (count)
			  : "r" (source.hi), "r" (destination.hi)
			  : "cc", "memory");
}

/*
 * Implement VDI opcode 110 for the resident native memory-form layout.
 * The PC driver consumes the original GEM word/plane representation directly,
 * so standard and device forms have the same byte order here.  Transformation
 * is therefore a checked memmove, not an empty compatibility stub.  This is
 * particularly important for the original TRIMAGE.C fix_icon() path, which
 * intentionally transforms icon mask/data in place.
 */
static WORD __attribute__((optimize("Os")))
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

static UWORD __attribute__((optimize("Os")))
gem_vdi_sound_divisor(UWORD frequency)
{
	UWORD remaining_high;
	UWORD remaining_low;
	UWORD step;
	UWORD multiple;
	UWORD divisor;
	UWORD old_low;

	/*
	 * PIT channel two runs at 1,193,181 Hz, represented here as the explicit
	 * word pair 0012h:34ddh.  Build the unsigned quotient with doubled 16-bit
	 * subtraction steps.  Every shift is a single-bit 8086 operation; there
	 * is no C wide scalar, MUL, DIV, helper call, or fractional value.
	 *
	 * The PIT interface accepts one 16-bit reload word.  Quotients below one
	 * round up to one, while a quotient above 65535 saturates at 65535.  All
	 * ordinary audible GEM frequencies are inside that range.
	 */
	if (!frequency)
		return 0;
	remaining_high = 0x0012U;
	remaining_low = 0x34ddU;
	step = frequency;
	multiple = 1U;
	while (step <= 0x7fffU && multiple <= 0x7fffU) {
		step += step;
		multiple += multiple;
	}
	divisor = 0;
	while (multiple) {
		while (remaining_high || remaining_low >= step) {
			old_low = remaining_low;
			remaining_low -= step;
			if (old_low < step)
				remaining_high--;
			if (divisor > (UWORD) (0xffffU - multiple))
				return 0xffffU;
			divisor += multiple;
		}
		step >>= 1;
		multiple >>= 1;
	}
	return divisor ? divisor : 1U;
}

static UWORD __attribute__((optimize("Os")))
gem_vdi_sound_ticks(WORD duration)
{
	UWORD ticks;

	/*
	 * Original GEM durations are twentieths of a second.  ELKS uses a fixed
	 * 100 Hz sequencer, so five kernel ticks equal one GEM duration unit.
	 * Values above 13107 saturate at one 16-bit tick word.  Four checked adds
	 * replace multiplication and preserve exact rounding for every lower value.
	 */
	if (duration <= 0)
		return 1U;
	ticks = (UWORD) duration;
	if (ticks > 13107U)
		return 0xffffU;
	duration = (WORD) ticks;
	ticks += (UWORD) duration;
	ticks += (UWORD) duration;
	ticks += (UWORD) duration;
	ticks += (UWORD) duration;
	return ticks;
}

static WORD __attribute__((optimize("Os")))
gem_vdi_sound_stop(VOID)
{
	struct audio_seq sequence;

	sequence.events = NULL;
	sequence.count = 0;
	sequence.rate_hz = 0;
	sequence.flags = AUDIO_SEQ_F_STOP;
	return ioctl(0, KIOCSNDSEQ, &sequence) >= 0 ? TRUE : FALSE;
}

static WORD __attribute__((optimize("Os")))
gem_vdi_sound_play(WORD frequency, WORD duration)
{
	struct audio_event event;
	struct audio_seq sequence;
	WORD result;

	if (!gem_vdi_sound_enabled)
		return TRUE;
	if (frequency < 0)
		return FALSE;
	event.divisor = gem_vdi_sound_divisor((UWORD) frequency);
	event.ticks = gem_vdi_sound_ticks(duration);
	event.flags = event.divisor ? AUDIO_F_TONE : AUDIO_F_REST;
	event.priority = 0;
	sequence.events = &event;
	sequence.count = 1;
	sequence.rate_hz = 0;
	sequence.flags = 0;
	result = (WORD) ioctl(0, KIOCSNDSEQ, &sequence);
	return result == 1 ? TRUE : FALSE;
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
				gem_vdi_sound_enabled = FALSE;
				(void) gem_vdi_sound_stop();
			} else if ((WORD) gem_vdi_intin[0] == 1) {
				gem_vdi_sound_enabled = TRUE;
			}
			gem_vdi_intout[0] = gem_vdi_sound_enabled ? 1U : 0U;
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
		__asm__ volatile ("movw %%ds,%0" : "=r" (owner_segment)
			: : "memory");
		while (remaining--) {
			x2 = *line++;
			y2 = *line++;
			if (workstation->line_type == GEM_VDI_USER_LINE)
				gem_vdi_pattern_line(gem_vdi_screen, x1, y1,
					x2, y2, workstation->line_pattern, TRUE);
			else
				gem_vdi_line(gem_vdi_screen, x1, y1, x2, y2,
					TRUE);
			__asm__ volatile ("movw %0,%%ds" : : "r" (owner_segment)
				: "memory");
			x1 = x2;
			y1 = y2;
		}
		return TRUE;
	case GEM_VDI_OP_GTEXT:
		return gem_vdi_draw_text(workstation);
	case GEM_VDI_OP_FILLAREA:
		if (!gem_vdi_control[1])
			return FALSE;
		gem_vdi_apply_workstation(workstation, workstation->fill_color);
		gem_vdi_fill_polygon(gem_vdi_screen,
			(WORD) gem_vdi_control[1], (WORD *) gem_vdi_ptsin);
		return TRUE;
	case GEM_VDI_OP_GDP:
		return gem_vdi_dispatch_gdp(workstation);
	case GEM_VDI_OP_TEXT_HEIGHT:
		/* The resident owner has one exact, unscaled 8 by 16 system face. */
		workstation->text_height = GEM_VDI_CHAR_HEIGHT;
		gem_vdi_ptsout[0] = GEM_VDI_CHAR_WIDTH;
		gem_vdi_ptsout[1] = (UWORD) workstation->text_height;
		gem_vdi_ptsout[2] = GEM_VDI_CHAR_WIDTH;
		gem_vdi_ptsout[3] = (UWORD) workstation->text_height;
		gem_vdi_control[2] = 2;
		return TRUE;
	case GEM_VDI_OP_SET_COLOR:
		/*
		 * VS_COLOR supplies one logical index followed by requested red,
		 * green, and blue intensities.  The VDI interface is unscaled integer
		 * zero through 1000.  Clamp malformed files or clients at that boundary,
		 * retain the exact requested words for VQ_COLOR, and program the mapped
		 * VGA DAC entry once.  No framebuffer data or object resource changes.
		 */
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
	case GEM_VDI_OP_LINE_COLOR:
		workstation->line_color = (WORD) gem_vdi_intin[0] & 15;
		return gem_vdi_set_one_output(workstation->line_color);
	case GEM_VDI_OP_TEXT_COLOR:
		workstation->text_color = (WORD) gem_vdi_intin[0] & 15;
		return gem_vdi_set_one_output(workstation->text_color);
	case GEM_VDI_OP_FILL_INTERIOR:
		workstation->fill_interior = (WORD) gem_vdi_intin[0];
		return gem_vdi_set_one_output(workstation->fill_interior);
	case GEM_VDI_OP_FILL_STYLE:
		workstation->fill_style = (WORD) gem_vdi_intin[0] & 7;
		return gem_vdi_set_one_output(workstation->fill_style);
	case GEM_VDI_OP_FILL_COLOR:
		workstation->fill_color = (WORD) gem_vdi_intin[0] & 15;
		return gem_vdi_set_one_output(workstation->fill_color);
	case GEM_VDI_OP_QUERY_COLOR:
		/*
		 * PALETTE asks both requested (flag zero) and realized (flag one)
		 * values.  The resident owner returns the retained requested intensities
		 * for both flags: its VGA realization differs by at most one six-bit DAC
		 * step, while EGA/CGA/Hercules cannot report a VGA DAC realization.  This
		 * preserves one deterministic 0..1000 interface on every adapter.
		 */
		return gem_vdi_palette_request();
	case GEM_VDI_OP_WRITE_MODE:
		workstation->write_mode = (WORD) gem_vdi_intin[0];
		if (workstation->write_mode < GEM_VDI_MD_REPLACE
		    || workstation->write_mode > GEM_VDI_MD_ERASE)
			workstation->write_mode = GEM_VDI_MD_REPLACE;
		return gem_vdi_set_one_output(workstation->write_mode);
	case GEM_VDI_OP_TEXT_ATTR:
		gem_vdi_intout[0] = 1;
		gem_vdi_intout[1] = (UWORD) workstation->text_color;
		gem_vdi_intout[2] = 0;
		gem_vdi_intout[3] = 0;
		gem_vdi_intout[4] = 0;
		gem_vdi_intout[5] = (UWORD) workstation->write_mode;
		gem_vdi_ptsout[0] = GEM_VDI_CHAR_WIDTH;
		gem_vdi_ptsout[1] = GEM_VDI_CHAR_HEIGHT;
		gem_vdi_ptsout[2] = GEM_VDI_CHAR_WIDTH;
		gem_vdi_ptsout[3] = GEM_VDI_CHAR_HEIGHT;
		gem_vdi_control[4] = 6;
		gem_vdi_control[2] = 2;
		return TRUE;
	case GEM_VDI_OP_EXTENDED:
		/* Return the ordinary 57-word capability record for either query. */
		gem_vdi_intout[0] = (UWORD) (gem_vdi_screen->xres - 1);
		gem_vdi_intout[1] = (UWORD) (gem_vdi_screen->yres - 1);
		gem_vdi_intout[4] = 2;
		gem_vdi_intout[11] = 1;
		gem_vdi_control[4] = GEM_VDI_INTOUT_WORDS;
		gem_vdi_control[2] = GEM_VDI_MAX_OUTPUT_POINTS;
		return TRUE;
	case GEM_VDI_OP_COPY_FORM:
	case GEM_VDI_OP_COPY_TRANSPARENT:
		return gem_vdi_screen_copy(workstation, request);
	case GEM_VDI_OP_TRANSFORM:
		return gem_vdi_transform_form(request);
	case GEM_VDI_OP_CURSOR_FORM:
		if (gem_vdi_control[3] < 37)
			return FALSE;
		gem_vdi_cursor.width = 16;
		gem_vdi_cursor.height = 16;
		gem_vdi_cursor.hot_x = (WORD) gem_vdi_intin[0];
		gem_vdi_cursor.hot_y = (WORD) gem_vdi_intin[1];
		gem_vdi_cursor.foreground = gem_vdi_resident_color(
			(WORD) gem_vdi_intin[3]);
		gem_vdi_cursor.background = gem_vdi_resident_color(
			(WORD) gem_vdi_intin[4]);
		index = 0;
		while (index < 16U) {
			gem_vdi_cursor.mask[index] = gem_vdi_intin[index + 5U];
			gem_vdi_cursor.image[index] = gem_vdi_intin[index + 21U];
			index++;
		}
		gem_vdi_set_cursor(&gem_vdi_cursor);
		return TRUE;
	case GEM_VDI_OP_USER_LINE:
		if (!gem_vdi_control[3])
			return FALSE;
		workstation->line_pattern = gem_vdi_intin[0];
		return TRUE;
	case GEM_VDI_OP_FILL_RECT:
		gem_vdi_fill_output(workstation);
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
		(void) gem_vdi_read_keyboard(&character, &modifiers, &scan_code);
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
	case GEM_VDI_OP_TEXT_NAME: {
		static const BYTE name[] = "GEM SYSTEM FONT";
		const BYTE *source;

		gem_vdi_intout[0] = 1;
		source = name;
		index = 1U;
		while (*source && index < 33U)
			gem_vdi_intout[index++] = (UBYTE) *source++;
		gem_vdi_control[4] = 33;
		return TRUE;
	}
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

	/*
	 * gemaes is the sole process which owns the ELKS console graphics lock.
	 * This idempotent open happens before broker registration, matching the
	 * classic GEM sequence in which GSX geometry exists before GRAF_HANDLE or
	 * RSRC_LOAD.  There is no reference-count arithmetic at this boundary.
	 */
	if (gem_vdi_screen)
		return TRUE;
	__asm__ volatile ("movw %%ds,%0" : "=r" (owner_segment)
		: : "memory");
	screen = gem_vdi_open();
	__asm__ volatile ("movw %0,%%ds" : : "r" (owner_segment) : "memory");
	if (!screen)
		return FALSE;
	gem_vdi_screen = screen;
	gem_vdi_font_segment = 0;
	gem_vdi_font_offset = 0;
	gem_bios_video_font_8x16(&gem_vdi_font_segment,
		&gem_vdi_font_offset);
	gem_vdi_cursor_hot_x = 0;
	gem_vdi_cursor_hot_y = 0;
	/*
	 * BIOS mode selection restores the adapter palette.  Reapply the original
	 * GEM/3 requested colors before any resident client draws, so every logical
	 * index and the PALETTE accessory begin from the same state.  On non-VGA
	 * adapters the hardware helper returns without I/O and the logical table is
	 * still available through the classic VDI query call.
	 */
	gem_vdi_palette_apply_all();

	/*
	 * A BIOS graphics mode set clears every adapter to hardware color zero,
	 * which is black with the standard PC palette.  Original GEM opens its
	 * physical workstation onto a white screen.  AES deliberately represents
	 * the menu bar as a hollow object, so black menu text would disappear if
	 * this invariant were deferred to an application workstation.
	 *
	 * GEM logical white maps to hardware color fifteen in this native driver.
	 * The fill uses adapter-native scan-line spans and needs no temporary
	 * bitmap, allocation, conversion pass, multiplication, or division.
	 */
	gem_vdi_set_mode(GEM_VDI_REPLACE);
	gem_vdi_set_foreground(gem_vdi_screen,
		gem_vdi_resident_color(GEM_VDI_WHITE));
	gem_vdi_set_clip(gem_vdi_screen, 0, NULL);
	gem_vdi_fill_rect(gem_vdi_screen, 0, 0, gem_vdi_screen->xres,
		gem_vdi_screen->yres);

	/*
	 * The physical VDI opens with one cursor hide outstanding.  Original
	 * GEM's workstation open installs the arrow and balances that initial
	 * hide before AES starts accepting GRAF_MOUSE requests.  Keep that exact
	 * boundary here: later M_OFF/M_ON requests retain their nested hide
	 * semantics, while an ordinary ARROW or HOURGLASS request changes only
	 * the form.  Reading the current mouse word pair also places the first
	 * saved-background rectangle at the real serial-mouse coordinates.
	 */
	gem_vdi_set_cursor(&gem_vdi_arrow_cursor);
	gem_vdi_cursor_hot_x = gem_vdi_arrow_cursor.hot_x;
	gem_vdi_cursor_hot_y = gem_vdi_arrow_cursor.hot_y;
	mouse_x = 0;
	mouse_y = 0;
	mouse_buttons = 0;
	(void) gem_vdi_read_mouse(&mouse_x, &mouse_y, &mouse_buttons);
	gem_vdi_move_cursor(mouse_x - gem_vdi_cursor_hot_x,
		mouse_y - gem_vdi_cursor_hot_y);
	gem_vdi_show_cursor(gem_vdi_screen);
	/* A generated far-video access must not escape into resident near data. */
	__asm__ volatile ("movw %0,%%ds" : : "r" (owner_segment) : "memory");
	return TRUE;
}

WORD __attribute__((optimize("Os")))
gem_vdi_resident_get_metrics(UWORD *width, UWORD *height,
	UWORD *character_width, UWORD *character_height)
{
	if (!gem_vdi_screen || !width || !height || !character_width
	    || !character_height)
		return FALSE;

	/*
	 * Screen values are exact pixel counts, not maximum coordinates.  The
	 * cell is the resident fixed GEM system font's 8 by 16 pixel box.
	 */
	*width = (UWORD) gem_vdi_screen->xres;
	*height = (UWORD) gem_vdi_screen->yres;
	*character_width = GEM_VDI_CHAR_WIDTH;
	*character_height = GEM_VDI_CHAR_HEIGHT;
	return TRUE;
}

GEM_VDI_SCREEN *
gem_vdi_resident_screen(VOID)
{
	/* The pointer never crosses INT EF and remains owned by resident gemaes. */
	return gem_vdi_screen;
}

static const GEM_VDI_CURSOR * __attribute__((optimize("Os")))
gem_vdi_resident_system_cursor(WORD number)
{
	switch (number) {
	case 0:
		return &gem_vdi_arrow_cursor;
	case 1:
		return &gem_vdi_text_cursor;
	case 2:
		return &gem_vdi_hourglass_cursor;
	case 3:
	case 4:
		return &gem_vdi_hand_cursor;
	case 5:
	case 6:
	case 7:
		return &gem_vdi_cross_cursor;
	default:
		return NULL;
	}
}

static VOID __attribute__((optimize("Os")))
gem_vdi_resident_apply_cursor(const GEM_VDI_CURSOR *cursor)
{
	GEM_VDI_COORD x;
	GEM_VDI_COORD y;
	GEM_VDI_WORD buttons;

	gem_vdi_set_cursor(cursor);
	gem_vdi_cursor_hot_x = cursor->hot_x;
	gem_vdi_cursor_hot_y = cursor->hot_y;
	gem_vdi_read_mouse(&x, &y, &buttons);
	gem_vdi_move_cursor(x - cursor->hot_x, y - cursor->hot_y);
}

WORD __attribute__((optimize("Os")))
gem_vdi_resident_mouse(const struct gemtrap_request *request,
	WORD application, WORD number, GEM_BINDINGS_POINTER_SLOT form)
{
	const GEM_VDI_CURSOR *cursor;
	UWORD index;

	if (!gem_vdi_screen || application < 0)
		return FALSE;
	if (number == 256) {
		gem_vdi_hide_cursor(gem_vdi_screen);
		return TRUE;
	}
	if (number == 257) {
		gem_vdi_show_cursor(gem_vdi_screen);
		return TRUE;
	}

	cursor = gem_vdi_resident_system_cursor(number);
	if (cursor) {
		gem_vdi_resident_apply_cursor(cursor);
		return TRUE;
	}
	if (number != 255 || !request
	    || !gem_resident_memory_pointer(request, form,
					   sizeof(gem_vdi_mouse_form)))
		return FALSE;

	/*
	 * Copy the client's original one-plane MFORM exactly once into resident
	 * fixed storage.  Color words are translated to the already-open physical
	 * palette; mask and image rows retain their original bit order.
	 */
	gem_resident_memory_from(request->ds, form.lo, &gem_vdi_mouse_form,
		sizeof(gem_vdi_mouse_form));
	if (gem_vdi_mouse_form.planes != 1)
		return FALSE;
	gem_vdi_cursor.width = 16;
	gem_vdi_cursor.height = 16;
	gem_vdi_cursor.hot_x = gem_vdi_mouse_form.hot_x;
	gem_vdi_cursor.hot_y = gem_vdi_mouse_form.hot_y;
	gem_vdi_cursor.foreground = gem_vdi_resident_color(
		gem_vdi_mouse_form.foreground);
	gem_vdi_cursor.background = gem_vdi_resident_color(
		gem_vdi_mouse_form.background);
	index = 0;
	while (index < 16U) {
		gem_vdi_cursor.mask[index] = gem_vdi_mouse_form.mask[index];
		gem_vdi_cursor.image[index] = gem_vdi_mouse_form.image[index];
		index++;
	}
	gem_vdi_resident_apply_cursor(&gem_vdi_cursor);
	return TRUE;
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

	/*
	 * This seam is same-process resident GSX, not a client VDI parameter
	 * block.  y is the top of the advertised 16-pixel system-font cell.  The
	 * native BIOS system glyph occupies that exact cell.  Each character
	 * advances eight unscaled pixels and only set glyph bits replace video, so menu
	 * backgrounds remain intact.  The fixed copy bound also bounds the string
	 * scan exactly.
	 */
	gem_vdi_set_mode(GEM_VDI_REPLACE);
	gem_vdi_set_foreground(gem_vdi_screen,
		gem_vdi_resident_color(color));
	index = 0;
	while (index < max_characters
	       && gem_vdi_resident_text_buffer[index])
		index++;
	if (index && gem_vdi_font_segment
	    && gem_vdi_text_run(gem_vdi_screen, x, y,
		(const GEM_VDI_UBYTE *) gem_vdi_resident_text_buffer,
		index, 1U, gem_vdi_font_segment, gem_vdi_font_offset))
		return TRUE;
	index = 0;
	__asm__ volatile ("movw %%ds,%0" : "=r" (owner_segment)
		: : "memory");
	while (index < max_characters
	       && gem_vdi_resident_text_buffer[index]) {
		if (x > gem_vdi_screen->xres - GEM_VDI_GLYPH_WIDTH)
			break;
		glyph = gem_vdi_system_glyph_rows(
			(UBYTE) gem_vdi_resident_text_buffer[index]);
		gem_vdi_glyph(gem_vdi_screen, x, y, GEM_VDI_CHAR_WIDTH,
			GEM_VDI_CHAR_HEIGHT, glyph, 0x80);
		__asm__ volatile ("movw %0,%%ds" : : "r" (owner_segment)
			: "memory");
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
	/*
	 * GEM returns the BIOS scan in the high byte and ASCII in the low byte.
	 * The fixed eight-bit shift is compile-time constant; both source values
	 * are masked first, so the result cannot overflow its one-word interface.
	 */
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
					  sizeof(parameter_block),
					  request->data_limit))
		return -1;
	gem_resident_memory_from(request->ds, request->dx, &parameter_block,
		sizeof(parameter_block));

	/* The classic VDIPB is exactly five four-byte slots, never a C pointer. */
	if (sizeof(parameter_block) != 20
	    || !gem_resident_memory_pointer(request, parameter_block.contrl,
					   GEM_VDI_CONTROL_WORDS * 2U))
		return -1;
	gem_resident_memory_from(request->ds, parameter_block.contrl.lo,
		gem_vdi_control, GEM_VDI_CONTROL_WORDS * 2U);

	if (!gem_resident_memory_word_bytes(gem_vdi_control[3],
					    GEM_VDI_INTIN_WORDS,
					    &integer_input_bytes)
	    || !gem_resident_memory_point_bytes(gem_vdi_control[1],
						GEM_VDI_MAX_INPUT_POINTS,
						&point_input_bytes)
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

	/* Output counts are produced here; stale caller values never escape. */
	gem_vdi_control[2] = 0;
	gem_vdi_control[4] = 0;
	__asm__ volatile ("movw %%ds,%0" : "=r" (owner_segment)
		: : "memory");
	result = gem_vdi_dispatch(request, application);
	/*
	 * GNU ia16 far-video accesses may temporarily load their target segment
	 * into DS.  Restore the exact resident data segment before examining the
	 * original VDI arrays or copying a reply.  SS is intentionally not used as
	 * a substitute because the ELKS owner may have a distinct stack segment.
	 */
	__asm__ volatile ("movw %0,%%ds" : : "r" (owner_segment) : "memory");

	if (!gem_resident_memory_word_bytes(gem_vdi_control[4],
					    GEM_VDI_INTOUT_WORDS,
					    &integer_output_bytes)
	    || !gem_resident_memory_point_bytes(gem_vdi_control[2],
						GEM_VDI_MAX_OUTPUT_POINTS,
						&point_output_bytes)
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
	gem_vdi_sound_enabled = TRUE;
	gem_vdi_font_segment = 0;
	gem_vdi_font_offset = 0;
	if (gem_vdi_screen) {
		gem_vdi_close(gem_vdi_screen);
		gem_vdi_screen = NULL;
	}
}

/*
 * Release the physical adapter and input devices around one synchronous
 * full-screen child launch, without touching the client workstation
 * records.  gem_vdi_close() restores the text mode and the cooked tty
 * state the child expects; resume reruns the idempotent physical open, so
 * palette, font, cursor, and the opening white fill are re-established
 * exactly as at startup.  The caller repaints through the ordinary AES
 * redraw cascade afterwards.
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
