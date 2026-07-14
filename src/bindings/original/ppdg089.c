/*****************************************************************************
 *
 *	-------------------------------------------------------------
 *	GEM Desktop					  Version 3.1
 *	Serial No.  XXXX-0000-654321		  All Rights Reserved
 *	Copyright (C) 1985 - 1989	Digital Research Inc.
 *	-------------------------------------------------------------
 *
 *****************************************************************************/

#include "ppdgem.h"
#include "ppdg0.h"

WORD appl_xbvset(GEM_U32_WORDS bvdisk, GEM_U32_WORDS bvhard)
{
  AP_XBVMODE  = 1;
  AP_XBVDISK  = bvdisk;
  AP_XBVHARD  = bvhard;

  return gem_if( APPL_XBVSET );
}
