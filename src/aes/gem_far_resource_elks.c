/* direct ELKS paragraph allocation for GEM resources */

#include <errno.h>
#include <malloc.h>

#include "gem_far_resource.h"

/* these calls take paragraphs */

#define GEM_FAR_PARAGRAPH_MASK 15U

static VOID
gem_far_resource_clear(GEM_FAR_RESOURCE *resource)
{
	resource->base.lo = 0;
	resource->base.hi = 0;
	resource->bytes = 0;
}

/* round without overflowing */
static UWORD
gem_far_resource_paragraphs(UWORD bytes)
{
	UWORD paragraphs;

	paragraphs = bytes;
	__asm__ volatile ("shrw %0\n\t"
		"shrw %0\n\t" "shrw %0\n\t" "shrw %0":"+r" (paragraphs)
		::"cc");
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

	/* the new segment starts at offset zero */
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

	/* subtract before comparing */
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

	/* copy DS:SI to ES:DI */
	__asm__ volatile ("pushw %%es\n\t"
		"movw %3,%%es\n\t"
		"cld\n\t"
		"rep movsb\n\t"
		"popw %%es":"+S" (source), "+D"(offset), "+c"(count)
		:"r"(segment)
		:"cc", "memory");
	return 1;
}
