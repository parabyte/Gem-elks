/*	DESKSUPP.C	05/04/84 - 06/20/85	Lee Lorenzen		*/
/*	for 3.0 (xm)	3/12/86	 - 01/17/87	MDF			*/
/*	for 3.0			11/13/87		mdf		*/

/*
*       Copyright 1999, Caldera Thin Clients, Inc.                      
*       This software is licenced under the GNU Public License.         
*       Please see LICENSE.TXT for further information.                 
*                                                                       
*                  Historical Copyright                                 
*	-------------------------------------------------------------
*	GEM Desktop					  Version 3.0
*	Serial No.  XXXX-0000-654321		  All Rights Reserved
*	Copyright (C) 1987			Digital Research Inc.
*	-------------------------------------------------------------
*/

#include "ppddesk.h"

/*
*	Clear out the selections for this particular window
*/
VOID desk_clear(WORD wh)
{
	WNODE		*pw;
	GRECT		c;
	WORD	    root;
						/* get current size	*/
	wind_get(wh, WF_WXYWH, &c.g_x, &c.g_y, &c.g_w, &c.g_h);
						/* find its tree of 	*/
						/*   items		*/
	pw = win_find(wh);

	/* DESKTOP v1.2: We have to clear selected desktop objects */
	if (pw) root = pw->w_root;
	else    root = 1;
						/* clear all selections	*/
	act_allchg(wh, G.a_screen, root, 0, &gl_rfull, &c,
		 SELECTED, FALSE, TRUE);
}

/*
*	Verify window display by building a new view.
*/
VOID desk_verify(WORD wh, WORD changed)
{
	WNODE		*pw;
	WORD		xc, yc, wc, hc;

	/* DESKTOP v1.2: The desktop itself... */
	if (wh)
	{ 
		pw = win_find(wh);
		if (pw)
		{
		  if (changed)
		  {
	    	wind_get(wh, WF_WXYWH, &xc, &yc, &wc, &hc);
	    	win_bldview(pw, xc, yc, wc, hc);
	  	  }
	  	G.g_croot = pw->w_root;
		}
	}
	else G.g_croot = 1;	// DESKTOP v1.2: The Desktop

	G.g_cwin = wh;
	G.g_wlastsel = wh;
}


WORD do_wredraw(WORD w_handle, WORD xc, WORD yc, WORD wc, WORD hc)
{
	GRECT		clip_r, t;
	WNODE		*pw;
	LPTREE		tree = G.a_screen;	// DESKTOP v1.2
	WORD		root;

	clip_r.g_x = xc;
	clip_r.g_y = yc;
	clip_r.g_w = wc;
	clip_r.g_h = hc;

	if (w_handle != 0)
	{
	  pw = win_find(w_handle);
	  if (!pw)
	    return FALSE;
	//  tree = G.a_screen; // DESKTOP v1.2 does this above
	  root = pw->w_root;
	}
	else root = 1;    // DESKTOP v1.2 draws the desk  return( TRUE );

	graf_mouse(M_OFF, 0);

	wind_get(w_handle, WF_FIRSTXYWH, &t.g_x, &t.g_y, &t.g_w, &t.g_h);
	while ( t.g_w && t.g_h )
	{
	  if ( rc_intersect(&clip_r, &t) )
	    objc_draw(tree, root, MAX_DEPTH, t.g_x, t.g_y, t.g_w, t.g_h);
	  wind_get(w_handle, WF_NEXTXYWH, &t.g_x, &t.g_y, &t.g_w, &t.g_h);
	}
	graf_mouse(M_ON, 0);
	return FALSE;
}


/*
*	Picks ob_x, ob_y, ob_width, ob_height fields out of object list.
*/

VOID get_xywh(LPTREE olist, WORD obj, WORD *px, WORD *py, WORD *pw, WORD *ph)
{
	LPOBJ object;

	object = desk_object_at(olist, obj);
	*px = object->ob_x;
	*py = object->ob_y;
	*pw = object->ob_width;
	*ph = object->ob_height;
}

/*
*	Picks ob_spec field out of object list.
*/

LPICON get_spec(LPTREE olist, WORD obj)
{
	GEM_TYPED_SLOT_POINTER slot;
	LPOBJ object;

	object = desk_object_at(olist, obj);
	slot.words = object->ob_spec;
	return slot.icon;
}

VOID do_xyfix(WORD *px, WORD *py)
{
	WORD		tx, ty, tw, th;

	wind_get(0, WF_WXYWH, &tx, &ty, &tw, &th);
	tx = *px;
	*px = (tx & 0x000f);
	if ( *px < 8 ) *px =  tx & 0xfff0;
	else           *px = (tx & 0xfff0) + 16;
	*py = max(*py, ty);
}

VOID do_wopen(WORD new_win, WORD wh, WORD curr, WORD x, WORD y, WORD w, WORD h)
{
	GRECT		d,c;
	WORD		right, bottom;

	do_xyfix(&x, &y);
	/*
	 * DESKTOP.INF stores window geometry in character cells.  An original
	 * profile can therefore describe a window wider or taller than the
	 * current ELKS video mode.  The resident GEM window manager correctly
	 * rejects an outer rectangle which crosses the physical screen edge, but
	 * old Desktop ignored WIND_OPEN's return value and left the icon merely
	 * deselected.  Constrain the saved rectangle to the actual root work area
	 * before retaining the original grow-box and WIND_OPEN sequence.
	 *
	 * Every value is a signed 16-bit pixel coordinate.  Width and height are
	 * clamped before the right/bottom subtraction, so neither addition nor
	 * subtraction can overflow on the 640 by 480 XT target.  There is no
	 * scaling, rounding, division, allocation, or wrapper call here.
	 */
	if (w <= 0 || h <= 0)
	{
	  x = G.g_xfull;
	  y = G.g_yfull;
	  w = G.g_wfull;
	  h = G.g_hfull;
	}
	if (w > G.g_wdesk)
	  w = G.g_wdesk;
	if (h > G.g_hdesk)
	  h = G.g_hdesk;
	right = G.g_xdesk + G.g_wdesk;
	bottom = G.g_ydesk + G.g_hdesk;
	if (x < G.g_xdesk)
	  x = G.g_xdesk;
	if (y < G.g_ydesk)
	  y = G.g_ydesk;
	if (x > right - w)
	  x = right - w;
	if (y > bottom - h)
	  y = bottom - h;
	// DESKTOP v1.2: Zooming box effect
	get_xywh(G.g_screen, curr,      &d.g_x, &d.g_y, &d.g_w, &d.g_h);
	get_xywh(G.g_screen, G.g_croot, &c.g_x, &c.g_y, &c.g_w, &c.g_h);

	// DESKTOP v1.2: Zooming box effect
	d.g_x += c.g_x;
	d.g_y += c.g_y;
				
	graf_growbox(d.g_x, d.g_y, d.g_w, d.g_h, x, y, w, h);
	
	act_chg(G.g_cwin, G.a_screen, G.g_croot, curr, &c, SELECTED, 
			FALSE, TRUE, TRUE);
	if (new_win)
	{
	  wind_open(wh, x, y, w, h);
	}
	G.g_wlastsel = wh;
}


/* DESKTOP v1.2 version... */
VOID do_wfull(WORD wh)
{
	GRECT		curr, prev, full;

	gl_whsiztop = NIL;
	wind_get(wh, WF_CXYWH, &curr.g_x, &curr.g_y, &curr.g_w, &curr.g_h);
	wind_get(wh, WF_PXYWH, &prev.g_x, &prev.g_y, &prev.g_w, &prev.g_h);
	wind_get(wh, WF_FXYWH, &full.g_x, &full.g_y, &full.g_w, &full.g_h);

	if (rc_equal(&curr, &full))
	{
		wind_set(wh, WF_CXYWH, prev.g_x, prev.g_y, prev.g_w, prev.g_h);
		graf_shrinkbox(prev.g_x, prev.g_y, prev.g_w, prev.g_h,
					   full.g_x, full.g_y, full.g_w, full.g_h);
	}	
	else
	{
		graf_growbox(curr.g_x, curr.g_y, curr.g_w, curr.g_h,
					 full.g_x, full.g_y, full.g_w, full.g_h);
		wind_set(wh, WF_CXYWH, full.g_x, full.g_y, full.g_w, full.g_h);
		
	}
}



/* Crippled DESKTOP v2.x+ version...
VOID do_wfull(WORD wh)
{
	WORD		tmp_wh, y;
	GRECT		curr, prev, full, temp;

	gl_whsiztop = NIL;
	wind_get(wh, WF_CXYWH, &curr.g_x, &curr.g_y, &curr.g_w, &curr.g_h);
	wind_get(wh, WF_PXYWH, &prev.g_x, &prev.g_y, &prev.g_w, &prev.g_h);
	wind_get(wh, WF_FXYWH, &full.g_x, &full.g_y, &full.g_w, &full.g_h);
			// have to check for shrinking a window that	//
			// was full when Desktop was first started.	//
	if ( (rc_equal(&curr, &prev)) && (curr.g_h > gl_normwin.g_h) )
	{	// shrink full window		//
		// find the other window (assuming only 2 windows)	//
	  if ( G.g_wlist[0].w_id == wh)
	    tmp_wh = G.g_wlist[1].w_id;
	  else
	    tmp_wh = G.g_wlist[0].w_id;
	    			// decide which window we're shrinking	//
	  wind_get(tmp_wh, WF_CXYWH, &temp.g_x, &temp.g_y,
	  	   &temp.g_w, &temp.g_h);
	  if (temp.g_y > gl_normwin.g_y)
	    y = gl_normwin.g_y;		// shrinking upper window	//
	  else				// shrinking lower window	//
	    y = gl_normwin.g_y + gl_normwin.g_h + (gl_hbox / 2);
 	  wind_set(wh, WF_CXYWH, gl_normwin.g_x, y,
	  	   gl_normwin.g_w, gl_normwin.g_h);
	} // if //
					// already full, so change	//
					// back to previous		//
	else if ( rc_equal(&curr, &full) )
	  wind_set(wh, WF_CXYWH, prev.g_x, prev.g_y, prev.g_w, prev.g_h);
	  				// make it full			//
	else
	{
	  gl_whsiztop = wh;
	  wind_set(wh, WF_SIZTOP, full.g_x, full.g_y, full.g_w, full.g_h);
	}
} // do_wfull //
*/


/*
*	Open a directory, it may be the root or a subdirectory.
*/

WORD do_diropen(WNODE *pw, WORD new_win, WORD curr_icon, WORD drv, 
				BYTE *ppath, BYTE *pname, BYTE *pext, GRECT *pt,
				WORD redraw)
{
	WORD		ret;
	PNODE		*old_path, *tmp;

//	dbg("do_diropen: ppath=%s pname=%s pext=%s\n", ppath, pname, pext);	
						/* convert to hourglass	*/
	graf_mouse(HGLASS, 0);
						/* open a path node	*/
	tmp = pn_open(drv, ppath, pname, pext, F_SUBDIR);
	if ( tmp == NULL)
	{
 	  //dbg("do_diropen: pn_open failed; returning.\n");
	  graf_mouse(ARROW, 0);
	  return(FALSE);
	}
						/* activate path by 	*/
						/*   search and sort	*/
						/*   of directory	*/
	ret = pn_active(tmp);
	if ( ret != E_NOFILES )
	{
	  /*
	   * Do not replace a live window with a partially enumerated PNODE.
	   * E_NOFILES is the normal GEMDOS end-of-search result, including an
	   * empty directory; every other value is a real open failure.
	   */
	  DOS_AX = ret;
	  DOS_ERR = TRUE;
	  pn_close(tmp);
	  graf_mouse(ARROW, 0);
	  return(FALSE);
	}
	DOS_ERR = FALSE;
	old_path = new_win ? (PNODE *) 0 : pw->w_path;
	pw->w_path = tmp;
	if (old_path)
	  pn_close(old_path);
						/* set new name and info*/
						/*   lines for window	*/
	win_sname(pw);
	win_sinfo(pw);		// DESKTOP v1.2 reinstated 
	wind_setl(pw->w_id, WF_NAME, ADDR(&pw->w_name[0]));
	wind_setl(pw->w_id, WF_INFO, ADDR(&pw->w_info[0])); // DESKTOP v1.2
	
						/* do actual wind_open	*/
	if (curr_icon)
	{
		do_wopen(new_win, pw->w_id, curr_icon, 
				pt->g_x, pt->g_y, pt->g_w, pt->g_h);
		if (new_win)
	  		win_top(pw);
	}
						/* verify contents of	*/
						/*   windows object list*/
						/*   by building view	*/
						/*   and make it curr.	*/
	desk_verify(pw->w_id, TRUE);
	/*
	 * WIND_OPEN already asks the new window owner for its first repaint.
	 * Existing windows have no window-manager geometry event, so preserve the
	 * caller's original redraw choice for a directory change in place.
	 */
	if (redraw && !new_win)
	  fun_msg(WM_REDRAW, pw->w_id, pt->g_x, pt->g_y,
		  pt->g_w, pt->g_h);

	graf_mouse(ARROW, 0);
	return(TRUE);
} /* do_diropen */

/*
*	Open an application
*/

WORD do_aopen(ANODE *pa, WORD isapp, WORD curr, WORD drv, 
			  BYTE *ppath, BYTE *pname)
{
	WORD		ret, done;
	WORD		isgraf, isover, isparm, uninstalled, direct_app;
	BYTE		*ptmp, *pcmd, *ptail;
	BYTE		name[13];

	done = FALSE;
						/* set flags		*/
	isgraf = pa->a_flags & AF_ISGRAF;
#if MULTIAPP
	isover = (pa->a_flags & AF_ISFMEM) ? 2 : -1;
#else
	isover = (pa->a_flags & AF_ISFMEM) ? 2 : 1;
#endif
	isparm = pa->a_flags & AF_ISPARM;
	direct_app = isapp;
	uninstalled = ( (*pa->a_pappl == '*') ||
			(*pa->a_pappl == '?') ||
			(*pa->a_pappl == 0) );
						/* change current dir.	*/
						/*   to selected icon's	*/
	if (!pro_chdir(drv, ppath))
	{
	  (void) d_errmsg();
	  return(FALSE);
	}
						/* see if application	*/
						/*   was selected 	*/
						/*   directly or a 	*/
						/*   data file with an	*/
						/*   associated primary	*/
						/*   application	*/
	pcmd = ptail = NULLPTR;
	G.g_cmd[0] = G.g_tail[1] = 0;
	ret = TRUE;

	if ( (!uninstalled) && (!isapp) )
	{
						/* an installed	docum.	*/
	  pcmd = pa->a_pappl;
	  ptail = pname;
	  /* [JCE] If the app is DESKTOP.APP, then use the builtin file 
	   * viewer */
	  if (!strcmp(pcmd, "DESKTOP.APP"))
	  {
		return do_type(curr);
	  }
	}
	else
	{
	  if ( isapp )
	  {
						/* DOS-based app. has	*/
						/*   been selected	*/
	    if (isparm)
	    {
	      pcmd = &G.g_cmd[0];
	      ptail = &G.g_tail[1];
	      ret = opn_appl(pname, "\0", pcmd, ptail, sizeof(G.g_tail) - 1);
	    }
	    else
	      pcmd = pname;
	  } /* if isapp */
	  else
	  {
						/* DOS-based document 	*/
						/*   has been selected	*/
	    fun_alert(1, STNOAPPL);
	    ret = FALSE;
	  } /* else */
	} /* else */
						/* see if it is a 	*/
						/*   batch file		*/
	if (pcmd && wildcmp( ini_str(STGEMBAT), pcmd) )
	{
	  direct_app = FALSE;
						/* if is app. then copy	*/
						/*   typed in parameter	*/
						/*   tail, else it was	*/
						/*   a document installed*/
						/*   to run a batch file*/
	  strlcpy(G.g_1text, (isapp) ? &G.g_tail[1] : ptail, sizeof(G.g_1text));
	  ptmp = &name[0];
	  pname = pcmd;
	  while ( *pname != '.' )
	    *ptmp++ = *pname++;
	  *ptmp = 0;
	  ret = pro_cmd(&name[0], &G.g_1text[0], TRUE);
	  pcmd = &G.g_cmd[0];
	  ptail = &G.g_tail[1];
	} /* if */
	/*
	 * DOS kept one current directory for Desktop and the GEM/XM loader.
	 * ELKS correctly gives gemdesk and the resident gemaes owner separate
	 * process-local current directories.  Keep the original PROC_CREATE /
	 * PROC_RUN path, but pass it the selected application's absolute POSIX
	 * pathname instead of relying on DOS's shared-directory side effect.
	 *
	 * fpd_bldspec() is the existing bounded Desktop path builder.  It accepts
	 * the complete filename (including its dot), rejects embedded slashes,
	 * and fails before the 67-byte original PNODE limit can wrap or truncate.
	 */
	if (ret && direct_app)
	{
	  if (!fpd_bldspec(drv, ppath, pcmd, (BYTE *) "", G.g_srcpth))
	  {
	    fun_alert(1, STDEEPPA);
	    ret = FALSE;
	  }
	  else
	    pcmd = G.g_srcpth;
	}
						/* user wants to run	*/
						/*   another application*/
	if (ret)
	{
	  if ( (pcmd  !=  G.g_cmd)     && 
	       (pcmd  !=  NULL)  ) strlcpy(G.g_cmd, pcmd, sizeof(G.g_cmd));
	  if ( (ptail != &G.g_tail[1]) && 
	       (ptail !=  NULL)  ) strlcpy(G.g_tail+1, ptail, sizeof(G.g_tail)-1);
	  /*
	   * SHEL_FIND is the original GEM path service.  On ELKS its resident
	   * implementation validates absolute direct-click paths and resolves a
	   * bare DESKTOP.INF association through GEMSYS, GEMAPPS, then /bin.
	   * PROC_RUN therefore never receives a command relative to gemaes's cwd.
	   */
	  if (!shel_find(G.a_cmd))
	    fun_alert(1, STAPGONE);
	  else
	    done = pro_run(isgraf, isover, G.g_cwin, curr);
	} /* if ret */
#if MULTIAPP
	return(FALSE);				/* don't want to exit	*/
#else
	return(done);
#endif
} /* do_aopen */


/*
*	Open a disk
*/

WORD do_dopen(WORD curr)
{
	WNODE		*pw;

	pw = win_alloc(curr);
	if (pw)
	{
	  LPOBJ root_object;

	  root_object = desk_object_at(G.g_screen, pw->w_root);
	  /* A legacy disk icon now means the single POSIX root. */
	  if (!do_diropen(pw, TRUE, curr, 0, "/", "*", "",
			   (GRECT *)&root_object->ob_x, TRUE))
	  {
	    if (DOS_ERR)
	      (void) d_errmsg();
	    win_free(pw);
	  }
	}
	else
	{
	  fun_alert(1, STNOWIND);
	}
	return( FALSE );
}


/*
*	Open a folder
*/
/* Extra parameters not in DESKTOP v1.2 */
VOID do_fopen(WNODE *pw, WORD curr, WORD drv, 
	      BYTE *ppath, BYTE *pname, BYTE *pext, WORD chkall, WORD redraw)
{
	GRECT		t;
	WORD		ok;
	BYTE		*pnew, *parent;

//	dbg("do_fopen: drv=%d ppath=%s pname=%s pext=%s\n", drv, ppath, pname, pext);	
	ok = TRUE;
	pnew = ppath;
	wind_get(pw->w_id, WF_WXYWH, &t.g_x, &t.g_y, &t.g_w, &t.g_h);
	pro_chdir(drv, "");
	if (DOS_ERR)
	{
	  //dbg("do_fopen: pro_chdir() failed!\n", drv);
	  true_closewnd(pw);
	/* Elaborate checks not present in DESKTOP v1.2; bring them back
	 * for FreeGEM */
	  if ( DOS_AX == E_PATHNOTFND )
	  {
	    if (!chkall)
	    {
	      fun_alert(1, STDEEPPA);
	      //dbg("do_fopen: STDEEPPA 2\n");
	    }
	    else
	    {
	      pro_chdir(drv, "");
	      pnew = "";
	    }
	  } 
	  else
	    return;			
	} /* if DOS_ERR */
	else
	{
	  pro_chdir(drv, ppath);
	  if (DOS_ERR)
	  {
	    //dbg("do_fopen: DOS error %d!\n", DOS_AX);
	    if ( DOS_AX == E_PATHNOTFND )
	    {				/* DOS path is too long?	*/
	      if (chkall)
	      {
	        pro_chdir(drv, "");
		pnew = "";
	      }
	      else
	      {
	        fun_alert(1, STDEEPPA);
	    					/* back up one level	*/
			parent = ppath;
			while (*parent)
			  parent++;
			while (parent > ppath + 1 && parent[-1] == '/')
			  *--parent = 0;
			parent = scan_slsh(ppath);
			if (parent == ppath)
			{
			  ppath[0] = '/';
			  ppath[1] = 0;
			}
			else
			  *parent = 0;
			pname = "*";
			pext  = "";
			return;
	      } /* else */
	    } /* if DOS_AX */
	    else
	      return;			/* error opening disk drive	*/
	  } /* if DOS_ERR */
	} /* else */
/* Desktop 1 does this, but it screws up the
 * deep path error handling which is probably why it isn't in 
 * Desktop 3.
 *	if (!DOS_ERR)
	{
		ppath = "";
		pname = "*";
		pext  = "*";
	} */
	/* Again, DESKTOP v1.2 doesn't do all this checking */
	if (ok)
	{
	  ok = do_diropen(pw, FALSE, curr, drv, pnew, pname, pext, &t, redraw);
	  if ( !ok )
	  {
	    if (DOS_ERR)
	    {
	      (void) d_errmsg();
	      return;
	    }
	    fun_alert(1, STDEEPPA);
	    //dbg("do_fopen: Current path: %s\n", ppath);
	    					/* back up one level	*/
		    parent = ppath;
		    while (*parent)
		      parent++;
		    while (parent > ppath + 1 && parent[-1] == '/')
		      *--parent = 0;
		    parent = scan_slsh(ppath);
		    if (parent == ppath)
		    {
		      ppath[0] = '/';
		      ppath[1] = 0;
		    }
		    else
		      *parent = 0;
	    //dbg("do_fopen: Truncated path: %s\n", ppath);
	    if (!do_diropen(pw, FALSE, curr, drv, pnew, pname, pext,
			    &t, redraw) && DOS_ERR)
	      (void) d_errmsg();
	  }
	}
} /* do_fopen */


/*
*	Open an icon
*/

WORD do_open(WORD curr)
{
	WORD		done;
	ANODE		*pa;
	WNODE		*pw;
	FNODE		*pf;
	WORD		drv, isapp;
	BYTE		path[66], name[9], ext[4], *path_end;

	done = FALSE;

	pa = i_find(G.g_cwin, curr, &pf, &isapp);
	pw = win_find(G.g_cwin);
	if ( pf )
	  fpd_parse(&pw->w_path->p_spec[0],&drv,&path[0],&name[0],&ext[0]);

	if ( pa )
	{	
	  switch( pa->a_type )
	  {
	    case AT_ISFILE:
#if MULTIAPP
		if (!strcmp("DESKTOP.APP", &pf->f_name[0]))
		  break;
#endif
		done = do_aopen(pa,isapp,curr,drv,&path[0],&pf->f_name[0]);
		break;
	    case AT_ISFOLD:
	    /* No "New folder" in DESKTOP v1.2 
		if ( (pf->f_attr & F_FAKE) && pw )
		  fun_mkdir(pw);
		else*/
		{
		  if (!path[0])
		    strlcpy(path, "/", sizeof(path));
		  path_end = path;
		  while (*path_end)
		    path_end++;
		  if (path_end[-1] != '/')
		    strlcat(path, "/", sizeof(path));
		  /* No check in DESKTOP v1.2, but that's no reason to
		   * omit it from the FreeGEM version */
//		  dbg("do_open: path=%s pf->f_name=%s\n", path, pf->f_name);
		  if ( (strlen(&path[0]) + LEN_ZFNAME) >= (LEN_ZPATH-3) )
		  {
		    fun_alert(1, STDEEPPA);
//		    dbg("STDEEPPA 1\n");
		  }
		  else
		  {
		    strlcat(path, pf->f_name, sizeof(path));
		    pw->w_cvrow = 0;		/* reset slider		*/
//		    dbg("do_open: new path=%s\n", path);
		    do_fopen(pw, curr, drv, &path[0], &name[0],
		    	     &ext[0], FALSE, TRUE);
//		    dbg("do_open: do_fopen() returned\n");
		  }
		}
		break;
	    case AT_ISDISK:

/* DESKTOP v1.2 opens a new window here
		drv = (0x00ff & pa->a_letter);
		path[0] = 0;
		name[0] = ext[0] = '*';
		name[1] = ext[1] = 0;
		do_fopen(pw, curr, drv, &path[0], &name[0], &ext[0], FALSE, TRUE);*/
		do_dopen(curr);
		break;
		// DESKTOP v1.2: Trash
		case AT_ISTRSH:
		fun_alert(1, STNOOPEN);
		break;
	  }
	}

	return(done);
}



/*
*	Get information on an icon.
*/

WORD do_info(WORD curr)
{
	WORD		ret, junk;
	ANODE		*pa;
	WNODE		*pw;
	FNODE		*pf;
	LPTREE		tree;

	pa = i_find(G.g_cwin, curr, &pf, &junk);
	pw = win_find(G.g_cwin);

	if ( pa )
	{	
	  switch( pa->a_type )
	  {
	    case AT_ISFILE:
		ret = inf_file(&pw->w_path->p_spec[0], pf);
		if (ret)
		  fun_rebld(pw);
		break;
	    case AT_ISFOLD:
/* No fake folders in DESKTOP v1.2
		if (pf->f_attr & F_FAKE)
		{
		  tree = G.a_trees[ADTRINFO];
		  inf_show(tree, 0);
		  tree[TRINFOK].ob_state = NORMAL;
		}
		else */
		  inf_folder(&pw->w_path->p_spec[0], pf);
		break;
	    case AT_ISDISK:
		// DESKTOP v2.x+ version inf_disk( pf->f_junk );
		junk = (get_spec(G.g_screen, curr)->ib_char) & 0xFF;
		inf_disk(junk);
		break;
		// Trashcan in DESKTOP v1.2
		case AT_ISTRSH:
		tree = G.a_trees[ADTRINFO];
		inf_show(tree, 0);
		tree[TRINFOK].ob_state = NORMAL;
		break;
	  }
	}
	return( FALSE );
}

#if MC68K

/* don't need this routine */

#else

/*
*	This routines purpose is to format a disk by execing a
*	FORMAT.COM above us in memory.  Unfortunately, the ROM BIOS
*	has a bug of using the contents of FORMAT's PSP while doing
*	a Disk Verify function using INT 13h.  This forces us to 
*	place the FORMAT we exec into a safe location in memory.
*	The safe location is an address with segment values between
*	x00x and xEDx. We fudge this on both side by 400 paragraphs.
*	Thanks alot, Bill and Phil.
*/
/*	The MULTIAPP version of this routine is closely tied to the	*/
/*	routine pro_chcalc() in DESKPRO.C.  The high and low memory	*/
/*	boundaries have to be jimmied to force the channel allocator	*/
/*	to put FORMAT in the right place.				*/

	VOID
romerr(curr)
	WORD		curr;
{

#if MULTIAPP
	/*
	 * The ELKS port is deliberately single-tasking GEM.  The original XM
	 * channel allocator depends on DOS paragraph addresses and is not part of
	 * this target; a MULTIAPP build must provide a separate word-pair port.
	 */
	(void) curr;

#else
	UWORD		seg;
	LPVOID		testform;
	UWORD		lavail;

	lavail = dos_avail_word();
	testform = dos_alloc_word(lavail);
	seg = FP_SEG(testform);
	dos_free(testform);
	testform = (LPVOID) 0;
	if ( ((seg << 4) & 0xff00) >= 0xe900)
	  testform = dos_alloc_word(0x1b00U);

	pro_run(FALSE, 0, -1, curr);

	if (testform)
	  dos_free(testform);

#endif
} /* romerr */

#endif

/*
*	Format the currently selected disk.
*/
	VOID
do_format(curr)
	WORD		curr;
{
	WORD		junk, ret, foundit;
	BYTE		msg[6];
	ANODE		*pa;
	FNODE		*pf;

	pa = i_find(G.g_cwin, curr, &pf, &junk);

#if defined(ELKS) && ELKS
	/*
	 * Icon art is cosmetic on ELKS.  The only desktop disk ANODE denotes
	 * the mounted POSIX root, so reject Format before touching the FNODE
	 * pointer (desktop objects intentionally have no FNODE).
	 */
	if (pa && pa->a_type == AT_ISDISK)
	{
	  fun_root_format_alert();
	  return;
	}
#endif

	if ( (pa) && (pa->a_type == AT_ISDISK) )
	{
	  msg[0] = pf->f_junk ;
	  msg[1] = 0;
	  ret = fun_alert(2, STFORMAT, msg);
	  strlcpy(msg + 1, ":", 5);
	  if (ret == 1)
	  {
#if defined(ELKS) && ELKS
	    /*
	     * FORMAT.COM, PSP relocation, and DOS/keyboard/video ownership do not
	     * exist on ELKS.  Preserve Format Disk as a synchronous native block
	     * operation: gemdesk_posix_format() maps only A/B to /dev/fd0/fd1,
	     * execs /bin/mkfs directly, and waits for the kernel-owned child.
	     */
	    graf_mouse(HOURGLASS, 0);
	    if (!gemdesk_posix_format((WORD) msg[0]))
	      form_alert(1, "[3][Unable to format the selected |"
			"ELKS floppy device][ OK ]");
	    else
	      do_chkall(TRUE);
#elif MC68K
	    ret = pro_cmd( ini_str(STDKFORM), &msg[0], TRUE);
	    if (ret)
	      done = pro_run(FALSE, FALSE, G.g_cwin, curr);
#else
	    strlcpy(G.g_cmd, ini_str(STDKFRM1), sizeof(G.g_cmd));
	    foundit = shel_find(G.a_cmd);
	  /* Not in DESKTOP v1.2  if (!foundit)
	    {
	      strlcpy( G.g_cmd, ini_str(STDKFRM2), sizeof(G.g_cmd));
	      foundit = shel_find(G.a_cmd);
	    }*/
	    if (foundit)
	    {
	      strlcpy(G.g_tail + 1, msg, sizeof(G.g_tail)-1);

	      takedos();
	      takekey();
	      takevid();

	      romerr(curr);
	      givevid();
	      givekey();
	      givedos();
	    } /* if */
	    else
	    /* Not in DESKTOP v1.2  fun_alert(1, STNOFRMT); */
#endif
	    graf_mouse(ARROW, 0);
	  } /* if ret */
	} /* if */
} /* do_format */

/*
*	Routine to check the all windows directory by doing a change
*	disk/directory to it and redrawing the window;
*/
	VOID
do_chkall(redraw)
	WORD		redraw;
{
	WORD		ii;
	WORD		drv;
	BYTE		path[66], name[9], ext[4];
	WNODE		*pw;
	for(ii = 0; ii < NUM_WNODES; ii++)
	{
	  pw = &G.g_wlist[ii];
	  if (pw->w_id)
	  {
	    fpd_parse(&pw->w_path->p_spec[0], &drv, &path[0],
	    	      &name[0], &ext[0]);
	    do_fopen(pw, 0, drv, &path[0], &name[0], &ext[0], TRUE, redraw);
	  }
	  else
	  {
	    desk_verify(0, TRUE);

	  }
	}
} /* do_chkall */


WORD alert_s(WORD defbut, WORD alert_num, BYTE *s)
{
	char tmp[256];
	WORD ret;
	UWORD in_index;
	UWORD out_index;
	UWORD string_index;
	GEM_SLOT_BYTE_POINTER found_string;

	rsrc_gaddr(R_STRING, alert_num, (LPVOID *)&found_string);
	lstlcpy((BYTE *) tmp, found_string, sizeof(tmp));
	/*
	 * The original Desktop used sprintf() here, but every retained caller only
	 * needs the literal "%s" form.  Keeping that one bounded substitution avoids
	 * ELKS stdio's formatter, division helper, and wide arithmetic in this
	 * 8086-small-model client.  Text is truncated at the destination boundary
	 * and is always terminated; no length, index, or intermediate exceeds one
	 * 16-bit word.
	 */
	in_index = 0;
	out_index = 0;
	while (tmp[in_index]
	       && out_index + 1U < (UWORD) sizeof(G.g_2text)) {
		if (tmp[in_index] == '%' && tmp[in_index + 1U] == 's') {
			string_index = 0;
			while (s[string_index]
			       && out_index + 1U
			          < (UWORD) sizeof(G.g_2text)) {
				G.g_2text[out_index++] = s[string_index++];
			}
			in_index += 2U;
		} else {
			G.g_2text[out_index++] = tmp[in_index++];
		}
	}
	G.g_2text[out_index] = '\0';
	ret = form_alert(defbut, G.g_2text);
	return ret;
}


WORD menu_item_to_alert_s( WORD def_but, WORD alert_num, WORD item )
{
OBJECT far	*ptrItem;
char *		ptr ;
GEM_TYPED_SLOT_POINTER slot;

    /* get a local copy of the menu item text */
    ptrItem = desk_object_at(G.a_trees[ADMENU], item);
    slot.words = ptrItem->ob_spec;
    lstlcpy((BYTE *) G.g_1text, slot.bytes, sizeof(G.g_1text));

    /* remove leading spaces, strip out underbars and ellipses */
    ptr = G.g_1text + strspn( G.g_1text, " " ) ;
    memmove( (void *)G.g_1text, (void *)ptr, strlen( G.g_1text ) ) ;
    
    ptr = strchr( G.g_1text, '_' ) ;
    if ( ptr )
	memmove( (void *)ptr, (void *)(ptr+1), strlen( G.g_1text ) ) ;
	
    ptr = strchr( G.g_1text, '.' ) ;
    if ( ptr )
	*ptr = '\0' ;
    
    return( alert_s( def_but, alert_num, G.g_1text ) ) ;
    
} /* menu_item_to_alert_s() */

