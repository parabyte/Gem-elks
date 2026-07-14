/*	DESK1.C		*/
/*	Routines specific to Desktop 1.x */
/*
*       Copyright 2001, John Elliott.     
*       Copyright 1999, Lineo inc.
*       This software is licenced under the GNU Public License.         
*       Please see LICENSE.TXT for further information.                 
*                                                                       
*                  Historical Copyright                                 
*	-------------------------------------------------------------
*	GEM Desktop					  Version 2.3
*	Serial No.  XXXX-0000-654321		  All Rights Reserved
*	Copyright (C) 1987			Digital Research Inc.
*	-------------------------------------------------------------
*/

#include "ppddesk.h"

VOID zoom_closed(WORD close, WORD w_id, WORD xicon, WORD yicon)
{
	GRECT rc;
	wind_get(w_id, WF_WXYWH, &rc.g_x, &rc.g_y, &rc.g_w, &rc.g_h);
	if (close) wind_close(w_id);

	graf_shrinkbox(xicon, yicon, G.g_wicon, G.g_hicon,
			rc.g_x, rc.g_y, rc.g_w, rc.g_h);
}



WORD w_setpath(WNODE *pw, WORD drv, BYTE *path, BYTE *name, BYTE *ext)
{
	WORD icx, icy;	// bp10 bp12
	GRECT rc;	// bp08 ich xoff yoff
	WORD res = 0;

//	dbg("w_setpath: drv=%d path=%s name=%s ext=%s\n",
//			drv, path, name, ext);

	wind_get(pw->w_id,WF_WXYWH, &rc.g_x, &rc.g_y, &rc.g_w, &rc.g_h);
	icx = rc.g_x + (rc.g_w / 2) - (G.g_wicon / 2);
	icy = rc.g_y + (rc.g_h / 2) - (G.g_hicon / 2);
	zoom_closed(0, pw->w_id, icx, icy);
	do_fopen(pw, 0, drv, path, name, ext, FALSE, TRUE); // XXX The last
				// 2 parameters are guessed at	
	return res;
}

WORD true_closewnd(WNODE *pw)
{
	GRECT rc;	// ich xoff yoff bp10
	WORD  res = 0;
	// DESKTOP v1.2 also has 3 unused WORD variables here!

	wind_get(pw->w_id,WF_WXYWH, &rc.g_x, &rc.g_y, &rc.g_w, &rc.g_h);
	zoom_closed(1, pw->w_id, G.g_screen[pw->w_obid].ob_x, 
			G.g_screen[pw->w_obid].ob_y);
	pn_close(pw->w_path);
	win_free(pw);
	do_chkall(FALSE);	// XXX This FALSE was guessed at
	return res;
}


WORD fun_close(WNODE *pw, WORD trueclose)
{
	BYTE *ppath, *pend;
	BYTE ext[4];	// bp+50h
	BYTE name[9];	// bp+47h
	BYTE path[66];	// bp+6
	WORD drv;	// bp+4
	WORD rv;	// bp+2

	if (!pw->w_path) 
	{
		form_alert(1,"[1][Invalid WNODE passed to fun_close()][ OK ]");
		return 0;
	}
	graf_mouse(HOURGLASS, NULL);
	fpd_parse(pw->w_path->p_spec, &drv, &path[0], &name[0], &ext[0]);
	if (trueclose) path[0] = 0;
	if (!path[0])
	{
		rv = true_closewnd(pw);
	}
	else
	{
		ppath = pend = path;
		pend += strlen(path)-1;
		while (pend != ppath && (*pend != '/'))
		{
			--pend;
		}
		if (pend == ppath)
		{
			path[0] = '/';
			path[1] = 0;
		}
		else
			*pend = 0;
		rv = w_setpath(pw, drv, path, name, ext);
	}
	graf_mouse(ARROW, NULL);
	return rv;
}	


WNODE *win_ontop()
{
	WORD tail    = G.g_screen->ob_tail;

	if (!G.g_screen[tail].ob_width || !G.g_screen[tail].ob_height) return 0; 
	return &G.g_wlist[tail - 2];	// Windows are obs 2,3,4,5
}


/*
 * Divide a signed screen coordinate by a positive grid extent using only
 * 16-bit operations.  The quotient is truncated toward zero and the
 * remainder has the numerator's sign, matching the 8086 IDIV behavior used
 * by the original Desktop.
 *
 * Normal icon and screen coordinates have magnitude no greater than the
 * physical display.  Power-of-two grid sizes use one-bit shifts; other sizes
 * use a bounded subtraction loop.  A zero or negative extent cannot occur
 * after GEM screen initialization.  It returns zero here defensively and
 * leaves the complete coordinate as the remainder.
 */
/* Align drive icon on a grid */
VOID snap_disk(WORD x, WORD y, LPWORD px, LPWORD py)
{
	WORD xgrid, ygrid, icw, ich, xoff, yoff, screen_rem;

	icw  = G.g_icw;
	desk_word_divide(x, icw, &xoff);
	/* x - remainder is exactly quotient * icw, without multiplication. */
	xgrid = x - xoff;
	if (xoff <= (icw >> 1)) *px = xgrid;
	else		 		   *px = xgrid + icw;
	*px = min(gl_width - icw, *px);
	desk_word_divide(gl_width, icw, &screen_rem);
	if ( *px < (gl_width >> 1)) *px += screen_rem;

	y -= G.g_ydesk;
	ich = G.g_ich;
	desk_word_divide(y, ich, &yoff);
	/* y - remainder is exactly quotient * ich, without multiplication. */
	ygrid = y - yoff;
	if (yoff <= (ich >> 1)) *py = ygrid;
	else				    *py = ygrid + ich;
	*py = min(G.g_hdesk - ich, *py);
	desk_word_divide(G.g_hdesk, ich, &screen_rem);
	if ( *py < (G.g_hdesk >> 1)) *py += screen_rem;
	*py += G.g_ydesk;
}



WORD fun_file2desk(PNODE *pn_src,	// 0E
			  ANODE *an_dest,	// 10
			  WORD dobj)		// 12
{
	LPICON dicon;
	WORD operation;

	operation = -1;
	if (an_dest)
	{
		switch(an_dest->a_type)
		{
			case AT_ISDISK:
				(void) dicon;
				strlcpy(G.g_tmppth, "/*", sizeof(G.g_tmppth));
				operation = OP_COPY;
				break;
			case AT_ISTRSH: 
				operation = OP_DELETE;
				break;
		}
	}
	if (operation == -1)
		return(FALSE);
	return fun_op(operation, pn_src, G.g_tmppth,
			0, 0, 0, 0);	// GEM/1 doesn't *have* the last 5 arguments!
}



WORD fun_file2win(PNODE *pn_src, 	// 0a
			  BYTE  *spec,	 	// 0c
			  ANODE *an_dest,	// 0e
			  FNODE *fn_dest)	// 10
{
	BYTE *p;
	WORD buflen;

	strlcpy(G.g_tmppth, spec, sizeof(G.g_tmppth));

	/*
		for (p = G.g_tmppth; (*p) != '*'; ++p);
		*p = 0;
	*/
	p = strchr(G.g_tmppth, '*');
	if (p) 
	{
		*p = 0;
		buflen = sizeof(G.g_tmppth) - strlen(G.g_tmppth);
		if (an_dest && an_dest->a_type == AT_ISFOLD)
		{
			strlcpy(p, fn_dest->f_name, buflen);
			strlcat(p, "/*", buflen);
		}
		else strlcat(p, "*", buflen);
	}
	return fun_op(OP_COPY, pn_src, G.g_tmppth,
			0, 0, 0, 0);	// GEM/1 doesn't *have* the last 5 arguments!
}

VOID fun_win2desk(WORD wh, WORD obj)
{
	WNODE *wn_src;
	ANODE *an_dest;
	
	an_dest = app_afind(TRUE, AT_ISFILE, obj, NULL, NULL);
	wn_src = win_find(wh);
	if (fun_file2desk(wn_src->w_path, an_dest, obj))
	{
		fun_rebld(wn_src);
	}
}


WORD fun_file2any(WORD sobj,	  // 12
			  WNODE *wn_dest, // 14
			  ANODE *an_dest, // 16
			  FNODE *fn_dest, // 18
			  WORD dobj)	  // 1A
{
	WORD active, okay = FALSE;
	FNODE *bp8;
	LPICON ib_src;
	PNODE *pn_src;
	
	ib_src = get_spec(G.g_screen, sobj);
	pn_src = pn_open(ib_src->ib_char, "", "*", "*", F_SUBDIR);
	if (pn_src)
	{
		active = pn_active(pn_src);
		if (active == E_NOFILES)
			DOS_ERR = FALSE;
		else
		{
			DOS_AX = active;
			DOS_ERR = TRUE;
			(void) d_errmsg();
		}
		if (active == E_NOFILES && pn_src->p_flist)
		{
			for (bp8 = pn_src->p_flist; bp8; bp8 = bp8->f_next)
			{
				bp8->f_obid = 0;
			}
			G.g_screen->ob_state = SELECTED;
			if (wn_dest)
			{
				okay = fun_file2win(pn_src, wn_dest->w_path->p_spec, an_dest, fn_dest);
			}
			else
			{
				okay = fun_file2desk(pn_src, an_dest, dobj);
			}
			G.g_screen->ob_state = 0;
		}
		pn_close(pn_src);
		desk_clear(0);
	}
	return okay;
}

VOID fun_desk2win(WORD wh, WORD dobj) 
{
	WNODE *wn_dest;
	FNODE *fn_dest;
	WORD sobj, copied, isapp;
	FNODE *fn_src;
	ANODE *an_src, *an_dest;

	wn_dest = win_find(wh);
	an_dest = i_find(wh, dobj, &fn_dest, &isapp);
	sobj = 0;
	while ((sobj = win_isel(G.g_screen, 1, sobj)))
	{
		an_src = i_find(0, sobj, &fn_src, &isapp);
		if (an_src->a_type == AT_ISTRSH)
		{
			fun_alert(1, STNODRA2);
			continue;
		}
		copied = fun_file2any(sobj, wn_dest, an_dest, fn_dest, dobj);
		if (copied) fun_rebld(wn_dest);
	}
}


VOID fun_desk2desk(WORD dobj)
{
	WORD sobj,  isapp;
	FNODE *fn;
	WORD cont;
	ANODE *source;
	ANODE *target;
	LPICON lpicon;
	char drvname[2];

	target = app_afind(1, 0, dobj, NULL, NULL);
	sobj  = 0;
	while ((sobj = win_isel(G.g_screen, 1, sobj)))
	{	
		source = i_find(0, sobj, &fn, &isapp);
		
		if (source == target) continue;
		if (source->a_type == AT_ISTRSH)
		{
			fun_alert(1, STNOSTAK);
			continue;
		}
		cont = 1;
		if (target->a_type == AT_ISTRSH)
		{
#if defined(ELKS) && ELKS
			if (source->a_type == AT_ISDISK)
			{
				fun_root_delete_alert();
				continue;
			}
#endif
			lpicon = get_spec(G.g_screen, sobj);
			drvname[0] = lpicon->ib_char & 0xFF;
			drvname[1] = 0;
			cont = fun_alert(2, STDELDIS, drvname);
		}
		if (cont != 1) continue;
		fun_file2any(sobj, NULL, target, NULL, dobj);
	}
}



WORD desk1_drag(WORD wh, WORD dest_wh, WORD sobj, WORD dobj, WORD mx, WORD my)
{
	WORD done = 0;

	if (wh)	// Dragging something from window
	{
		if (dest_wh) fun_drag(wh, dest_wh, dobj, mx, my);
		else		
		{
			if (sobj == dobj)
			{
				fun_alert(1, STNODRA1);
			}
			else
			{
				fun_win2desk(wh, dobj);
			}
		}
	}
	else	// Dragging something from desk
	{
		if (dest_wh)
		{
			fun_desk2win(dest_wh, dobj);
		}
		else	// Dragging from desk to desk
		{
			if (sobj != dobj) fun_desk2desk(dobj);
		}
	}
	return done;
}

