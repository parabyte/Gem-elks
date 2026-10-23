/* JCE 1-3-1998: replaces VDIASM.ASM. ELKS enters INT EFh directly. */

#include "ppdgem.h"

#define GEM_VECTOR_OFFSET 0x03bcU

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

	/* point the trap block at the static arrays */
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
	UWORD offset;
	UWORD selector;
	WORD result;

	gem_bindings_vdi_ensure();
	/* CX=0473h, DS:DX=parameter block */
	offset = (UWORD) &pblock;
	selector = 0x0473U;
	__asm__ volatile ("int $0xef"
			  : "=a" (result), "+c" (selector), "+d" (offset)
			  :
			  : "cc", "memory");
	return result;
}

WORD
gemcheck(VOID)
{
	UWORD target_offset;
	UWORD target_segment;

	target_offset = gem_bindings_far_word(0, GEM_VECTOR_OFFSET);
	target_segment = gem_bindings_far_word(0, GEM_VECTOR_OFFSET + 2);
	target_offset += 2;

	if (gem_bindings_far_byte(target_segment, target_offset++) != (UBYTE) 'G')
		return FALSE;
	if (gem_bindings_far_byte(target_segment, target_offset++) != (UBYTE) 'E')
		return FALSE;
	if (gem_bindings_far_byte(target_segment, target_offset) != (UBYTE) 'M')
		return FALSE;
	return TRUE;
}
