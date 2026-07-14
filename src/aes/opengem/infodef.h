/*	INFODEF.H	09/26/84 - 09/26/84		Gregg Morris	*/
/*	merge source	5/26/87				mdf		*/

/*
*       Copyright 1999, Caldera Thin Clients, Inc.                      
*       This software is licenced under the GNU Public License.         
*       Please see LICENSE.TXT for further information.                 
*                                                                       
*                  Historical Copyright                                 
*	-------------------------------------------------------------
*	GEM Desktop					  Version 2.3
*	Serial No.  XXXX-0000-654321		  All Rights Reserved
*	Copyright (C) 1985, 1986		Digital Research Inc.
*	-------------------------------------------------------------
*/

#ifndef GEM_FCB_DEFINED
typedef struct __attribute__((packed)) fcb
{
	BYTE		fcb_reserved[21];
	BYTE		fcb_attr;
	WORD		fcb_time;
	WORD		fcb_date;
	/*
	 * GEMDOS stores the unscaled byte size as two little-endian words.
	 * Code must use the carry-aware GEM word-pair helpers for arithmetic;
	 * this packed fallback retains the original 43-byte DTA layout.
	 */
	GEM_U32_WORDS	fcb_size;
	BYTE		fcb_name[13];
} FCB;
#endif

#define ARROW 0
#define HGLASS 2

#define FLOPPY 0
#define HARD 1

