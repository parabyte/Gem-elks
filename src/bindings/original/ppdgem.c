/* JCE 1-3-1998: replaces GEMASM.ASM. ELKS enters INT EFh directly. */

#include "ppdgem.h"

#define GEM_VECTOR_OFFSET 0x03bcU

UWORD
gem_bindings_data_segment(VOID)
{
	UWORD segment;

	/* read the caller data segment */
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

	/* turn a near pointer into DS:offset */
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
	/* near pointers only reach DS */
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

	/* read one word from segment:offset */
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

WORD
gem(GEM_BINDINGS_AESPB *parameter_block)
{
	UWORD offset;
	UWORD segment;
	UWORD selector;
	UWORD zero;
	WORD result;

	/* CX=200, DX=0, ES:BX=parameter block */
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

	/* INT EFh vector, offset then segment */
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
