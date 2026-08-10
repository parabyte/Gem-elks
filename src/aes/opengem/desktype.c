/****************************************************************************
*   Copyright 1999, Caldera Thin Client Systems, Inc.                       *
*   This software is licensed under the GNU Public License.                 *
*   See LICENSE.TXT for further information.                                *
*                                                                           *
*   Historical Copyright                                                    *
*                                                                           *
*   Copyright (c) 1985,1991,1992 Digital Research Inc.			    *
*   All rights reserved.						    *
*   The Software Code contained in this listing is proprietary to Digital   *
*   Research Inc., Monterey, California, and is covered by U.S. and other   *
*   copyright protection.  Unauthorized copying, adaption, distribution,    *
*   use or display is prohibited and may be subject to civil and criminal   *
*   penalties.  Disclosure to others is prohibited.  For the terms and      *
*   conditions of software code use, refer to the appropriate Digital       *
*   Research License Agreement.						    *
*****************************************************************************
*		      U.S. GOVERNMENT RESTRICTED RIGHTS			    *
*                    ---------------------------------                      *
*  This software product is provided with RESTRICTED RIGHTS.  Use, 	    *
*  duplication or disclosure by the Government is subject to restrictions   *
*  as set forth in FAR 52.227-19 (c) (2) (June, 1987) when applicable or    *
*  the applicable provisions of the DOD FAR supplement 252.227-7013 	    *
*  subdivision (b)(3)(ii) (May 1981) or subdivision (c)(1)(ii) (May 1987).  *
*  Contractor/manufacturer is Digital Research Inc. / 70 Garden Court /     *
*  BOX DRI / Monterey, CA 93940.					    *
*****************************************************************************
 *
 * Backported to GEM/3 desktop and Pacific C by John Elliott, 6 August 2005
 * 
 * ViewMAX history: 
 *
 * $Header: m:/davinci/users//groups/panther/dsk/rcs/desktype.c 4.6 92/04/06 09:44:01 Fontes Stable $
* $Log:	desktype.c $
 * Revision 4.6  92/04/06  09:44:01  Fontes
 * Initial cut to extract/display Windows exe embedded icons.
 * 
 * Revision 4.5  92/04/03  17:15:53  sbc
 * WNODEs and PNODEs to fars, lots of other housekeeping
 * 
 * Revision 4.4  92/03/26  14:44:50  sbc
 * WNODEs and PNODEs to far ptrs. Also merge in RSF's changes
 * 
 * Revision 4.3  92/03/13  14:42:03  sbc
 * Merge in Keiko's changes required for Double Byte Character Support
 * 
 * Revision 4.2  92/02/06  11:56:13  sbc
 * add parenthesis to WM_HSLID case to make it compile.
 * This "fix" makes no sense..... sigh....
 * 
 * Revision 4.1  91/09/17  13:57:17  system
 * Initial work for DOS window, screen saver, etc.
 * 
 * Revision 3.1  91/08/19  16:38:53  system
 * ViewMAX 2 sources
 * 
* Date    Who  SPR#	Comments
* ------- ---  ----	---------------------------------------------------
* 911122  K.H		Fix the caluclation of end_column for redrawing.
* 911112  K.H		Fix window x-coordinate was not aligned correctly.
* 911107  K.H		Convert the dbcs second byte character where a string
*			begins to space character. (#if DBCS)
* 911105  K.H		Fix vr_recfl erased the end pixel of the string.
*****************************************************************************/

#include "ppddesk.h"

		    /* Bytes per line when displaying file in hexadecimal */
#define BYTES_PER_HEX_LINE  16

#define HEX_LINE_LENGTH (6 + 2 + BYTES_PER_HEX_LINE * 3 + 1 + BYTES_PER_HEX_LINE)
                        /* Line length when displaying file in hexadecimal */
#define MAX_STR_LEN 128                 /* Maximum text string length */

/* Alignments for vst_alignment */
#define ALI_LEFT 0
#define ALI_BASE 0
#define ALI_TOP  5

#define MAXFILE 16
/* external data */

extern GLOBES G;
extern WORD DOS_ERR;
extern WORD gl_handle, gl_wchar, gl_hchar, gl_apid;
extern BYTE ILL_TYPE[];

/*
 * The original ViewMAX viewer used a four-byte C scalar for file positions.
 * Preserve that field shape without permitting a wide C scalar in this 8086
 * tree; the active viewer below provides the same whole-file navigation with
 * explicit word pairs and a bounded page buffer.
 */
typedef GEM_U32_WORDS SLONG;
#if (defined(SMALL_DATA) && defined(MULTIAPP))

/*
 * The original ViewMAX viewer below keeps the complete file in a dynamically
 * sized viewing scheme and uses four-byte C arithmetic for its statistics.
 * Those implementation details are not safe in this strict 16-bit build.
 * The small-model viewer instead keeps one fixed 256-byte page while retaining
 * whole-file seeking, line/page movement and fine AES slider positioning.
 *
 * Offsets are unscaled byte counts.  Page addition carries from lo to hi and
 * saturates at ffff:ffff if the representation overflows.  Page subtraction
 * clamps at zero.  ELKS lseek still receives the full two-word field, while
 * each read is only 256 bytes and can never cross a near-data segment.
 * ELKS file offsets are signed, so a SEEK_END result with bit 15 set in the
 * high half is rejected as E_BADDATA instead of being mistaken for a size.
 *
 * Slider positions use a ten-bit fixed-point fraction: 0 means the first byte
 * and 1024 means one complete file.  Ratios round down, byte seek targets round
 * down, and positions at the final visible page saturate to AES value 999.
 * Every intermediate remains an 8- or 16-bit word.  File-sized quantities are
 * explicit low/high halves only because the ELKS seek ABI requires them.
 *
 * This storage is static because Show Contents is modal in Desktop.  There is
 * no heap allocation, no conversion process, and no wrapper file.  Text is
 * rendered directly; a page containing a NUL or an unexpected control byte
 * starts in hexadecimal mode.  Ctrl-T can always switch either way.
 */
#define TYPE_PAGE_BYTES       256U
#define TYPE_TEXT_COLUMNS      78U
#define TYPE_HEX_BYTES         16U
#define TYPE_HEX_LINE_CHARS    75U
#define TYPE_LINE_BYTES        80U
#define TYPE_MAX_COLUMN       511U
#define TYPE_TEXT_SCAN_BYTES  (TYPE_MAX_COLUMN + TYPE_TEXT_COLUMNS + 1U)
#define TYPE_TITLE_BYTES  (LEN_ZPATH + 3)
#define TYPE_INFO_BYTES        64U
#define TYPE_SEEK_FIRST         8
#define TYPE_SEEK_LAST          9
#define TYPE_SLIDER_FIRST       1U
#define TYPE_SLIDER_LAST      999U
#define TYPE_SLIDER_FULL     1000U
#define TYPE_FRACTION_BITS     10U
#define TYPE_FRACTION_ONE    1024U

#define TYPE_IO_ERROR          -7
#define TYPE_CLOSE_ERROR       -8

MLOCAL UBYTE type_page[TYPE_PAGE_BYTES];
MLOCAL BYTE type_line[TYPE_LINE_BYTES];
MLOCAL BYTE *type_title;
MLOCAL BYTE *type_info;
MLOCAL BYTE type_good_items[] = { CLOSITEM, TEXTITEM, 0 };
MLOCAL BYTE type_hex_digits[] = "0123456789ABCDEF";

MLOCAL GEM_U32_WORDS type_offset;
MLOCAL GEM_U32_WORDS type_size;
MLOCAL UWORD type_count;
MLOCAL UWORD type_column;
MLOCAL WORD type_file;
MLOCAL WORD type_window;
MLOCAL WORD type_show_text;
MLOCAL WORD type_quit;
MLOCAL WORD type_result;
MLOCAL WORD type_was_full;
MLOCAL GRECT type_work;
MLOCAL GRECT type_previous;

/* Compare two unsigned word-pair byte offsets without widening either one. */
MLOCAL WORD
type_words_compare(GEM_U32_WORDS left, GEM_U32_WORDS right)
{
	if (left.hi != right.hi)
		return left.hi < right.hi ? -1 : 1;
	if (left.lo != right.lo)
		return left.lo < right.lo ? -1 : 1;
	return 0;
}

/* Add one near-buffer-sized byte count with carry and saturating overflow. */
MLOCAL GEM_U32_WORDS
type_words_add(GEM_U32_WORDS value, UWORD amount)
{
	UWORD old_lo;

	old_lo = value.lo;
	value.lo += amount;
	if (value.lo < old_lo) {
		if (value.hi == 0xffffU) {
			value.lo = 0xffffU;
			return value;
		}
		value.hi++;
	}
	return value;
}

/* Subtract a near-buffer-sized count.  Requests before zero clamp to zero. */
MLOCAL GEM_U32_WORDS
type_words_sub(GEM_U32_WORDS value, UWORD amount)
{
	if (!value.hi && value.lo < amount)
		return gem_u32_words(0, 0);
	if (value.lo < amount)
		value.hi--;
	value.lo -= amount;
	return value;
}

MLOCAL WORD
type_words_equal(GEM_U32_WORDS left, GEM_U32_WORDS right)
{
	return left.lo == right.lo && left.hi == right.hi;
}

/* Subtract an already range-checked word pair, propagating one low borrow. */
MLOCAL GEM_U32_WORDS
type_words_sub_pair(GEM_U32_WORDS left, GEM_U32_WORDS right)
{
	UWORD old_lo;

	old_lo = left.lo;
	left.lo -= right.lo;
	left.hi -= right.hi;
	if (old_lo < right.lo)
		left.hi--;
	return left;
}

/*
 * Return floor(numerator * 1024 / denominator) without a wide multiply or
 * divide.  Both operands are unsigned byte offsets and numerator must be less
 * than denominator.  Ten restoring-division steps produce a Q0.10 fraction
 * in the range 0..1023.  Doubling the remainder cannot overflow its explicit
 * two-word container because ELKS file sizes are restricted to 0..7fffffff.
 */
MLOCAL UWORD
type_fraction_10(GEM_U32_WORDS numerator, GEM_U32_WORDS denominator)
{
	GEM_U32_WORDS remainder;
	UWORD quotient;
	UWORD carry;
	UBYTE count;

	if (!denominator.hi && !denominator.lo)
		return 0;
	if (type_words_compare(numerator, denominator) >= 0)
		return TYPE_FRACTION_ONE - 1U;

	remainder = numerator;
	quotient = 0;
	for (count = 0; count < TYPE_FRACTION_BITS; count++) {
		carry = remainder.lo & 0x8000U;
		remainder.lo <<= 1;
		remainder.hi = (UWORD) ((remainder.hi << 1)
					 | (carry ? 1U : 0U));
		quotient <<= 1;
		if (type_words_compare(remainder, denominator) >= 0) {
			remainder = type_words_sub_pair(remainder, denominator);
			quotient |= 1U;
		}
	}
	return quotient;
}

/*
 * Convert a word-pair ratio to GEM's 1..999 slider scale.  The constant
 * factor 999/1024 is evaluated as x - ceil(x * 25 / 1024).  Multiplication by
 * 25 is three constant shifts and additions; its largest value is 25575, so
 * it cannot overflow one 16-bit unsigned word.
 */
MLOCAL UWORD
type_slider_position(GEM_U32_WORDS numerator, GEM_U32_WORDS denominator)
{
	UWORD fraction;
	UWORD product;
	UWORD position;

	if (!numerator.hi && !numerator.lo)
		return TYPE_SLIDER_FIRST;
	if (type_words_compare(numerator, denominator) >= 0)
		return TYPE_SLIDER_LAST;
	fraction = type_fraction_10(numerator, denominator);
	product = (UWORD) ((fraction << 4) + (fraction << 3) + fraction);
	position = fraction - (UWORD) ((product + 1023U) >> 10);
	if (position < TYPE_SLIDER_FIRST)
		position = TYPE_SLIDER_FIRST;
	if (position > TYPE_SLIDER_LAST)
		position = TYPE_SLIDER_LAST;
	return position;
}

/* Convert an interior GEM slider position to a Q0.10 fraction. */
MLOCAL UWORD
type_fraction_from_slider(UWORD position)
{
	UWORD product;
	UWORD fraction;

	if (position > TYPE_SLIDER_LAST)
		position = TYPE_SLIDER_LAST;
	product = (UWORD) ((position << 4) + (position << 3) + position);
	fraction = position + desk_uword_divide(product, TYPE_SLIDER_LAST,
						  NULL);
	if (fraction >= TYPE_FRACTION_ONE)
		fraction = TYPE_FRACTION_ONE - 1U;
	return fraction;
}

/*
 * Return floor(value * fraction / 1024), where fraction is Q0.10.  Splitting
 * value into a whole part and a ten-bit remainder avoids a 42-bit temporary:
 *
 *   value = whole * 1024 + remainder
 *
 * The word-pair shift/add loop forms whole * fraction.  Only the small
 * remainder product needs two words, and gem_u32_mul_u16 supplies those two
 * explicit halves without an 8086 MUL instruction or compiler helper.
 */
MLOCAL GEM_U32_WORDS
type_words_scale_fraction(GEM_U32_WORDS value, UWORD fraction)
{
	GEM_U32_WORDS whole;
	GEM_U32_WORDS addend;
	GEM_U32_WORDS result;
	GEM_U32_WORDS product;
	UWORD remainder;
	UWORD correction;
	UWORD carry;
	UWORD factor;
	UBYTE count;

	whole.lo = (UWORD) ((value.lo >> 10) | (value.hi << 6));
	whole.hi = value.hi >> 10;
	remainder = value.lo & 0x03ffU;
	result = gem_u32_words(0, 0);
	addend = whole;
	factor = fraction;
	for (count = 0; count < TYPE_FRACTION_BITS; count++) {
		if (factor & 1U)
			gem_u32_add_to(&result, addend);
		factor >>= 1;
		carry = addend.lo & 0x8000U;
		addend.lo <<= 1;
		addend.hi = (UWORD) ((addend.hi << 1)
					 | (carry ? 1U : 0U));
	}
	product = gem_u32_mul_u16(remainder, fraction);
	correction = (UWORD) ((product.lo >> 10) | (product.hi << 6));
	gem_u32_add_to(&result, gem_u32_words(correction, 0));
	return result;
}

/* Emit one word as four hexadecimal digits; no division is required. */
MLOCAL VOID
type_hex_word(UWORD value, BYTE *out)
{
	out[0] = type_hex_digits[(value >> 12) & 0x000fU];
	out[1] = type_hex_digits[(value >> 8) & 0x000fU];
	out[2] = type_hex_digits[(value >> 4) & 0x000fU];
	out[3] = type_hex_digits[value & 0x000fU];
}

MLOCAL BYTE *
type_append(BYTE *out, BYTE *end, const BYTE *text)
{
	while (out < end && *text)
		*out++ = *text++;
	*out = 0;
	return out;
}

/* Number of character cells which the bounded line buffer can display. */
MLOCAL UWORD
type_visible_columns(VOID)
{
	WORD visible_word;
	UWORD visible;
	UWORD maximum;

	visible_word = desk_word_divide(type_work.g_w, gl_wchar, NULL);
	visible = visible_word > 0 ? (UWORD) visible_word : 1U;
	maximum = type_show_text ? TYPE_TEXT_COLUMNS : TYPE_HEX_LINE_CHARS;
	if (visible > maximum)
		visible = maximum;
	return visible;
}

/* Return the real horizontal starting-column range for the active mode. */
MLOCAL UWORD
type_horizontal_range(VOID)
{
	UWORD content;
	UWORD visible;

	content = type_show_text ? TYPE_MAX_COLUMN + 1U
				 : TYPE_HEX_LINE_CHARS;
	visible = type_visible_columns();
	return visible < content ? content - visible : 0;
}

MLOCAL VOID
set_hor_slider(WORD wh)
{
	GEM_U32_WORDS numerator;
	GEM_U32_WORDS denominator;
	UWORD content;
	UWORD visible;
	UWORD range;
	UWORD position;
	UWORD extent;

	content = type_show_text ? TYPE_MAX_COLUMN + 1U
				 : TYPE_HEX_LINE_CHARS;
	visible = type_visible_columns();
	range = visible < content ? content - visible : 0;
	if (!range) {
		type_column = 0;
		position = TYPE_SLIDER_FIRST;
		extent = TYPE_SLIDER_FULL;
	} else {
		if (type_column > range)
			type_column = range;
		numerator = gem_u32_words(type_column, 0);
		denominator = gem_u32_words(range, 0);
		position = type_slider_position(numerator, denominator);
		numerator.lo = visible;
		denominator.lo = content;
		extent = type_slider_position(numerator, denominator);
	}
	wind_set(wh, WF_HSLIDE, (WORD) position, 0, 0, 0);
	wind_set(wh, WF_HSLSIZ, (WORD) extent, 0, 0, 0);
}

MLOCAL VOID
set_vert_slider(WORD wh)
{
	GEM_U32_WORDS page_end;
	GEM_U32_WORDS page_size;
	UWORD position;
	UWORD extent;

	page_end = type_words_add(type_offset, type_count);
	if (type_words_compare(page_end, type_size) >= 0)
		position = TYPE_SLIDER_LAST;
	else
		position = type_slider_position(type_offset, type_size);
	page_size = gem_u32_words(TYPE_PAGE_BYTES, 0);
	if (type_words_compare(page_size, type_size) >= 0)
		extent = TYPE_SLIDER_FULL;
	else
		extent = type_slider_position(page_size, type_size);
	wind_set(wh, WF_VSLIDE, (WORD) position, 0, 0, 0);
	wind_set(wh, WF_VSLSIZ, (WORD) extent, 0, 0, 0);
}

MLOCAL VOID
set_sliders(WORD wh)
{
	/* AES consumes the fine 1..999 positions and 1..1000 thumb extents. */
	set_hor_slider(wh);
	set_vert_slider(wh);
}

/*
 * The information line uses hexadecimal word pairs so no decimal division
 * or four-byte printf helper enters the XT executable.
 */
MLOCAL VOID
set_info_line(WORD wh)
{
	BYTE *out;
	BYTE *end;
	GEM_U32_WORDS page_end;

	out = type_info;
	end = type_info + TYPE_INFO_BYTES - 1;
	out = type_append(out, end, type_show_text ? "Text " : "Hex  ");
	if (end - out >= 9) {
		type_hex_word(type_offset.hi, out);
		out += 4;
		*out++ = ':';
		type_hex_word(type_offset.lo, out);
		out += 4;
		*out = 0;
	}
	out = type_append(out, end, " / ");
	if (end - out >= 9) {
		type_hex_word(type_size.hi, out);
		out += 4;
		*out++ = ':';
		type_hex_word(type_size.lo, out);
		out += 4;
		*out = 0;
	}
	page_end = type_words_add(type_offset, type_count);
	if (type_words_compare(page_end, type_size) >= 0)
		out = type_append(out, end, " end");
	type_append(out, end, "  PgUp/PgDn Ctrl-T Esc");
	wind_setl(wh, WF_INFO, type_info);
	set_sliders(wh);
}

MLOCAL VOID
type_io_alert(WORD error)
{
	if (error <= 0)
		error = E_BADDATA;
	form_error(error);
}

/* Seek and fill the static page.  All syscall failures are reported. */
MLOCAL WORD
seek_read(VOID)
{
	GEM_U32_WORDS actual;
	GEM_U32_WORDS got;
	WORD error;

	actual = dos_lseek(type_file, SEEK_SET, type_offset);
	if (DOS_ERR || !type_words_equal(actual, type_offset)) {
		error = DOS_ERR ? DOS_AX : E_BADDATA;
		type_io_alert(error);
		type_result = TYPE_IO_ERROR;
		return FALSE;
	}
	got = dos_read(type_file, gem_u32_words(TYPE_PAGE_BYTES, 0),
		       (LPBYTE) type_page);
	if (DOS_ERR || got.hi || got.lo > TYPE_PAGE_BYTES) {
		error = DOS_ERR ? DOS_AX : E_BADDATA;
		type_io_alert(error);
		type_result = TYPE_IO_ERROR;
		return FALSE;
	}
	type_count = got.lo;
	return TRUE;
}

/*
 * A byte-ratio slider can land in the middle of a text row.  Search at most
 * one bounded page behind that byte and, when a line delimiter is present,
 * move the target to the following row.  Files with a row longer than one
 * page retain the exact byte target, so the search cost is always bounded and
 * every part of the file remains reachable.
 *
 * The low-word subtraction below is deliberate modulo-65536 arithmetic.  The
 * pair helper established that target - lookback is in the range 0..256, so
 * it remains exact even when the subtraction crosses a low-word boundary.
 */
MLOCAL WORD
type_align_text_offset(GEM_U32_WORDS target, GEM_U32_WORDS *aligned)
{
	GEM_U32_WORDS lookback;
	UWORD distance;
	UWORD index;
	UWORD line_start;
	WORD found;

	if (!target.hi && !target.lo) {
		*aligned = target;
		return TRUE;
	}
	lookback = type_words_sub(target, TYPE_PAGE_BYTES);
	type_offset = lookback;
	if (!seek_read())
		return FALSE;
	distance = target.lo - lookback.lo;
	if (distance > type_count)
		distance = type_count;
	line_start = distance;
	found = FALSE;
	for (index = 0; index < distance; index++) {
		if (type_page[index] == '\r' || type_page[index] == '\n') {
			line_start = index + 1U;
			found = TRUE;
		}
	}
	*aligned = found ? type_words_add(lookback, line_start) : target;
	return TRUE;
}

MLOCAL VOID
scan_file(WORD wh)
{
	UWORD index;
	UBYTE value;

	index = 0;
	while (index < type_count) {
		value = type_page[index++];
		if (!value)
			goto binary;
		if (value < ' ' && value != '\t' && value != '\r' &&
		    value != '\n' && value != '\f')
			goto binary;
	}
	type_show_text = TRUE;
	set_info_line(wh);
	return;

binary:
	type_show_text = FALSE;
	set_info_line(wh);
}

MLOCAL WORD
start_scan_file(WORD wh)
{
	if (!seek_read())
		return FALSE;
	if (!type_count) {
		form_alert(1, ini_str(STEMPTY));
		return FALSE;
	}
	scan_file(wh);
	return TRUE;
}

MLOCAL UBYTE
type_display_byte(UBYTE value)
{
	if (value < ' ' || value > 0x7eU)
		return '.';
	return value;
}

MLOCAL VOID
type_hex_byte(UBYTE value, BYTE *out)
{
	out[0] = type_hex_digits[(value >> 4) & 0x0fU];
	out[1] = type_hex_digits[value & 0x0fU];
}

/* Draw only visible intersections supplied by AES's rectangle iterator. */
MLOCAL VOID
do_redraw(WORD wh, GRECT *area)
{
	GRECT box;
	GRECT dirty;
	WORD xy[4];
	WORD y;
	WORD rows_word;
	WORD columns_word;
	UWORD index;
	UWORD rows;
	UWORD columns;
	UWORD source_column;
	UWORD output_column;
	UWORD tab_stop;
	UWORD row_bytes;
	UWORD line_start;
	UWORD count;
	UWORD byte;
	UWORD skip;
	UBYTE value;
	BYTE *out;
	GEM_U32_WORDS line_offset;
	GEM_U32_WORDS view_offset;
	GEM_U32_WORDS next_page;
	WORD stream_error;

	view_offset = type_offset;
	stream_error = FALSE;
	graf_mouse(M_OFF, 0);
	wind_get(wh, WF_FIRSTXYWH, &box.g_x, &box.g_y,
		 &box.g_w, &box.g_h);
	while (box.g_w && box.g_h) {
		dirty = box;
		if (rc_intersect(area, &dirty) && rc_intersect(&type_work, &dirty)) {
			/* A prior rectangle may have streamed beyond the cached page. */
			if (!type_words_equal(type_offset, view_offset)) {
				type_offset = view_offset;
				if (!seek_read()) {
					stream_error = TRUE;
					break;
				}
			}
			xy[0] = dirty.g_x;
			xy[1] = dirty.g_y;
			xy[2] = dirty.g_x + dirty.g_w - 1;
			xy[3] = dirty.g_y + dirty.g_h - 1;
			vs_clip(gl_handle, TRUE, xy);
			vswr_mode(gl_handle, MD_REPLACE);
			vsf_interior(gl_handle, FIS_SOLID);
			vsf_color(gl_handle, WHITE);
			vr_recfl(gl_handle, xy);
			vst_color(gl_handle, BLACK);

			rows_word = desk_word_divide(type_work.g_h, gl_hchar, NULL);
			if (rows_word < 1)
				rows_word = 1;
			if (rows_word > 32)
				rows_word = 32;
			rows = (UWORD) rows_word;
			index = 0;
			y = type_work.g_y;

			if (type_show_text) {
				columns_word = desk_word_divide(type_work.g_w,
							   gl_wchar, NULL);
				if (columns_word < 1)
					columns_word = 1;
				if (columns_word > TYPE_TEXT_COLUMNS)
					columns_word = TYPE_TEXT_COLUMNS;
				columns = (UWORD) columns_word;

				/*
				 * Refill the same bounded buffer as rows cross a page.
				 * TYPE_TEXT_SCAN_BYTES limits one visual row to the
				 * complete horizontal range plus the visible tail, so a
				 * malformed file containing no newline cannot monopolise
				 * an XT while it redraws a window.
				 */
				while (rows) {
					if (index >= type_count) {
						if (!type_count)
							break;
						next_page = type_words_add(type_offset,
									   type_count);
						if (type_words_compare(next_page, type_size) >= 0)
							break;
						type_offset = next_page;
						if (!seek_read()) {
							stream_error = TRUE;
							break;
						}
						index = 0;
					}
					if (stream_error)
						break;
					source_column = 0;
					output_column = 0;
					row_bytes = 0;
					while (row_bytes < TYPE_TEXT_SCAN_BYTES) {
						if (index >= type_count) {
							if (!type_count)
								break;
							next_page = type_words_add(type_offset,
										   type_count);
							if (type_words_compare(next_page,
									       type_size) >= 0)
								break;
							type_offset = next_page;
							if (!seek_read()) {
								stream_error = TRUE;
								break;
							}
							index = 0;
						}
						if (!type_count || index >= type_count)
							break;
						if (type_page[index] == '\r' ||
						    type_page[index] == '\n')
							break;
						value = type_page[index++];
						row_bytes++;
						if (value == '\t') {
							tab_stop = (source_column + 8U)
								& 0xfff8U;
							while (source_column < tab_stop) {
								if (source_column >= type_column &&
								    output_column < columns)
									type_line[output_column++] = ' ';
								source_column++;
							}
						} else {
							if (source_column >= type_column &&
							    output_column < columns)
								type_line[output_column++] =
									type_display_byte(value);
							source_column++;
						}
					}
					if (!stream_error && index < type_count &&
					    type_page[index] == '\r')
						index++;
					if (!stream_error && index < type_count &&
					    type_page[index] == '\n')
						index++;
					type_line[output_column] = 0;
					gsx_tblt(IBM, type_work.g_x, y,
						 output_column, type_line);
					y += gl_hchar;
					rows--;
					if (stream_error)
						break;
				}
			} else {
				/* Render every visible sixteen-byte row, refilling at 256. */
				while (rows) {
					if (index >= type_count) {
						if (!type_count)
							break;
						next_page = type_words_add(type_offset,
									   type_count);
						if (type_words_compare(next_page, type_size) >= 0)
							break;
						type_offset = next_page;
						if (!seek_read()) {
							stream_error = TRUE;
							break;
						}
						index = 0;
					}
					if (stream_error)
						break;
					line_start = index;
					count = type_count - index;
					if (count > TYPE_HEX_BYTES)
						count = TYPE_HEX_BYTES;
					line_offset = type_words_add(type_offset,
								     line_start);
					type_hex_word(line_offset.hi, type_line);
					type_hex_word(line_offset.lo, type_line + 4);
					type_line[8] = ' ';
					type_line[9] = ' ';
					out = type_line + 10;
					byte = 0;
					while (byte < TYPE_HEX_BYTES) {
						if (byte < count)
							type_hex_byte(
								type_page[line_start + byte], out);
						else {
							out[0] = ' ';
							out[1] = ' ';
						}
						out[2] = ' ';
						out += 3;
						byte++;
					}
					*out++ = ' ';
					byte = 0;
					while (byte < TYPE_HEX_BYTES) {
						*out++ = byte < count
							? type_display_byte(
								type_page[line_start + byte]) : ' ';
						byte++;
					}
					*out = 0;
					skip = type_column;
					if (skip > TYPE_HEX_LINE_CHARS)
						skip = TYPE_HEX_LINE_CHARS;
					gsx_tblt(IBM, type_work.g_x, y,
						 TYPE_HEX_LINE_CHARS - skip,
						 type_line + skip);
					y += gl_hchar;
					index += count;
					rows--;
				}
			}
			vs_clip(gl_handle, FALSE, NULL);
			vsf_color(gl_handle, BLACK);
		}
		if (stream_error)
			break;
		wind_get(wh, WF_NEXTXYWH, &box.g_x, &box.g_y,
			 &box.g_w, &box.g_h);
	}
	/* Navigation routines expect type_page to describe the visible offset. */
	if (!type_words_equal(type_offset, view_offset)) {
		type_offset = view_offset;
		if (!seek_read())
			type_quit = TRUE;
	}
	graf_mouse(M_ON, 0);
}

MLOCAL VOID
seek_row(WORD action)
{
	GEM_U32_WORDS old_offset;
	GEM_U32_WORDS requested;
	GEM_U32_WORDS lookback;
	GEM_U32_WORDS next;
	GEM_U32_WORDS remainder;
	UWORD distance;
	UWORD index;

	old_offset = type_offset;
	requested = old_offset;
	switch (action) {
	case WA_UPPAGE:
		requested = type_words_sub(old_offset, TYPE_PAGE_BYTES);
		break;
	case WA_DNPAGE:
		next = type_words_add(old_offset, TYPE_PAGE_BYTES);
		if (type_words_compare(next, type_size) < 0)
			requested = next;
		break;
	case WA_DNLINE:
		if (type_show_text) {
			/* Find the next bounded CR, LF, or CR/LF text row. */
			index = 0;
			while (index < type_count && type_page[index] != '\r' &&
			       type_page[index] != '\n')
				index++;
			if (index < type_count && type_page[index] == '\r')
				index++;
			if (index < type_count && type_page[index] == '\n')
				index++;
			next = type_words_add(old_offset, index);
		} else {
			next = type_words_add(old_offset, TYPE_HEX_BYTES);
		}
		if (type_words_compare(next, type_size) < 0)
			requested = next;
		break;
	case WA_UPLINE:
		if (!old_offset.hi && !old_offset.lo)
			break;
		if (!type_show_text) {
			requested = type_words_sub(old_offset, TYPE_HEX_BYTES);
			break;
		}
		/*
		 * Search one bounded page behind the current text row.  The low
		 * word difference is exact in the known 0..256 range, including
		 * the single-borrow case.
		 */
		lookback = type_words_sub(old_offset, TYPE_PAGE_BYTES);
		type_offset = lookback;
		if (!seek_read()) {
			type_offset = old_offset;
			type_quit = TRUE;
			return;
		}
		distance = old_offset.lo - lookback.lo;
		if (distance > type_count)
			distance = type_count;
		index = distance;
		if (index && type_page[index - 1] == '\n')
			index--;
		if (index && type_page[index - 1] == '\r')
			index--;
		while (index && type_page[index - 1] != '\r' &&
		       type_page[index - 1] != '\n')
			index--;
		requested = type_words_add(lookback, index);
		type_offset = old_offset;
		break;
	case TYPE_SEEK_FIRST:
		requested = gem_u32_words(0, 0);
		break;
	case TYPE_SEEK_LAST:
		if (!type_size.hi && type_size.lo <= TYPE_PAGE_BYTES) {
			requested = gem_u32_words(0, 0);
			break;
		}
		/* Align down to 256 bytes; exact multiples use the prior page. */
		requested = type_size;
		remainder = gem_u32_words(requested.lo & 0x00ffU, 0);
		requested.lo &= 0xff00U;
		if (!remainder.lo)
			requested = type_words_sub(requested, TYPE_PAGE_BYTES);
		break;
	default:
		return;
	}

	if (type_words_equal(requested, old_offset))
		return;
	type_offset = requested;
	if (!seek_read()) {
		type_offset = old_offset;
		type_quit = TRUE;
		return;
	}
	set_info_line(type_window);
	do_redraw(type_window, &type_work);
}

/*
 * Seek to any vertical-thumb position.  Interior AES positions map to an
 * exact Q0.10 fraction of the complete POSIX file size; first and last keep
 * the historical page-boundary behavior.  Hexadecimal targets round down to
 * a sixteen-byte row.  Text targets use a bounded look-behind to prefer a
 * physical line boundary without scanning or allocating the complete file.
 */
MLOCAL VOID
seek_slider(UWORD position)
{
	GEM_U32_WORDS old_offset;
	GEM_U32_WORDS requested;
	UWORD fraction;

	if (position <= TYPE_SLIDER_FIRST) {
		seek_row(TYPE_SEEK_FIRST);
		return;
	}
	if (position >= TYPE_SLIDER_LAST) {
		seek_row(TYPE_SEEK_LAST);
		return;
	}

	old_offset = type_offset;
	fraction = type_fraction_from_slider(position);
	requested = type_words_scale_fraction(type_size, fraction);
	if (type_show_text) {
		if (!type_align_text_offset(requested, &requested)) {
			type_offset = old_offset;
			type_quit = TRUE;
			return;
		}
	} else {
		requested.lo &= 0xfff0U;
	}
	type_offset = requested;
	if (!seek_read()) {
		type_offset = old_offset;
		type_quit = TRUE;
		return;
	}
	set_info_line(type_window);
	do_redraw(type_window, &type_work);
}

MLOCAL VOID
do_full(WORD wh)
{
	GRECT target;

	if (!type_was_full) {
		wind_get(wh, WF_CXYWH, &type_previous.g_x,
			 &type_previous.g_y, &type_previous.g_w,
			 &type_previous.g_h);
		wind_get(wh, WF_FXYWH, &target.g_x, &target.g_y,
			 &target.g_w, &target.g_h);
		type_was_full = TRUE;
	} else {
		target = type_previous;
		type_was_full = FALSE;
	}
	wind_set(wh, WF_CXYWH, target.g_x, target.g_y,
		 target.g_w, target.g_h);
	wind_get(wh, WF_WXYWH, &type_work.g_x, &type_work.g_y,
		 &type_work.g_w, &type_work.g_h);
	do_redraw(wh, &type_work);
}

MLOCAL VOID
handle_key(UWORD key, WORD wh)
{
	UWORD ascii;
	UWORD range;

	ascii = key & 0x00ffU;
	if (ascii == 0x001bU) {
		type_quit = TRUE;
		return;
	}
	if (ascii == ('T' - '@') || ascii == 't' || ascii == 'T') {
		type_show_text = !type_show_text;
		type_column = 0;
		set_info_line(wh);
		do_redraw(wh, &type_work);
		return;
	}
	if (ascii == ' ' || ascii == '+' || ascii == '\r' ||
	    key == 0x5100U) {
		seek_row(WA_DNPAGE);
		return;
	}
	if (ascii == '\b' || ascii == '-' || key == 0x4900U) {
		seek_row(WA_UPPAGE);
		return;
	}
	if (key == 0x4700U)
		seek_row(TYPE_SEEK_FIRST);
	else if (key == 0x4f00U)
		seek_row(TYPE_SEEK_LAST);
	else if (key == 0x4800U)
		seek_row(WA_UPLINE);
	else if (key == 0x5000U)
		seek_row(WA_DNLINE);
	else if (key == 0x4b00U) {
		if (type_column)
			type_column--;
		set_info_line(wh);
		do_redraw(wh, &type_work);
	} else if (key == 0x4d00U) {
		range = type_horizontal_range();
		if (type_column < range)
			type_column++;
		set_info_line(wh);
		do_redraw(wh, &type_work);
	}
}

MLOCAL VOID
handle_message(WORD *message, WORD wh)
{
	GRECT redraw;
	GEM_U32_WORDS column_offset;
	UWORD range;
	UWORD fraction;

	if (message[0] == MN_SELECTED) {
		if (message[3] == VIEWMENU && message[4] == CLOSITEM)
			type_quit = TRUE;
		else if (message[3] == VIEWMENU && message[4] == TEXTITEM) {
			type_show_text = !type_show_text;
			type_column = 0;
			set_info_line(wh);
			do_redraw(wh, &type_work);
		}
		else {
			type_result = hndl_menu(message[3], message[4]);
			if (type_result)
				type_quit = TRUE;
		}
		menu_tnormal(G.a_trees[ADMENU], message[3], TRUE);
		return;
	}
	if (message[3] != wh) {
		fun_msg(message[0], message[3], message[4], message[5],
			message[6], message[7]);
		return;
	}
	switch (message[0]) {
	case WM_REDRAW:
		redraw.g_x = message[4];
		redraw.g_y = message[5];
		redraw.g_w = message[6];
		redraw.g_h = message[7];
		do_redraw(wh, &redraw);
		break;
	case WM_TOPPED:
		wind_set(wh, WF_TOP, wh, 0, 0, 0);
		men_list(G.a_trees[ADMENU], type_good_items, TRUE);
		break;
	case WM_CLOSED:
		type_quit = TRUE;
		break;
	case WM_FULLED:
		do_full(wh);
		break;
	case WM_ARROWED:
		switch (message[4]) {
		case WA_UPPAGE:
		case WA_DNPAGE:
		case WA_UPLINE:
		case WA_DNLINE:
			seek_row(message[4]);
			return;
		case WA_LFPAGE:
			if (type_column < 8)
				type_column = 0;
			else
				type_column -= 8;
			break;
		case WA_RTPAGE:
			range = type_horizontal_range();
			if (type_column >= range || range - type_column < 8U)
				type_column = range;
			else
				type_column += 8;
			break;
		case WA_LFLINE:
			if (type_column)
				type_column--;
			break;
		case WA_RTLINE:
			range = type_horizontal_range();
			if (type_column < range)
				type_column++;
			break;
		}
		set_info_line(wh);
		do_redraw(wh, &type_work);
		break;
	case WM_HSLID:
		range = type_horizontal_range();
		if (message[4] <= (WORD) TYPE_SLIDER_FIRST)
			type_column = 0;
		else if (message[4] >= (WORD) TYPE_SLIDER_LAST)
			type_column = range;
		else {
			fraction = type_fraction_from_slider((UWORD) message[4]);
			column_offset = type_words_scale_fraction(
				gem_u32_words(range, 0), fraction);
			type_column = column_offset.lo;
		}
		set_info_line(wh);
		do_redraw(wh, &type_work);
		break;
	case WM_VSLID:
		seek_slider((UWORD) message[4]);
		break;
	case WM_SIZED:
	case WM_MOVED:
		wind_set(wh, WF_CXYWH, message[4], message[5],
			 message[6], message[7]);
		wind_get(wh, WF_WXYWH, &type_work.g_x, &type_work.g_y,
			 &type_work.g_w, &type_work.g_h);
		type_was_full = FALSE;
		do_redraw(wh, &type_work);
		break;
	}
}

MLOCAL VOID
get_handle_events(WORD wh)
{
	WORD which;
	WORD message[8];
	UWORD mouse_x;
	UWORD mouse_y;
	UWORD button;
	UWORD key_state;
	UWORD key;
	UWORD clicks;

	while (!type_quit) {
		which = evnt_multi(MU_MESAG | MU_KEYBD,
			0, 0, 0,
			0, 0, 0, 0, 0,
			0, 0, 0, 0, 0,
			(LPWORD) ADDR(message),
			0, 0, &mouse_x, &mouse_y, &button,
			&key_state, &key, &clicks);
		wind_update(BEG_UPDATE);
		if (which & MU_MESAG)
			handle_message(message, wh);
		if (which & MU_KEYBD)
			handle_key(key, wh);
		wind_update(END_UPDATE);
	}
}

/* Build the selected FNODE's absolute POSIX path through Desktop's PNODE. */
MLOCAL WORD
type_selected_path(WORD curr, BYTE *path)
{
	WORD drive;
	WORD is_app;
	ANODE *anode;
	WNODE *window;
	FNODE *file;
	BYTE path_part[LEN_ZPATH];
	BYTE name[LEN_ZFNAME];
	BYTE stem[MAXFILE];

	anode = i_find(G.g_cwin, curr, &file, &is_app);
	window = win_find(G.g_cwin);
	if (!anode || !file || !window || !window->w_path)
		return FALSE;
	if (anode->a_type != AT_ISFILE && anode->a_type != AT_ISWIND)
		return FALSE;

	/* fpd_parse supplies the POSIX directory from the live PNODE. */
	fpd_parse(window->w_path->p_spec, &drive, path_part, stem, NULL);
	lstlcpy((BYTE far *) name, file->f_name, LEN_ZFNAME);
	/*
	 * Pass the complete POSIX component as the name.  Splitting at a dot
	 * would corrupt leading-dot files and names containing several dots.
	 */
	if (!fpd_bldspec(drive, path_part, name, "", path)) {
		form_error(E_PATHNOTFND);
		return FALSE;
	}
	return TRUE;
}

/*
 * Show Contents return values retain the historical window/open errors and
 * add -7 for seek/read failure and -8 for close failure.  A normal user close
 * returns FALSE, as expected by do_filemenu().
 */
WORD
do_type(WORD curr)
{
	GEM_U32_WORDS actual;
	WORD close_error;
	BYTE path[LEN_ZPATH];
	BYTE title[TYPE_TITLE_BYTES];
	BYTE info[TYPE_INFO_BYTES];

	type_result = FALSE;
	type_file = -1;
	type_window = -1;
	type_title = title;
	type_info = info;
	if (!type_selected_path(curr, path))
		return FALSE;

	type_title[0] = ' ';
	type_title[1] = 0;
	strlcat(type_title, path, TYPE_TITLE_BYTES);
	strlcat(type_title, " ", TYPE_TITLE_BYTES);
	strlcpy(type_info, "Reading file...", TYPE_INFO_BYTES);
	type_offset = gem_u32_words(0, 0);
	type_count = 0;
	type_column = 0;
	type_quit = FALSE;
	type_was_full = FALSE;

	graf_mouse(HOURGLASS, 0);
	type_file = dos_open(path, 0);
	if (DOS_ERR) {
		alert_s(1, STBADOPN, path);
		graf_mouse(ARROW, 0);
		return -6;
	}

	/* SEEK_END obtains the exact POSIX size as two 16-bit byte-count halves. */
	type_size = dos_lseek(type_file, SEEK_END, gem_u32_words(0, 0));
	if (DOS_ERR || (type_size.hi & 0x8000U)) {
		type_io_alert(DOS_ERR ? DOS_AX : E_BADDATA);
		type_result = TYPE_IO_ERROR;
		goto close_file;
	}
	actual = dos_lseek(type_file, SEEK_SET, type_offset);
	if (DOS_ERR || !type_words_equal(actual, type_offset)) {
		type_io_alert(DOS_ERR ? DOS_AX : E_BADDATA);
		type_result = TYPE_IO_ERROR;
		goto close_file;
	}
	if (!type_size.hi && !type_size.lo) {
		form_alert(1, ini_str(STEMPTY));
		goto close_file;
	}

	type_window = create_window(WKIND_SHOW, type_title, type_info, &type_work);
	if (type_window == -1) {
		type_result = -4;
		goto close_file;
	}
	if (!start_scan_file(type_window))
		goto close_window;
	men_list(G.a_trees[ADMENU], type_good_items, TRUE);
	graf_mouse(ARROW, 0);
	do_redraw(type_window, &type_work);
	get_handle_events(type_window);

close_window:
	wind_update(BEG_UPDATE);
	wind_close(type_window);
	wind_delete(type_window);
	wind_update(END_UPDATE);
	type_window = -1;

close_file:
	close_error = 0;
	if (type_file >= 0 && !dos_close(type_file))
		close_error = DOS_AX;
	type_file = -1;
	if (close_error) {
		type_io_alert(close_error);
		if (!type_result)
			type_result = TYPE_CLOSE_ERROR;
	}
	graf_mouse(ARROW, 0);
	return type_result;
}

#else

/* Local data */

MLOCAL BYTE	GOOD_TYPE[] = { CLOSITEM, 0 };
MLOCAL WORD max_str_len;                /* Maximum text string length */
MLOCAL GRECT work_area;                 /* Work area of view window */
MLOCAL BYTE info_line[81];              /* Window information line */

MLOCAL WORD file_handle;                /* File handle */
MLOCAL BOOLEAN file_error;              /* True if error occurs reading file */
MLOCAL UBYTE far *file_buffer;         /* Buffer to read file into */
MLOCAL WORD file_buffer_size;           /* Size of file_buffer */
MLOCAL UBYTE far *file_buffer_end;     /* Points to byte following last byte
                                           in file_buffer */
MLOCAL BOOLEAN show_as_text;            /* True if file appears to be text */
MLOCAL BOOLEAN scanning_file;           /* True if scanning file for file size
                                           and text line length and number of
                                           text lines */
MLOCAL SLONG file_size;                 /* Number of bytes in file */
MLOCAL WORD max_text_line_length;       /* Maximum text line length in file */
MLOCAL WORD max_line_length;            /* Maximum line length in file as
                                           displayed */
MLOCAL SLONG num_text_lines;            /* Number of text lines in file */
MLOCAL SLONG num_lines;                 /* Number of lines in file as
                                           displayed */
MLOCAL SLONG num_words;                 /* Number of words in file */
MLOCAL WORD scan_column;                /* Current column when scanning file */
MLOCAL BOOLEAN scan_in_word;            /* True if scan is within word */
MLOCAL SLONG window_offset;             /* File offset corresponding to start of
                                           window */
MLOCAL WORD window_column;              /* Number of first column displayed
                                           in window, 0 if at start of line
                                           in file */
MLOCAL SLONG window_row;                /* Number of first row displayed in
                                           window, 0 if first line in file */
MLOCAL BOOLEAN quit_flag;               /* Set true when quit selected */
MLOCAL WORD return_value;       /* Set with error code or TRUE to terminate program */

MLOCAL void handle_key(UWORD key, WORD wh);

#if DBCS
extern void cvt_dbc2( UBYTE *, UBYTE * ) ;		/* deskinf.c */
#endif /* DBCS */



/*-----------------------------------------------------------------------------
Purpose : Set horizontal slider position and size
Entry   : wh    Window handle
Exit    : 
*/
MLOCAL void set_hor_slider(WORD wh)
{
    WORD hslide, hslsize;

    if (work_area.g_w / gl_wchar >= max_line_length) {
        hslide = 1;
        hslsize = 1000;
    } else {
        hslide = 1 + (WORD) (((SLONG) window_column * 999)
            / (SLONG) (max_line_length - work_area.g_w / gl_wchar));
        hslsize = 1 + (WORD) (((SLONG) (work_area.g_w / gl_wchar) * 999)
            / (SLONG) max_line_length);
    }
    wind_set(wh, WF_HSLIDE, hslide, 0, 0, 0);
    wind_set(wh, WF_HSLSIZ, hslsize, 0, 0, 0);
} /* set_hor_slider() */

/*-----------------------------------------------------------------------------
Purpose : Set vertical slider position and size
Entry   : wh    Window handle
Exit    : 
*/
MLOCAL void set_vert_slider(WORD wh)
{
    WORD vslide, vslsize;

    if ((SLONG) (work_area.g_h / gl_hchar) >= num_lines) {
        vslide = 1;
        vslsize = 1000;
    } else {
        vslide = 1 + (WORD) ((window_row * 999)
            / (num_lines - (SLONG) (work_area.g_h / gl_hchar)));
        vslsize = 1 + (WORD) (((SLONG) (work_area.g_h / gl_hchar) * 999)
            / num_lines);
    }
    wind_set(wh, WF_VSLIDE, vslide, 0, 0, 0);
    wind_set(wh, WF_VSLSIZ, vslsize, 0, 0, 0);
} /* set_vert_slider() */

/*-----------------------------------------------------------------------------
Purpose : Set slider positions and sizes
Entry   : wh    Window handle
Exit    : 
*/
MLOCAL void set_sliders(WORD wh)
{
    set_hor_slider(wh);
    set_vert_slider(wh);
} /* set_sliders() */

/*-----------------------------------------------------------------------------
Purpose : Seek and read from file
Entry   : 
Exit    : Returns zero if successful
*/
MLOCAL BOOLEAN seek_read(void)
{
    if (dos_lseek(file_handle, 0, window_offset) == window_offset)
        file_buffer_end = file_buffer + 
	    dos_read( file_handle, file_buffer_size, (LPBYTE)file_buffer);
    else
        file_error = TRUE;
    return(file_error);
} /* seek_read() */

/*-----------------------------------------------------------------------------
Purpose : Search file for given row
Entry   : required_row  Required row
Exit    : 
*/
MLOCAL void seek_row(SLONG required_row)
{
    BOOLEAN reread_required;
    UBYTE far *s;

    reread_required = TRUE;
    if (show_as_text) {
        graf_mouse(HOURGLASS, 0);

    /* Start search from beginning of file if required row nearer start than
       current row */

        if (required_row < window_row / 2) {
            window_row = 0;
            window_offset = 0;
            seek_read();
        }
        while (window_row < required_row && !file_error
                && file_buffer_end > file_buffer) {
            s = file_buffer;
            while (window_row < required_row && s < file_buffer_end - 1) {
                while (*s != '\r' && *s != '\n' && s < file_buffer_end - 1)
                    s++;
                if (s < file_buffer_end - 1) {
                    if (*s == '\r' && *(s + 1) == '\n')
                        s++;
                    s++;
                    window_row++;
                }
            }
            window_offset += (s - file_buffer);
            seek_read();
            reread_required = FALSE;
        }
        while (window_row > required_row && !file_error) {
            s = file_buffer + file_buffer_size;
            if ((window_offset -= file_buffer_size) < 0) {
                s = (UBYTE far*)((SLONG)s + window_offset);
                window_offset = 0;
            }
            if (seek_read() == 0) {
                while (window_row > required_row && s > file_buffer + 1) {
                    s--;
                    if (*s == '\n' && *(s - 1) == '\r')
                        s--;
                    if (s > file_buffer) {
                        do {
                            s--;
                        } while (*s != '\r' && *s != '\n' && s > file_buffer);
                    }
                    if (*s == '\r' || *s == '\n') {
                        s++;
                        window_row--;
                    }
                }
                if ((window_offset += (s - file_buffer)) == 0)
                    window_row = 0;
            }
        }
        graf_mouse(ARROW, 0);
    } else {
        window_row = required_row;
        window_offset = window_row * BYTES_PER_HEX_LINE;
    }
    if (reread_required)
        seek_read();
} /* seek_row() */

/*----------------------------------------------------------------------------
Purpose : Set information line
Entry   : wh    Window handle
Exit    : 
*/
MLOCAL void set_info_line(WORD wh)
{
    if (scanning_file) {
        strlcpy( info_line, ini_str(STFSCAN), sizeof(info_line) );
	
    } else {
	if ( show_as_text ) {
	    sprintf( info_line, ini_str( STFWORDS ),
				file_size, num_text_lines, num_words ) ;
	   }
	else {
	    sprintf( info_line, ini_str( STFBYTES ), file_size ) ;
	   }
    }
    wind_setl(wh, WF_INFO, info_line);
    
} /* set_info_line() */

/*-----------------------------------------------------------------------------
Purpose : Redraw all or part of window
Entry   : wh            Window handle
          area          Area to be redrawn
Exit    : 
*/
MLOCAL void do_redraw(WORD wh, GRECT *area)
{
    BOOLEAN end_of_line;
    UBYTE c;
    WORD i, j;
    WORD column, row;
    WORD start_column, start_row, end_column, end_row;
    WORD string_length;
    WORD hor_alignment, vert_alignment;
    UBYTE *s, *t;
    UBYTE far *buffer_ptr;
    UBYTE text_buffer[MAX_STR_LEN + 1];
    WORD xy_array[4];
    GRECT box, dirty_dest;
#if DBCS
    UBYTE far *slptr, far *p;
    BYTE dbc1;
#endif /* DBCS */

    graf_mouse(M_OFF, 0);

    wind_get(wh, WF_FIRSTXYWH, &box.g_x, &box.g_y, &box.g_w, &box.g_h);
    while (box.g_w != 0 && box.g_h != 0) {
        if (rc_intersect(area, &box)) {
            rc_copy(&box, &dirty_dest);
            if (rc_intersect(&work_area, &dirty_dest)) {

            /* Calculate dirty source in text units */

                start_column = (dirty_dest.g_x - work_area.g_x) / gl_wchar
                    + window_column;
                start_row = (dirty_dest.g_y - work_area.g_y) / gl_hchar;
                end_column
                    = (dirty_dest.g_x - work_area.g_x + dirty_dest.g_w - 1
                        + 1 + gl_wchar - 1) / gl_wchar + window_column;
                end_row = (dirty_dest.g_y - work_area.g_y + dirty_dest.g_h - 1
                        + 1 + gl_hchar - 1) / gl_hchar;

            /* Set attributes */

                vst_alignment(gl_handle, ALI_LEFT, ALI_TOP, &hor_alignment,
                    &vert_alignment);   /* Set text alignment to top left */
                vst_color(gl_handle, BLACK);
                vsf_interior(gl_handle, FIS_HOLLOW);	/* Set interior pattern */
		vswr_mode( gl_handle, MD_REPLACE );	/* Set Replace-mode	*/
		
            /* Set clipping rectangle */

                xy_array[0] = dirty_dest.g_x;
                xy_array[1] = dirty_dest.g_y;
                xy_array[2] = dirty_dest.g_x + dirty_dest.g_w - 1;
                xy_array[3] = dirty_dest.g_y + dirty_dest.g_h - 1;
                vs_clip(gl_handle, 1, xy_array);    /* Enable clipping */

                buffer_ptr = file_buffer;
                row = 0;
                while (row < end_row) {

                    end_of_line = FALSE;

                /* Point to start of start row */

                    if (show_as_text) {
                        while (row < start_row
                                && buffer_ptr < file_buffer_end) {
                            while (*buffer_ptr != '\r' && *buffer_ptr != '\n')
                                buffer_ptr++;
                            if (buffer_ptr < file_buffer_end) {
                                buffer_ptr++;
                                if (*(buffer_ptr - 1) == '\r'
                                        && *buffer_ptr == '\n'
                                        && buffer_ptr < file_buffer_end)
                                    buffer_ptr++;
                                row++;
                            }
                        }
                        row = start_row;    /* In case at file_buffer end */

                    /* Point to start column */

#if DBCS
			slptr = buffer_ptr;	/* save start pointer */
#endif /* DBCS */
                        column = 0;
                        while (!end_of_line && column < start_column
                                && buffer_ptr < file_buffer_end) {
                            if (buffer_ptr >= file_buffer_end
                                    || *buffer_ptr == '\r'
                                    || *buffer_ptr == '\n') {
                                end_of_line = TRUE;
                            } else {
                                if (*buffer_ptr == '\t') {
                                    if ((column = (column / 8 + 1) * 8)
                                            > start_column)
                                        column = start_column;
                                    else
                                        buffer_ptr++;
                                } else {
                                    column++;
                                    buffer_ptr++;
                                }
                            }
                        }
                        s = text_buffer;
                    } else {
                        row = start_row;
                        if ((buffer_ptr
                                    = file_buffer + row * BYTES_PER_HEX_LINE)
                                >= file_buffer_end) {
                            buffer_ptr = file_buffer_end;
                            column = start_column;
                            end_of_line = TRUE;
                        } else {

                        /* Create hex display in text_buffer */
			    t = text_buffer ;
                            memset(text_buffer, ' ', HEX_LINE_LENGTH);
			    t += sprintf( (BYTE *)t, "%06lX  ", 
				window_offset + row * BYTES_PER_HEX_LINE ) ;
                            i = (WORD)(file_buffer_end - buffer_ptr) ;
                            if ( i > BYTES_PER_HEX_LINE)
                                i = BYTES_PER_HEX_LINE;
                            for (j = 0; j < i; j++) {
				t += sprintf( (BYTE *)t, "%02X ", 
						0xff & (*(buffer_ptr + j)) );
                            }
			    *t = ' ' ;
                            text_buffer[6 + 2 + (BYTES_PER_HEX_LINE / 2) * 3
                                - 1] = '-';
                            t = text_buffer + HEX_LINE_LENGTH
                                - BYTES_PER_HEX_LINE;
                            while (i > 0) {
				*t = (*buffer_ptr == '\0') ? '.' : *buffer_ptr;
                                t++;
                                buffer_ptr++;
                                i--;
                            }
                            if (start_column < HEX_LINE_LENGTH) {
                                column = start_column;
                                s = text_buffer + start_column;
                            } else {
                                column = HEX_LINE_LENGTH;
                                end_of_line = TRUE;
                            }
                        }
                    }

                /* Output text */

                    while (!end_of_line && column < end_column) {

                        xy_array[0] = work_area.g_x
                            + (column - window_column) * gl_wchar;

                        string_length = 0;
                        if (show_as_text) {

                        /* Transfer text to text_buffer, translating characters
                           */

                            t = s = text_buffer;    /* Start of text */
                            while (!end_of_line && column < end_column
                                    && string_length < max_str_len) {
                                if (buffer_ptr >= file_buffer_end
                                        || *buffer_ptr == '\r'
                                        || *buffer_ptr == '\n') {
                                    end_of_line = TRUE;
                                } else {
                                    if (*buffer_ptr == '\t') {
                                        i = (column / 8 + 1) * 8;
                                        while (column < i && column < end_column
                                                && string_length
                                                    < max_str_len) {
                                            *s++ = ' ';
                                            column++;
                                            string_length++;
                                        }
                                        if (column == i)
                                            buffer_ptr++;
                                        else
                                            string_length = max_str_len;
                                    } else {
                                        if (*buffer_ptr == '\0')
                                            *s = '.';
                                        else
                                            *s = *buffer_ptr;
#if DBCS
					if ( dbcs_expected() && (s == t) &&
					    (buffer_ptr > slptr) )
					{
					    dbc1 = 0;
					    for(p=buffer_ptr-1; p>=slptr; p--)
						if (dbcs_lead(*p))
						    dbc1++;
						else
						    break;
					    if (dbc1 & 1)
						*s = ' ';
					}
#endif /* DBCS */
                                        s++;
                                        column++;
                                        string_length++;
                                        buffer_ptr++;
                                    }
                                }
                            }
                        } else {
                            t = s;      /* Start of text */
#if DBCS
			    if (dbcs_expected())
				cvt_dbc2(text_buffer, t);
#endif /* DBCS */
                            while (!end_of_line && column < end_column
                                    && string_length < max_str_len) {
                                if (s == text_buffer + HEX_LINE_LENGTH) {
                                    end_of_line = TRUE;
                                } else {
                                    s++;
                                    column++;
                                    string_length++;
                                }
                            }
                        }

                        c = *s;		/* Save character at end of string */
                        *s = '\0';      /* Mark end of string */
                        v_gtext(gl_handle, xy_array[0], work_area.g_y + row * gl_hchar, 
				(BYTE*)t);
                        *s = c;     /* Restore character overwritten with NUL */
                    }

                /* Clear after end of line if necessary */

                    if (column < end_column) {
                        xy_array[0] = work_area.g_x
                            + (column - window_column) * gl_wchar;
                        xy_array[1] = work_area.g_y + row * gl_hchar;
                        xy_array[2] = dirty_dest.g_x + dirty_dest.g_w;
                        xy_array[3] = work_area.g_y + (row + 1) * gl_hchar;
                        vr_recfl(gl_handle, xy_array);   /* NULL not used */
                    }

                    start_row++;        /* Next required row */
                }

                vs_clip(gl_handle, 0, NULL);       /* Disable clipping */
                vst_alignment(gl_handle, ALI_LEFT, ALI_BASE, &hor_alignment,
                    &vert_alignment);   /* Set text alignment to top left */
            }
        }
        wind_get(wh, WF_NEXTXYWH, &box.g_x, &box.g_y, &box.g_w,
            &box.g_h);
    }

    graf_mouse(M_ON, 0);
} /* do_redraw() */

MLOCAL VOID draw_first_text_page(WORD wh)
{
    UBYTE far *p;
    WORD row;
    WORD col;
    WORD rows;
    WORD cols;
    WORD xy[4];
    WORD hor_alignment;
    WORD vert_alignment;
    BYTE line[MAX_STR_LEN + 1];

    if (!show_as_text || !file_buffer || !file_buffer_end)
        return;

    rows = work_area.g_h / gl_hchar;
    cols = work_area.g_w / gl_wchar;
    if (rows > 24)
        rows = 24;
    if (cols > MAX_STR_LEN)
        cols = MAX_STR_LEN;

    xy[0] = work_area.g_x;
    xy[1] = work_area.g_y;
    xy[2] = work_area.g_x + work_area.g_w - 1;
    xy[3] = work_area.g_y + work_area.g_h - 1;
    vs_clip(gl_handle, 1, xy);
    vst_alignment(gl_handle, ALI_LEFT, ALI_TOP, &hor_alignment,
        &vert_alignment);
    vst_color(gl_handle, BLACK);

    p = file_buffer;
    row = 0;
    while (row < rows && p < file_buffer_end) {
        col = 0;
        while (col < cols && p < file_buffer_end
                && *p != '\r' && *p != '\n') {
            line[col++] = *p ? *p : '.';
            p++;
        }
        line[col] = '\0';
        v_gtext(gl_handle, work_area.g_x, work_area.g_y + row * gl_hchar,
            (BYTE *)line);
        while (p < file_buffer_end && (*p == '\r' || *p == '\n'))
            p++;
        row++;
    }

    vs_clip(gl_handle, 0, NULL);
    vst_alignment(gl_handle, ALI_LEFT, ALI_BASE, &hor_alignment,
        &vert_alignment);
    (void) wh;
}

/*-----------------------------------------------------------------------------
Purpose : Toggle window between full and previous size
Entry   : wh    Window handle
Exit    : 
*/
MLOCAL void do_full(WORD wh)
{
    GRECT	curr, full;

    graf_mouse(M_OFF, 0);
    wind_get(wh, WF_CXYWH, &curr.g_x, &curr.g_y, &curr.g_w, &curr.g_h);
    wind_get(wh, WF_FXYWH, &full.g_x, &full.g_y, &full.g_w, &full.g_h);
    if ( curr.g_h > full.g_h/2 )
	full.g_h/=2;
    wind_set(wh, WF_CXYWH, full.g_x, full.g_y, full.g_w, full.g_h);
    wind_get(wh, WF_WXYWH, &work_area.g_x, &work_area.g_y,
        &work_area.g_w, &work_area.g_h);
    graf_mouse(M_ON, 0);
} /* do_full() */

/*-----------------------------------------------------------------------------
Purpose : Handle message event
Entry   : message       Message buffer
          wh            Window handle
Exit    : 
*/
/* (hca: not MLOCAL, used in deskinf.c) */
void handle_message(WORD *message, WORD wh)
{
    BOOLEAN redraw_required;            /* True if work area needs to updated */
    SLONG required_row;
    GRECT work;
    WORD on_top,junk;
    
    wind_get( 0,WF_TOP,&on_top, &junk, &junk, &junk );

    redraw_required = FALSE;

    switch (message[0]) {

    case MN_SELECTED:   /* Handle a menu event */
      if( on_top == wh ){
        switch( message[3] ){
          case VIEWMENU:
            switch( message[4] ){
	      case CLOSITEM:
                quit_flag = TRUE;
                return_value = FALSE;   /* Force termination (JFL) */
                break;
	      case TEXTITEM:
                handle_key( 'T'-'@',wh );	/* Toggle text mode */
	        break;
/*	      case ZOOMITEM: XXX ViewMAX
                do_full( wh );
		break;		*/		/* Must be full box then */
	     }
	     break;
	   default:
             quit_flag = return_value = hndl_menu( message[3], message[4] );
	     break;
	  }
        }
        else
	  quit_flag = return_value = hndl_menu( message[3], message[4] );
        break;
    case WM_REDRAW:
	if( quit_flag )
	  break;
	if (message[3] == wh) 
	{
            work.g_x = message[4];
            work.g_y = message[5];
            work.g_w = message[6];
            work.g_h = message[7];
            do_redraw(message[3], &work);
        } 
	else 
	{
            fun_msg(WM_REDRAW, message[3], message[4], message[5], message[6],
                message[7]);
        }
        break;

    case WM_TOPPED:
        if (message[3] == wh) 
	{
            wind_set(message[3], WF_TOP, message[3], 0, 0, 0);
	    men_list( G.a_trees[ADMENU], GOOD_TYPE, TRUE );
  	    men_list( G.a_trees[ADMENU], ILL_TYPE, FALSE );
	}
        else
	{
            fun_msg(WM_TOPPED, message[3], message[4], message[5], message[6],
		    message[7] );
	}
    break;

    case WM_CLOSED:
        if (message[3] == wh){
            quit_flag = TRUE;
            return_value = FALSE;
            on_top = FALSE;
	}
        else
	{
            fun_msg(WM_CLOSED, message[3], message[4], message[5], message[6],
		    message[7] );
	}
        break;

    case WM_FULLED:
        if (message[3] == wh) 
	{
            do_full(message[3]);
        }
        else
            fun_msg(WM_FULLED, message[3], message[4], message[5], message[6],
		    message[7] );
        break;

    case WM_ARROWED:
        if (message[3] == wh) 
	{
            switch (message[4]) {       /* Requested action */

            case WA_UPPAGE:
                if (window_row > 0) {
                    if ((required_row
                                = window_row - work_area.g_h / gl_hchar + 1)
                            < 0)
                        required_row = 0;
                    seek_row(required_row);
                    redraw_required = TRUE;
                }
                break;

            case WA_DNPAGE:
                if (window_row < num_lines - work_area.g_h / gl_hchar) {
                    if ((required_row
                                = window_row + work_area.g_h / gl_hchar - 1)
                            > num_lines - work_area.g_h / gl_hchar)
                        required_row = num_lines - work_area.g_h / gl_hchar;
                    seek_row(required_row);
                    redraw_required = TRUE;
                }
                break;

            case WA_UPLINE:
                if (window_row > 0) {
                    seek_row(window_row - 1);
                    redraw_required = TRUE;
                }
                break;

            case WA_DNLINE:
                if (window_row < num_lines - work_area.g_h / gl_hchar) {
                    seek_row(window_row + 1);
                    redraw_required = TRUE;
                }
                break;

            case WA_LFPAGE:
                if (window_column > 0) {
                    if ((window_column -= work_area.g_w / gl_wchar - 1) < 0)
                        window_column = 0;
                    redraw_required = TRUE;
                }
                break;

            case WA_RTPAGE:
                if (window_column < max_line_length - work_area.g_w / gl_wchar) {
                    if ((window_column += work_area.g_w / gl_wchar - 1)
                            > max_line_length - work_area.g_w / gl_wchar)
                        window_column
                            = max_line_length - work_area.g_w / gl_wchar;
                    redraw_required = TRUE;
                }
                break;

            case WA_LFLINE:
                if (window_column > 0) {
                    window_column--;
                    redraw_required = TRUE;
                }
                break;

            case WA_RTLINE:
                if (window_column
                        < max_line_length - work_area.g_w / gl_wchar) {
                    window_column++;
                    redraw_required = TRUE;
                }
                break;
            } /* end switch */
        } 
	else
	{
            fun_msg(WM_ARROWED, message[3], message[4], message[5], 
		    message[6], message[7] );
	}
	break;

    case WM_HSLID:
        if (message[3] == wh) {
            if (work_area.g_w / gl_wchar < max_line_length) {
                if( message[4]<1 ) message[4]=1;
                window_column = (WORD) (((SLONG) (message[4] - 1)
                        * (SLONG) (max_line_length - (work_area.g_w / gl_wchar)))
                        / 999);
                redraw_required = TRUE;
            }
        }
        break;

    case WM_VSLID:
        if (message[3] == wh) {
            if (!scanning_file && (SLONG) (work_area.g_h / gl_hchar)
                    < num_lines) {
                if( message[4]<1 ) message[4]=1;
		seek_row(((SLONG) (message[4]-1)
                    * (num_lines - (SLONG) (work_area.g_h / gl_hchar))) / 999);
                redraw_required = TRUE;
            }
        }
	else
            fun_msg(WM_VSLID, message[3], message[4], message[5], message[6],
		    message[7] );
        break;

    case WM_SIZED:
    case WM_MOVED:
        if (message[3] == wh) {
            wind_calc( WC_WORK, WKIND_SHOW, message[4], message[5], message[6],
                message[7], &work.g_x, &work.g_y, &work.g_w, &work.g_h);
            work.g_x = WORD_ALIGN( work.g_x );
            work.g_w = (work.g_w / gl_wchar) * gl_wchar;
            work.g_h = (work.g_h / gl_hchar) * gl_hchar;
            wind_calc(WC_BORDER, WKIND_SHOW, work.g_x, work.g_y, work.g_w,
                work.g_h, &message[4], &message[5], &message[6], &message[7]);
            message[4] = WORD_ALIGN( message[4] );
            wind_set(message[3], WF_CXYWH, message[4], message[5], message[6],
                message[7]);
            wind_get(message[3], WF_WXYWH, &work_area.g_x, &work_area.g_y,
                &work_area.g_w, &work_area.g_h);
            if (window_column > max_line_length - work.g_w / gl_wchar) {
                if ((window_column = max_line_length - work.g_w / gl_wchar) < 0)
                    window_column = 0;
            }
            redraw_required = TRUE;
            break;
        }
    else
            fun_msg(message[0], message[3], message[4], message[5], 
		    message[6], message[7] );
    }

    if (redraw_required) 
    {
        do_redraw(message[3], &work_area);
        set_sliders(message[3]);
    }
} /* handle_message() */

/*-----------------------------------------------------------------------------
Purpose : Handle keyboard event
Entry   : key           VDI keyboard code
          wh            Window handle
Exit    : 
*/
MLOCAL void handle_key(UWORD key, WORD wh)
{
    SLONG old_offset, current_offset;
    UBYTE far *s;
    WORD on_top,junk;
    
    wind_get( 0,WF_TOP,&on_top, &junk, &junk, &junk );
    if( wh!=on_top ){
      quit_flag = return_value = hndl_kbd( key );
      return;
    }
    
    if ((key & 0xff) == 'T' - '@') {    /* Ctrl T - toggle text/hex display */
        graf_mouse(HOURGLASS, 0);
        if (show_as_text) {
            show_as_text = FALSE;
            max_line_length = HEX_LINE_LENGTH;
            num_lines
                = (file_size + BYTES_PER_HEX_LINE - 1) / BYTES_PER_HEX_LINE;
            window_row = window_offset / BYTES_PER_HEX_LINE;
            window_offset = (window_offset / BYTES_PER_HEX_LINE)
                * BYTES_PER_HEX_LINE;
        } else {
            show_as_text = TRUE;
            max_line_length = max_text_line_length;
            num_lines = num_text_lines;

    /* Find row and offset for start of text line */

            old_offset = window_offset;
            window_offset = current_offset = 0;
            window_row = 0;
            while (current_offset < old_offset && !file_error) {
                if (dos_lseek(file_handle, 0, current_offset)
                        == current_offset) {
                    file_buffer_end = file_buffer + dos_read(file_handle,
                        file_buffer_size, (LPBYTE)file_buffer );
                    s = file_buffer;
                    while (current_offset + (s - file_buffer) < old_offset
                            && s < file_buffer_end - 1) {
                        if (*s == '\r' || *s == '\n') {
                            if (*s == '\r' && *(s + 1) == '\n')
                                s++;
                            window_row++;
                            window_offset
                                = current_offset + (s + 1 - file_buffer);
                        }
                        s++;
                    }
                    current_offset += (s - file_buffer);
                } else {
                    file_error = TRUE;
                }
            }
        }
        seek_read();
        do_redraw(wh, &work_area);
        set_sliders(wh);
        graf_mouse(ARROW, 0);
    }
} /* handle_key() */

/*-----------------------------------------------------------------------------
Purpose : Scan part file to accumulate maximum text line length and number of
          text lines
Entry   : wh    Window handle
Exit    : 
*/
MLOCAL void scan_file(WORD wh)
{
    WORD ii;
    WORD old_max_line_length;
    SLONG old_num_lines;
    UBYTE *s;
    UBYTE buffer[513];

    if (dos_lseek(file_handle, 0, file_size) == file_size) 
    {
        ii = dos_read( file_handle, sizeof(buffer), (LPBYTE)buffer );
        if ( ii == sizeof(buffer))
            ii = sizeof(buffer) - 1;
        else
            scanning_file = FALSE;
        old_max_line_length = max_text_line_length;
        old_num_lines = num_text_lines;
        s = buffer;
        while (s < buffer + ii) 
	{
            if (*s == '\r' || *s == '\n') 
	    {
                if (*s == '\r' && *(s + 1) == '\n')
                    s++;
                scan_column = 0;
                num_text_lines++;
            } 
	    else 
	    {
                if ((*s >= '0' && *s <= '9') || (*s >= 'A' && *s <= 'Z')
                        || (*s >= 'a' && *s <= 'z')) 
		{
                    if (!scan_in_word) 
		    {
                        num_words++;
                        scan_in_word = TRUE;
                    }
                } 
		else 
		{
                    scan_in_word = FALSE;
                }
                if (++scan_column > max_text_line_length)
                    max_text_line_length = scan_column;
            }
            s++;
        }
        file_size += (s - buffer);
        if (show_as_text) 
	{
            if (max_text_line_length > old_max_line_length
                    || num_text_lines > old_num_lines) 
	    {
                max_line_length = max_text_line_length;
                num_lines = num_text_lines;
                set_sliders(wh);
            }
        } 
	else 
	{
            num_lines
                = (file_size + BYTES_PER_HEX_LINE - 1) / BYTES_PER_HEX_LINE;
            set_sliders(wh);
        }
        if (!scanning_file) 
	{
            set_info_line(wh);
        }
    }
    else {
        file_error = TRUE;
        scanning_file = FALSE;
    }
} /* scan_file() */

/*-----------------------------------------------------------------------------
Purpose : Read and scan start of file to accumulate maximum text line length and
          number of text lines and decide whether text file
Entry   : wh    Window handle
Exit    : 
*/
MLOCAL void start_scan_file(WORD wh)
{
    WORD i;
    WORD num_text_chars;
    UBYTE far *s;
#if DBCS
    WORD type = CT_ADE;
#endif /* DBCS */

    file_error = FALSE;
    window_offset = 0;
    if ((i = dos_read(file_handle, file_buffer_size, (LPBYTE)file_buffer ))
            == 0) {
	form_alert(1, ini_str(STEMPTY));
        /* alert( 0x0101, EREMPTY ); */
        quit_flag = TRUE;
    } else {
        file_buffer_end = file_buffer + i;
        scanning_file = (i == file_buffer_size);
        max_text_line_length = 0;
        num_text_lines = num_words = 0;
        scan_column = 0;
        scan_in_word = FALSE;
        num_text_chars = 0;
        s = file_buffer;
        while (s < file_buffer_end - 1) {
#if DBCS
	    type = chkctype(*s, type);
	    if (type == CT_DBC1 || type == CT_DBC2)
                num_text_chars++;
	    else
#endif /* DBCS */
            if ((*s >= ' ' && *s <= 0x7e) || (*s >= 0x09 && *s <= 0x0d))
                num_text_chars++;
            if (*s == '\r' || *s == '\n') {
                if (*s == '\r' && *(s + 1) == '\n')
                    s++;
                scan_column = 0;
                num_text_lines++;
            } else {
                if ((*s >= '0' && *s <= '9') || (*s >= 'A' && *s <= 'Z')
                        || (*s >= 'a' && *s <= 'z')) {
                    if (!scan_in_word) {
                        num_words++;
                        scan_in_word = TRUE;
                    }
                } else {
                    scan_in_word = FALSE;
                }
                if (++scan_column > max_text_line_length)
                    max_text_line_length = scan_column;
            }
            s++;
        }
        file_size = i = (WORD)(s - file_buffer);

    /* Assume non-text file if less than 70% text */

        show_as_text = (((SLONG) num_text_chars * 10) / i >= 7);
        if (max_text_line_length == 0)
            max_text_line_length = 1;
        if (num_text_lines == 0)
            num_text_lines = 1;
        if (show_as_text) {
            max_line_length = max_text_line_length;
            num_lines = num_text_lines;
        } else {
            max_line_length = HEX_LINE_LENGTH;
            num_lines
                = (file_size + BYTES_PER_HEX_LINE - 1) / BYTES_PER_HEX_LINE;
        }
        window_column = 0;
        window_row = 0;
        quit_flag = FALSE;
        set_sliders(wh);
        set_info_line(wh);
    }
} /* start_scan_file() */

/*-----------------------------------------------------------------------------
Purpose : Get and handle events
Entry   : wh            Window handle
Exit    : 
*/
GLOBAL void get_handle_events(WORD wh)
{
    WORD ev_which;
    UWORD mousex, mousey, bstate, kbd_state, kbd_code, bclicks;
    WORD message[8];                    /* Message buffer */
    WORD on_top,junk;
    
    wind_get( 0,WF_TOP,&on_top, &junk, &junk, &junk );

    ev_which = MU_MESAG | MU_KEYBD | MU_BUTTON;
    if( scanning_file && wh==on_top )
        ev_which |= MU_TIMER;
	
    ev_which = evnt_multi( ev_which,
        2, 1, 1,                        /* Button requirements */
        0, 0, 0, 0, 0,                  /* Mouse rectangle 1 */
        0, 0, 0, 0, 0,                  /* Mouse rectangle 2 */
        (LPWORD)(ADDR(message)),	/* Message buffer */
        10, 0,                          /* Timer counts */
        &mousex, &mousey, &bstate,
        &kbd_state, &kbd_code,
        &bclicks);                      /* Return values */

    wind_update( BEG_UPDATE );
    
  /* XXX ViewMAX   hilite_obj( FALSE ); */

    if (ev_which & MU_BUTTON && (wind_find(mousex,mousey)!=wh) )
	quit_flag = return_value = hndl_button(bclicks, mousex, mousey, bstate, kbd_state);
    
    if (ev_which & MU_MESAG)
        handle_message(message, wh);
    
    if (ev_which & MU_KEYBD)
        handle_key(kbd_code, wh);
    
    if ( scanning_file && ev_which == MU_TIMER) 
        scan_file(wh);

  /* XXX ViewMAX  hilite_obj( TRUE ); */

    wind_update( END_UPDATE );
    
} /* get_handle_events() */



/*-----------------------------------------------------------------------------
Purpose : Type file
Entry   : curr  Current item
Exit    : Returns zero if successful
	  -4 if can't create window
	  -5 if can't alloc enough memory for one line
	  -6 if DOS_ERR on open file
Caller	: do_filemenu() in desktop.c
*/
WORD do_type(WORD curr)
{
    WORD	i;
    WORD	wh;			/* View window handle */
    WORD	drive;
    UWORD	mem_avail;
    ANODE	*panode;
    WNODE *	pwnode;
    FNODE *	pfnode;
    BYTE 	name[LEN_ZFNAME];
    BYTE	*ptr1;
    BYTE	*ptr2;
    BYTE	path_name[LEN_ZPATH], file_stem[MAXFILE] ;
    BYTE	path_spec[LEN_ZPATH], title[LEN_ZPATH + 2];
    GRECT	d,c;

    return_value = 0;

    panode = i_find(G.g_cwin, curr, &pfnode, &i);
    pwnode = win_find(G.g_cwin);

    if (panode) {
        if (panode->a_type == AT_ISFILE || panode->a_type == AT_ISWIND) 
	{
        /* Parse path name into drive, path, file name stem and extension */

            fpd_parse(pwnode->w_path->p_spec, &drive, path_name, file_stem,
						(BYTE *)NULL ) ;

        /* Split file name into stem and extension */
	    lstlcpy((BYTE far *)name, pfnode->f_name, LEN_ZFNAME);
	    strlcpy( file_stem, name, MAXFILE ) ;
	    ptr2 = strrchr( file_stem, '.' ) ;
	    if ( ptr2 ) {
		ptr1 = ptr2 + 1 ;	/* ptr1 points to extension */
		*ptr2 = '\0' ;	/* ptr2 points to end of root file name */
	    }
	    else
		ptr1 = name + strlen( name ) ; /* no extension found */

        /* Build complete path/file spec */

            fpd_bldspec(drive, path_name, file_stem, ptr1, path_spec);
	 
        /* Copy path/file spec without passwords to title */

	    sprintf( title, " %s ", path_spec ) ;
	    ptr1 = title ;
            while ( ptr1 ) {
		ptr1 = strchr( title, ';' ) ;	/* find a password */
		if ( ptr1 ) {			
		    *ptr1 = '\0' ;
		    ptr2 = strchr( ptr1+1, '/' ) ;	/* find next component */
		    if ( ptr2 )			/* shift string to left */
			memmove( (void *)ptr1, (void *)ptr2, 1 + strlen( ptr2 ) ) ;
		}
	    }

            graf_mouse(HOURGLASS, 0);

        /* Get maximum string length */

/* Not used to save space, assumes maximum string length of MAX_STR_LEN
            vq_extnd(gl_handle, 1, work_out);
            if ((max_str_len = work_out[15]) > MAX_STR_LEN)
*/
                max_str_len = MAX_STR_LEN;

            info_line[0] = '\0';

/* Do zooming box effect */
	    get_xywh(G.g_screen, curr,      &d.g_x, &d.g_y, &d.g_w, &d.g_h);
	    get_xywh(G.g_screen, G.g_croot, &c.g_x, &c.g_y, &c.g_w, &c.g_h);
	    d.g_x += c.g_x;
	    d.g_y += c.g_y;
	    graf_growbox(d.g_x, d.g_y, d.g_w, d.g_h, G.g_xdesk, 
			    G.g_ydesk, G.g_wdesk, G.g_hdesk);

	    wh = create_window( WKIND_SHOW, title, info_line, &work_area ) ;
	    if ( wh == -1 )
		return_value = -4 ;

	    else {
                graf_mouse(ARROW, 0);
                wind_update(END_UPDATE);

            /* Allocate memory */

                file_buffer_size = (work_area.g_w / gl_wchar + 2)
                    * (work_area.g_h / gl_hchar) + 1;
#if 1
		mem_avail = dos_avail_word();
/* XXX We have no find yet
 		if ( (LONG)file_buffer_size > mem_avail )
		{
			// See if there's memory being held by Find	
		    release_last_find();
		    mem_avail = dos_avail();
		}
*/
	        if ( (UWORD)file_buffer_size > mem_avail )
		{
			/* Resize to fit avail space, preserving	*/
			/*	divisibility. We know mem_avail fits	*/
			/*	in a word because it's < size		*/
			file_buffer_size = (WORD)mem_avail - 
			    ((WORD)mem_avail % (work_area.g_x/ gl_wchar+2));
		}
		file_buffer = (UBYTE far *) dos_alloc(file_buffer_size);
#else /* 1 */
                do {
                    if ((file_buffer
                            = (UBYTE far *) dos_alloc(file_buffer_size)) == 0)
                        file_buffer_size -= work_area.g_x / gl_wchar + 2;
                } while (file_buffer == NULL && file_buffer_size > 0);
#endif /* 1 */
                if (file_buffer == NULL) {
                    menu_item_to_alert_s( /*0x0101*/1, STNOMEM, TYPITEM );
                    return_value = -5;
                } else {

                /* Open file */

                    file_handle = dos_open( (BYTE far *)path_spec, 0);

                    if (DOS_ERR) {
			if( DOS_AX != E_PASSWFAIL )
			{
                            alert_s( /*0x0101*/1, STBADOPN, path_spec );
                            return_value = -6;
			} /* endif E_PASSWFAIL */
                    } else {

                    /* Read start of file and investigate */

                        start_scan_file(wh);

	                do_redraw(wh, &work_area);
	                draw_first_text_page(wh);

                    /* Main loop */

                        while (!quit_flag)
                            get_handle_events(wh);

                    /* Normal termination */

                        dos_close(file_handle);
                    } /* endif DOS_ERR */

                /* Finalisation */

                    dos_free( file_buffer );
                } /* endif NULL file_buffer */
   /* reset text alignment (JFL) */
                vst_alignment(gl_handle, ALI_LEFT, ALI_BASE, &i, &i);
                wind_update(BEG_UPDATE);
		/* close_window( wh ) ; */
		wind_get(wh, WF_WXYWH, &c.g_x, &c.g_y, &c.g_w, &c.g_h);
		wind_close( wh );
		wind_delete( wh );
	        graf_shrinkbox(d.g_x, d.g_y, d.g_w, d.g_h, c.g_x,
			    c.g_y, c.g_w, c.g_h);
            } /* endif wind_create */
            graf_mouse(ARROW, 0);
        } /* endif pa_node ISFILE */
    } /* endif pa_node non NULL */

    return(return_value);
} /* do_type() */

#endif
/*
 *	EOF:	desktype.c
 */
