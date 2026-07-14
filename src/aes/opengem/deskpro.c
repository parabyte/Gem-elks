/*	DESKPRO.C	4/18/84 - 03/19/85	Lee Lorenzen		*/
/*	for 3.0		3/11/86 - 01/28/87	MDF			*/
/*	merge source	5/27/87	- 5/28/87	mdf			*/

/*
*       Copyright 1999, Caldera Thin Clients, Inc.                      
*       This software is licenced under the GNU Public License.         
*       Please see LICENSE.TXT for further information.                 
*                                                                       
*                  Historical Copyright                                 
*	-------------------------------------------------------------
*	GEM Desktop					  Version 2.3
*	Serial No.  XXXX-0000-654321		  All Rights Reserved
*	Copyright (C) 1985 - 1987			Digital Research Inc.
*	-------------------------------------------------------------
*/

#include "ppddesk.h"

WORD pro_chdir(WORD drv, BYTE *ppath)
{
	(void) drv;

	/*
	 * An empty original DOS path meant the selected drive's root.  ELKS has
	 * one POSIX namespace, so map that case to "/" and perform the real
	 * syscall.  Returning dos_chdir's status prevents application launches
	 * from silently inheriting a stale working directory.
	 */
	strlcpy(G.g_srcpth, (ppath && *ppath) ? ppath : "/",
		 sizeof(G.g_srcpth));
	return dos_chdir(ADDR(&G.g_srcpth[0]));
} /* pro_chdir */

/*
 * Append one string to a shell tail without multiplication, wide lengths, or
 * an unbounded libc call.  `limit` is the maximum content length, excluding
 * the terminating NUL.  The caller reserves the final two bytes used when
 * pro_run inserts the historical leading space and trailing carriage return.
 */
MLOCAL WORD
pro_tail_append(BYTE *tail, UWORD limit, UWORD *used, const BYTE *source)
{
	if (!tail || !used || !source)
	  return(FALSE);
	while (*source)
	{
	  if (*used >= limit)
	    return(FALSE);
	  tail[*used] = *source++;
	  (*used)++;
	}
	tail[*used] = 0;
	return(TRUE);
}

/*
 * The ELKS command-tail parser uses a pair of double quotes to keep one argv
 * entry together; it does not implement a backslash escape for a quote.
 * Reject an embedded quote or line ending instead of silently changing the
 * command which /bin/sh receives.  The 125-byte countdown also makes this a
 * bounded validation pass on an XT, even if a corrupt caller omits the NUL.
 */
MLOCAL WORD
pro_tail_command_safe(const BYTE *source)
{
	UWORD left;
	BYTE character;

	if (!source)
	  return(FALSE);
	left = 125U;
	while (*source)
	{
	  if (!left)
	    return(FALSE);
	  character = *source++;
	  if (character == '"' || character == '\r' || character == '\n')
	    return(FALSE);
	  left--;
	}
	return(TRUE);
}

WORD pro_cmd(BYTE *psubcmd, BYTE *psubtail, WORD exitflag)
{
	UWORD used;

	/*
	 * A resident AES cannot return an owner-data pointer through SHEL_ENVRN
	 * to a client with a separate ELKS data segment.  `/bin/sh` is the native
	 * COMSPEC equivalent and is executed directly by the existing process
	 * manager, with no conversion program or wrapper process.
	 */
	strlcpy(G.g_cmd, "/bin/sh", sizeof(G.g_cmd));
	G.g_tail[0] = 0;
	G.g_tail[1] = 0;
	if (!exitflag)
	  return(TRUE);
	if (!psubcmd || !*psubcmd || !psubtail
	    || !pro_tail_command_safe(psubcmd)
	    || !pro_tail_command_safe(psubtail))
	  return(FALSE);

	used = 0;
	if (!pro_tail_append(G.g_tail + 1, 125U, &used,
			     (const BYTE *) "-c \"")
	    || !pro_tail_append(G.g_tail + 1, 125U, &used, psubcmd)
	    || !pro_tail_append(G.g_tail + 1, 125U, &used,
				(const BYTE *) " ")
	    || !pro_tail_append(G.g_tail + 1, 125U, &used, psubtail)
	    || !pro_tail_append(G.g_tail + 1, 125U, &used,
				(const BYTE *) "\""))
	{
	  G.g_tail[1] = 0;
	  return(FALSE);
	}
	return(TRUE);
} /* pro_cmd */


WORD pro_run(WORD isgraf, WORD isover, WORD wh, WORD curr)
{
	WORD		ret, len, i;

	G.g_tail[0] = len = strlen(&G.g_tail[1]);
	if ( (len) && (!isgraf) )
	{
	  for(i = len; i; i--)
	    G.g_tail[i+1] = G.g_tail[i];
	  G.g_tail[1] = ' ';
	  len++;
	} /* if */
	G.g_tail[0] = len;
	G.g_tail[len+1] = 0x0D;
#if MULTIAPP
	if (isover != 3)		/* keep icon SELECTED during FORMAT */
	  do_wopen(FALSE, wh, curr, G.g_xdesk, G.g_ydesk, G.g_wdesk, G.g_hdesk);
#endif
	ret = pro_exec(isgraf, isover, G.a_cmd, G.a_tail);
	if (isover == -1)
	  ret = FALSE;
	else
	{
#if MULTIAPP
	  if (isover == 3)			/* for FORMAT		*/
#else
	  if (wh != -1)
#endif
	    do_wopen(FALSE, wh, curr, G.g_xdesk, G.g_ydesk,
	    	     G.g_wdesk, G.g_hdesk);
	} /* else */
	return(ret);
} /* pro_run */



WORD pro_exec(WORD isgraf, WORD isover, LPBYTE pcmd, LPBYTE ptail)
{
	WORD		ret;

#if MULTIAPP
	WORD		chnum;
	GEM_U32_WORDS	base_hint;
	GEM_U32_WORDS	size_hint;

	if (isover != 3)
#endif
	graf_mouse(HGLASS, 0);

#if MULTIAPP
	if ((isover == -1) || (isover == 2) || (isover == 3))
	{
	  /*
	   * GEM/XM selected a DOS paragraph range before loading a channel.
	   * ELKS gives every exec a kernel-owned data and stack segment, so the
	   * historical base is deliberately zero.  The size is retained only as
	   * an unscaled byte-count hint in two 16-bit words; it never reserves or
	   * walks physical memory in Desktop.
	   */
	  base_hint = gem_u32_words(0, 0);
	  size_hint = gem_u32_words(0, 0);

	  ret = proc_create(base_hint, size_hint, 1, isgraf, &chnum);
	  if (!ret)
	  {
	    fun_alert(1,STNOROOM);
	    return(FALSE);
	  }
	  ret = proc_run(chnum, isgraf, isover, pcmd, ptail);
	  if (!ret)
	    proc_delete(chnum);
	  if (isover==3)
	    ret = 0;
	}
	else
#endif
	  ret = shel_write(TRUE, isgraf, isover, pcmd, ptail);
	if (!ret)
	{
	  graf_mouse(ARROW, 0);
	}
	return( ret );
} /*  */



WORD pro_exit(LPBYTE pcmd, LPBYTE ptail)
{
	WORD		ret;

	ret = shel_write(FALSE, FALSE, 1, pcmd, ptail);
	return( ret );
} /* pro_exit */
