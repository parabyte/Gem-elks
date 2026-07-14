/*
 * gem_far_resource.h - ELKS resident far-resource memory seam.
 *
 * Classic GEM returns resource addresses as an 8086 offset and segment.
 * Keeping those two words explicit prevents a 16-bit compiler from adding
 * 32-bit arithmetic helpers merely to move an address between interfaces.
 * This header does not change the active near-pointer AES types and does not
 * imply that a resident AES resource service is present.
 */

#ifndef ELKS_GEM_FAR_RESOURCE_H
#define ELKS_GEM_FAR_RESOURCE_H

#include "aes.h"

/*
 * `lo` is an unscaled byte offset and `hi` is an 8086 segment value.  There
 * is no rounding, saturation, or normalization when this address crosses an
 * interface.  0:0 is the only null address.
 */
typedef GEM_U32_WORDS GEM_FAR_ADDRESS;

/*
 * ELKS _fmemalloc() returns a paragraph-aligned segment, so every resource
 * block begins at offset zero.  `bytes` retains the caller's exact byte
 * count; it deliberately does not contain the rounded allocation size,
 * because a 4096-paragraph allocation is 65536 bytes and cannot be expressed
 * in one 16-bit scalar.
 */
typedef struct gem_far_resource {
	GEM_FAR_ADDRESS base;
	UWORD bytes;
} GEM_FAR_RESOURCE;

/* Compile-time layout checks protect the offset:segment ABI. */
typedef BYTE GEM_FAR_ADDRESS_MUST_BE_4_BYTES
	[(sizeof(GEM_FAR_ADDRESS) == 4) ? 1 : -1];
typedef BYTE GEM_FAR_RESOURCE_MUST_BE_6_BYTES
	[(sizeof(GEM_FAR_RESOURCE) == 6) ? 1 : -1];

/*
 * Allocate and free one caller-owned ELKS far segment.  Allocation rounds a
 * nonzero byte count upward to a 16-byte paragraph without allowing 16-bit
 * addition to wrap.  On allocation failure `resource` is cleared and errno
 * is retained from ELKS.  Free clears the descriptor only after the kernel
 * confirms that the calling process owns and released the segment.
 */
WORD gem_far_resource_alloc(GEM_FAR_RESOURCE *resource, UWORD bytes);
WORD gem_far_resource_free(GEM_FAR_RESOURCE *resource);

/*
 * Copy caller-DS bytes into the resource with one 8086 REP MOVSB.  `offset`
 * and `count` are unscaled bytes.  The function rejects every span outside
 * the exact `bytes` extent; it never permits offset wrap into another segment.
 */
WORD gem_far_resource_copy_in(const GEM_FAR_RESOURCE *resource,
			      UWORD offset, const UBYTE *source,
			      UWORD count);

#endif /* ELKS_GEM_FAR_RESOURCE_H */
