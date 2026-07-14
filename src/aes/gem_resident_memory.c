/*
 * gem_resident_memory.c - one checked client-memory seam for resident GEM.
 *
 * ELKS keeps the delivered client's data segment pinned until the resident
 * owner replies.  The assembly below changes DS or ES only for the duration
 * of one forward byte copy and restores every segment register before GNU C
 * resumes.  Count is an unscaled 16-bit byte count.  A caller validates the
 * range first; a zero count performs no memory access.
 */

#include "gem_resident_memory.h"

static UWORD
gem_resident_memory_data_segment(VOID)
{
	UWORD segment;

	/* MOV from DS is available on every 8088/8086 and changes no flags. */
	__asm__ volatile ("movw %%ds,%0" : "=r" (segment));
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
			  "popw %%ds"
			  : "+S" (source_offset), "+D" (destination),
			    "+c" (count)
			  : "r" (client_segment), "r" (owner_segment)
			  : "cc", "memory");
}

VOID
gem_resident_memory_to(const VOID *source, UWORD client_segment,
	UWORD destination_offset, UWORD count)
{
	__asm__ volatile ("pushw %%es\n\t"
			  "movw %3,%%es\n\t"
			  "cld\n\t"
			  "rep movsb\n\t"
			  "popw %%es"
			  : "+S" (source), "+D" (destination_offset),
			    "+c" (count)
			  : "r" (client_segment)
			  : "cc", "memory");
}

VOID
gem_resident_memory_fill(UWORD client_segment, UWORD destination_offset,
	UBYTE value, UWORD count)
{
	UWORD fill;

	/*
	 * STOSB consumes AL only.  Clearing the high byte makes the register
	 * value deterministic for the compiler without changing the unscaled
	 * eight-bit fill value or introducing a wider temporary.
	 */
	fill = value;
	__asm__ volatile ("pushw %%es\n\t"
			  "movw %3,%%es\n\t"
			  "cld\n\t"
			  "rep stosb\n\t"
			  "popw %%es"
			  : "+D" (destination_offset), "+c" (count)
			  : "a" (fill), "r" (client_segment)
			  : "cc", "memory");
}

WORD
gem_resident_memory_word_bytes(UWORD words, UWORD maximum, UWORD *bytes)
{
	if (!bytes || words > maximum)
		return FALSE;
	*bytes = words;
	__asm__ volatile ("shlw %0" : "+r" (*bytes) : : "cc");
	return TRUE;
}

WORD
gem_resident_memory_point_bytes(UWORD points, UWORD maximum, UWORD *bytes)
{
	if (!bytes || points > maximum)
		return FALSE;
	*bytes = points;
	__asm__ volatile ("shlw %0\n\tshlw %0"
			  : "+r" (*bytes) : : "cc");
	return TRUE;
}

WORD
gem_resident_memory_slot_bytes(UWORD slots, UWORD maximum, UWORD *bytes)
{
	if (!bytes || slots > maximum)
		return FALSE;
	*bytes = slots;
	__asm__ volatile ("shlw %0\n\tshlw %0"
			  : "+r" (*bytes) : : "cc");
	return TRUE;
}
