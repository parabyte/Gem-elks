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

#if defined(ELKS) && ELKS
/*
 * ST1STD through ST3STD are the generic file, directory, and native *.APP
 * defaults.  ST4STD through ST6STD install DOS executable, command, and batch
 * suffixes; they must not shadow explicit native DESKTOP.INF associations.
 */
#define DESK_STANDARD_ASSOCIATION_COUNT 3
#else
#define DESK_STANDARD_ASSOCIATION_COUNT 6
#endif

#if defined(ELKS) && ELKS
#include <unistd.h>
#endif

#define MIN_WINT 4
#define MIN_HINT 2
#define DISK_ICONBLK_SIZE 34

#if defined(ELKS) && ELKS
static BYTE app_icon_failure_stage = '?';

static WORD
app_icon_fail(BYTE stage)
{
	app_icon_failure_stage = stage;
	return FALSE;
}
#else
#define app_icon_fail(stage) FALSE
#endif

/* ICONBLK is the exact packed 34-byte original GEM record. */
#define APP_ICON_AT(icons, index) \
	((LPICON)desk_byte_index((BYTE *)(icons), (UWORD)(index), \
				 (UWORD)sizeof(ICONBLK)))

MLOCAL WORD
icon_offset(UWORD index, UWORD stride, UWORD *offset)
{
	GEM_U32_WORDS product;

	product = gem_u32_mul_u16(index, stride);
	if (product.hi)
	  return FALSE;
	*offset = product.lo;
	return TRUE;
}

/* [JCE] All ALCYON bits commented out, as they are no longer 
 *      going to compile or work. */

/************************************************************************/
/* a p p _ r d i c o n							*/
/************************************************************************/
	WORD
app_rdicon()
{
	UWORD		temp;
	GEM_U32_WORDS	product;
	LPBYTE		stmp, dtmp;
	WORD		handle, length, i, iwb, ih;
	WORD		num_icons, num_masks, last_icon, num_wds, 
			num_bytes, msk_bytes, tmp;
/*#if ALCYON
	WORD		stlength, ret;
	BYTE		*fixup, *poffset, **namelist;
#endif*/
	WORD		fixup, poffset;
							/* open the file	*/
	handle = app_getfh(TRUE,
		ini_str( (gl_height <= 300) ? STGEMLOI : STGEMHIC), 0);
	if (!handle)
	  return app_icon_fail('O');
						/* how much to read?	*/
	length = NUM_IBLKS * DISK_ICONBLK_SIZE;
/*#if ALCYON
	ret = dos_read(handle, 2, ADDR(&stlength));
#endif*/
	dos_read_word(handle, 2, (LPBYTE)&gl_iconstart);
	dos_read_word(handle, 2, (LPBYTE)&poffset);

	/* gl_iconstart = offset of icon names table relative to
	 * compile address of icon file 
	 *
	 * poffset = compile address of icon file
     *
     * Or, in shorthand:
     * gl_iconstart = filebase + offset(names)
     * poffset      = filebase
     */

	/* Add the offset of the icon bitmaps to poffset */
	poffset += 4 + length;

	/* gl_iconstart = filebase + offset(names)
	   poffset      = filebase + offset(bitmaps)
	   */
	gl_iconstart -= poffset;
	/* gl_iconstart = offset(names) - offset(bitmaps)
	 *              = length of the bitmap & string tables. 
	 *                Brilliant! */
	
	/*
	 * ICONBLK is deliberately packed to the original 34-byte GEM disk
	 * layout.  ELKS on an 8086 is little-endian, and its three four-byte
	 * pointer slots are stored as explicit low/high word pairs.  Read the
	 * original records in place: there is no converted asset, temporary
	 * record array, or target-side field marshalling in this path.
	 */
	dos_read_word(handle, (UWORD) length, (LPBYTE)&G.g_idlist[0]);
	memcpy(&G.g_iblist[0], &G.g_idlist[0], NUM_IBLKS * sizeof(ICONBLK));
	
	/* find no. of icons	*/
	/*   actually used	*/
	num_icons = last_icon = 0;
	while ( (last_icon < NUM_IBLKS) &&
		!(APP_ICON_AT(&G.g_idlist[0], last_icon)->ib_pmask.lo
			== 0xffffU &&
		  APP_ICON_AT(&G.g_idlist[0], last_icon)->ib_pmask.hi
			== 0xffffU) )
	{
	  LPICON idicon;

	  idicon = APP_ICON_AT(&G.g_idlist[0], last_icon);
	  tmp = wmax(idicon->ib_pmask.lo, idicon->ib_pdata.lo);
	  num_icons = wmax(num_icons, tmp);
	  last_icon++;
	}
	num_icons++;
						/* how many words of 	*/
						/*   data to read?	*/
						/* assume all icons are	*/
						/*   same w,h as first	*/
	product = gem_u32_mul_u16(G.g_idlist[0].ib_wicon,
				  G.g_idlist[0].ib_hicon);
	if (product.hi)
	  return app_icon_fail('G');
	num_wds = product.lo >> 4;
	num_bytes = num_wds << 1;

						/* allocate some memory	*/
						/*   in bytes     	*/
				/* gl_iconstart = size of icon bit blocks	*/
				/*   and strings on Lattice C		*/
				/* NUM_NAMICS is for string pointers	*/
				/* stlength is strings on ALCYON C	*/
/*#if ALCYON
	length = gl_iconstart + ((NUM_NAMICS + 1) * 4) + stlength;
#else*/
	length = gl_iconstart + (NUM_NAMICS << 1);
/*#endif*/
	/* Load the icon bitblocks & strings */
	G.a_datastart = dos_alloc_word((UWORD) length);
	if (!G.a_datastart)
	  return app_icon_fail('D');
						/* read it		*/
	dos_read_word(handle, (UWORD) length, G.a_datastart);
	dos_close(handle);
						/* fix up str ptrs	*/
	gl_numics = 0;
/*
#if ALCYON
	namelist = (BYTE *) G.a_datastart + gl_pstart;
	for (i = 0; i < NUM_NAMICS; i++)
	{
	  fixup = *namelist - poffset;
	  *namelist++ = fixup;
#endif
*/

	for (i = 0; i < NUM_NAMICS; ++i)
	{
		LPBYTE pv = G.a_datastart + gl_iconstart + i * 2;

		fixup = *((UWORD *)pv) - poffset;
		*((UWORD *)pv) = (UWORD)fixup;

		if (fixup >= 0 && fixup < gl_iconstart && G.a_datastart[fixup])
		  ++gl_numics;
	}
						/* figure out which are	*/
						/*   mask & which data	*/
	for (i=0; i<last_icon; i++)
	{
	  LPICON idicon;

	  idicon = APP_ICON_AT(&G.g_idlist[0], i);
	  G.g_ismask[idicon->ib_pmask.lo] = TRUE;
	  G.g_ismask[idicon->ib_pdata.lo] = FALSE;
	}
						/* fix up mask ptrs	*/
	num_masks = 0;
	for (i=0; i<num_icons; i++)
	{
	  if (G.g_ismask[i])
	  {
	    G.g_ismask[i] = num_masks;
	    num_masks++;
	  }
	  else
	    G.g_ismask[i] = -1;
	}
						/* allocate memory for	*/
						/*   transformed mask	*/
						/*   forms		*/
	if (!icon_offset((UWORD) num_masks, (UWORD) num_bytes, &temp))
	  return app_icon_fail('M');
	msk_bytes = temp;
	G.a_buffstart = dos_alloc_word((UWORD) msk_bytes);
	if (!G.a_buffstart)
	  return app_icon_fail('B');
						/* fix up icon pointers	*/
	for (i=0; i<last_icon; i++)
	{
	  LPICON idicon;
	  LPICON workicon;

	  idicon = APP_ICON_AT(&G.g_idlist[0], i);
	  workicon = APP_ICON_AT(&G.g_iblist[0], i);
						/* first the mask	*/
	  if (!icon_offset((UWORD) G.g_ismask[idicon->ib_pmask.lo],
			   (UWORD) num_bytes, &temp))
	    return app_icon_fail('P');
	  workicon->ib_pmask =
		gem_near_pointer_words(G.a_buffstart + temp);
	  if (!icon_offset(idicon->ib_pmask.lo,
			   (UWORD) num_bytes, &temp))
	    return app_icon_fail('Q');
	  idicon->ib_pmask =
		gem_near_pointer_words(G.a_datastart + temp);
						/* now the data		*/
	  if (!icon_offset(idicon->ib_pdata.lo,
			   (UWORD) num_bytes, &temp))
	    return app_icon_fail('R');
	  idicon->ib_pdata =
		gem_near_pointer_words(G.a_datastart + temp);
	  workicon->ib_pdata = idicon->ib_pdata;
						/* now the text ptrs	*/
	  idicon->ib_ytext = workicon->ib_ytext =
			G.g_idlist[0].ib_hicon;
	  idicon->ib_wtext = workicon->ib_wtext =
		desk_word_multiply(12, gl_wschar);
	  idicon->ib_htext = workicon->ib_htext = gl_hschar + 2;
	}
						/* transform forms	*/
	iwb = (WORD)desk_uword_divide(G.g_idlist[0].ib_wicon, 8, NULL);
	ih = G.g_idlist[0].ib_hicon;

	for (i=0; i<num_icons; i++)
	{
	  if (G.g_ismask[i] != -1)
	  {
						/* preserve standard	*/
						/*   form of masks	*/
	    if (!icon_offset((UWORD) i, (UWORD) num_bytes, &temp))
	      return app_icon_fail('S');
	    stmp = G.a_datastart + temp;
	    if (!icon_offset((UWORD) G.g_ismask[i], (UWORD) num_bytes,
			     &temp))
	      return app_icon_fail('T');
	    dtmp = G.a_buffstart + temp;
	    /* Icon files store 16-bit words; host WORD is wider on POSIX. */
	    LBCOPY(dtmp, stmp, num_bytes);
	  }
	  else
	  {
						/* transform over std.	*/
						/*   form of datas	*/
	    if (!icon_offset((UWORD) i, (UWORD) num_bytes, &temp))
	      return app_icon_fail('U');
	    dtmp = G.a_datastart + temp;
	  }
	  gsx_trans(dtmp, iwb, dtmp, iwb, ih);
	}
#if defined(ELKS) && ELKS
	/*
	 * Named icon 27 is the historical DR DOS application/document pair.
	 * Keep the original ICN bytes and their parsed idlist untouched, but make
	 * the live ELKS working list use GEM's original tool pair instead.  This
	 * is two fixed 34-byte metadata copies during startup; drawing still reads
	 * the already transformed original bitmap data with no conversion layer.
	 */
	if (last_icon > (ID_GENERIC + 27))
	{
	  memcpy(&G.g_iblist[IA_GENERIC + 27], &G.g_iblist[IA_TOOL],
		 sizeof(ICONBLK));
	  memcpy(&G.g_iblist[ID_GENERIC + 27], &G.g_iblist[ID_TOOL],
		 sizeof(ICONBLK));
	}
#endif
/* Move to before splash screen 	app_tran(0); */
/* DESKTOP v1.2 does not frob the icon masks like this
	for (i=0; i<last_icon; i++)
	{
	  if ( i == IG_FOLDER )
	    G.g_iblist[i].ib_pmask = G.g_iblist[IG_TRASH].ib_pmask;
	  if ( ( i == IG_FLOPPY ) ||
	       ( i == IG_HARD ) )
	    G.g_iblist[i].ib_pmask = G.g_iblist[IG_TRASH].ib_pdata;
	  if ( (i >= IA_GENERIC) &&
	       (i < ID_GENERIC) )
	    G.g_iblist[i].ib_pmask = G.g_iblist[IA_GENERIC].ib_pdata;
	  if ( (i >= ID_GENERIC) &&
	       (i < (NUM_ANODES - 1)) )
	    G.g_iblist[i].ib_pmask = G.g_iblist[ID_GENERIC].ib_pdata;
	}*/
	return(TRUE);
} /* app_rdicon */


MLOCAL WORD app_start1()
{
	WORD fh, i;
						/* remember start drive	*/
	gl_stdrv = dos_gdrv();

	G.g_pbuff = &gl_buffer[0];
	
	for(i=NUM_ANODES - 2; i >= 0; i--)
	  G.g_alist[i].a_next = &G.g_alist[i + 1];
	G.g_ahead = (ANODE *) NULL;
	G.g_aavail = &G.g_alist[0];
	G.g_alist[NUM_ANODES - 1].a_next = (ANODE *) NULL;

	shel_get(ADDR(&gl_afile[0]), SIZE_AFILE);
	if (gl_afile[0] != '#')
	{
						/* invalid signature	*/
						/*   so read from disk	*/
	  fh = app_getfh(TRUE, ini_str(STGEMAPP), 0x0);
	  if (!fh)
	    return(FALSE);
	  G.g_afsize = dos_read_word(fh, SIZE_AFILE, ADDR(&gl_afile[0]));
	  dos_close(fh);
	  gl_afile[G.g_afsize] = 0;
	}
	return TRUE;
}

MLOCAL WORD app_start2()
{
	WORD xcnt, ycnt, xcent, x, y;
	WORD remainder;
	ANODE *pa;

	G.g_wicon = (12 * gl_wschar) + (2 * G.g_idlist[0].ib_xtext);
	G.g_hicon = G.g_idlist[0].ib_hicon + gl_hschar + 2;

	G.g_icw = (gl_height <= 300) ? 0 : 8;
	G.g_icw += G.g_wicon;
	xcnt = desk_word_divide(gl_width, G.g_icw, &remainder);
	G.g_icw += desk_word_divide(remainder, xcnt, NULL);
	G.g_ich = G.g_hicon + MIN_HINT;
	ycnt = desk_word_divide(gl_height - gl_hbox, G.g_ich, &remainder);
	G.g_ich += desk_word_divide(remainder, ycnt, NULL);

	/* DESKTOP v1.2: Scale drive coordinates */
	for (pa = G.g_ahead; pa; pa = pa->a_next)
	{
		x = desk_word_multiply(pa->a_xspot, G.g_icw);
		y = desk_word_multiply(pa->a_yspot, G.g_ich) + G.g_ydesk;
		snap_disk(x, y, &pa->a_xspot, &pa->a_yspot);		
	}
	
	xcent = (G.g_wicon - G.g_idlist[0].ib_wicon) / 2;
	G.g_nmicon = 9;
	G.g_xyicon[0] = xcent;  G.g_xyicon[1] = 0;
	G.g_xyicon[2]=xcent; G.g_xyicon[3]=G.g_hicon-gl_hschar-2;
	G.g_xyicon[4] = 0;  G.g_xyicon[5] = G.g_hicon-gl_hschar-2;
	G.g_xyicon[6] = 0;  G.g_xyicon[7] = G.g_hicon;
	G.g_xyicon[8] = G.g_wicon;  G.g_xyicon[9] = G.g_hicon;
	G.g_xyicon[10]=G.g_wicon; G.g_xyicon[11] = G.g_hicon-gl_hschar-2;
	G.g_xyicon[12]=G.g_wicon - xcent; G.g_xyicon[13]=G.g_hicon-gl_hschar-2;
	G.g_xyicon[14] = G.g_wicon - xcent;  G.g_xyicon[15] = 0;
	G.g_xyicon[16] = xcent;  G.g_xyicon[17] = 0;
	G.g_nmtext = 5;
	G.g_xytext[0] = 0;  		G.g_xytext[1] = 0;
	G.g_xytext[2] = desk_word_multiply(gl_wchar, 12);
	G.g_xytext[3] = 0;
	G.g_xytext[4] = desk_word_multiply(gl_wchar, 12);
	G.g_xytext[5] = gl_hchar;
	G.g_xytext[6] = 0;  		G.g_xytext[7] = gl_hchar;
	G.g_xytext[8] = 0; 		G.g_xytext[9] = 0;
	return TRUE;
}


/*
*	Initialize the application list by reading in the DESKTOP.INF
*	file, either from memory or from the disk if the shel_get
*	indicates no message is there.
*/
WORD app_start()
{
	WORD		i;
	ANODE		*pa;
	WSAVE		*pws;
	BYTE		*pcurr, *ptmp;
	WORD		envr, wincnt;
#if MULTIAPP
	WORD		numaccs;
	BYTE		*savbuff;
	
	numaccs = 0;
#endif		
	if (!app_start1()) {
#if defined(ELKS) && ELKS
		static const BYTE message[] =
			"gemdesk: cannot load DESKTOP.INF\n";

		(void) write(2, message, sizeof(message) - 1U);
#endif
		return FALSE;
	}

	wincnt = 0;
	pcurr = &gl_afile[0];
	while (*pcurr)
	{
	  if (*pcurr != '#')
	    pcurr++;
	  else
	  {
	    pcurr++;
	    switch(*pcurr)
	    {
		  case 'T':				/* DESKTOP v1.2: Trash */
		  	pa = app_alloc(TRUE);
		  	pcurr = app_parse(pcurr, pa);

		    for (i = 0; i < DESK_STANDARD_ASSOCIATION_COUNT; i++)
	        {
		      	pa = app_alloc(TRUE);
      		    app_parse(ini_str(ST1STD+i)+1, pa);
      		    if (pa->a_type == AT_ISNONE)
      		      app_free(pa);
		    } /* for */
		  	break;
	      case 'M':				/* legacy DOS media */
	      case 'G':				/* GEM Application	*/
	      case 'F':				/* File, no parms */
	      case 'f':				/*   use full memory	*/
	      case 'P':				/* Parm app */
	      case 'p':				/*   use full memory	*/
	      case 'D':				/* Directory		*/
			/*else	// DESKTOP v1.2: This is done by the 'Trash' line
			{ 
			
			  if (prevdisk == 'M') 
			  {
			    for (i = 0; i < 6; i++)
		        {
			      	pa = app_alloc(TRUE);
	      		    app_parse(ini_str(ST1STD+i)+1, pa);
			    }
			  } 
			  prevdisk = ' ';
			}*/
			pa = app_alloc(TRUE);
	    	pcurr = app_parse(pcurr, pa);
	    	if (pa->a_type == AT_ISNONE)
	    	  app_free(pa);
			break;
#if MULTIAPP			
	      case 'A':				/* Desk Accessory	*/
		    pcurr++;
			pcurr = scan_2(pcurr, &(gl_caccs[numaccs].acc_swap));
			savbuff = G.g_pbuff;
			G.g_pbuff = &(gl_caccs[numaccs].acc_name[0]);
			pcurr = scan_str(pcurr, &ptmp);
			G.g_pbuff = savbuff;
			numaccs++;
			break;
#endif
	      case 'W':				/* Window		*/
			pcurr++;
			if ( wincnt < NUM_WNODES )
			{
			  pws = (WSAVE *)desk_byte_index(
				(BYTE *)&G.g_cnxsave.win_save[0], (UWORD)wincnt,
				(UWORD)sizeof(WSAVE));
			  pcurr = scan_2(pcurr, &pws->hsl_save);
			  pcurr = scan_2(pcurr, &pws->vsl_save);
/* DESKTOP v1.2 puts these in pws->*_save, not gl_savewin */
			  pcurr = scan_2(pcurr, &pws->x_save);
			  pws->x_save = desk_word_multiply(pws->x_save,
							 gl_wchar);
			  pcurr = scan_2(pcurr, &pws->y_save);
			  pws->y_save = desk_word_multiply(pws->y_save,
							 gl_hchar);
			  pcurr = scan_2(pcurr, &pws->w_save);
			  pws->w_save = desk_word_multiply(pws->w_save,
							 gl_wchar);
			  pcurr = scan_2(pcurr, &pws->h_save);
			  pws->h_save = desk_word_multiply(pws->h_save,
							 gl_hchar);
/* */
			  pcurr = scan_2(pcurr, &pws->obid_save);
			  ptmp = &pws->pth_save[0];
			  pcurr++;
			  while ( *pcurr != '@' )
			    *ptmp++ = *pcurr++;
			  *ptmp = 0;
			  wincnt++;
/* DESKTOP v1.2 does these a bit before.
			  gl_savewin[wincnt].g_x = x * gl_wchar;
			  gl_savewin[wincnt].g_y = y * gl_hchar;
			  gl_savewin[wincnt].g_w = w * gl_wchar;
			  gl_savewin[wincnt++].g_h = h * gl_hchar;
*/
			}
			break;
	      case 'E':
			pcurr++;
			pcurr = scan_2(pcurr, &envr);
			G.g_cnxsave.vitem_save = ( (envr & 0x80) != 0);
			G.g_cnxsave.sitem_save = ( (envr & 0x60) >> 5);
			G.g_cnxsave.cdele_save = ( (envr & 0x10) != 0);
			G.g_cnxsave.ccopy_save = ( (envr & 0x08) != 0);
			G.g_cnxsave.cdclk_save = envr & 0x07;
			pcurr = scan_2(pcurr, &envr);
/* Bring these 4 back which weren't in 1.2 */
			G.g_cnxsave.covwr_save = ( (envr & 0x10) == 0);
		 	G.g_cnxsave.cmclk_save = ( (envr & 0x08) != 0);
			G.g_cnxsave.cdtfm_save = ( (envr & 0x04) == 0);
			G.g_cnxsave.ctmfm_save = ( (envr & 0x02) == 0);
			desk_sound(FALSE, !(envr & 0x01), 0);
/* From BALJ's Desktop */
			pcurr = scan_2(pcurr,&envr); 
			G.g_cnxsave.cdetn_save = ( (envr & 0x08) == 0);
			G.g_cnxsave.cdetd_save = ( (envr & 0x04) == 0);
			G.g_detdrives = G.g_cnxsave.cdetd_save;
			G.g_probedrives = G.g_cnxsave.cdetn_save;
			break;
	    }
	  }
	}
	if (!app_rdicon()) {
#if defined(ELKS) && ELKS
		static const BYTE message[] =
			"gemdesk: cannot load original ICN data\n";

		(void) write(2, message, sizeof(message) - 1U);
		(void) write(2, &app_icon_failure_stage, 1U);
		(void) write(2, "\n", 1U);
#endif
		return(FALSE);
	}

	return app_start2();
}

