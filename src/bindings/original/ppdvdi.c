/* VDI calls over the stock ELKS UNIX socket */

#include "ppdgem.h"

#ifndef USER_INTIN
GLOBAL WORD contrl[11];
GLOBAL WORD intin[80];
GLOBAL WORD ptsin[256];
GLOBAL WORD intout[45];
GLOBAL WORD ptsout[12];
#else
EXTERN WORD contrl[];
EXTERN WORD intin[];
EXTERN WORD ptsin[];
EXTERN WORD intout[];
EXTERN WORD ptsout[];
#endif

GLOBAL GEM_BINDINGS_VDIPB pblock;

static UBYTE pblock_ready;

VOID
gem_bindings_vdi_ensure(VOID)
{
	if (pblock_ready)
		return;

	/* point the request block at the static arrays */
	gem_bindings_store_pointer(&pblock.contrl, contrl);
	gem_bindings_store_pointer(&pblock.intin, intin);
	gem_bindings_store_pointer(&pblock.ptsin, ptsin);
	gem_bindings_store_pointer(&pblock.intout, intout);
	gem_bindings_store_pointer(&pblock.ptsout, ptsout);
	pblock_ready = TRUE;
}

VOID
gem_bindings_vdi_set_slot(GEM_BINDINGS_POINTER_SLOT *slot,
			  const VOID FAR *pointer)
{
	/* let a binding redirect one array */
	gem_bindings_vdi_ensure();
	gem_bindings_store_pointer(slot, pointer);
}

WORD
vdi(VOID)
{
	gem_bindings_vdi_ensure();
	return gem_bindings_call(0x0473U, (UWORD) &pblock);
}

WORD
gemcheck(VOID)
{
	return gem_bindings_connect();
}
