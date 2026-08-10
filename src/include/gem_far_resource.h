/*
 * gem_far_resource.h - ELKS far-resource memory seam
 *
 * classic GEM passes resource addresses as an 8086 offset and segment, the
 * two words stay separate here
 */

#ifndef ELKS_GEM_FAR_RESOURCE_H
#define ELKS_GEM_FAR_RESOURCE_H

#include "aes.h"

/* lo is a byte offset, hi an 8086 segment, 0:0 is the only null */
typedef GEM_U32_WORDS GEM_FAR_ADDRESS;

/* _fmemalloc() gives a paragraph-aligned segment so every block starts at offset zero. bytes keeps the exact count not the rounded size, 4096 paragraphs is 65536 bytes which dont fit one word */
typedef struct gem_far_resource {
	GEM_FAR_ADDRESS base;
	UWORD bytes;
} GEM_FAR_RESOURCE;

typedef BYTE GEM_FAR_ADDRESS_MUST_BE_4_BYTES
	[(sizeof(GEM_FAR_ADDRESS) == 4) ? 1 : -1];
typedef BYTE GEM_FAR_RESOURCE_MUST_BE_6_BYTES
	[(sizeof(GEM_FAR_RESOURCE) == 6) ? 1 : -1];

/* alloc and free one caller-owned ELKS far segment. on failure resource is cleared and errno keeps whatever ELKS set */
WORD gem_far_resource_alloc(GEM_FAR_RESOURCE *resource, UWORD bytes);
WORD gem_far_resource_free(GEM_FAR_RESOURCE *resource);

/* copy bytes from the caller's DS into the resource. spans outside the exact bytes extent are rejected so the offset cant wrap into another segment */
WORD gem_far_resource_copy_in(const GEM_FAR_RESOURCE *resource,
	UWORD offset, const UBYTE *source, UWORD count);

#endif				/* ELKS_GEM_FAR_RESOURCE_H */
