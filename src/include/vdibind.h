/*
 * vdibind.h - the VDI call prototypes
 *
 * Split out of aes.h, which had grown to carry what DRI shipped
 * as five separate headers.  Include "aes.h" to get them all;
 * this header stands alone for code that wants only this part.
 */

#ifndef ELKS_GEM_VDIBIND_H
#define ELKS_GEM_VDIBIND_H

#include "gem_types.h"
#include "obdefs.h"
#include "gsxdefs.h"

/***************************************************************************
 * VDI Functions
 ***************************************************************************/

/* Workstation functions */
WORD v_opnwk(WORD work_in[], WORD *handle, WORD work_out[]);
WORD v_clswk(WORD handle);
WORD v_clrwk(WORD handle);
WORD v_updwk(WORD handle);
WORD v_opnvwk(WORD work_in[], WORD *handle, WORD work_out[]);
WORD v_clsvwk(WORD handle);
WORD vq_extnd(WORD handle, WORD owflag, WORD work_out[]);

/* Output primitives */
WORD v_pline(WORD handle, WORD count, WORD xy[]);
WORD v_pmarker(WORD handle, WORD count, WORD xy[]);
WORD v_gtext(WORD handle, WORD x, WORD y, BYTE *string);
WORD v_fillarea(WORD handle, WORD count, WORD xy[]);
WORD v_cellarray(WORD handle, WORD xy[], WORD row_length,
	WORD el_per_row, WORD num_rows, WORD wr_mode, WORD *colors);
WORD vr_recfl(WORD handle, WORD *xy);

/* GDP (Graphics Device Primitives) */
WORD v_bar(WORD handle, WORD xy[]);
WORD v_arc(WORD handle, WORD xc, WORD yc, WORD rad, WORD sang, WORD eang);
WORD v_pieslice(WORD handle, WORD xc, WORD yc, WORD rad, WORD sang, WORD eang);
WORD v_circle(WORD handle, WORD xc, WORD yc, WORD rad);
WORD v_ellipse(WORD handle, WORD xc, WORD yc, WORD xrad, WORD yrad);
WORD v_ellarc(WORD handle, WORD xc, WORD yc, WORD xrad, WORD yrad,
	WORD sang, WORD eang);
WORD v_ellpie(WORD handle, WORD xc, WORD yc, WORD xrad, WORD yrad,
	WORD sang, WORD eang);
WORD v_rbox(WORD handle, WORD xy[]);
WORD v_rfbox(WORD handle, WORD xy[]);
WORD v_justified(WORD handle, WORD x, WORD y, BYTE string[],
	WORD length, WORD word_space, WORD char_space);
WORD vqt_justified(WORD handle, WORD x, WORD y, BYTE string[],
	WORD length, WORD word_space, WORD char_space, WORD offsets[]);

/* Attribute functions */
WORD vsl_type(WORD handle, WORD style);
WORD vsl_width(WORD handle, WORD width);
WORD vsl_color(WORD handle, WORD index);
WORD vsl_udsty(WORD handle, WORD pattern);
WORD vsl_ends(WORD handle, WORD beg_style, WORD end_style);
WORD vsm_type(WORD handle, WORD symbol);
WORD vsm_height(WORD handle, WORD height);
WORD vsm_color(WORD handle, WORD index);
WORD vst_height(WORD handle, WORD height,
	WORD *char_width, WORD *char_height,
	WORD *cell_width, WORD *cell_height);
WORD vst_rotation(WORD handle, WORD angle);
WORD vst_font(WORD handle, WORD font);
WORD vst_color(WORD handle, WORD index);
WORD vst_alignment(WORD handle, WORD hor_in, WORD vert_in,
	WORD *hor_out, WORD *vert_out);
WORD vst_effects(WORD handle, WORD effect);
WORD vst_point(WORD handle, WORD point,
	WORD *char_width, WORD *char_height,
	WORD *cell_width, WORD *cell_height);
WORD vsf_interior(WORD handle, WORD style);
WORD vsf_style(WORD handle, WORD index);
WORD vsf_color(WORD handle, WORD index);
WORD vsf_perimeter(WORD handle, WORD per_vis);
WORD vsf_udpat(WORD handle, WORD fill_pat[], WORD planes);
WORD vswr_mode(WORD handle, WORD mode);
WORD vs_color(WORD handle, WORD index, WORD *rgb);

/* Raster operations */
WORD vro_cpyfm(WORD handle, WORD wr_mode, WORD xy[], LPMFDB srcMFDB,
	LPMFDB desMFDB);
WORD vrt_cpyfm(WORD handle, WORD wr_mode, WORD xy[], LPMFDB srcMFDB,
	LPMFDB desMFDB, WORD *index);
WORD vr_trnfm(WORD handle, LPMFDB srcMFDB, LPMFDB desMFDB);

/* Input functions */
WORD v_show_c(WORD handle, WORD reset);
WORD v_hide_c(WORD handle);
WORD vq_mouse(WORD handle, WORD *status, WORD *px, WORD *py);
WORD vq_key_s(WORD handle, WORD *status);
/*
 * The logical input devices.  The resident answers all of these from
 * the current sample and never blocks, so the request forms behave
 * like the sample forms and report a zero terminator when nothing is
 * pending.
 */
WORD vrq_locator(WORD handle, WORD x, WORD y, WORD *xout, WORD *yout,
	WORD *term);
WORD vsm_locator(WORD handle, WORD x, WORD y, WORD *xout, WORD *yout,
	WORD *term);
WORD vrq_choice(WORD handle, WORD ch_in, WORD *ch_out);
WORD vsm_choice(WORD handle, WORD *choice);
WORD vrq_string(WORD handle, WORD max_length, WORD echo_mode,
	WORD echo_xy[], BYTE string[]);
WORD vsm_string(WORD handle, WORD max_length, WORD echo_mode,
	WORD echo_xy[], BYTE string[]);
WORD vex_butv(WORD handle, LPVOID usercode, LPVOID *savecode);
WORD vex_motv(WORD handle, LPVOID usercode, LPVOID *savecode);
WORD vex_curv(WORD handle, LPVOID usercode, LPVOID *savecode);
WORD vex_timv(WORD handle, LPVOID tim_addr, LPVOID *old_addr, WORD *scale);
WORD vsc_form(WORD handle, WORD *cur_form);

/* Inquiry functions */
WORD vq_color(WORD handle, WORD index, WORD set_flag, WORD rgb[]);
WORD vq_cellarray(WORD handle, WORD xy[], WORD row_len, WORD num_rows,
	WORD *el_used, WORD *rows_used, WORD *stat, WORD colors[]);
WORD vq_chcells(WORD handle, WORD *rows, WORD *columns);
WORD vqin_mode(WORD handle, WORD dev_type, WORD *mode);
WORD vqt_width(WORD handle, BYTE character,
	WORD *cell_width, WORD *left_delta, WORD *right_delta);
WORD vqt_extent(WORD handle, BYTE string[], WORD extent[]);
WORD vqt_attributes(WORD handle, WORD attributes[]);
WORD vqt_fontinfo(WORD handle, WORD *minade, WORD *maxade, WORD distances[],
	WORD *maxwidth, WORD effects[]);
WORD vqt_name(WORD handle, WORD element_num, BYTE name[]);
WORD vqt_font_info(WORD handle, WORD *minADE, WORD *maxADE,
	WORD distances[], WORD *maxwidth, WORD effects[]);
WORD vql_attributes(WORD handle, WORD attributes[]);
WORD vqm_attributes(WORD handle, WORD attributes[]);
WORD vqf_attributes(WORD handle, WORD attributes[]);
WORD v_get_pixel(WORD handle, WORD x, WORD y, WORD *pel, WORD *index);

/* Control functions */
WORD vs_clip(WORD handle, WORD clip_flag, WORD xy[]);
WORD vsin_mode(WORD handle, WORD dev_type, WORD mode);

/* Escape functions */
WORD v_sound(WORD handle, WORD frequency, WORD duration);
WORD vs_mute(WORD handle, WORD action);

/* Font functions */
WORD vst_load_fonts(WORD handle, WORD select);
WORD vst_unload_fonts(WORD handle, WORD select);

VOID v_copies(WORD handle, WORD count);
VOID v_etext(WORD handle, WORD x, WORD y, UBYTE string[], WORD offsets[]);
VOID v_orient(WORD handle, WORD orientation);
VOID v_tray(WORD handle, WORD tray);
WORD v_xbit_image(WORD handle, BYTE *filename, WORD aspect,
	WORD x_scale, WORD y_scale, WORD h_align, WORD v_align,
	WORD rotate, WORD background, WORD foreground, WORD xy[]);
WORD vst_ex_load_fonts(WORD handle, WORD select, WORD font_max, WORD font_free);
VOID v_ps_halftone(WORD handle, WORD index, WORD angle, WORD frequency);
VOID v_setrgbi(WORD handle, WORD primtype, WORD r, WORD g, WORD b, WORD i);
VOID v_topbot(WORD handle, WORD height, WORD *char_width, WORD *char_height,
	WORD *cell_width, WORD *cell_height);
VOID vs_bkcolor(WORD handle, WORD color);
VOID v_set_app_buff(LPVOID address, WORD nparagraphs);
WORD v_bez_on(WORD handle);
WORD v_bez_off(WORD handle);
WORD v_bez_qual(WORD handle, WORD prcnt);
VOID v_pat_rotate(WORD handle, WORD angle);
VOID vs_grayoverride(WORD handle, WORD grayval);
VOID v_bez(WORD handle, WORD count, LPWORD xyarr, UBYTE *bezarr,
	LPWORD minmax, LPWORD npts, LPWORD nmove);
VOID v_bezfill(WORD handle, WORD count, LPWORD xyarr, UBYTE *bezarr,
	LPWORD minmax, LPWORD npts, LPWORD nmove);

WORD xgrf_stepcalc(WORD orgw, WORD orgh, WORD xc, WORD yc,
	WORD w, WORD h, WORD *pcx, WORD *pcy,
	WORD *pcnt, WORD *pxstep, WORD *pystep);
WORD xgrf_2box(WORD xc, WORD yc, WORD w, WORD h,
	WORD corners, WORD cnt, WORD xstep, WORD ystep, WORD doubled);
WORD xgrf_colour(WORD type, WORD fg, WORD bg, WORD style, WORD index);
#define xgrf_color xgrf_colour
WORD xgrf_dtimage(LPMFDB deskMFDB);

WORD prop_get(LPBYTE program, LPBYTE section, LPBYTE buf, WORD buflen,
	WORD options);
WORD prop_put(LPBYTE program, LPBYTE section, LPBYTE buf, WORD options);
WORD prop_del(LPBYTE program, LPBYTE section, WORD options);
WORD prop_gui_get(WORD propnum);
WORD prop_gui_set(WORD propnum, WORD value);
WORD xshl_getshell(LPBYTE program);
WORD xshl_setshell(LPBYTE program);

/***************************************************************************
 * Icon Helpers
 ***************************************************************************/

VOID fix_icon(WORD vdi_handle, LPTREE tree);
VOID trans_gimage(WORD vdi_handle, LPTREE tree, WORD obj);

#endif				/* vdibind.h */
