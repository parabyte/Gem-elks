/*
 * gem_rsc.h - the GEM resource file format
 *
 * one place for how a PC GEM .RSC is laid out: record sizes, field offsets
 * inside each record, and the reads that turn a header table plus an index into
 * an address. nothing above needs to spell it out again.
 *
 * the records themselves - RSHDR, OBJECT, TEDINFO, ICONBLK, BITBLK - and the
 * R_* selector numbers are in aes.h since theyre part of the published GEM
 * interface, this header adds the on-disk arithmetic that goes with them.
 *
 * everything here works on one loaded image described by a segment and a byte
 * count, so the per-process resources and the AES's own GEM.RSC share the code
 */

#ifndef ELKS_GEM_RSC_H
#define ELKS_GEM_RSC_H

#include "aes.h"
#include "gem_object_resident.h"

/* record sizes, on disk and in memory alike */
#define GEM_RSC_HEADER_BYTES		36U
#define GEM_RSC_OBJECT_BYTES		24U
#define GEM_RSC_TEDINFO_BYTES		28U
#define GEM_RSC_ICONBLK_BYTES		34U
#define GEM_RSC_BITBLK_BYTES		14U
#define GEM_RSC_POINTER_BYTES		4U
#define GEM_RSC_USERBLK_BYTES		8U

/* field offsets the relocation and the callers both need */
#define GEM_RSC_OB_SPEC			12U
#define GEM_RSC_OB_X			16U
#define GEM_RSC_TE_PTEXT		0U
#define GEM_RSC_TE_PTMPLT		4U
#define GEM_RSC_TE_PVALID		8U
#define GEM_RSC_TE_TXTLEN		24U
#define GEM_RSC_IB_PMASK		0U
#define GEM_RSC_IB_PDATA		4U
#define GEM_RSC_IB_PTEXT		8U
#define GEM_RSC_BI_PDATA		0U

/* one loaded image: the paragraph segment its bytes start in and how many there are. offsets are always from the start */
typedef struct gem_rsc_image {
	UWORD segment;
	UWORD bytes;
} GEM_RSC_IMAGE;

/* build a far pointer from an explicit segment and offset */
VOID __far *gem_rsc_pointer(UWORD segment, UWORD offset);

/* is [offset, offset + count) inside limit? */
WORD gem_rsc_range(UWORD offset, UWORD count, UWORD limit);

/* single fields of a loaded image, by byte offset from its start */
UBYTE gem_rsc_byte(const GEM_RSC_IMAGE *image, UWORD offset);
VOID gem_rsc_byte_set(const GEM_RSC_IMAGE *image, UWORD offset, UBYTE value);
GEM_U32_WORDS gem_rsc_pair(const GEM_RSC_IMAGE *image, UWORD offset);
VOID gem_rsc_pair_set(const GEM_RSC_IMAGE *image, UWORD offset,
	GEM_U32_WORDS value);

/* write a four-byte slot at an arbitrary address, like RSRC_SADDR does */
VOID gem_rsc_address_set(GEM_FAR_ADDRESS target, GEM_FAR_ADDRESS value);

/* the -1:-1 the resource compiler writes for an absent pointer */
WORD gem_rsc_pair_is_nil(GEM_U32_WORDS value);
VOID gem_rsc_invalid_address(GEM_FAR_ADDRESS *address);

/* read the 36-byte header off the front of a loaded image */
VOID gem_rsc_header(const GEM_RSC_IMAGE *image, RSHDR *header);

/* copy one RSHDR field by field, so no packing assumption is made */
VOID gem_rsc_header_copy(RSHDR *destination, const RSHDR *source);

/* base + index * size, refused when the index is past count or the math would wrap */
WORD gem_rsc_index_offset(UWORD base, UWORD index, UWORD count, UWORD size,
	UWORD *offset);

/* resolve one R_* selector and index against a loaded image. returns FALSE and an invalid address when the selector, index or stored pointer isnt usable */
WORD gem_rsc_address(const GEM_RSC_IMAGE *image, UWORD type, UWORD index,
	GEM_FAR_ADDRESS *address);

/* copy a nul-terminated string out of a loaded image into near memory. returns the length without the terminator, or -1 */
WORD gem_rsc_string(const GEM_RSC_IMAGE *image, UWORD offset, BYTE *buffer,
	UWORD size);

#endif				/* ELKS_GEM_RSC_H */
