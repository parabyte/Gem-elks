/*
 * JCE 1-3-1998: this replaces VDIASM.ASM.
 *
 * The GNU ia16 version retains the original VDI parameter arrays and INT EF
 * control flow.  Only the DOS int86x wrapper and compiler-specific far C
 * pointers are replaced by explicit offset/segment words.
 */

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
static VDIFUNC pVdi = NULL;

VOID
gem_bindings_vdi_ensure(VOID)
{
	if (pblock_ready)
		return;

	/*
	 * These five stores reproduce the original GSX parameter block.  Each
	 * array is near static data, so its far trap address is DS:offset.
	 */
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
	/* A wrapper may redirect one array before the first call to vdi(). */
	gem_bindings_vdi_ensure();
	gem_bindings_store_pointer(slot, pointer);
}

VDIFUNC
divert_vdi(VDIFUNC function)
{
	VDIFUNC previous;

	previous = pVdi;
	if (function == (VDIFUNC) vdi)
		function = NULL;
	pVdi = function;
	return previous;
}

WORD
vdi(VOID)
{
	UWORD offset;
	UWORD selector;
	WORD result;

	gem_bindings_vdi_ensure();
	if (pVdi)
		return (*pVdi)(&pblock);

	/*
	 * Original VDI trap contract: DS:DX points at the packed five-slot
	 * parameter block, CX is 0473h, and INT EF returns the status in AX.
	 * The block is static near data, so DS already names its segment.
	 */
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
