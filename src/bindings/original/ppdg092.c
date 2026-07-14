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

WORD proc_run(WORD proc_num, WORD isgraf, WORD isover, LPBYTE pcmd,
		                LPBYTE ptail)
{
        PR_NUM = proc_num;
        PR_ISGRAF = isgraf;
        PR_ISOVER = isover;
        gem_bindings_store_pointer(&PR_PCMD, pcmd);
        gem_bindings_store_pointer(&PR_PTAIL, ptail);
        return( gem_if(PROC_RUN) );
}

#endif /* GEM_BINDINGS_ENABLE_DOS_PROCESS */
