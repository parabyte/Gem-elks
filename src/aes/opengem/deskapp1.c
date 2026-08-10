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
#include <unistd.h>

/* Report a bounded original Desktop pathname only on an actual I/O error. */
static VOID
app_file_error(const BYTE *operation, const BYTE *path)
{
	UWORD length;

	length = 0;
	while (length < LEN_ZPATH && path[length])
		length++;
	(void) write(2, operation, strlen(operation));
	(void) write(2, path, length);
	(void) write(2, "\n", 1U);
}
#endif

#define MIN_WINT 4
#define MIN_HINT 2

/*
#if DEBUG
void dump_anodes(char *s)
{
	ANODE *pa;
	
	dbg("dump_anodes(%s):\n", s);
	
	for (pa = G.g_ahead; pa; pa = pa->a_next)
	{
		dbg("    flags=%04x type=%04x obid=%04x aicon=%04x dicon=%04x %c %s %s\n",
				pa->a_flags, pa->a_type, pa->a_obid, pa->a_aicon,
				pa->a_dicon, pa->a_letter, pa->a_pappl, pa->a_pdata);
	}
	
}
#endif
*/

/* strcpy() with length bound */
VOID strlcpy(char *dest, char *src, int len)
{
	if (len < 1) return;
	if (strlen(src) < len)
	{
		strcpy(dest, src);
		return;
	}
	strncpy(dest, src, len-1);
	dest[len-1] = 0;	
}

VOID strlcat(char *dest, char *src, int len)
{
	strlcpy(dest + strlen(dest), src, len - strlen(dest));
}

/*
*	Allocate an application object.
*/
ANODE *app_alloc(WORD tohead)
{
	ANODE		*pa, *ptmpa;

	pa = G.g_aavail;
	if (pa)
	{
	  G.g_aavail = pa->a_next;
	  if ( (tohead) ||
	       (!G.g_ahead) )
	  {
	    pa->a_next = G.g_ahead;
	    G.g_ahead = pa;
	  }
	  else
	  {
	    ptmpa = G.g_ahead;
	    while( ptmpa->a_next )
	      ptmpa = ptmpa->a_next;
	    ptmpa->a_next = pa;
	    pa->a_next = (ANODE *) NULL;
	  }
	}
	return(pa);
}


/*
*	Free an application object.
*/

VOID app_free(ANODE *pa)
{
	ANODE		*ptmpa;

	if (G.g_ahead == pa)
	  G.g_ahead = pa->a_next;
	else
	{
	  ptmpa = G.g_ahead;
	  while ( (ptmpa) &&
		  (ptmpa->a_next != pa) )
	    ptmpa = ptmpa->a_next;
	  if (ptmpa)
	    ptmpa->a_next = pa->a_next;
	}
	pa->a_next = G.g_aavail;
	G.g_aavail = pa;
}


/*
*	Convert a single hex ASCII digit to a number
*/

WORD hex_dig(BYTE achar)
{
	if ( (achar >= '0') && (achar <= '9') ) return(achar - '0');	
	if ( (achar >= 'A') && (achar <= 'F') ) return(achar - 'A' + 10);
	return(0);
}
	

/*
*	Reverse of hex_dig().
*/

BYTE uhex_dig(WORD wd)
{
	if ( (wd >= 0   ) && (wd <= 9   ) ) return(wd + '0');	
	if ( (wd >= 0x0a) && (wd <= 0x0f) ) return(wd + 'A' - 0x0a);
	return(' ');
}
	

/*
*	Scan off and convert the next two hex digits and return with
*	pcurr pointing one space past the end of the four hex digits
*/

BYTE *scan_2(BYTE *pcurr, WORD *pwd)
{
	UWORD		temp;
	
	temp = 0x0;
	temp |= hex_dig(*pcurr++) << 4;
	temp |= hex_dig(*pcurr++);
	if (temp == 0x00ff) temp = NIL;
	*pwd = temp;
	return(	pcurr );
}

/*
*	Reverse of scan_2().
*/

BYTE *save_2(BYTE *pcurr, UWORD wd)
{
	*pcurr++ = uhex_dig((wd >> 4) & 0x000f);
	*pcurr++ = uhex_dig(wd & 0x000f);
	return(	pcurr );
}

#if MULTIAPP


/*
*	Scan off and convert the next four hex digits and return with
*	pcurr pointing one space past the end of the four hex digits.
*	Start of field is marked with an 'R'.  If no field, set it to
*	default memory size -- DEFMEMREQ.
*/

BYTE *scan_memsz(BYTE *pcurr, UWORD *pwd)
{
	UWORD		temp1, temp2;
	UBYTE		*bytes;
	
	temp1 = 0x0;
	while (*pcurr == ' ')
	  pcurr++;
	if (*pcurr == 'R')
	{
	  pcurr++;
	  pcurr = scan_2(pcurr, (WORD *)&temp1);	/* hi byte	*/
	  pcurr = scan_2(pcurr, (WORD *)&temp2);	/* lo byte	*/
	  /*
	   * The ELKS target is little-endian.  Store the two parsed bytes
	   * directly so ia16-gcc cannot emit an 80186 immediate-count shift.
	   * Each input is already bounded to eight bits by scan_2().
	   */
	  bytes = (UBYTE *) &temp1;
	  bytes[1] = (UBYTE) temp1;
	  bytes[0] = (UBYTE) temp2;
	}
	if (temp1 == 0)
	  temp1 = DEFMEMREQ;
	*pwd = temp1;
	return(	pcurr );
}

/*
*	Reverse of scan_memsz().
*/

BYTE *save_memsz(BYTE *pcurr, UWORD wd)
{
	UBYTE		*bytes;

	/* See scan_memsz(): the on-disk field is high byte, then low byte. */
	bytes = (UBYTE *) &wd;
	*pcurr++ = 'R';
	pcurr = save_2(pcurr, bytes[1]);
	pcurr = save_2(pcurr, bytes[0]);
	return(	pcurr );
}

#endif

/*
*	Scan off spaces until a string is encountered.  An @ denotes
*	a null string.  Copy the string into a string buffer until
*	a @ is encountered.  This denotes the end of the string.  Advance
*	pcurr past the last byte of the string.
*
* 	BIG WARNING: Note that the terminator is an @ sign, not a
* 	0 byte. Passing a null-terminated string as pcurr will invoke 
* 	undefined behaviour.
*
*/

BYTE *scan_str(BYTE *pcurr, BYTE **ppstr)
{
	/* Skip over spaces */
	while(*pcurr == ' ') pcurr++;

	/* ppstr points to the start of the buffer in gl_buffer */
	*ppstr = G.g_pbuff;
	while(*pcurr != '@') 
	{
		*G.g_pbuff++ = *pcurr++;
		/* [JCE 20-8-2005] Don't allow an overflow */
		if (G.g_pbuff > (gl_buffer + SIZE_BUFF - 1)) 
		{
			break;
		}
	}
	*G.g_pbuff++ = 0;
	pcurr++;
	/* Returns the next free space in the buffer */
	return(pcurr);
}


/*
*	Reverse of scan_str.
*/

BYTE *save_str(BYTE *pcurr, BYTE *pstr)
{
	while(*pstr)
	  *pcurr++ = *pstr++;
	*pcurr++ = '@';
	*pcurr++ = ' ';
	return(pcurr);
}

/*
 * Install the one namespace root which exists on ELKS.
 *
 * The original FreeGEM routine enumerated DOS drive letters, asked GEMDOS
 * for each drive type, and allocated one desktop ANODE per detected device.
 * ELKS instead presents one POSIX namespace.  Its root directory is not a
 * removable drive which needs probing, so the old "scan drives" preference
 * does not hide it.  A saved or user-installed disk node is left untouched;
 * this makes the operation idempotent when resident AES starts Desktop more
 * than once.
 *
 * The ANODE and IG_HARD bitmap remain the original Desktop structures and
 * original ICN asset.  gl_stdrv supplies an internal A..Z compatibility
 * selector for the few unchanged GEMDOS-facing dialog paths.  File-manager
 * operations do not synthesize that letter: do_dopen() maps this node
 * directly to the absolute POSIX path "/".
 */
VOID app_detect()
{
	ANODE		*pa;

	for (pa = G.g_ahead; pa; pa = pa->a_next)
	  if (pa->a_type == AT_ISDISK)
	    return;

	pa = app_alloc(TRUE);
	if (!pa)
	  return;

	pa->a_type = AT_ISDISK;
	pa->a_flags = AF_ISCRYS | AF_ISGRAF | AF_ISDESK | AF_WASDET;
	pa->a_obid = NIL;
	pa->a_aicon = IG_HARD;
	pa->a_dicon = NIL;
	/* dos_sdrv() admits only 0..25, so this 16-bit addition cannot wrap. */
	pa->a_letter = (WORD) ('A' + gl_stdrv);
	pa->a_xspot = G.g_xdesk + G.g_wdesk - G.g_icw;
	pa->a_yspot = G.g_ydesk;
	(void) scan_str("POSIX /@", &pa->a_pappl);
	(void) scan_str("@", &pa->a_pdata);
}       /* app_detect */


/*
*	Parse a single line from the DESKTOP.INF file.
*/

BYTE *app_parse(BYTE *pcurr, ANODE *pa)
{
	switch(*pcurr)
	{
	  case 'T':			    /* DESKTOP v1.2: Trash */
	    pa->a_type  = AT_ISTRSH;
	    pa->a_flags = AF_ISCRYS | AF_ISGRAF | AF_ISDESK;
	    break;
	  case 'M':				/* legacy DOS media	*/
		pa->a_type = AT_ISNONE;
		pa->a_flags = NONE;
		break;
	  case 'G':				/* GEM App File		*/
		pa->a_type = AT_ISFILE;
		pa->a_flags = AF_ISCRYS | AF_ISGRAF;
		break;
	  case 'F':				/* native file, no parms */
	  case 'f':				/*   needs full memory	*/
		pa->a_type = AT_ISFILE;
		pa->a_flags = (*pcurr == 'F') ? NONE : AF_ISFMEM;
		break;
	  case 'P':				/* native app needs parms */
	  case 'p':				/*   needs full memory	*/
		pa->a_type = AT_ISFILE;
		pa->a_flags = (*pcurr == 'P') ? 
				AF_ISPARM : AF_ISPARM | AF_ISFMEM;
		break;
	  case 'D':				/* Directory (Folder)	*/
		pa->a_type = AT_ISFOLD;
		break;
	}
	pcurr++;
	if (pa->a_flags & AF_ISDESK)
	{
	  pcurr = scan_2(pcurr, &pa->a_xspot);
	  pcurr = scan_2(pcurr, &pa->a_yspot);
	}
	pcurr = scan_2(pcurr, &pa->a_aicon);
	pcurr = scan_2(pcurr, &pa->a_dicon);
	pcurr++;
	if (pa->a_flags & AF_ISDESK)
	{
	  pa->a_letter = (*pcurr == ' ') ? 0 : *pcurr;
	  pcurr += 2;
	}
	pcurr = scan_str(pcurr, &pa->a_pappl);
	pcurr = scan_str(pcurr, &pa->a_pdata);
#if MULTIAPP
	if (!(pa->a_flags & AF_ISDESK))			/* only for apps */
	  pcurr = scan_memsz(pcurr, (UWORD *)&pa->a_memreq);
#endif
	return(pcurr);
}

VOID app_tran(WORD bi_num)
{
#if GEM_TRAP_FAR_DATA
	/*
	 * The resident ELKS VDI consumes original GEM standard-form words
	 * directly.  DOS GEM transformed them into a private driver layout here;
	 * doing that across the resident resource segment would add a copy and
	 * would destroy the byte-exact form used by the native raster backend.
	 */
	(void) bi_num;
#else
	LPBIT		lpbi;
	BITBLK		lb;
	LPVOID		bitmap;

	rsrc_gaddr(R_BITBLK, bi_num, (LPVOID *)&lpbi);

	LBCOPY(ADDR(&lb), (LPBYTE)lpbi, sizeof(BITBLK));
	bitmap = gem_near_words_pointer(lb.bi_pdata);
	if (bitmap)
	  gsx_trans(bitmap, lb.bi_wb, bitmap, lb.bi_wb, lb.bi_hl);
#endif
}


WORD app_getfh(WORD openit, BYTE *pname, WORD attr)
{
	WORD		handle, tmpdrv;
	LPBYTE		lp;

	handle = 0;
	strcpy(G.g_srcpth, pname);
	lp = ADDR(&G.g_srcpth[0]);
	tmpdrv = dos_gdrv();
	if (tmpdrv != gl_stdrv)
	  dos_sdrv(gl_stdrv);
	if ( shel_find(lp) )
	{
	  if (openit)
	    handle = dos_open(lp, attr);
	  else
	    handle = dos_create(lp, attr);
	  if ( DOS_ERR )
	  {
	    handle = 0;
#if defined(ELKS) && ELKS
	    app_file_error("gemdesk: POSIX open failed: ", lp);
#endif
	  }
	}
#if defined(ELKS) && ELKS
	else
	  app_file_error("gemdesk: POSIX lookup failed: ", lp);
#endif
	if (tmpdrv != gl_stdrv)
	  dos_sdrv(tmpdrv);
	return(handle);
}

