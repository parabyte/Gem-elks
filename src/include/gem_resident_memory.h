/*
 * gem_resident_memory.h - checked 8086 client-memory access for resident GEM.
 *
 * A delivered ELKS GEM trap pins exactly one client data segment.  Resident
 * AES and VDI code must therefore retain every historical address as an
 * offset/segment word pair and must reject any range outside that segment.
 * These helpers perform the original byte copies directly with REP MOVSB;
 * they do not flatten an address, allocate a conversion buffer, or form a C
 * value wider than one 8086 word.
 */

#ifndef ELKS_GEM_RESIDENT_MEMORY_H
#define ELKS_GEM_RESIDENT_MEMORY_H

#include "gemtrap.h"

#include "gem_bindings_elks.h"

/* Validate the half-open byte interval [offset, offset + count). */
WORD gem_resident_memory_range(UWORD offset, UWORD count, UWORD limit);

/* Validate a classic far slot against the one DS pinned by this request. */
WORD gem_resident_memory_pointer(const struct gemtrap_request *request,
	GEM_BINDINGS_POINTER_SLOT pointer, UWORD count);

/* Copy bytes between a pinned client segment and resident near data. */
VOID gem_resident_memory_from(UWORD client_segment, UWORD source_offset,
	VOID *destination, UWORD count);
VOID gem_resident_memory_to(const VOID *source, UWORD client_segment,
	UWORD destination_offset, UWORD count);

/*
 * Fill one validated client range with an unscaled byte value.  The caller
 * validates offset and count first.  REP STOSB writes exactly count bytes;
 * a zero count performs no access and there is no allocation or staging
 * copy, which is important for the original 2048-byte SHEL_GET buffer.
 */
VOID gem_resident_memory_fill(UWORD client_segment,
	UWORD destination_offset, UBYTE value, UWORD count);

/*
 * Convert a bounded word count to bytes with one 8086 single-bit shift.
 * The maximum is checked first, so the result cannot wrap.  There is no
 * rounding or saturation: success returns exactly words times two bytes.
 */
WORD gem_resident_memory_word_bytes(UWORD words, UWORD maximum,
	UWORD *bytes);

/*
 * Convert a bounded point-pair count to bytes.  Each VDI point contains two
 * 16-bit coordinates, so success returns exactly points times four bytes.
 * Two single-bit shifts are used; the prechecked maximum prevents overflow.
 */
WORD gem_resident_memory_point_bytes(UWORD points, UWORD maximum,
	UWORD *bytes);

/* Convert a bounded four-byte offset/segment slot count to bytes. */
WORD gem_resident_memory_slot_bytes(UWORD slots, UWORD maximum,
	UWORD *bytes);

#endif /* ELKS_GEM_RESIDENT_MEMORY_H */
