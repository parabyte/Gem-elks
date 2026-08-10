/*
 * aesbind.h - the AES call prototypes
 *
 * Split out of aes.h, which had grown to carry what DRI shipped
 * as five separate headers.  Include "aes.h" to get them all;
 * this header stands alone for code that wants only this part.
 */

#ifndef ELKS_GEM_AESBIND_H
#define ELKS_GEM_AESBIND_H

#include "gem_types.h"
#include "obdefs.h"
#include "gemdefs.h"

/***************************************************************************
 * Utility Functions
 ***************************************************************************/

/*
 * Plain integer multiply/divide, truncating toward zero.  A zero
 * divisor or out-of-range quotient clamps to the 16-bit limit.
 */
WORD MUL_DIV(WORD m1, UWORD m2, WORD d1);
UWORD UMUL_DIV(UWORD m1, UWORD m2, UWORD d1);
WORD x_mul_div(WORD m1, WORD m2, WORD d1);

/* Rectangle functions */
WORD rc_equal(GRECT *p1, GRECT *p2);
VOID rc_copy(GRECT *psbox, GRECT *pdbox);
WORD rc_intersect(GRECT *p1, GRECT *p2);
WORD rc_inside(WORD x, WORD y, GRECT *pt);
VOID rc_grect_to_array(GRECT *area, WORD *array);

/***************************************************************************
 * AES Application Functions
 ***************************************************************************/

WORD appl_init(LPXBUF xbuf);
WORD appl_exit(void);
WORD appl_write(WORD rwid, WORD length, LPVOID pbuff);
WORD appl_read(WORD rwid, WORD length, LPVOID pbuff);
WORD appl_find(LPVOID pname);
WORD appl_tplay(LPVOID tbuffer, WORD tlength, WORD tscale);
WORD appl_trecord(LPVOID tbuffer, WORD tlength);
WORD appl_bvset(UWORD bvdisk, UWORD bvhard);
WORD appl_xbvget(GEM_U32_WORDS *bvdisk, GEM_U32_WORDS *bvhard);
WORD appl_xbvset(GEM_U32_WORDS bvdisk, GEM_U32_WORDS bvhard);
VOID appl_yield(void);
WORD appl_getinfo(WORD type, WORD *out1, WORD *out2, WORD *out3, WORD *out4);
WORD appl_search(WORD mode, BYTE *fname, WORD *type, WORD *ap_id);
#define xapp_getinfo appl_getinfo

/***************************************************************************
 * AES Event Functions
 ***************************************************************************/

UWORD evnt_keybd(void);
WORD evnt_button(WORD clicks, UWORD mask, UWORD state,
	WORD *pmx, WORD *pmy, WORD *pmb, WORD *pks);
WORD evnt_mouse(WORD flags, WORD x, WORD y, WORD width, WORD height,
	WORD *pmx, WORD *pmy, WORD *pmb, WORD *pks);
WORD evnt_mesag(LPVOID pbuff);
WORD evnt_timer(UWORD locnt, UWORD hicnt);
WORD evnt_multi(UWORD flags, UWORD bclk, UWORD bmsk, UWORD bst,
	UWORD m1flags, UWORD m1x, UWORD m1y, UWORD m1w, UWORD m1h,
	UWORD m2flags, UWORD m2x, UWORD m2y, UWORD m2w, UWORD m2h,
	LPVOID mepbuff, UWORD tlc, UWORD thc,
	UWORD *pmx, UWORD *pmy, UWORD *pmb, UWORD *pks, UWORD *pkr, UWORD *pbr);
WORD evnt_dclick(WORD rate, WORD setit);

/***************************************************************************
 * AES Menu Functions
 ***************************************************************************/

WORD menu_bar(LPTREE tree, WORD showit);
WORD menu_icheck(LPTREE tree, WORD itemnum, WORD checkit);
WORD menu_ienable(LPTREE tree, WORD itemnum, WORD enableit);
WORD menu_tnormal(LPTREE tree, WORD titlenum, WORD normalit);
WORD menu_text(LPTREE tree, WORD inum, LPBYTE ptext);
/* Set the eight-character process name used by original menu_fixup(). */
WORD gem_menu_set_app_name(WORD pid, LPBYTE pstr);
WORD menu_register(WORD pid, LPVOID pstr);
WORD menu_unregister(WORD mid);
WORD menu_click(WORD click, WORD setit);
WORD menu_popup(LPTREE tree, WORD x, WORD y, WORD *item);
WORD menu_attach(WORD flag, LPTREE tree, WORD item, LPVOID menu);
WORD menu_istart(WORD flag, LPTREE tree, WORD imenu, WORD item);
WORD menu_settings(WORD flag, LPVOID values);

/***************************************************************************
 * AES Object Functions
 ***************************************************************************/

WORD objc_add(LPTREE tree, WORD parent, WORD child);
WORD objc_delete(LPTREE tree, WORD delob);
WORD objc_draw(LPTREE tree, WORD drawob, WORD depth,
	WORD xc, WORD yc, WORD wc, WORD hc);
WORD objc_find(LPTREE tree, WORD startob, WORD depth, WORD mx, WORD my);
WORD objc_order(LPTREE tree, WORD mov_obj, WORD newpos);
WORD objc_offset(LPTREE tree, WORD obj, WORD *poffx, WORD *poffy);
WORD objc_edit(LPTREE tree, WORD obj, WORD inchar, WORD *idx, WORD kind);
WORD objc_change(LPTREE tree, WORD drawob, WORD depth,
	WORD xc, WORD yc, WORD wc, WORD hc, WORD newstate, WORD redraw);
WORD objc_sysvar(WORD mode, WORD which, WORD in1, WORD in2,
	WORD *out1, WORD *out2);

/***************************************************************************
 * AES Form Functions
 ***************************************************************************/

WORD form_do(LPTREE form, WORD start);
WORD form_dial(WORD dtype, WORD ix, WORD iy, WORD iw, WORD ih,
	WORD x, WORD y, WORD w, WORD h);
WORD form_alert(WORD defbut, LPBYTE astring);
WORD form_error(WORD errnum);
WORD form_center(LPTREE tree, WORD *pcx, WORD *pcy, WORD *pcw, WORD *pch);
WORD form_keybd(LPTREE form, WORD obj, WORD nxt_obj, WORD thechar,
	WORD *pnxt_obj, WORD *pchar);
WORD form_button(LPTREE form, WORD obj, WORD clks, WORD *pnxt_obj);

/***************************************************************************
 * AES Graphics Functions
 ***************************************************************************/

WORD graf_rubbox(WORD xorigin, WORD yorigin, WORD wmin, WORD hmin,
	WORD *pwend, WORD *phend);
WORD graf_dragbox(WORD w, WORD h, WORD sx, WORD sy,
	WORD xc, WORD yc, WORD wc, WORD hc, WORD *pdx, WORD *pdy);
WORD graf_mbox(WORD w, WORD h, WORD srcx, WORD srcy, WORD dstx, WORD dsty);
/* Atari-style names for the same two calls. */
#define graf_movebox   graf_mbox
#define graf_rubberbox graf_rubbox
WORD graf_watchbox(LPTREE tree, WORD obj, UWORD instate, UWORD outstate);
WORD graf_slidebox(LPTREE tree, WORD parent, WORD obj, WORD isvert);
WORD graf_handle(WORD *pwchar, WORD *phchar, WORD *pwbox, WORD *phbox);
WORD graf_mouse(WORD m_number, LPVOID m_addr);
VOID graf_mkstate(WORD *pmx, WORD *pmy, WORD *pmstate, WORD *pkstate);
WORD graf_growbox(WORD sx, WORD sy, WORD sw, WORD sh,
	WORD dx, WORD dy, WORD dw, WORD dh);
WORD graf_shrinkbox(WORD sx, WORD sy, WORD sw, WORD sh,
	WORD dx, WORD dy, WORD dw, WORD dh);

/***************************************************************************
 * AES Scrap Functions
 ***************************************************************************/

WORD scrp_read(LPVOID pscrap);
WORD scrp_write(LPVOID pscrap);
WORD scrp_clear(void);

/***************************************************************************
 * AES File Selector Functions
 ***************************************************************************/

WORD fsel_input(LPVOID pipath, LPVOID pisel, WORD *pbutton);
WORD fsel_exinput(LPVOID pipath, LPVOID pisel, WORD *pbutton, LPBYTE ptitle);

/***************************************************************************
 * AES Window Functions
 ***************************************************************************/

WORD wind_create(UWORD kind, WORD wx, WORD wy, WORD ww, WORD wh);
WORD wind_open(WORD handle, WORD wx, WORD wy, WORD ww, WORD wh);
WORD wind_close(WORD handle);
WORD wind_delete(WORD handle);
WORD wind_get(WORD w_handle, WORD w_field,
	WORD *pw1, WORD *pw2, WORD *pw3, WORD *pw4);
WORD wind_set(WORD w_handle, WORD w_field, WORD w2, WORD w3, WORD w4, WORD w5);
WORD wind_find(WORD mx, WORD my);
WORD wind_update(WORD beg_update);
WORD wind_calc(WORD wctype, UWORD kind,
	WORD x, WORD y, WORD w, WORD h, WORD *px, WORD *py, WORD *pw, WORD *ph);
WORD wind_new(void);

/***************************************************************************
 * AES Resource Functions
 ***************************************************************************/

/* Raw-asset helpers shared by the RSC and desktop-icon loaders. */
WORD gem_asset_find_path(const BYTE *name, BYTE *found, UWORD found_size);
WORD gem_asset_read_exact(WORD fd, UBYTE *buffer, UWORD length);
VOID gem_object_clear(OBJECT *object);

WORD rsrc_load(LPBYTE rsname);
WORD rsrc_free(void);
WORD rsrc_gaddr(WORD rstype, WORD rsid, LPVOID *paddr);
WORD rsrc_saddr(WORD rstype, WORD rsid, LPVOID lngval);
WORD rsrc_obfix(LPTREE tree, WORD obj);

/***************************************************************************
 * AES Shell Functions
 ***************************************************************************/

WORD shel_read(LPVOID pcmd, LPVOID ptail);
WORD shel_write(WORD doex, WORD isgr, WORD iscr, LPVOID pcmd, LPVOID ptail);
WORD shel_find(LPVOID ppath);
WORD shel_envrn(LPVOID ppath, LPVOID psrch);
WORD shel_get(LPBYTE pbuffer, WORD len);
WORD shel_put(LPBYTE pbuffer, WORD len);
WORD shel_rdef(LPVOID lpcmd, LPVOID lpdir);
WORD shel_wdef(LPVOID lpcmd, LPVOID lpdir);

/***************************************************************************
 * GEM stdout diagnostics
 ***************************************************************************/

VOID gem_stdout_msg(const char *msg);
VOID gem_stdout_kv(const char *tag, const char *value);
VOID gem_stdout_word(const char *tag, WORD value);


#endif				/* aesbind.h */
