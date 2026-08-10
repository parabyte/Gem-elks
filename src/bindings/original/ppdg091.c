/*****************************************************************************
 *
 * Copyright 2006, John Elliott <jce@seasip.demon.co.uk>
 * Copyright 1999, Caldera Thin Clients, Inc.
 *
 *       This software is licenced under the GNU Public License.         
 *       Please see LICENSE.TXT for further information.                 
 *
 *****************************************************************************/

#include "ppdgem.h"
#include "ppdg0.h"

#if GEM_BINDINGS_ENABLE_DOS_PROCESS

WORD proc_create(GEM_BINDINGS_POINTER_SLOT ibegaddr,
			GEM_BINDINGS_POINTER_SLOT isize,
			WORD isswap, WORD isgem,
		                WORD *onum)
{
        WORD    ret;

        PR_IBEGADDR = ibegaddr;
        PR_ISIZE = isize;
        PR_ISSWAP = isswap;
        PR_ISGEM = isgem;
        ret = gem_if(PROC_CREATE); 
        *onum = PR_ONUM;
	return(ret);
}

#endif /* GEM_BINDINGS_ENABLE_DOS_PROCESS */
