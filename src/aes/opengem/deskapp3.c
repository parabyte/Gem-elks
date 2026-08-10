/*	 DESKAPP.C	06/11/84 - 07/11/85		Lee Lorenzen	*/
/*	for 3.0		3/6/86   - 5/6/86		MDF		*/
/*	for 2.3		9/25/87				mdf		*/

/*
*       Copyright 1999, Caldera Thin Clients, Inc.                      
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

#define MIN_WINT 4
#define MIN_HINT 2

/*
*	Reverse list when we write so that we can read it in naturally
*/
VOID app_revit()
{
	ANODE		*pa;
	ANODE		*pnxtpa;
						/* reverse list		*/
	pa = G.g_ahead;
	G.g_ahead = (ANODE *) NULL;
	while(pa)
	{
	  pnxtpa = pa->a_next;
	  pa->a_next = G.g_ahead;
	  G.g_ahead = pa;
	  pa = pnxtpa;
	}
}


/*
*	Save the current state of all the icons to a file called 
*	DESKTOP.INF
*/
WORD app_save(WORD todisk)
{
	WORD		i, fh, ret;
	UWORD		envr;
	BYTE		*pcurr, *ptmp;
	ANODE		*pa;
	WSAVE		*pws;

	memset(gl_afile, 0, SIZE_AFILE);
	pcurr = &gl_afile[0];
						/* save evironment	*/
	*pcurr++ = '#';
	*pcurr++ = 'E';
	envr = 0x0;
	envr |= (G.g_cnxsave.vitem_save) ? 0x80 : 0x00;
	envr |= ((G.g_cnxsave.sitem_save) << 5) & 0x60;
	envr |= (G.g_cnxsave.cdele_save) ? 0x10 : 0x00;
	envr |= (G.g_cnxsave.ccopy_save) ? 0x08 : 0x00;
	envr |= G.g_cnxsave.cdclk_save;
	pcurr = save_2(pcurr, envr);
/* Not in DESKTOP v1.2; bring them back */
	envr = (G.g_cnxsave.covwr_save)  ? 0x00 : 0x10;
	envr |= (G.g_cnxsave.cmclk_save) ? 0x08 : 0x00;
	envr |= (G.g_cnxsave.cdtfm_save) ? 0x00 : 0x04;
	envr |= (G.g_cnxsave.ctmfm_save) ? 0x00 : 0x02;
	envr |= desk_sound(FALSE, -1, 0) ? 0x00 : 0x01;
	pcurr = save_2(pcurr, envr );
	envr  = 0;
	envr |= (G.g_cnxsave.cdetd_save) ? 0x00 : 0x04;
	envr |= (G.g_cnxsave.cdetn_save) ? 0x00 : 0x08;
	pcurr = save_2(pcurr, envr );
	*pcurr++ = 0x0d;
	*pcurr++ = 0x0a;
						/* save windows		*/
	for(i=0; i<NUM_WNODES; i++)
	{
	  *pcurr++ = '#';
	  *pcurr++ = 'W';
	  pws = (WSAVE *)desk_byte_index((BYTE *)&G.g_cnxsave.win_save[0],
					 (UWORD)i, (UWORD)sizeof(WSAVE));
	  pcurr = save_2(pcurr, pws->hsl_save);
	  pcurr = save_2(pcurr, pws->vsl_save);
	  pcurr = save_2(pcurr, desk_word_divide(pws->x_save, gl_wchar,
							 NULL));
	  pcurr = save_2(pcurr, desk_word_divide(pws->y_save, gl_hchar,
							 NULL));
	  pcurr = save_2(pcurr, desk_word_divide(pws->w_save, gl_wchar,
							 NULL));
	  pcurr = save_2(pcurr, desk_word_divide(pws->h_save, gl_hchar,
							 NULL));
	  pcurr = save_2(pcurr, pws->obid_save);
	  ptmp = &pws->pth_save[0];
	  *pcurr++ = ' ';
	  if (*ptmp != '@')
	  {
	    while (*ptmp)
	      *pcurr++ = *ptmp++;
	  }
	  *pcurr++ = '@';
	  *pcurr++ = 0x0d;
	  *pcurr++ = 0x0a;
	}		
#if MULTIAPP
	for (i=0; i<3; i++)
	  if (gl_caccs[i].acc_name[0])
	  {
	    *pcurr++ = '#';
	    *pcurr++ = 'A';
	    pcurr = save_2(pcurr, gl_caccs[i].acc_swap);
	    *pcurr++ = ' ';
	    pcurr = save_str(pcurr, &(gl_caccs[i].acc_name[0]));
	    pcurr--;
	    *pcurr++ = 0x0d;
	    *pcurr++ = 0x0a;
	  }
#endif	
						/* reverse ANODE list	*/
	app_revit();
						/* save ANODE list	*/
	for(pa=G.g_ahead; pa; pa=pa->a_next)
	{
	  /* From BALJ's desktop: Don't save autodetected drives 
	   * (why not, though?)
          if ( (pa->a_flags >= AF_WASDET) && todisk )
		              continue; */
	  *pcurr++ = '#';
	  switch(pa->a_type)
	  {
	    case AT_ISDISK:
		*pcurr++ = 'M';
		break;
	    case AT_ISFILE:
		if ( (pa->a_flags & AF_ISCRYS) &&
		     (pa->a_flags & AF_ISGRAF) )
		  *pcurr++ = 'G';
		else
		{  
		  *pcurr = (pa->a_flags & AF_ISPARM) ? 'P' : 'F';
		  if (pa->a_flags & AF_ISFMEM)
		    *pcurr += 'a' - 'A';
		  pcurr++;
		}
		break;
	    case AT_ISFOLD:
		*pcurr++ = 'D';
		break;
		
		case AT_ISTRSH:	// DESKTOP v1.2
		*pcurr++ = 'T';
		break;
	  }
	  
	  if (pa->a_flags & AF_ISDESK)
	  {
	    pcurr = save_2(pcurr, desk_word_divide(pa->a_xspot, G.g_icw,
							 NULL));
	    pcurr = save_2(pcurr, desk_word_divide(
					pa->a_yspot - G.g_ydesk, G.g_ich, NULL));
	  }
	  pcurr = save_2(pcurr, pa->a_aicon);
	  pcurr = save_2(pcurr, pa->a_dicon);
	  *pcurr++ = ' ';
	  if (pa->a_flags & AF_ISDESK)
	  {
	    *pcurr++ = pa->a_letter ? pa->a_letter : ' ';
	    *pcurr++ = ' ';
	  }
	  pcurr = save_str(pcurr, pa->a_pappl);
	  pcurr = save_str(pcurr, pa->a_pdata);
	  pcurr--;
#if MULTIAPP
	  if (!(pa->a_flags & AF_ISDESK))	/* only for apps	*/
	  {
	    pcurr++;				/* leave blank		*/
	    pcurr = save_memsz(pcurr, pa->a_memreq);
	  }
#endif
	  *pcurr++ = 0x0d;
	  *pcurr++ = 0x0a;
						/* skip standards	*/
//	  if ( (pa->a_type == AT_ISDISK) && 
//	       (pa->a_next->a_type != AT_ISDISK) )
	  if ( (pa->a_type == AT_ISTRSH))	/* DESKTOP v1.2 */
	  {
	    for(i=0; i<6; i++)
	      pa = pa->a_next;
	  }
	}
	*pcurr++ = 0x1a;
	*pcurr++ = 0x0;
						/* reverse list back	*/
	app_revit();
						/* calculate size 	*/
	G.g_afsize = pcurr - &gl_afile[0];
						/* save in memory	*/
	shel_put(ADDR(&gl_afile[0]), G.g_afsize);
						/* save to disk		*/
	if (todisk)
	{
	  G.g_afsize--;
	  fh = 0;
	  while (!fh)
	  {
	    fh = app_getfh(FALSE, ini_str(STGEMAPP), 0x0);
	    if (!fh)
	    {
	      ret = fun_alert(1, STNOINF);
	      if (ret == 2)
	        return(FALSE);
	    }
	  }
	  G.g_afsize = dos_write_word(fh, (UWORD) G.g_afsize,
				      ADDR(&gl_afile[0]));
	  dos_close(fh);
	}
	return TRUE;
}


/*
*	Build the desktop list of objects based on this current 
*	application list.
*/

BYTE app_blddesk()
{
	WORD		obid;
	GEM_U32_WORDS	bvdisk, bvhard, bvect;
	ANODE		*pa;
	LPOBJ		pob;
	ICONBLK		*pic;
#if ALCYON
	GEM_U32_WORDS	*ptr;
#endif						/* free all this windows*/
						/*   kids and set size	*/
	obj_wfree(DROOT, 0, 0, gl_width, gl_height);
#if ALCYON
	ptr = &global[3];
	G.g_screen[DROOT].ob_spec = LLGET(ptr);
#else
	G.g_screen[DROOT].ob_spec =
		gem_u32_words((UWORD) global[3], (UWORD) global[4]);
#endif
	// FreeGEM only...
	G.g_screen[DROOT].ob_flags = USECOLORCAT;
	
	bvdisk = gem_u32_words(0, 0);
	bvhard = gem_u32_words(0, 0);

	for(pa = G.g_ahead; pa; pa = pa->a_next)
	{
	  if (pa->a_flags & AF_ISDESK)
	  {
		    obid = obj_ialloc(DROOT, pa->a_xspot, pa->a_yspot,
						G.g_wicon, G.g_hicon);
		    if (!obid)
		      break;
						/* set up disk vector	*/
	    if (pa->a_type == AT_ISDISK)
	    {
		  WORD drive_index;

		  bvect = gem_u32_words(0, 0);
		  drive_index = pa->a_letter - 'A';
		  if (drive_index >= 0 && drive_index < 32)
		  {
		    bvect.hi = 0x8000U;
		    while (drive_index--)
		    {
		      bvect.lo = (UWORD) ((bvect.lo >> 1)
					 | ((bvect.hi & 1U) ? 0x8000U : 0));
		      bvect.hi >>= 1;
		    }
		  }
		  //dbg("%c -> %lx\n", pa->a_letter, bvect);
	      bvdisk.lo |= bvect.lo;
	      bvdisk.hi |= bvect.hi;
	      if (pa->a_aicon == IG_HARD)
	      {
		bvhard.lo |= bvect.lo;
		bvhard.hi |= bvect.hi;
	      }
	    }
						/* remember it		*/
	    pa->a_obid = obid;
						/* build object		*/
	    pob = desk_object_at(G.g_screen, obid);
	    pob->ob_state = NORMAL;
	    pob->ob_flags = DESK_ITEM_FLAGS(obid, NONE);
	    pob->ob_type = G_ICON;
	    G.g_index[obid] = pa->a_aicon;
	    pic = (ICONBLK *)desk_byte_index((BYTE *)&gl_icons[0],
					  (UWORD)obid,
					  (UWORD)sizeof(ICONBLK));
	    pob->ob_spec = gem_near_pointer_words(pic);
	    memcpy(pic, desk_byte_index((BYTE *)&G.g_iblist[0],
					(UWORD)pa->a_aicon,
					(UWORD)sizeof(ICONBLK)),
		   sizeof(ICONBLK));
	    pic->ib_xicon = desk_word_divide(G.g_wicon - pic->ib_wicon,
						 2, NULL);
	    pic->ib_ptext = gem_near_pointer_words(pa->a_pappl);
#if !defined(ELKS) || !ELKS
	    pic->ib_char |= (0x00ff & pa->a_letter);
#endif
	  } /* if */
	} /* for */

	if ((gl_xbuf.abilities.lo & ABLE_XBVSET) || global[0] >= 0x400)
	{
		appl_xbvset(bvdisk, bvhard);
	}
	else appl_bvset(bvdisk.hi, bvhard.hi);
	
	return( get_defdrv(bvdisk, bvhard) );
} /* app_blddesk */


/*
*	Find the ANODE that is appropriate for this object.
*/
ANODE *app_afind(WORD isdesk, WORD atype, WORD obid, BYTE *pname, WORD *pisapp)
{
	ANODE		*pa;

	for(pa = G.g_ahead; pa; pa = pa->a_next)
	{
	  if (isdesk)
	  {
	    if (pa->a_obid == obid)
	      return(pa);
	  }
	  else
	  {
	    if ( (pa->a_type == atype) && !(pa->a_flags & AF_ISDESK) )
	    {
	      if ( wildcmp(pa->a_pdata, pname) )
	      {
		*pisapp = FALSE;
		return(pa);
	      }
	      if ( wildcmp(pa->a_pappl, pname) )
	      {
		*pisapp = TRUE;
		return(pa);
	      } /* if */
	    } /* if */
	  } /* else */
	} /* for */
	return(0);
} /* app_afind */

/************************************************************************/
/* g e t _ d e f d r v							*/
/************************************************************************/
BYTE get_defdrv(GEM_U32_WORDS dr_exist, GEM_U32_WORDS dr_hard)
{
/* this routine returns the drive letter of the lowest drive: lowest	*/
/* lettered hard disk if possible, otherwise lowest lettered floppy	*/
/* (which is usually A)							*/
/* in dr_exist, MSbit = A						*/
	GEM_U32_WORDS	mask, hd_disk;
	WORD		ii;
	BYTE		drvletr;

	mask = gem_u32_words(0, 0x8000U);
	hd_disk.lo = dr_exist.lo & dr_hard.lo;
	hd_disk.hi = dr_exist.hi & dr_hard.hi;
	drvletr = 'A';
	if (hd_disk.lo || hd_disk.hi)
	{		/* there's a hard disk out there somewhere	*/
	  for (ii = 0; ii <= 31; ii++)
	  {
	    if ((mask.lo & hd_disk.lo) || (mask.hi & hd_disk.hi))
	    {
	      drvletr = ii + 'A';
	      break;
	    } /* if */
	    mask.lo = (UWORD) ((mask.lo >> 1)
			       | ((mask.hi & 1U) ? 0x8000U : 0));
	    mask.hi >>= 1;
	  } /* for */
	} /* if hd_disk */
	return(drvletr);
} /* get_defdrv */
