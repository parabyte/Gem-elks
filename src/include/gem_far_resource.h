/* ELKS far resource segments */

#ifndef ELKS_GEM_FAR_RESOURCE_H
#define ELKS_GEM_FAR_RESOURCE_H

#include "aes.h"

/* offset first, segment second */
typedef GEM_U32_WORDS GEM_FAR_ADDRESS;

/* keep the exact byte limit */
typedef struct gem_far_resource {
	GEM_FAR_ADDRESS base;
	UWORD bytes;
} GEM_FAR_RESOURCE;

typedef BYTE GEM_FAR_ADDRESS_MUST_BE_4_BYTES
	[(sizeof(GEM_FAR_ADDRESS) == 4) ? 1 : -1];
typedef BYTE GEM_FAR_RESOURCE_MUST_BE_6_BYTES
	[(sizeof(GEM_FAR_RESOURCE) == 6) ? 1 : -1];

/* allocate one ELKS far segment */
WORD gem_far_resource_alloc(GEM_FAR_RESOURCE *resource, UWORD bytes);
WORD gem_far_resource_free(GEM_FAR_RESOURCE *resource);

/* copy one checked span into the segment */
WORD gem_far_resource_copy_in(const GEM_FAR_RESOURCE *resource,
	UWORD offset, const UBYTE *source, UWORD count);

#endif				/* ELKS_GEM_FAR_RESOURCE_H */
