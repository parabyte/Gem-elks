/*
 * gem_compat_aes.c - extra AES bindings the pinned import left out
 *
 * each one fills in the AES parameter block and calls gem_if()
 */

#include "ppdgem.h"
#include "ppdg0.h"


WORD
appl_read(rwid, length, pbuff)
WORD rwid, length;
LPVOID pbuff;
{
	AP_RWID = rwid;
	AP_LENGTH = length;
	gem_bindings_store_pointer(&AP_PBUFF, pbuff);
	return (gem_if(APPL_READ));
}


WORD
appl_write(rwid, length, pbuff)
WORD rwid, length;
LPVOID pbuff;
{
	AP_RWID = rwid;
	AP_LENGTH = length;
	gem_bindings_store_pointer(&AP_PBUFF, pbuff);
	return (gem_if(APPL_WRITE));
}


UWORD
evnt_keybd()
{
	return ((UWORD) gem_if(EVNT_KEYBD));
}


WORD
evnt_mouse(flags, x, y, width, height, pmx, pmy, pmb, pks)
WORD flags, x, y, width, height;
WORD *pmx, *pmy, *pmb, *pks;
{
	MO_FLAGS = flags;
	MO_X = x;
	MO_Y = y;
	MO_WIDTH = width;
	MO_HEIGHT = height;
	gem_if(EVNT_MOUSE);
	*pmx = EV_MX;
	*pmy = EV_MY;
	*pmb = EV_MB;
	*pks = EV_KS;
	return ((WORD) RET_CODE);
}


WORD
evnt_mesag(pbuff)
LPVOID pbuff;
{
	gem_bindings_store_pointer(&ME_PBUFF, pbuff);
	return (gem_if(EVNT_MESAG));
}


WORD
menu_text(tree, inum, ptext)
LPTREE tree;
WORD inum;
LPBYTE ptext;
{
	gem_bindings_store_pointer(&MM_ITREE, tree);
	ITEM_NUM = inum;
	gem_bindings_store_pointer(&MM_PTEXT, ptext);
	return (gem_if(MENU_TEXT));
}


WORD
objc_add(tree, parent, child)
LPTREE tree;
WORD parent, child;
{
	gem_bindings_store_pointer(&OB_TREE, tree);
	OB_PARENT = parent;
	OB_CHILD = child;
	return (gem_if(OBJC_ADD));
}


WORD
form_keybd(form, obj, nxt_obj, thechar, pnxt_obj, pchar)
LPTREE form;
WORD obj, nxt_obj, thechar;
WORD *pnxt_obj, *pchar;
{
	gem_bindings_store_pointer(&FM_FORM, form);
	FM_OBJ = obj;
	FM_ICHAR = thechar;
	FM_INXTOB = nxt_obj;
	gem_if(FORM_KEYBD);
	*pnxt_obj = FM_ONXTOB;
	*pchar = FM_OCHAR;
	return ((WORD) RET_CODE);
}


WORD
graf_dragbox(w, h, sx, sy, xc, yc, wc, hc, pdx, pdy)
WORD w, h, sx, sy, xc, yc, wc, hc;
WORD *pdx, *pdy;
{
	GR_I1 = w;
	GR_I2 = h;
	GR_I3 = sx;
	GR_I4 = sy;
	GR_I5 = xc;
	GR_I6 = yc;
	GR_I7 = wc;
	GR_I8 = hc;
	gem_if(GRAF_DRAGBOX);
	*pdx = GR_O1;
	*pdy = GR_O2;
	return ((WORD) RET_CODE);
}


/* int_in[0] is an unused padding word */
WORD
graf_watchbox(tree, obj, instate, outstate)
LPTREE tree;
WORD obj;
UWORD instate, outstate;
{
	gem_bindings_store_pointer(&GR_TREE, tree);
	GR_PARENT = 0;
	GR_OBJ = obj;
	GR_INSTATE = instate;
	GR_OUTSTATE = outstate;
	return (gem_if(GRAF_WATCHBOX));
}


WORD
rsrc_free()
{
	return (gem_if(RSRC_FREE));
}


WORD
rsrc_saddr(rstype, rsid, lngval)
WORD rstype;
WORD rsid;
LPVOID lngval;
{
	RS_TYPE = rstype;
	RS_INDEX = rsid;
	gem_bindings_store_pointer(&RS_INADDR, lngval);
	return (gem_if(RSRC_SADDR));
}


WORD
rsrc_obfix(tree, obj)
LPTREE tree;
WORD obj;
{
	gem_bindings_store_pointer(&RS_TREE, tree);
	RS_OBJ = obj;
	return (gem_if(RSRC_OBFIX));
}


WORD
shel_read(pcmd, ptail)
LPVOID pcmd, ptail;
{
	gem_bindings_store_pointer(&SH_PCMD, pcmd);
	gem_bindings_store_pointer(&SH_PTAIL, ptail);
	return (gem_if(SHEL_READ));
}


WORD
shel_envrn(ppath, psrch)
LPVOID ppath, psrch;
{
	gem_bindings_store_pointer(&SH_PATH, ppath);
	gem_bindings_store_pointer(&SH_SRCH, psrch);
	return (gem_if(SHEL_ENVRN));
}


WORD
objc_delete(tree, delob)
LPTREE tree;
WORD delob;
{
	gem_bindings_store_pointer(&OB_TREE, tree);
	OB_DELOB = delob;
	return (gem_if(OBJC_DELETE));
}


WORD
form_button(form, obj, clks, pnxt_obj)
LPTREE form;
WORD obj, clks;
WORD *pnxt_obj;
{
	WORD result;

	gem_bindings_store_pointer(&FM_FORM, form);
	FM_OBJ = obj;
	FM_CLKS = clks;
	result = gem_if(FORM_BUTTON);
	if (pnxt_obj != (WORD *) 0)
		*pnxt_obj = FM_ONXTOB;
	return (result);
}


/* the Scrap Manager. SCRP_READ fills the buffer with the scrap dir and
 * returns the file-type bit vector; buffer needs room for a path plus
 * its trailing separator */
WORD
scrp_read(pscrap)
LPVOID pscrap;
{
	gem_bindings_store_pointer(&SC_PATH, pscrap);
	return (gem_if(SCRP_READ));
}


WORD
scrp_write(pscrap)
LPVOID pscrap;
{
	gem_bindings_store_pointer(&SC_PATH, pscrap);
	return (gem_if(SCRP_WRITE));
}


WORD
scrp_clear()
{
	return (gem_if(SCRP_CLEAR));
}


/* the event tape. both calls block until their tape ends; the resident
 * parks the request and answers when the tape runs out. one record is
 * six bytes */
WORD
appl_tplay(tbuffer, tlength, tscale)
LPVOID tbuffer;
WORD tlength, tscale;
{
	gem_bindings_store_pointer(&AP_TBUFFER, tbuffer);
	AP_TLENGTH = tlength;
	AP_TSCALE = tscale;
	return (gem_if(APPL_TPLAY));
}


WORD
appl_trecord(tbuffer, tlength)
LPVOID tbuffer;
WORD tlength;
{
	gem_bindings_store_pointer(&AP_TBUFFER, tbuffer);
	AP_TLENGTH = tlength;
	return (gem_if(APPL_TRECORD));
}
