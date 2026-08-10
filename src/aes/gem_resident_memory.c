/*
 * gem_resident_memory.c - checked client-memory copies for resident GEM
 *
 * ELKS keeps the clients data segment pinned until the resident owner
 * replies so the server reads and writes it directly, DS/ES change
 * only for one forward byte copy, callers check the range first
 */

#include "gem_resident_memory.h"

static UWORD
gem_resident_memory_data_segment(VOID)
{
	UWORD segment;

	__asm__ volatile ("movw %%ds,%0":"=r" (segment));
	return segment;
}

WORD
gem_resident_memory_range(UWORD offset, UWORD count, UWORD limit)
{
	if (offset > limit)
		return FALSE;
	return count <= (UWORD) (limit - offset);
}

WORD
gem_resident_memory_pointer(const struct gemtrap_request *request,
	GEM_BINDINGS_POINTER_SLOT pointer, UWORD count)
{
	if (!request)
		return FALSE;
	if (!count)
		return TRUE;
	if (pointer.hi != request->ds)
		return FALSE;
	return gem_resident_memory_range(pointer.lo, count,
		request->data_limit);
}

VOID
gem_resident_memory_from(UWORD client_segment, UWORD source_offset,
	VOID *destination, UWORD count)
{
	UWORD owner_segment;

	owner_segment = gem_resident_memory_data_segment();
	__asm__ volatile ("pushw %%ds\n\t"
		"pushw %%es\n\t"
		"movw %4,%%es\n\t"
		"movw %3,%%ds\n\t"
		"cld\n\t"
		"rep movsb\n\t"
		"popw %%es\n\t"
		"popw %%ds":"+S" (source_offset), "+D"(destination), "+c"(count)
		:"r"(client_segment), "r"(owner_segment)
		:"cc", "memory");
}

VOID
gem_resident_memory_to(const VOID *source, UWORD client_segment,
	UWORD destination_offset, UWORD count)
{
	__asm__ volatile ("pushw %%es\n\t"
		"movw %3,%%es\n\t"
		"cld\n\t"
		"rep movsb\n\t"
		"popw %%es":"+S" (source), "+D"(destination_offset), "+c"(count)
		:"r"(client_segment)
		:"cc", "memory");
}

VOID
gem_resident_memory_fill(UWORD client_segment, UWORD destination_offset,
	UBYTE value, UWORD count)
{
	UWORD fill;

	fill = value;		/* STOSB uses AL only */
	__asm__ volatile ("pushw %%es\n\t"
		"movw %3,%%es\n\t"
		"cld\n\t"
		"rep stosb\n\t"
		"popw %%es":"+D" (destination_offset), "+c"(count)
		:"a"(fill), "r"(client_segment)
		:"cc", "memory");
}

WORD
gem_resident_memory_word_bytes(UWORD words, UWORD maximum, UWORD *bytes)
{
	if (!bytes || words > maximum)
		return FALSE;
	*bytes = words;
	__asm__ volatile ("shlw %0":"+r" (*bytes)::"cc");
	return TRUE;
}

WORD
gem_resident_memory_point_bytes(UWORD points, UWORD maximum, UWORD *bytes)
{
	if (!bytes || points > maximum)
		return FALSE;
	*bytes = points;
	__asm__ volatile ("shlw %0\n\tshlw %0":"+r" (*bytes)::"cc");
	return TRUE;
}

WORD
gem_resident_memory_slot_bytes(UWORD slots, UWORD maximum, UWORD *bytes)
{
	if (!bytes || slots > maximum)
		return FALSE;
	*bytes = slots;
	__asm__ volatile ("shlw %0\n\tshlw %0":"+r" (*bytes)::"cc");
	return TRUE;
}
