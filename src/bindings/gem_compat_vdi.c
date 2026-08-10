/*
 * gem_compat_vdi.c - extra VDI bindings the pinned import left out
 *
 * where the resident's point layout differs from the Atari binding
 * (circle and ellipse radii), the resident layout wins
 */

#include "ppdgem.h"
#include "ppdv0.h"


/* resident ignores the eleven work_in words; reply is the handle in
 * contrl[6], 45 intout words, six ptsout pairs */
WORD
v_opnwk(work_in, handle, work_out)
WORD work_in[], *handle, work_out[];
{
	WORD result;

	i_intin(work_in);
	i_intout(work_out);
	i_ptsout(work_out + 45);

	contrl[0] = 1;
	contrl[1] = 0;
	contrl[3] = 11;
	contrl[6] = 0;
	result = vdi();

	*handle = contrl[6];
	i_intin(intin);
	i_intout(intout);
	i_ptsout(ptsout);
	return result;
}


WORD
v_clswk(handle)
WORD handle;
{
	contrl[0] = 2;
	contrl[1] = 0;
	contrl[3] = 0;
	contrl[6] = handle;
	return vdi();
}


WORD
v_clrwk(handle)
WORD handle;
{
	contrl[0] = 3;
	contrl[1] = 0;
	contrl[3] = 0;
	contrl[6] = handle;
	return vdi();
}


WORD
v_updwk(handle)
WORD handle;
{
	contrl[0] = 4;
	contrl[1] = 0;
	contrl[3] = 0;
	contrl[6] = handle;
	return vdi();
}


WORD
v_fillarea(handle, count, xy)
WORD handle, count, xy[];
{
	WORD result;

	i_ptsin(xy);

	contrl[0] = 9;
	contrl[1] = count;
	contrl[3] = 0;
	contrl[6] = handle;
	result = vdi();

	i_ptsin(ptsin);
	return result;
}


WORD
v_bar(handle, xy)
WORD handle, xy[];
{
	WORD result;

	i_ptsin(xy);

	contrl[0] = 11;
	contrl[1] = 2;
	contrl[3] = 0;
	contrl[5] = 1;
	contrl[6] = handle;
	result = vdi();

	i_ptsin(ptsin);
	return result;
}


/* resident takes the radius from ptsin[2] and reuses it for both axes */
WORD
v_circle(handle, xc, yc, rad)
WORD handle, xc, yc, rad;
{
	ptsin[0] = xc;
	ptsin[1] = yc;
	ptsin[2] = rad;
	ptsin[3] = 0;

	contrl[0] = 11;
	contrl[1] = 2;
	contrl[3] = 0;
	contrl[5] = 4;
	contrl[6] = handle;
	return vdi();
}


/* three input pairs make the resident read the second radius from ptsin[3] */
WORD
v_ellipse(handle, xc, yc, xrad, yrad)
WORD handle, xc, yc, xrad, yrad;
{
	ptsin[0] = xc;
	ptsin[1] = yc;
	ptsin[2] = xrad;
	ptsin[3] = yrad;
	ptsin[4] = 0;
	ptsin[5] = 0;

	contrl[0] = 11;
	contrl[1] = 3;
	contrl[3] = 0;
	contrl[5] = 5;
	contrl[6] = handle;
	return vdi();
}


WORD
v_rbox(handle, xy)
WORD handle, xy[];
{
	WORD result;

	i_ptsin(xy);

	contrl[0] = 11;
	contrl[1] = 2;
	contrl[3] = 0;
	contrl[5] = 8;
	contrl[6] = handle;
	result = vdi();

	i_ptsin(ptsin);
	return result;
}


/* rgb holds the three 0..1000 intensities */
WORD
vs_color(handle, index, rgb)
WORD handle, index, *rgb;
{
	intin[0] = index;
	intin[1] = rgb[0];
	intin[2] = rgb[1];
	intin[3] = rgb[2];

	contrl[0] = 14;
	contrl[1] = 0;
	contrl[3] = 4;
	contrl[6] = handle;
	return vdi();
}


WORD
vq_color(handle, index, set_flag, rgb)
WORD handle, index, set_flag, rgb[];
{
	intin[0] = index;
	intin[1] = set_flag;

	contrl[0] = 26;
	contrl[1] = 0;
	contrl[3] = 2;
	contrl[6] = handle;
	vdi();

	rgb[0] = intout[1];
	rgb[1] = intout[2];
	rgb[2] = intout[3];
	return (intout[0]);
}


/* same 45-word/six-pair record as the open call */
WORD
vq_extnd(handle, owflag, work_out)
WORD handle, owflag, work_out[];
{
	WORD result;

	i_intout(work_out);
	i_ptsout(work_out + 45);

	intin[0] = owflag;

	contrl[0] = 102;
	contrl[1] = 0;
	contrl[3] = 1;
	contrl[6] = handle;
	result = vdi();

	i_intout(intout);
	i_ptsout(ptsout);
	return result;
}


WORD
v_show_c(handle, reset)
WORD handle, reset;
{
	intin[0] = reset;

	contrl[0] = 122;
	contrl[1] = 0;
	contrl[3] = 1;
	contrl[6] = handle;
	return vdi();
}


WORD
v_hide_c(handle)
WORD handle;
{
	contrl[0] = 123;
	contrl[1] = 0;
	contrl[3] = 0;
	contrl[6] = handle;
	return vdi();
}


WORD
vq_mouse(handle, status, px, py)
WORD handle, *status, *px, *py;
{
	WORD result;

	contrl[0] = 124;
	contrl[1] = 0;
	contrl[3] = 0;
	contrl[6] = handle;
	result = vdi();

	*status = intout[0];
	*px = ptsout[0];
	*py = ptsout[1];
	return result;
}


WORD
vq_key_s(handle, status)
WORD handle, *status;
{
	WORD result;

	contrl[0] = 128;
	contrl[1] = 0;
	contrl[3] = 0;
	contrl[6] = handle;
	result = vdi();

	*status = intout[0];
	return result;
}


/* 37-word MFORM: hot spot, plane count, the two colors, then sixteen
 * mask and sixteen data rows */
WORD
vsc_form(handle, cur_form)
WORD handle, *cur_form;
{
	WORD result;

	i_intin(cur_form);

	contrl[0] = 111;
	contrl[1] = 0;
	contrl[3] = 37;
	contrl[6] = handle;
	result = vdi();

	i_intin(intin);
	return result;
}


/* polymarkers, marker attrs, line ends, user fill patterns, attribute
 * inquiries, logical input and the exception vectors. the resident
 * serves them all; the pinned import just left the wrappers out */

WORD
v_pmarker(handle, count, xy)
WORD handle, count, xy[];
{
	WORD result;

	i_ptsin(xy);
	contrl[0] = 7;
	contrl[1] = count;
	contrl[3] = 0;
	contrl[6] = handle;
	result = vdi();
	i_ptsin(ptsin);
	return result;
}


WORD
vsm_type(handle, symbol)
WORD handle, symbol;
{
	intin[0] = symbol;
	contrl[0] = 18;
	contrl[1] = 0;
	contrl[3] = 1;
	contrl[6] = handle;
	if (vdi() <= 0 || contrl[4] < 1)
		return 0;
	return intout[0];
}


WORD
vsm_height(handle, height)
WORD handle, height;
{
	ptsin[0] = 0;
	ptsin[1] = height;
	contrl[0] = 19;
	contrl[1] = 1;
	contrl[3] = 0;
	contrl[6] = handle;
	if (vdi() <= 0 || contrl[2] < 1)
		return 0;
	return ptsout[1];
}


WORD
vsm_color(handle, index)
WORD handle, index;
{
	intin[0] = index;
	contrl[0] = 20;
	contrl[1] = 0;
	contrl[3] = 1;
	contrl[6] = handle;
	if (vdi() <= 0 || contrl[4] < 1)
		return 0;
	return intout[0];
}


WORD
vsl_ends(handle, beg_style, end_style)
WORD handle, beg_style, end_style;
{
	intin[0] = beg_style;
	intin[1] = end_style;
	contrl[0] = 108;
	contrl[1] = 0;
	contrl[3] = 2;
	contrl[6] = handle;
	if (vdi() <= 0 || contrl[4] < 2)
		return 0;
	return intout[0];
}


/* VSF_UDPAT takes sixteen words per plane. the resident is one-plane, so
 * further planes are passed but only the first is stored */
WORD
vsf_udpat(handle, fill_pat, planes)
WORD handle, fill_pat[], planes;
{
	WORD result;

	if (planes < 1)
		planes = 1;
	i_intin(fill_pat);
	contrl[0] = 112;
	contrl[1] = 0;
	contrl[3] = 16 * planes;
	contrl[6] = handle;
	result = vdi();
	i_intin(intin);
	return result;
}


WORD
vql_attributes(handle, attributes)
WORD handle, attributes[];
{
	contrl[0] = 35;
	contrl[1] = 0;
	contrl[3] = 0;
	contrl[6] = handle;
	if (vdi() <= 0 || contrl[4] < 5 || contrl[2] < 1)
		return 0;
	attributes[0] = intout[0];
	attributes[1] = intout[1];
	attributes[2] = intout[2];
	attributes[3] = intout[3];
	attributes[4] = intout[4];
	attributes[5] = ptsout[0];
	return 1;
}


WORD
vqm_attributes(handle, attributes)
WORD handle, attributes[];
{
	contrl[0] = 36;
	contrl[1] = 0;
	contrl[3] = 0;
	contrl[6] = handle;
	if (vdi() <= 0 || contrl[4] < 3 || contrl[2] < 1)
		return 0;
	attributes[0] = intout[0];
	attributes[1] = intout[1];
	attributes[2] = intout[2];
	attributes[3] = ptsout[0];
	attributes[4] = ptsout[1];
	return 1;
}


WORD
vqf_attributes(handle, attributes)
WORD handle, attributes[];
{
	contrl[0] = 37;
	contrl[1] = 0;
	contrl[3] = 0;
	contrl[6] = handle;
	if (vdi() <= 0 || contrl[4] < 5)
		return 0;
	attributes[0] = intout[0];
	attributes[1] = intout[1];
	attributes[2] = intout[2];
	attributes[3] = intout[3];
	attributes[4] = intout[4];
	return 1;
}


WORD
vsin_mode(handle, dev_type, mode)
WORD handle, dev_type, mode;
{
	intin[0] = dev_type;
	intin[1] = mode;
	contrl[0] = 33;
	contrl[1] = 0;
	contrl[3] = 2;
	contrl[6] = handle;
	if (vdi() <= 0 || contrl[4] < 1)
		return 0;
	return intout[0];
}


WORD
vqin_mode(handle, dev_type, mode)
WORD handle, dev_type, *mode;
{
	intin[0] = dev_type;
	contrl[0] = 115;
	contrl[1] = 0;
	contrl[3] = 1;
	contrl[6] = handle;
	if (vdi() <= 0 || contrl[4] < 1)
		return 0;
	if (mode != (WORD *) 0)
		*mode = intout[0];
	return 1;
}


/* the locator, choice and string devices. the resident answers each
 * from the current sample instead of blocking, so the request and sample
 * entry points are the same call; a zero terminator means nothing pending */
WORD
vsm_locator(handle, x, y, xout, yout, term)
WORD handle, x, y, *xout, *yout, *term;
{
	ptsin[0] = x;
	ptsin[1] = y;
	contrl[0] = 28;
	contrl[1] = 1;
	contrl[3] = 0;
	contrl[6] = handle;
	if (vdi() <= 0 || contrl[2] < 1)
		return 0;
	if (xout != (WORD *) 0)
		*xout = ptsout[0];
	if (yout != (WORD *) 0)
		*yout = ptsout[1];
	if (term != (WORD *) 0)
		*term = contrl[4] ? intout[0] : 0;
	return contrl[4] ? 1 : 0;
}


WORD
vrq_locator(handle, x, y, xout, yout, term)
WORD handle, x, y, *xout, *yout, *term;
{
	return vsm_locator(handle, x, y, xout, yout, term);
}


WORD
vsm_choice(handle, choice)
WORD handle, *choice;
{
	contrl[0] = 30;
	contrl[1] = 0;
	contrl[3] = 0;
	contrl[6] = handle;
	if (vdi() <= 0 || contrl[4] < 1)
		return 0;
	if (choice != (WORD *) 0)
		*choice = intout[0];
	return intout[0] ? 1 : 0;
}


WORD
vrq_choice(handle, ch_in, ch_out)
WORD handle, ch_in, *ch_out;
{
	(void) ch_in;
	return vsm_choice(handle, ch_out);
}


WORD
vsm_string(handle, max_length, echo_mode, echo_xy, string)
WORD handle, max_length, echo_mode, echo_xy[];
BYTE string[];
{
	WORD length;

	(void) echo_mode;
	(void) echo_xy;
	contrl[0] = 31;
	contrl[1] = 0;
	contrl[3] = 0;
	contrl[6] = handle;
	if (vdi() <= 0)
		return 0;
	length = contrl[4];
	if (length > max_length)
		length = max_length;
	if (length > 0 && string != (BYTE *) 0)
		string[0] = (BYTE) intout[0];
	if (string != (BYTE *) 0)
		string[length > 0 ? 1 : 0] = 0;
	return length;
}


WORD
vrq_string(handle, max_length, echo_mode, echo_xy, string)
WORD handle, max_length, echo_mode, echo_xy[];
BYTE string[];
{
	return vsm_string(handle, max_length, echo_mode, echo_xy, string);
}


/* the four exception vectors. a GEM client is its own ELKS process here
 * so the resident cant call back into it; each reports a null previous
 * vector and installs nothing. VEX_TIMV still answers with the BIOS tick
 * period in milliseconds */
WORD
vex_timv(handle, tim_addr, old_addr, scale)
WORD handle, *scale;
LPVOID tim_addr, *old_addr;
{
	(void) tim_addr;
	contrl[0] = 118;
	contrl[1] = 0;
	contrl[3] = 0;
	contrl[6] = handle;
	if (vdi() <= 0 || contrl[4] < 1)
		return 0;
	if (scale != (WORD *) 0)
		*scale = intout[0];
	/* resident installs nothing, so the previous vector is null */
	if (old_addr != (LPVOID *) 0)
		*old_addr = (LPVOID) 0;
	return 1;
}


static WORD
vex_vector(handle, opcode, usercode, savecode)
WORD handle, opcode;
LPVOID usercode, *savecode;
{
	(void) usercode;
	contrl[0] = opcode;
	contrl[1] = 0;
	contrl[3] = 0;
	contrl[6] = handle;
	if (vdi() <= 0)
		return 0;
	if (savecode != (LPVOID *) 0)
		*savecode = (LPVOID) 0;
	return 1;
}


WORD
vex_butv(handle, usercode, savecode)
WORD handle;
LPVOID usercode, *savecode;
{
	return vex_vector(handle, 125, usercode, savecode);
}


WORD
vex_motv(handle, usercode, savecode)
WORD handle;
LPVOID usercode, *savecode;
{
	return vex_vector(handle, 126, usercode, savecode);
}


WORD
vex_curv(handle, usercode, savecode)
WORD handle;
LPVOID usercode, *savecode;
{
	return vex_vector(handle, 127, usercode, savecode);
}


/* V_JUSTIFIED is GDP ten. the two spacing flags lead intin, the string
 * follows one char to the word, ptsin carries the position then the
 * length the string has to fill */
WORD
v_justified(handle, x, y, string, length, word_space, char_space)
WORD handle, x, y, length, word_space, char_space;
BYTE string[];
{
	WORD count;

	count = 0;
	while (string[count] != 0 && count < 78) {
		intin[count + 2] = string[count] & 0x00ff;
		count++;
	}
	intin[0] = word_space;
	intin[1] = char_space;

	ptsin[0] = x;
	ptsin[1] = y;
	ptsin[2] = length;
	ptsin[3] = 0;

	contrl[0] = 11;
	contrl[1] = 2;
	contrl[3] = count + 2;
	contrl[5] = 10;
	contrl[6] = handle;
	return vdi();
}


/* VQT_JUSTIFIED asks where V_JUSTIFIED would put each char. reply is one
 * point per char, so OFFSETS needs two words each */
WORD
vqt_justified(handle, x, y, string, length, word_space, char_space, offsets)
WORD handle, x, y, length, word_space, char_space, offsets[];
BYTE string[];
{
	WORD count, index;

	count = 0;
	while (string[count] != 0 && count < 78) {
		intin[count + 2] = string[count] & 0x00ff;
		count++;
	}
	intin[0] = word_space;
	intin[1] = char_space;

	ptsin[0] = x;
	ptsin[1] = y;
	ptsin[2] = length;
	ptsin[3] = 0;

	contrl[0] = 132;
	contrl[1] = 2;
	contrl[3] = count + 2;
	contrl[6] = handle;
	if (vdi() <= 0)
		return 0;
	for (index = 0; index < contrl[2] && index < count; index++) {
		offsets[index * 2] = ptsout[index * 2];
		offsets[index * 2 + 1] = ptsout[index * 2 + 1];
	}
	return contrl[2];
}
