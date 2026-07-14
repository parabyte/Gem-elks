
/*
*       Copyright 1999, Caldera Thin Clients, Inc.                      
*       This software is licenced under the GNU Public License.         
*       Please see LICENSE.TXT for further information.                 
*              Historical Copyright                             
*	-------------------------------------------------------------
*	GEM Desktop					  Version 3.0
*	Serial No.  XXXX-0000-654321		  All Rights Reserved
*	Copyright (C) 1986			Digital Research Inc.
*	-------------------------------------------------------------
*/

#include "ppddesk.h"

#if MULTIAPP

MLOCAL WORD	iac_chkd;
/*

EXTERN ACCNODE gl_caccs[];


EXTERN LONG	pr_begacc;
EXTERN LONG	pr_begdsk;
EXTERN WORD	gl_hbox;
EXTERN BYTE	gl_bootdr;
#define BEG_UPDATE 1
#define END_UPDATE 0
*/
#define NUM_AFILES 20
#define NUM_FSNAME  8
#define LEN_FSNAME 16

#if defined(ELKS) && ELKS
/*
 * Native accessories are ordinary executable files in the installed POSIX
 * application directory.  These are absolute paths because gemdesk and the
 * resident AES owner are separate ELKS processes with separate current
 * directories.  Each string is far below G.g_cmd's original 128-byte bound.
 */
#define IAC_ELKS_DIRECTORY      "/GEMAPPS"
#define IAC_ELKS_PATTERN        "/GEMAPPS/*.ACC"
#define IAC_ELKS_COMMAND_PREFIX "/GEMAPPS/"
#endif

GLOBAL LPBYTE	ad_tmp1;
GLOBAL BYTE	gl_tmp1[LEN_FSNAME];
GLOBAL LPBYTE	ad_tmp2;
GLOBAL BYTE	gl_tmp2[LEN_FSNAME];
GLOBAL BYTE	*g_fslist[NUM_AFILES];
GLOBAL BYTE	g_fsnames[LEN_FSNAME * NUM_AFILES];


#if DEBUG
	VOID
printstr(lst)
	LPBYTE		lst;
{
	BYTE		ch;
	WORD		i;

	i = 0;
	while ((ch = lst[i++]) != 0)
	  dbg("%c", ch);
}
#endif

WORD iac_isnam(GEM_SLOT_BYTE_POINTER lst)
{
	BYTE		ch;

	if (!lst)
	  return(FALSE);
	ch = LBGET(lst);
	/*
	 * POSIX filenames preserve case and may begin with a digit.  Retain the
	 * original resource's leading-underscore placeholder rejection, but do
	 * not require the DOS-era uppercase-only spelling.
	 */
	return(((ch >= 'A') && (ch <= 'Z'))
		|| ((ch >= 'a') && (ch <= 'z'))
		|| ((ch >= '0') && (ch <= '9')));
}

VOID iac_init()
{
	WORD		i, j;
	BYTE		*npt;
	BYTE		accstr[9];

	gl_keepac = FALSE;
	for (i=0; i<3; i++)
	{
	  npt = &gl_caccs[i].acc_name[0];
#if defined(ELKS) && ELKS
	  /* ELKS, rather than a user-selected arena flag, owns residency. */
	  gl_caccs[i].acc_swap = 'Y';
#endif
	  if (iac_isnam(ADDR(npt)))
	  {
	    strlcpy(&accstr[0], "        ", sizeof(accstr));
	    for (j=0; (j<8) && npt[j] && (npt[j] != '.'); j++)
	      accstr[j] = npt[j];
	    if (appl_find(ADDR(&accstr[0])) < 0)
	      npt[0] = '\0';
#if !defined(ELKS) || !ELKS
	    else				/* check for no full step */
	      gl_keepac |= (gl_caccs[i].acc_swap == 'N');
#endif
	  }
	}
}


VOID iac_strcop(LPTREE tree, WORD obj, GEM_SLOT_BYTE_POINTER src)
{
	GEM_TYPED_SLOT_POINTER slot;
	GEM_SLOT_BYTE_POINTER dst;
	LPOBJ object;
	LPTEDI tedi;

	object = desk_object_at(tree, obj);
	dst = (GEM_SLOT_BYTE_POINTER) 0;
	if (object) {
	  slot.words = object->ob_spec;
	  if (object->ob_type == G_BUTTON || object->ob_type == G_STRING)
	    dst = slot.bytes;
	  else {
	    tedi = slot.tedinfo;
	    if (tedi) {
	      slot.words = tedi->te_ptext;
	      dst = slot.bytes;
	    }
	  }
	}
	if (dst && src)
	  LSTCPY(dst, src);
}



VOID iac_schar(LPTREE tree, WORD obj, BYTE ch)
{
	LPOBJ		object;
	UWORD		high;

	object = desk_object_at(tree, obj);
	if (!object)
	  return;
	high = object->ob_spec.hi & 0x00ff;
	high |= (UWORD)((UWORD)(UBYTE)ch << 8);
	object->ob_spec.hi = high;
	// [JCE] To make these controls behave like checkboxes
	if (ch == 'Y')
		object->ob_state |= SELECTED;
	else	object->ob_state &= ~SELECTED;
}

VOID iac_redrw(LPTREE tree, WORD obj, WORD state, WORD depth)
{
	WORD		x, y, w, h;
	LPOBJ		object;

	object = desk_object_at(tree, obj);
	if (!object)
	  return;
	objc_offset(tree, obj, &x, &y);
	w = object->ob_width;
	h = object->ob_height;
	object->ob_state = state;
	objc_draw(tree, obj, depth, x, y, w, h);
}

VOID iac_elev(LPTREE tree, WORD currtop, WORD count)
{
	WORD		h, y, th, minimum_height;

	y = 0;
	th = h = tree[ACFSVSLI].ob_height;
	if ( count > NUM_FSNAME)
	{
	  h = x_mul_div(NUM_FSNAME, h, count);
	  minimum_height = desk_word_divide(gl_hbox, 2, NULL);
	  h = max(minimum_height, h);	/* min size elevator	*/
	  y = x_mul_div(currtop, th-h, count-NUM_FSNAME);
	}
	tree[ACFSELEV].ob_y = y;
	tree[ACFSELEV].ob_height = h;
}


WORD iac_comp(VOID)
{
	WORD		chk;

	if ( (gl_tmp1[0] == ' ') &&
	     (gl_tmp2[0] == ' ') )
	{
	  chk = strcmp( scasb(&gl_tmp1[0], '.'), 
			scasb(&gl_tmp2[0], '.') );
	  if ( chk )
	    return( chk );
	}
	return ( strcmp(&gl_tmp1[0], &gl_tmp2[0]) );
}

VOID iac_mvnames(LPTREE tree, WORD start, WORD num)
{
	WORD		i, j, k;
	WORD		len;

	for (i=0; i<num; i++)
	{
	  LSTCPY(ad_tmp1, (LPBYTE)ADDR(g_fslist[i+start]));
	  len = 0;
	  while (gl_tmp1[len] != '.')
	    len++;
	  if (len < 8)				/* blank pad in middle	*/
	  {
	    for (j=11, k=len+3; j > 7; j--, k--)
	      gl_tmp1[j] = gl_tmp1[k];
	    for (j=len; j < 8; j++)
	      gl_tmp1[j] = ' ';
	  }
	  iac_strcop(tree, ACA1NAME+i, ad_tmp1);
	}
}

WORD iac_names(LPTREE tree)
{
	WORD		ret;
	WORD		len;
	WORD		i, j, gap;
	WORD		thefile;
	BYTE		*ptr, *temp;

					/* find all installed accessory files */
					/* stuff first 8 in object	*/
					/* adjust elevator size to number */
	thefile = 0;
	ptr = &g_fsnames[0];
	ad_tmp1 = (LPBYTE)ADDR(&gl_tmp1[0]);
	ad_tmp2 = (LPBYTE)ADDR(&gl_tmp2[0]);
	dos_sdta(G.a_wdta);
#if defined(ELKS) && ELKS
	/* The wildcard is interpreted directly by the POSIX-backed GEMDOS seam. */
	strlcpy(&G.g_cmd[0], IAC_ELKS_PATTERN, sizeof(G.g_cmd));
#else
	strlcpy(&G.g_cmd[1], "/GEMBOOT/*.ACC", sizeof(G.g_cmd) - 1);
	G.g_cmd[0] = gl_bootdr;
#endif
	ret = dos_sfirst(G.a_cmd, 0x16);
	while ( ret )
	{
	  len = LSTCPY(ADDR(g_fslist[thefile] = ptr), G.a_wdta+30);
	  ptr += len+1;

	  ret = dos_snext();

	  if (thefile++ >= NUM_AFILES)
	  {
	    ret = FALSE;
	    desk_sound(TRUE, 660, 4);
	  }
	}

	for(gap = thefile/2; gap > 0; gap /= 2)
	{
	  for(i = gap; i < thefile; i++)
	  {
	    for (j = i-gap; j >= 0; j -= gap)
	    {
	      LSTCPY(ad_tmp1, ADDR(g_fslist[j]));
	      LSTCPY(ad_tmp2, ADDR(g_fslist[j+gap]));
	      if ( iac_comp() <= 0 )
		break;
	      temp = g_fslist[j];
	      g_fslist[j] = g_fslist[j+gap];
	      g_fslist[j+gap] = temp;
	    }
	  }
	}
	iac_mvnames(tree, 0, min(thefile, NUM_FSNAME));
	return(thefile);
}


#define ACCMIN	0x2000


VOID iac_save(LPTREE tree)
{
	WORD		i;
	WORD		chnum;
	WORD		isswap;
	GEM_SLOT_BYTE_POINTER lst;
	GEM_TYPED_SLOT_POINTER slot;
	LPOBJ object;
	LPTEDI tedi;
	GEM_U32_WORDS	base_hint;
	GEM_U32_WORDS	size_hint;
	BYTE		didalert;
	WORD 		didrun;

	wind_update(END_UPDATE);
#if defined(ELKS) && ELKS
	strlcpy(&G.g_cmd[0], IAC_ELKS_DIRECTORY, sizeof(G.g_cmd));
#else
	strlcpy(&G.g_cmd[1], "/GEMBOOT", sizeof(G.g_cmd) - 1);
	G.g_cmd[0] = gl_bootdr;			/* get correct drive	*/
	dos_sdrv(0);
#endif
	dos_chdir(G.a_cmd);

	base_hint = gem_u32_words(0, 0);
	size_hint = gem_u32_words(0, 0);
	didalert = FALSE;
	gl_keepac = FALSE;
	proc_delete(-1);		/* delete all accessories	*/
	for (i=0; i<3; i++)
	{
	  object = desk_object_at(tree, ACC1NAME + i);
	  lst = (GEM_SLOT_BYTE_POINTER) 0;
	  if (object) {
	    slot.words = object->ob_spec;
	    if (object->ob_type == G_BUTTON || object->ob_type == G_STRING)
	      lst = slot.bytes;
	    else {
	      tedi = slot.tedinfo;
	      if (tedi) {
		slot.words = tedi->te_ptext;
		lst = slot.bytes;
	      }
	    }
	  }
	  if (iac_isnam(lst))
	  {
            lstlcpy(&(gl_caccs[i].acc_name[0]), lst,
		      sizeof(gl_caccs[i].acc_name));


#if defined(ELKS) && ELKS
	    /*
	     * The hidden GEM/XM checkbox must not leak a stale RSC or INF byte
	     * back into process policy.  FALSE asks the native process manager
	     * for its one kernel-owned, non-swapped accessory allocation mode.
	     */
	    gl_caccs[i].acc_swap = 'Y';
	    isswap = FALSE;
#else
	    gl_caccs[i].acc_swap = (BYTE)
		(desk_object_at(tree, ACC1FMEM + i)->ob_spec.hi >> 8);
	    isswap = (gl_caccs[i].acc_swap == 'N');
#endif
	    gl_keepac |= isswap;	/* if TRUE, no full step	*/
	    /* ELKS allocates each accessory's address space during exec. */
	    if (proc_create(base_hint, size_hint,
			    isswap, TRUE, &chnum))
	    {
#if defined(ELKS) && ELKS
	      /*
	       * proc_run executes in the resident AES owner, whose cwd is not the
	       * Desktop's cwd.  Pass the complete native path so a selected file
	       * always means the same inode in both processes.
	       */
	      strlcpy(&G.g_cmd[0], IAC_ELKS_COMMAND_PREFIX,
		       sizeof(G.g_cmd));
	      strlcat(&G.g_cmd[0], &(gl_caccs[i].acc_name[0]),
		       sizeof(G.g_cmd));
	      didrun = proc_run(chnum, 1, 3, G.a_cmd, ADDR("\0"));
#else
	      didrun = proc_run(chnum, 1, 3,
			     &(gl_caccs[i].acc_name[0]), ADDR("\0"));
#endif
	    }
	    else
	      didrun = FALSE;
	    if ((!didalert) && (!didrun))
	    {
	      form_alert(1, ini_str(STACCMEM));
	      didalert = TRUE;
	    }
	  }
	  else
	    gl_caccs[i].acc_name[0] = '\0';
        }
	wind_update(BEG_UPDATE);
}


WORD iac_scroll(LPTREE tree, WORD currtop, WORD count, WORD move)
{
	WORD		newtop;

	if (count <= NUM_FSNAME)
	  return(0);
	newtop = currtop + move;
	newtop = max(newtop, 0);
	newtop = min(newtop, count - NUM_FSNAME);
	if (newtop == currtop)
	  return(currtop);
	iac_elev(tree, newtop, count);
	iac_mvnames(tree, newtop, NUM_FSNAME);
	iac_redrw(tree, ACNAMBOX, NORMAL, 1);
	iac_redrw(tree, ACFSVSLI, NORMAL, 1);
	return(newtop);
}


WORD iac_dial(LPTREE tree)
{
	GEM_TYPED_SLOT_POINTER slot;
	LPOBJ object;
	LPTEDI tedi;
	WORD		touchob;
	WORD		cont;
	WORD		xd, yd, wd, hd;
	GEM_SLOT_BYTE_POINTER chspec;
	WORD		newstate;
	WORD		i;
	WORD		fcurrtop, fcount;
	WORD		move;
	GRECT		pt;
	WORD		mx, my, kret, bret;
	
	iac_chkd = ACC1NAME;
	fcount = iac_names(tree);
	fcurrtop = 0;
	iac_elev(tree, fcurrtop, fcount);
	for (i=0; i<3; i++)
	{
	  desk_object_at(tree, ACC1NAME + i)->ob_state = NORMAL;
	  chspec = ADDR(&(gl_caccs[i].acc_name[0]));
	  if (iac_isnam(chspec))
	  {
	    iac_strcop(tree, ACC1NAME+i, chspec);
#if !defined(ELKS) || !ELKS
	    iac_schar(tree, ACC1FMEM+i, gl_caccs[i].acc_swap);
#endif
	  }
	  else
	  {
	    iac_strcop(tree, ACC1NAME+i, ADDR(""));
#if !defined(ELKS) || !ELKS
	    iac_schar(tree, ACC1FMEM+i, 'Y');
#endif
	  }
	}
	desk_object_at(tree, iac_chkd)->ob_state = CHECKED;

	form_center(tree, &xd, &yd, &wd, &hd);
	form_dial(FMD_START, 0, 0, 0, 0, xd, yd, wd, hd);
	objc_draw(tree, ROOT, MAX_DEPTH, xd, yd, wd, hd);
	
	cont = TRUE;
	while (cont)
	{
	  touchob = form_do(tree, 0);
	  touchob &= 0x7fff;
	  newstate = NORMAL;
	  move = 0;
	  graf_mkstate(&mx, &my, &kret, &bret);
	  switch (touchob)
	  {
	    case ACC1NAME:
	    case ACC2NAME:
	    case ACC3NAME:
	      objc_change(tree, iac_chkd, 0, xd, yd, wd, hd, NORMAL, TRUE);
	      iac_chkd = touchob;
	      newstate = CHECKED;
	      break;

#if !defined(ELKS) || !ELKS
	    case ACC1FMEM:
	    case ACC2FMEM:
	    case ACC3FMEM:
	      iac_schar(tree, touchob,
		(desk_object_at(tree, touchob)->ob_spec.hi >> 8) == 'Y'
		? 'N' : 'Y');
	      newstate = desk_object_at(tree, touchob)->ob_state;
	      iac_redrw(tree, touchob, newstate, 0);
	      break;
#endif

	    case ACREMV:
	      iac_strcop(tree, iac_chkd, ADDR(""));
	      iac_redrw(tree, iac_chkd, CHECKED, 0);
	      break;

	    case ACA1NAME:
	    case ACA2NAME:
	    case ACA3NAME:
	    case ACA4NAME:
	    case ACA5NAME:
	    case ACA6NAME:
	    case ACA7NAME:
	    case ACA8NAME:
	      object = desk_object_at(tree, touchob);
	      chspec = (GEM_SLOT_BYTE_POINTER) 0;
	      if (object) {
		slot.words = object->ob_spec;
		if (object->ob_type == G_BUTTON || object->ob_type == G_STRING)
		  chspec = slot.bytes;
		else {
		  tedi = slot.tedinfo;
		  if (tedi) {
		    slot.words = tedi->te_ptext;
		    chspec = slot.bytes;
		  }
		}
	      }
	      if (iac_isnam(chspec))
	      {
	        iac_strcop(tree, iac_chkd, chspec);
	        iac_redrw(tree, iac_chkd, CHECKED, 0);
	      }
	      break;

	    case ACFUPARO:
	      move = -1;
	      break;

	    case ACFDNARO:
	      move = 1;
	      break;
	    
	    case ACFSVSLI:
		ob_actxywh(tree, ACFSELEV, &pt);
/* APPLE	pt.g_x -= 3;
		pt.g_w += 6; */
		if ( inside(mx, my, &pt) )
		  goto dofelev;
		move = (my <= pt.g_y) ? -1 : 1;
		break;
	    case ACFSELEV:
dofelev:	wind_update(3);
		ob_relxywh(tree, ACFSVSLI, &pt);
/* APPLE 	pt.g_x += 3;
		pt.g_w -= 6; */
		tree[ACFSVSLI].ob_x = pt.g_x;
		tree[ACFSVSLI].ob_width = pt.g_w;
		move = graf_slidebox(tree, ACFSVSLI, ACFSELEV, TRUE);
/* APPLE 	pt.g_x -= 3;
		pt.g_w += 6; */
		tree[ACFSVSLI].ob_x = pt.g_x;
		tree[ACFSVSLI].ob_width = pt.g_w;
		wind_update(2);
		move = x_mul_div(move, fcount-1, 1000) - fcurrtop;
		break;


	    case ACINST:
	    case ACCNCL:
	      cont = FALSE;
	      break;

	    default:
	      break;
	  }
	  objc_change(tree, touchob, 0, xd, yd, wd, hd, newstate, TRUE);
	  if (move)
	  {
	    fcurrtop = iac_scroll(tree, fcurrtop, fcount, move);
	  }
    
        }					/* undraw the form	*/
	show_hide(FMD_FINISH, tree);
	desk_object_at(tree, iac_chkd)->ob_state = NORMAL;
	return(touchob);
}

/************************************************************************/
/* i n s _ a c c	  						*/
/************************************************************************/

VOID ins_acc()
{			       
	LPTREE		tree;

	tree = G.a_trees[ADINSACC];

/* get current accessory names */
/* stuff them in slots in dialog */

	if (iac_dial(tree) == ACINST)
	{
	  iac_save(tree);
/* copy names from tree to current acc list */
/* delete some/all current accs and free channels */
/* run the new accessories */

	}
} /* ins_acc */

#endif /* MULTIAPP */
