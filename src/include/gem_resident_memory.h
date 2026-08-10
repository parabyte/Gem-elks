/*
 * gem_resident_memory.h - checked client-memory access for resident GEM
 *
 * a GEM trap pins one client data segment. every classic address stays an
 * offset/segment word pair and any range outside that segment is refused
 */

#ifndef ELKS_GEM_RESIDENT_MEMORY_H
#define ELKS_GEM_RESIDENT_MEMORY_H

#include "gemtrap.h"

#include "gem_bindings_elks.h"

/* check the half-open byte range [offset, offset + count) */
WORD gem_resident_memory_range(UWORD offset, UWORD count, UWORD limit);

/* check a classic far slot against the one DS this request pinned */
WORD gem_resident_memory_pointer(const struct gemtrap_request *request,
	GEM_BINDINGS_POINTER_SLOT pointer, UWORD count);

/* copy bytes between a pinned client segment and resident near data */
VOID gem_resident_memory_from(UWORD client_segment, UWORD source_offset,
	VOID *destination, UWORD count);
VOID gem_resident_memory_to(const VOID *source, UWORD client_segment,
	UWORD destination_offset, UWORD count);

/* fill one checked client range with a byte value, the caller checks offset and count first */
VOID gem_resident_memory_fill(UWORD client_segment,
	UWORD destination_offset, UBYTE value, UWORD count);

/* turn a bounded word count into bytes, the maximum check stops wrap */
WORD gem_resident_memory_word_bytes(UWORD words, UWORD maximum, UWORD *bytes);

/* turn a bounded point count into bytes, a VDI point is four bytes */
WORD gem_resident_memory_point_bytes(UWORD points, UWORD maximum, UWORD *bytes);

/* turn a bounded four-byte offset/segment slot count into bytes */
WORD gem_resident_memory_slot_bytes(UWORD slots, UWORD maximum, UWORD *bytes);

#endif				/* ELKS_GEM_RESIDENT_MEMORY_H */
