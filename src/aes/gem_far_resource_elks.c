/*
 * gem_far_resource_elks.c - ELKS paragraph allocation for GEM resources.
 *
 * It is linked only into the resident AES owner.  The process-local migration
 * Desktop and compact client bindings never acquire a competing resource
 * arena; each application block is released by its resident PD generation.
 */

#include <errno.h>

#include "gem_far_resource.h"

/*
 * Use the raw 16-bit ELKS system-call interfaces.  libc's convenient
 * fmemalloc() interface accepts a wider byte scalar and constructs its far
 * pointer with wider arithmetic, neither of which is needed here.
 */
extern int _fmemalloc(int paragraphs, unsigned short *segment);
extern int _fmemfree(unsigned short segment);

#define GEM_FAR_PARAGRAPH_MASK 15U

static VOID
gem_far_resource_clear(GEM_FAR_RESOURCE *resource)
{
	resource->base.lo = 0;
	resource->base.hi = 0;
	resource->bytes = 0;
}

/*
 * Round bytes upward to paragraphs using only 16-bit operations.  Four
 * one-bit shifts are explicitly requested because they are native 8086
 * instructions.  The common `(bytes + 15) >> 4` expression is not used: its
 * addition would wrap for byte counts from 65521 through 65535.
 *
 * For every nonzero 16-bit byte count this returns 1 through 4096.  The value
 * 4096 itself still fits in one word even though its byte capacity does not.
 */
static UWORD
gem_far_resource_paragraphs(UWORD bytes)
{
	UWORD paragraphs;

	paragraphs = bytes;
	__asm__ volatile ("shrw %0\n\t"
			  "shrw %0\n\t"
			  "shrw %0\n\t"
			  "shrw %0"
			  : "+r" (paragraphs)
			  :
			  : "cc");
	if (bytes & GEM_FAR_PARAGRAPH_MASK)
		paragraphs++;
	return paragraphs;
}

WORD
gem_far_resource_alloc(GEM_FAR_RESOURCE *resource, UWORD bytes)
{
	UWORD paragraphs;
	unsigned short segment;

	if (!resource) {
		errno = EINVAL;
		return 0;
	}

	gem_far_resource_clear(resource);
	if (!bytes) {
		errno = EINVAL;
		return 0;
	}

	paragraphs = gem_far_resource_paragraphs(bytes);
	segment = 0;
	if (_fmemalloc((WORD) paragraphs, &segment) != 0)
		return 0;

	/*
	 * ELKS allocates an independent segment at offset zero.  Keeping that
	 * zero is what lets original GEM resource offsets become far pointers by
	 * copying the segment word, without a 32-bit base addition.
	 */
	resource->base.lo = 0;
	resource->base.hi = (UWORD) segment;
	resource->bytes = bytes;
	return 1;
}

WORD
gem_far_resource_free(GEM_FAR_RESOURCE *resource)
{
	if (!resource || resource->base.lo != 0 || resource->base.hi == 0
	    || resource->bytes == 0) {
		errno = EINVAL;
		return 0;
	}

	if (_fmemfree((unsigned short) resource->base.hi) != 0)
		return 0;

	gem_far_resource_clear(resource);
	return 1;
}

WORD
gem_far_resource_copy_in(const GEM_FAR_RESOURCE *resource,
			 UWORD offset, const UBYTE *source, UWORD count)
{
	UWORD segment;

	/*
	 * Subtract before comparing the count.  Testing `offset + count` would
	 * allow a wrapped 16-bit sum to make an invalid span appear valid.
	 * A zero-length copy may name the exact one-past byte and needs no source.
	 */
	if (!resource || resource->base.lo != 0 || resource->base.hi == 0
	    || resource->bytes == 0 || offset > resource->bytes
	    || count > resource->bytes - offset) {
		errno = EINVAL;
		return 0;
	}
	if (!count)
		return 1;
	if (!source) {
		errno = EINVAL;
		return 0;
	}

	segment = resource->base.hi;

	/*
	 * DS:SI is the caller's near source and ES:DI is the exact resource
	 * destination.  ES is preserved for GNU C, SI/DI/CX are described as
	 * read-write operands, and CLD leaves the ABI-required forward direction.
	 * REP MOVSB and every surrounding instruction exist on the original 8086.
	 */
	__asm__ volatile ("pushw %%es\n\t"
			  "movw %3,%%es\n\t"
			  "cld\n\t"
			  "rep movsb\n\t"
			  "popw %%es"
			  : "+S" (source), "+D" (offset), "+c" (count)
			  : "r" (segment)
			  : "cc", "memory");
	return 1;
}
