/*
 * JCE 1-3-1998: this replaces GEMASM.ASM.
 *
 * The original replacement used a DOS compiler's int86x helper.  ELKS does
 * not provide that DOS register-union ABI, and calling through such a helper
 * would add avoidable setup on an 8088.  The small GNU ia16 seam below keeps
 * the original INT EF register contract directly.
 */

#include "ppdgem.h"

#define GEM_VECTOR_OFFSET 0x03bcU

static AESFUNC pAes = NULL;

UWORD
gem_bindings_data_segment(VOID)
{
	UWORD segment;

	/* MOV from DS is an original 8086 instruction and changes no flags. */
	__asm__ volatile ("movw %%ds,%0" : "=r" (segment));
	return segment;
}

VOID
gem_bindings_store_pointer(GEM_BINDINGS_POINTER_SLOT *slot,
			   const VOID FAR *pointer)
{
#if GEM_TRAP_FAR_DATA
	union gem_binding_far_pointer {
		const VOID FAR *pointer;
		GEM_BINDINGS_POINTER_SLOT words;
	} value;
#endif

	/*
	 * GNU ia16 near data pointers are one unscaled 16-bit byte offset.  A
	 * non-null pointer therefore needs the current DS in the high word when
	 * it crosses the classic GEM far-pointer ABI.  No arithmetic is done.
	 */
	if (!pointer) {
		slot->lo = 0;
		slot->hi = 0;
		return;
	}

#if GEM_TRAP_FAR_DATA
	value.pointer = pointer;
	*slot = value.words;
#else
	slot->lo = (UWORD) pointer;
	slot->hi = gem_bindings_data_segment();
#endif
}

GEM_SLOT_POINTER
gem_bindings_load_pointer(const GEM_BINDINGS_POINTER_SLOT *slot)
{
#if GEM_TRAP_FAR_DATA
	union gem_binding_far_pointer {
		GEM_SLOT_POINTER pointer;
		GEM_BINDINGS_POINTER_SLOT words;
	} value;

	value.words = *slot;
	return value.pointer;
#else
	/*
	 * A near C pointer cannot retain a foreign segment.  Accept 0:0 as null
	 * and DS-relative addresses exactly; reject every other segment instead
	 * of silently truncating it.  The offset is copied without rounding.
	 */
	if (!slot->lo && !slot->hi)
		return NULL;
	if (slot->hi != gem_bindings_data_segment())
		return NULL;
	return (GEM_SLOT_POINTER) slot->lo;
#endif
}

UWORD
gem_bindings_far_word(UWORD segment, UWORD offset)
{
	UWORD value;

	/*
	 * ES is saved and restored so callers retain the GNU C data-segment
	 * environment.  BX contains the exact 16-bit byte offset; an address at
	 * the end of a segment wraps exactly as an 8086 ES:BX memory reference.
	 */
	__asm__ volatile ("pushw %%es\n\t"
			  "movw %1,%%es\n\t"
			  "movw %%es:(%%bx),%0\n\t"
			  "popw %%es"
			  : "=a" (value)
			  : "r" (segment), "b" (offset)
			  : "memory");
	return value;
}

UBYTE
gem_bindings_far_byte(UWORD segment, UWORD offset)
{
	UBYTE value;

	__asm__ volatile ("pushw %%es\n\t"
			  "movw %1,%%es\n\t"
			  "movb %%es:(%%bx),%0\n\t"
			  "popw %%es"
			  : "=a" (value)
			  : "r" (segment), "b" (offset)
			  : "memory");
	return value;
}

AESFUNC
divert_aes(AESFUNC function)
{
	AESFUNC previous;

	previous = pAes;
	if (function == gem)
		function = NULL;
	pAes = function;
	return previous;
}

WORD
gem(GEM_BINDINGS_AESPB *parameter_block)
{
	UWORD offset;
	UWORD segment;
	UWORD selector;
	UWORD zero;
	WORD result;

	if (pAes)
		return (*pAes)(parameter_block);

	/*
	 * Original AES trap contract:
	 *
	 *   CX = 200, DX = 0, ES:BX = packed AES parameter block, INT EF.
	 *
	 * The parameter block is static near data in normal use.  Supplying DS
	 * explicitly in ES preserves the original far-pointer calling convention
	 * without a DOS register-union wrapper or a segment reload after return.
	 */
	offset = (UWORD) parameter_block;
	segment = gem_bindings_data_segment();
	selector = 200;
	zero = 0;
	__asm__ volatile ("pushw %%es\n\t"
			  "movw %4,%%es\n\t"
			  "int $0xef\n\t"
			  "popw %%es"
			  : "=a" (result), "+b" (offset),
			    "+c" (selector), "+d" (zero)
			  : "r" (segment)
			  : "cc", "memory");
	return result;
}

WORD
aescheck(VOID)
{
	static const UBYTE signature[6] = {
		(UBYTE) 'G', (UBYTE) 'E', (UBYTE) 'M',
		(UBYTE) 'A', (UBYTE) 'E', (UBYTE) 'S'
	};
	UWORD target_offset;
	UWORD target_segment;
	UWORD count;

	/*
	 * The real-mode interrupt table starts at 0000:0000 and stores each
	 * vector as offset followed by segment.  INT EF begins at byte 03bc.
	 */
	target_offset = gem_bindings_far_word(0, GEM_VECTOR_OFFSET);
	target_segment = gem_bindings_far_word(0, GEM_VECTOR_OFFSET + 2);
	target_offset += 2;
	count = 0;
	while (count < 6) {
		if (gem_bindings_far_byte(target_segment, target_offset)
		    != signature[count])
			return FALSE;
		target_offset++;
		count++;
	}
	return TRUE;
}
