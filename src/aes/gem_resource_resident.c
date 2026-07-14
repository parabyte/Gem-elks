/*
 * gem_resource_resident.c - original GEMRSLIB resource logic for ELKS PDs.
 *
 * The load, relocation, address lookup, address replacement, and coordinate
 * fix paths are direct 16-bit adaptations of FreeGEM GEMRSLIB.C (Digital
 * Research, 1984-1987; GPL release by Caldera Thin Clients, Inc., 1999).
 * ELKS replaces GEMDOS allocation and reads, but the on-disk RSHDR, OBJECT,
 * TEDINFO, ICONBLK, BITBLK, tree-index, and free-pointer records remain in
 * their original byte layout and are relocated in place exactly once.
 *
 * All file sizes, offsets, indexes, dimensions, counters, and arithmetic are
 * eight or sixteen bits.  A far address is always two explicit words.  The
 * only conversion to a GNU C far pointer is a four-byte union copy; no C
 * wide scalar, floating point, multiply, divide, near resource mirror, or
 * dynamic near allocation exists here.
 */

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include "gem_resource_resident.h"

#if !defined(ELKS) || !ELKS
#error gem_resource_resident.c requires the ELKS 16-bit far-segment ABI
#endif

#define GEM_RESOURCE_HEADER_BYTES       36U
#define GEM_RESOURCE_OBJECT_BYTES       24U
#define GEM_RESOURCE_TEDINFO_BYTES      28U
#define GEM_RESOURCE_ICONBLK_BYTES      34U
#define GEM_RESOURCE_BITBLK_BYTES       14U
#define GEM_RESOURCE_POINTER_BYTES      4U
#define GEM_RESOURCE_USERBLK_BYTES      8U
#define GEM_RESOURCE_COPY_BYTES         256U

#define GEM_RESOURCE_SPAN_OBJECT        0
#define GEM_RESOURCE_SPAN_TEDINFO       1
#define GEM_RESOURCE_SPAN_ICONBLK       2
#define GEM_RESOURCE_SPAN_BITBLK        3
#define GEM_RESOURCE_SPAN_FRSTR         4
#define GEM_RESOURCE_SPAN_FRIMG         5
#define GEM_RESOURCE_SPAN_TRINDEX       6
#define GEM_RESOURCE_SPAN_COUNT         7U

#define GEM_OBJECT_SPEC_OFFSET          12U
#define GEM_OBJECT_X_OFFSET             16U
#define GEM_TED_PTEXT_OFFSET            0U
#define GEM_TED_PTMPLT_OFFSET           4U
#define GEM_TED_PVALID_OFFSET           8U
#define GEM_ICON_PMASK_OFFSET           0U
#define GEM_ICON_PDATA_OFFSET           4U
#define GEM_ICON_PTEXT_OFFSET           8U
#define GEM_BIT_PDATA_OFFSET            0U

typedef union gem_resource_far_pointer {
	VOID __far *pointer;
	UBYTE __far *bytes;
	UWORD __far *words;
	GEM_U32_WORDS __far *pair;
	RSHDR __far *header;
	OBJECT __far *object;
	TEDINFO __far *tedinfo;
	ICONBLK __far *iconblk;
	BITBLK __far *bitblk;
	GEM_FAR_ADDRESS address;
} GEM_RESOURCE_FAR_POINTER;

typedef BYTE GEM_RESOURCE_FAR_POINTER_MUST_BE_FOUR_BYTES
	[(sizeof(GEM_RESOURCE_FAR_POINTER) == 4) ? 1 : -1];

typedef struct gem_resource_span {
	UWORD start;
	UWORD end;
	UWORD present;
} GEM_RESOURCE_SPAN;

/*
 * The resident owner dispatches one request at a time.  One aligned, fixed
 * near scratch block can therefore serve every PD without a lock or malloc.
 * Its word member guarantees that RSHDR word fields start aligned.
 */
typedef union gem_resource_scratch {
	UWORD alignment;
	UBYTE bytes[GEM_RESOURCE_COPY_BYTES];
} GEM_RESOURCE_SCRATCH;

static GEM_RESOURCE_SCRATCH gem_resource_scratch;
static GEM_RESOURCE_SPAN gem_resource_spans[GEM_RESOURCE_SPAN_COUNT];

static VOID __far *
gem_resource_pointer(UWORD segment, UWORD offset)
{
	GEM_RESOURCE_FAR_POINTER value;

	value.address.lo = offset;
	value.address.hi = segment;
	return value.pointer;
}

static WORD
gem_resource_range(UWORD offset, UWORD count, UWORD limit)
{
	if (offset > limit)
		return FALSE;
	return count <= (UWORD) (limit - offset);
}

static UBYTE
gem_resource_far_byte(const GEM_RESOURCE_RESIDENT *resident, UWORD offset)
{
	UBYTE __far *pointer;

	pointer = (UBYTE __far *) gem_resource_pointer(
		resident->storage.base.hi, offset);
	return *pointer;
}

static VOID
gem_resource_far_byte_set(GEM_RESOURCE_RESIDENT *resident, UWORD offset,
			  UBYTE value)
{
	UBYTE __far *pointer;

	pointer = (UBYTE __far *) gem_resource_pointer(
		resident->storage.base.hi, offset);
	*pointer = value;
}

static GEM_U32_WORDS
gem_resource_far_pair(const GEM_RESOURCE_RESIDENT *resident, UWORD offset)
{
	GEM_U32_WORDS result;
	GEM_U32_WORDS __far *pointer;

	pointer = (GEM_U32_WORDS __far *) gem_resource_pointer(
		resident->storage.base.hi, offset);
	result.lo = pointer->lo;
	result.hi = pointer->hi;
	return result;
}

static VOID
gem_resource_far_pair_set(GEM_RESOURCE_RESIDENT *resident, UWORD offset,
			  GEM_U32_WORDS value)
{
	GEM_U32_WORDS __far *pointer;

	pointer = (GEM_U32_WORDS __far *) gem_resource_pointer(
		resident->storage.base.hi, offset);
	pointer->lo = value.lo;
	pointer->hi = value.hi;
}

/*
 * RSRC_SADDR copies caller-supplied far words into the slot returned by
 * GEMRSLIB get_addr().  That slot can itself be outside the resource after
 * an application replaces a tree address, so retain the original operation:
 * form the destination only from its explicit offset and segment words and
 * store exactly four bytes.  No address normalization or wide addition is
 * performed here.
 */
static VOID
gem_resource_address_pair_set(GEM_FAR_ADDRESS target,
			      GEM_FAR_ADDRESS value)
{
	GEM_U32_WORDS __far *pointer;

	pointer = (GEM_U32_WORDS __far *) gem_resource_pointer(target.hi,
		target.lo);
	pointer->lo = value.lo;
	pointer->hi = value.hi;
}

static WORD
gem_resource_pair_is_nil(GEM_U32_WORDS value)
{
	return value.lo == 0xffffU && value.hi == 0xffffU;
}

static WORD
gem_resource_read_exact(WORD descriptor, UBYTE *buffer, UWORD count)
{
	WORD result;

	while (count) {
		result = (WORD) read(descriptor, buffer, count);
		if (result < 0) {
			if (errno == EINTR)
				continue;
			return FALSE;
		}
		if (!result) {
			errno = EIO;
			return FALSE;
		}
		buffer += (UWORD) result;
		count -= (UWORD) result;
	}
	return TRUE;
}

static VOID
gem_resource_header_copy(RSHDR *destination, const RSHDR *source)
{
	destination->rsh_vrsn = source->rsh_vrsn;
	destination->rsh_object = source->rsh_object;
	destination->rsh_tedinfo = source->rsh_tedinfo;
	destination->rsh_iconblk = source->rsh_iconblk;
	destination->rsh_bitblk = source->rsh_bitblk;
	destination->rsh_frstr = source->rsh_frstr;
	destination->rsh_string = source->rsh_string;
	destination->rsh_imdata = source->rsh_imdata;
	destination->rsh_frimg = source->rsh_frimg;
	destination->rsh_trindex = source->rsh_trindex;
	destination->rsh_nobs = source->rsh_nobs;
	destination->rsh_ntree = source->rsh_ntree;
	destination->rsh_nted = source->rsh_nted;
	destination->rsh_nib = source->rsh_nib;
	destination->rsh_nbb = source->rsh_nbb;
	destination->rsh_nstring = source->rsh_nstring;
	destination->rsh_nimages = source->rsh_nimages;
	destination->rsh_rssize = source->rsh_rssize;
}

/*
 * Form [offset, offset + count * record_bytes) with checked repeated adds.
 * This cold load-time walk emits only 16-bit ADD/SUB and avoids both a C
 * multiplication and the compiler's 8086 multiply helper.  No partial span
 * is returned after overflow or an out-of-file record.
 */
static WORD
gem_resource_make_span(GEM_RESOURCE_SPAN *span, UWORD offset, UWORD count,
		       UWORD record_bytes, UWORD limit)
{
	span->start = offset;
	span->end = offset;
	span->present = count != 0;
	if (offset > limit)
		return FALSE;
	if (count && (offset < GEM_RESOURCE_HEADER_BYTES || (offset & 1U)))
		return FALSE;
	while (count--) {
		if (record_bytes > (UWORD) (limit - span->end))
			return FALSE;
		span->end = (UWORD) (span->end + record_bytes);
	}
	return TRUE;
}

static WORD
gem_resource_spans_overlap(const GEM_RESOURCE_SPAN *left,
			   const GEM_RESOURCE_SPAN *right)
{
	if (!left->present || !right->present)
		return FALSE;
	return left->start < right->end && right->start < left->end;
}

static WORD
gem_resource_validate_header(const RSHDR *header)
{
	GEM_RESOURCE_SPAN *left;
	GEM_RESOURCE_SPAN *right;
	UWORD left_count;
	UWORD right_count;
	UWORD size;

	size = header->rsh_rssize;
	if (size < GEM_RESOURCE_HEADER_BYTES
	    || header->rsh_string > size || header->rsh_imdata > size)
		return FALSE;
	if (header->rsh_ntree && !header->rsh_nobs)
		return FALSE;

	if (!gem_resource_make_span(&gem_resource_spans[0],
			header->rsh_object, header->rsh_nobs,
			GEM_RESOURCE_OBJECT_BYTES, size)
	    || !gem_resource_make_span(&gem_resource_spans[1],
			header->rsh_tedinfo, header->rsh_nted,
			GEM_RESOURCE_TEDINFO_BYTES, size)
	    || !gem_resource_make_span(&gem_resource_spans[2],
			header->rsh_iconblk, header->rsh_nib,
			GEM_RESOURCE_ICONBLK_BYTES, size)
	    || !gem_resource_make_span(&gem_resource_spans[3],
			header->rsh_bitblk, header->rsh_nbb,
			GEM_RESOURCE_BITBLK_BYTES, size)
	    || !gem_resource_make_span(&gem_resource_spans[4],
			header->rsh_frstr, header->rsh_nstring,
			GEM_RESOURCE_POINTER_BYTES, size)
	    || !gem_resource_make_span(&gem_resource_spans[5],
			header->rsh_frimg, header->rsh_nimages,
			GEM_RESOURCE_POINTER_BYTES, size)
	    || !gem_resource_make_span(&gem_resource_spans[6],
			header->rsh_trindex, header->rsh_ntree,
			GEM_RESOURCE_POINTER_BYTES, size))
		return FALSE;

	/* No two nonempty typed tables may alias bytes later relocated in place. */
	left = gem_resource_spans;
	left_count = GEM_RESOURCE_SPAN_COUNT;
	while (left_count--) {
		right = left + 1;
		right_count = left_count;
		while (right_count--) {
			if (gem_resource_spans_overlap(left, right))
				return FALSE;
			right++;
		}
		left++;
	}
	return TRUE;
}

static WORD
gem_resource_validate_raw_address(const GEM_RESOURCE_RESIDENT *resident,
				  GEM_U32_WORDS address,
				  UWORD bytes, WORD required)
{
	if (gem_resource_pair_is_nil(address))
		return !required;
	if (address.hi)
		return FALSE;
	return gem_resource_range(address.lo, bytes, resident->storage.bytes);
}

static WORD
gem_resource_validate_string(const GEM_RESOURCE_RESIDENT *resident,
			     GEM_U32_WORDS address, WORD required,
			     UWORD *length)
{
	UWORD offset;
	UWORD count;

	if (gem_resource_pair_is_nil(address)) {
		if (length)
			*length = 0;
		return !required;
	}
	if (address.hi || address.lo >= resident->storage.bytes)
		return FALSE;
	offset = address.lo;
	count = 0;
	while (offset < resident->storage.bytes) {
		if (!gem_resource_far_byte(resident, offset)) {
			if (length)
				*length = count;
			return TRUE;
		}
		offset++;
		count++;
	}
	return FALSE;
}

static WORD
gem_resource_address_in_table(GEM_U32_WORDS address, UWORD table,
			      UWORD records, UWORD record_bytes)
{
	if (address.hi)
		return FALSE;
	while (records--) {
		if (address.lo == table)
			return TRUE;
		table = (UWORD) (table + record_bytes);
	}
	return FALSE;
}

static WORD
gem_resource_object_link_valid(WORD link, UWORD objects)
{
	return link == -1 || (link >= 0 && (UWORD) link < objects);
}

static WORD
gem_resource_validate_trees(const GEM_RESOURCE_RESIDENT *resident,
			    const RSHDR *header)
{
	GEM_U32_WORDS tree;
	UWORD count;
	UWORD offset;

	offset = header->rsh_trindex;
	count = header->rsh_ntree;
	while (count--) {
		tree = gem_resource_far_pair(resident, offset);
		if (gem_resource_pair_is_nil(tree)
		    || !gem_resource_address_in_table(tree,
				header->rsh_object, header->rsh_nobs,
				GEM_RESOURCE_OBJECT_BYTES))
			return FALSE;
		offset = (UWORD) (offset + GEM_RESOURCE_POINTER_BYTES);
	}
	return TRUE;
}

static WORD
gem_resource_validate_object_spec(const GEM_RESOURCE_RESIDENT *resident,
				  const RSHDR *header,
				  UWORD type, UWORD flags,
				  GEM_U32_WORDS spec)
{
	/*
	 * INDIRECT makes ob_spec name one original four-byte specification slot.
	 * GEMRSLIB relocates that first-level address without interpreting the
	 * slot as the object's direct text, image, or box value.
	 */
	if (flags & INDIRECT)
		return gem_resource_validate_raw_address(resident, spec,
			GEM_RESOURCE_POINTER_BYTES, TRUE);

	switch (type & 0x00ffU) {
	case G_BOX:
	case G_IBOX:
	case G_BOXCHAR:
		/* These original object types store a numeric box specification. */
		return TRUE;
	case G_TEXT:
	case G_BOXTEXT:
	case G_FTEXT:
	case G_FBOXTEXT:
		return !gem_resource_pair_is_nil(spec)
		       && gem_resource_address_in_table(spec,
				header->rsh_tedinfo, header->rsh_nted,
				GEM_RESOURCE_TEDINFO_BYTES);
	case G_IMAGE:
		return !gem_resource_pair_is_nil(spec)
		       && gem_resource_address_in_table(spec,
				header->rsh_bitblk, header->rsh_nbb,
				GEM_RESOURCE_BITBLK_BYTES);
	case G_ICON:
		return !gem_resource_pair_is_nil(spec)
		       && gem_resource_address_in_table(spec,
				header->rsh_iconblk, header->rsh_nib,
				GEM_RESOURCE_ICONBLK_BYTES);
	case G_BUTTON:
	case G_STRING:
	case G_TITLE:
		return gem_resource_validate_string(resident, spec, TRUE, NULL);
	case G_USERDEF:
		return gem_resource_validate_raw_address(resident, spec,
				GEM_RESOURCE_USERBLK_BYTES, TRUE);
	default:
		/* GEMRSLIB relocates every non-box extension as an address. */
		return gem_resource_validate_raw_address(resident, spec, 1U,
				TRUE);
	}
}

static WORD
gem_resource_validate_objects(const GEM_RESOURCE_RESIDENT *resident,
			      const RSHDR *header)
{
	GEM_U32_WORDS spec;
	OBJECT __far *object;
	UWORD count;
	UWORD offset;

	offset = header->rsh_object;
	count = header->rsh_nobs;
	while (count--) {
		object = (OBJECT __far *) gem_resource_pointer(
			resident->storage.base.hi, offset);
		if (!gem_resource_object_link_valid(object->ob_next,
				header->rsh_nobs)
		    || !gem_resource_object_link_valid(object->ob_head,
				header->rsh_nobs)
		    || !gem_resource_object_link_valid(object->ob_tail,
				header->rsh_nobs))
			return FALSE;
		spec.lo = object->ob_spec.lo;
		spec.hi = object->ob_spec.hi;
		if (!gem_resource_validate_object_spec(resident, header,
				object->ob_type, object->ob_flags, spec))
			return FALSE;
		offset = (UWORD) (offset + GEM_RESOURCE_OBJECT_BYTES);
	}
	return TRUE;
}

static WORD
gem_resource_validate_tedinfo(const GEM_RESOURCE_RESIDENT *resident,
			      const RSHDR *header)
{
	GEM_U32_WORDS pointer;
	TEDINFO __far *tedinfo;
	UWORD count;
	UWORD offset;

	offset = header->rsh_tedinfo;
	count = header->rsh_nted;
	while (count--) {
		tedinfo = (TEDINFO __far *) gem_resource_pointer(
			resident->storage.base.hi, offset);
		pointer.lo = tedinfo->te_ptext.lo;
		pointer.hi = tedinfo->te_ptext.hi;
		if (!gem_resource_validate_string(resident, pointer, FALSE, NULL))
			return FALSE;
		pointer.lo = tedinfo->te_ptmplt.lo;
		pointer.hi = tedinfo->te_ptmplt.hi;
		if (!gem_resource_validate_string(resident, pointer, FALSE, NULL))
			return FALSE;
		pointer.lo = tedinfo->te_pvalid.lo;
		pointer.hi = tedinfo->te_pvalid.hi;
		if (!gem_resource_validate_string(resident, pointer, FALSE, NULL))
			return FALSE;
		offset = (UWORD) (offset + GEM_RESOURCE_TEDINFO_BYTES);
	}
	return TRUE;
}

static WORD gem_resource_validate_bitmap_span(
	const GEM_RESOURCE_RESIDENT *resident, GEM_U32_WORDS pointer,
	WORD row_bytes, WORD rows);

static WORD
gem_resource_validate_iconblks(const GEM_RESOURCE_RESIDENT *resident,
			       const RSHDR *header)
{
	GEM_U32_WORDS pointer;
	ICONBLK __far *iconblk;
	UWORD count;
	UWORD offset;
	UWORD pixels;
	UWORD row_bytes;

	offset = header->rsh_iconblk;
	count = header->rsh_nib;
	while (count--) {
		iconblk = (ICONBLK __far *) gem_resource_pointer(
			resident->storage.base.hi, offset);
		pointer.lo = iconblk->ib_pmask.lo;
		pointer.hi = iconblk->ib_pmask.hi;
		if (iconblk->ib_wicon < 0 || iconblk->ib_hicon < 0)
			return FALSE;
		pixels = (UWORD) iconblk->ib_wicon;
		row_bytes = 0;
		while (pixels >= 8U) {
			pixels -= 8U;
			row_bytes++;
		}
		if (pixels)
			row_bytes++;
		if (!gem_resource_validate_bitmap_span(resident, pointer,
				(WORD) row_bytes, iconblk->ib_hicon))
			return FALSE;
		pointer.lo = iconblk->ib_pdata.lo;
		pointer.hi = iconblk->ib_pdata.hi;
		if (!gem_resource_validate_bitmap_span(resident, pointer,
				(WORD) row_bytes, iconblk->ib_hicon))
			return FALSE;
		pointer.lo = iconblk->ib_ptext.lo;
		pointer.hi = iconblk->ib_ptext.hi;
		if (!gem_resource_validate_string(resident, pointer, FALSE, NULL))
			return FALSE;
		offset = (UWORD) (offset + GEM_RESOURCE_ICONBLK_BYTES);
	}
	return TRUE;
}

static WORD
gem_resource_validate_bitmap_span(const GEM_RESOURCE_RESIDENT *resident,
				  GEM_U32_WORDS pointer,
				  WORD row_bytes, WORD rows)
{
	UWORD offset;
	UWORD count;
	UWORD width;

	if (gem_resource_pair_is_nil(pointer))
		return TRUE;
	if (row_bytes < 0 || rows < 0)
		return FALSE;
	offset = pointer.lo;
	width = (UWORD) row_bytes;
	count = (UWORD) rows;
	if (!gem_resource_validate_raw_address(resident, pointer,
			(count && width) ? 1U : 0U, TRUE))
		return FALSE;
	while (count--) {
		if (!gem_resource_range(offset, width, resident->storage.bytes))
			return FALSE;
		offset = (UWORD) (offset + width);
	}
	return TRUE;
}

static WORD
gem_resource_validate_bitblks(const GEM_RESOURCE_RESIDENT *resident,
			      const RSHDR *header)
{
	BITBLK __far *bitblk;
	GEM_U32_WORDS pointer;
	UWORD count;
	UWORD offset;

	offset = header->rsh_bitblk;
	count = header->rsh_nbb;
	while (count--) {
		bitblk = (BITBLK __far *) gem_resource_pointer(
			resident->storage.base.hi, offset);
		pointer.lo = bitblk->bi_pdata.lo;
		pointer.hi = bitblk->bi_pdata.hi;
		if (!gem_resource_validate_bitmap_span(resident, pointer,
				bitblk->bi_wb, bitblk->bi_hl))
			return FALSE;
		offset = (UWORD) (offset + GEM_RESOURCE_BITBLK_BYTES);
	}
	return TRUE;
}

static WORD
gem_resource_validate_free_table(const GEM_RESOURCE_RESIDENT *resident,
				 UWORD offset, UWORD count, WORD strings)
{
	GEM_U32_WORDS pointer;

	while (count--) {
		pointer = gem_resource_far_pair(resident, offset);
		if (strings) {
			if (!gem_resource_validate_string(resident, pointer, FALSE,
					NULL))
				return FALSE;
		} else if (!gem_resource_validate_raw_address(resident, pointer,
				1U, FALSE)) {
			return FALSE;
		}
		offset = (UWORD) (offset + GEM_RESOURCE_POINTER_BYTES);
	}
	return TRUE;
}

/* Validate the entire unmodified resource before the first pointer write. */
static WORD
gem_resource_validate_nested(const GEM_RESOURCE_RESIDENT *resident,
			     const RSHDR *header)
{
	return gem_resource_validate_trees(resident, header)
	       && gem_resource_validate_objects(resident, header)
	       && gem_resource_validate_tedinfo(resident, header)
	       && gem_resource_validate_iconblks(resident, header)
	       && gem_resource_validate_bitblks(resident, header)
	       && gem_resource_validate_free_table(resident,
			header->rsh_frstr, header->rsh_nstring, TRUE)
	       && gem_resource_validate_free_table(resident,
			header->rsh_frimg, header->rsh_nimages, FALSE);
}

static VOID
gem_resource_header_read(const GEM_RESOURCE_RESIDENT *resident,
			 RSHDR *destination)
{
	RSHDR __far *source;

	source = (RSHDR __far *) gem_resource_pointer(
		resident->storage.base.hi, 0);
	destination->rsh_vrsn = source->rsh_vrsn;
	destination->rsh_object = source->rsh_object;
	destination->rsh_tedinfo = source->rsh_tedinfo;
	destination->rsh_iconblk = source->rsh_iconblk;
	destination->rsh_bitblk = source->rsh_bitblk;
	destination->rsh_frstr = source->rsh_frstr;
	destination->rsh_string = source->rsh_string;
	destination->rsh_imdata = source->rsh_imdata;
	destination->rsh_frimg = source->rsh_frimg;
	destination->rsh_trindex = source->rsh_trindex;
	destination->rsh_nobs = source->rsh_nobs;
	destination->rsh_ntree = source->rsh_ntree;
	destination->rsh_nted = source->rsh_nted;
	destination->rsh_nib = source->rsh_nib;
	destination->rsh_nbb = source->rsh_nbb;
	destination->rsh_nstring = source->rsh_nstring;
	destination->rsh_nimages = source->rsh_nimages;
	destination->rsh_rssize = source->rsh_rssize;
}

static WORD
gem_resource_relocate_pair(GEM_RESOURCE_RESIDENT *resident, UWORD offset,
			   GEM_U32_WORDS *address)
{
	GEM_U32_WORDS value;

	value = gem_resource_far_pair(resident, offset);
	if (gem_resource_pair_is_nil(value)) {
		if (address)
			*address = value;
		return FALSE;
	}
	value.hi = resident->storage.base.hi;
	gem_resource_far_pair_set(resident, offset, value);
	if (address)
		*address = value;
	return TRUE;
}

static WORD
gem_resource_string_length(const GEM_RESOURCE_RESIDENT *resident,
			   UWORD offset, UWORD *length)
{
	UWORD count;

	count = 0;
	while (offset < resident->storage.bytes) {
		if (!gem_resource_far_byte(resident, offset)) {
			*length = count;
			return TRUE;
		}
		offset++;
		count++;
	}
	return FALSE;
}

static WORD
gem_resource_relocate_table(GEM_RESOURCE_RESIDENT *resident,
			    UWORD offset, UWORD count)
{
	while (count--) {
		(void) gem_resource_relocate_pair(resident, offset, NULL);
		offset = (UWORD) (offset + GEM_RESOURCE_POINTER_BYTES);
	}
	return TRUE;
}

static WORD
gem_resource_relocate_trees(GEM_RESOURCE_RESIDENT *resident,
			    const RSHDR *header)
{
	GEM_U32_WORDS tree;
	OBJECT __far *root;
	UWORD count;
	UWORD offset;

	offset = header->rsh_trindex;
	count = header->rsh_ntree;
	while (count--) {
		if (!gem_resource_relocate_pair(resident, offset, &tree))
			return FALSE;
		if (resident->metrics.options
		    & GEM_RESOURCE_OPTION_OUTLINED_ROOT) {
			root = (OBJECT __far *) gem_resource_pointer(tree.hi,
				tree.lo);
			if (root->ob_state == OUTLINED && root->ob_type == G_BOX)
				root->ob_state = SHADOWED;
		}
		offset = (UWORD) (offset + GEM_RESOURCE_POINTER_BYTES);
	}
	return TRUE;
}

static WORD
gem_resource_relocate_tedinfo(GEM_RESOURCE_RESIDENT *resident,
			      const RSHDR *header)
{
	GEM_U32_WORDS address;
	TEDINFO __far *tedinfo;
	UWORD count;
	UWORD length;
	UWORD offset;

	offset = header->rsh_tedinfo;
	count = header->rsh_nted;
	while (count--) {
		tedinfo = (TEDINFO __far *) gem_resource_pointer(
			resident->storage.base.hi, offset);
		if (gem_resource_relocate_pair(resident,
				offset + GEM_TED_PTEXT_OFFSET, &address)) {
			if (!gem_resource_string_length(resident, address.lo, &length))
				return FALSE;
			tedinfo->te_txtlen = (WORD) (length + 1U);
		}
		if (gem_resource_relocate_pair(resident,
				offset + GEM_TED_PTMPLT_OFFSET, &address)) {
			if (!gem_resource_string_length(resident, address.lo, &length))
				return FALSE;
			tedinfo->te_tmplen = (WORD) (length + 1U);
		}
		(void) gem_resource_relocate_pair(resident,
				offset + GEM_TED_PVALID_OFFSET, NULL);
		offset = (UWORD) (offset + GEM_RESOURCE_TEDINFO_BYTES);
	}
	return TRUE;
}

static WORD
gem_resource_relocate_iconblks(GEM_RESOURCE_RESIDENT *resident,
			       const RSHDR *header)
{
	UWORD count;
	UWORD offset;

	offset = header->rsh_iconblk;
	count = header->rsh_nib;
	while (count--) {
		(void) gem_resource_relocate_pair(resident,
				offset + GEM_ICON_PMASK_OFFSET, NULL);
		(void) gem_resource_relocate_pair(resident,
				offset + GEM_ICON_PDATA_OFFSET, NULL);
		(void) gem_resource_relocate_pair(resident,
				offset + GEM_ICON_PTEXT_OFFSET, NULL);
		offset = (UWORD) (offset + GEM_RESOURCE_ICONBLK_BYTES);
	}
	return TRUE;
}

static WORD
gem_resource_relocate_bitblks(GEM_RESOURCE_RESIDENT *resident,
			      const RSHDR *header)
{
	UWORD count;
	UWORD offset;

	offset = header->rsh_bitblk;
	count = header->rsh_nbb;
	while (count--) {
		(void) gem_resource_relocate_pair(resident,
				offset + GEM_BIT_PDATA_OFFSET, NULL);
		offset = (UWORD) (offset + GEM_RESOURCE_BITBLK_BYTES);
	}
	return TRUE;
}

typedef union gem_resource_position {
	UWORD word;
	struct __attribute__((packed)) {
		UBYTE columns;
		UBYTE offset;
	} bytes;
} GEM_RESOURCE_POSITION;

typedef BYTE GEM_RESOURCE_POSITION_MUST_BE_TWO_BYTES
	[(sizeof(GEM_RESOURCE_POSITION) == 2) ? 1 : -1];

/*
 * Repeat one word addition without letting an optimizing compiler recognize
 * the loop as a multiplication.  CX is an unscaled iteration count, `value`
 * and `addend` are unscaled words, and every ADD deliberately wraps modulo
 * 65536 just as GEMRSLIB's original 16-bit product did.  LOOP and ADD are
 * both original 8088/8086 instructions; no flags or segment register escape.
 */
static UWORD
gem_resource_repeat_add(UWORD value, UWORD count, UWORD addend)
{
	if (!count)
		return value;
	__asm__ volatile ("1:\n\t"
			  "addw %2,%0\n\t"
			  "loop 1b"
			  : "+&r" (value), "+c" (count)
			  : "r" (addend)
			  : "cc");
	return value;
}

/* Direct no-MUL adaptation of GEMRSLIB.C fix_chpos(). */
static VOID
gem_resource_fix_chpos(GEM_RESOURCE_RESIDENT *resident, UWORD field,
			UWORD kind)
{
	GEM_RESOURCE_POSITION position;
	UWORD result;
	UWORD __far *pointer;

	pointer = (UWORD __far *) gem_resource_pointer(
		resident->storage.base.hi, field);
	position.word = *pointer;
	if (kind == 0U) {
		result = gem_resource_repeat_add(0, position.bytes.columns,
			resident->metrics.character_width);
	} else if (kind == 1U) {
		result = gem_resource_repeat_add(0, position.bytes.columns,
			resident->metrics.character_height);
	} else if (kind == 2U && position.bytes.columns == 80U) {
		result = resident->metrics.screen_width;
	} else if (kind == 2U) {
		result = gem_resource_repeat_add(0, position.bytes.columns,
			resident->metrics.character_width);
	} else if (position.bytes.columns == 25U) {
		result = resident->metrics.screen_height;
	} else {
		result = gem_resource_repeat_add(0, position.bytes.columns,
			resident->metrics.character_height);
	}

	/* Preserve GEMRSLIB's exact >128 signed-offset test and word wrap. */
	if (position.bytes.offset > 128U)
		result = (UWORD) (result
			- (UWORD) (256U - position.bytes.offset));
	else
		result = (UWORD) (result + position.bytes.offset);
	*pointer = result;
}

static VOID
gem_resource_fix_object_at(GEM_RESOURCE_RESIDENT *resident, UWORD object)
{
	UWORD field;
	UWORD kind;

	field = (UWORD) (object + GEM_OBJECT_X_OFFSET);
	kind = 0;
	while (kind < 4U) {
		gem_resource_fix_chpos(resident, field, kind);
		field = (UWORD) (field + 2U);
		kind++;
	}
}

static VOID
gem_resource_fix_divider(GEM_RESOURCE_RESIDENT *resident,
			 OBJECT __far *object)
{
	GEM_U32_WORDS string;
	UWORD length;
	UWORD offset;

	if ((object->ob_type & 0x00ffU) != G_STRING
	    || !(object->ob_state & DISABLED))
		return;
	string.lo = object->ob_spec.lo;
	string.hi = object->ob_spec.hi;
	if (string.hi != resident->storage.base.hi
	    || !gem_resource_string_length(resident, string.lo, &length)
	    || !length
	    || gem_resource_far_byte(resident, string.lo) != (UBYTE) '-'
	    || gem_resource_far_byte(resident,
		(UWORD) (string.lo + length - 1U)) != (UBYTE) '-')
		return;
	offset = string.lo;
	while (length--) {
		gem_resource_far_byte_set(resident, offset, 0x13U);
		offset++;
	}
}

static WORD
gem_resource_relocate_objects(GEM_RESOURCE_RESIDENT *resident,
			      const RSHDR *header)
{
	OBJECT __far *object;
	UWORD count;
	UWORD offset;
	UWORD type;

	offset = header->rsh_object;
	count = header->rsh_nobs;
	while (count--) {
		object = (OBJECT __far *) gem_resource_pointer(
			resident->storage.base.hi, offset);
		type = object->ob_type & 0x00ffU;
		if (type != G_BOX && type != G_IBOX && type != G_BOXCHAR)
			(void) gem_resource_relocate_pair(resident,
				offset + GEM_OBJECT_SPEC_OFFSET, NULL);
		gem_resource_fix_object_at(resident, offset);
		gem_resource_fix_divider(resident, object);
		offset = (UWORD) (offset + GEM_RESOURCE_OBJECT_BYTES);
	}
	return TRUE;
}

static WORD
gem_resource_relocate(GEM_RESOURCE_RESIDENT *resident,
			 const RSHDR *header)
{
	return gem_resource_relocate_trees(resident, header)
	       && gem_resource_relocate_tedinfo(resident, header)
	       && gem_resource_relocate_iconblks(resident, header)
	       && gem_resource_relocate_bitblks(resident, header)
	       && gem_resource_relocate_table(resident, header->rsh_frstr,
			header->rsh_nstring)
	       && gem_resource_relocate_table(resident, header->rsh_frimg,
			header->rsh_nimages)
	       && gem_resource_relocate_objects(resident, header);
}

static WORD
gem_resource_index_offset(UWORD table, UWORD index, UWORD records,
			  UWORD record_bytes, UWORD *offset)
{
	if (index >= records)
		return FALSE;
	table = gem_resource_repeat_add(table, index, record_bytes);
	*offset = table;
	return TRUE;
}

static VOID
gem_resource_invalid_address(GEM_FAR_ADDRESS *address)
{
	address->lo = 0xffffU;
	address->hi = 0xffffU;
}

/* Direct explicit-word adaptation of GEMRSLIB.C get_addr(). */
static WORD
gem_resource_get_address(const GEM_RESOURCE_RESIDENT *resident,
			 UWORD type, UWORD index,
			 GEM_FAR_ADDRESS *address)
{
	RSHDR header;
	GEM_U32_WORDS stored;
	UWORD offset;

	if (!resident || !address
	    || !(resident->flags & GEM_RESOURCE_RESIDENT_LOADED)) {
		if (address)
			gem_resource_invalid_address(address);
		return FALSE;
	}
	gem_resource_header_read(resident, &header);

	switch (type) {
	case R_TREE:
		if (!gem_resource_index_offset(header.rsh_trindex, index,
				header.rsh_ntree, GEM_RESOURCE_POINTER_BYTES,
				&offset))
			break;
		stored = gem_resource_far_pair(resident, offset);
		if (gem_resource_pair_is_nil(stored))
			break;
		*address = stored;
		return TRUE;
	case R_OBJECT:
		if (!gem_resource_index_offset(header.rsh_object, index,
				header.rsh_nobs, GEM_RESOURCE_OBJECT_BYTES,
				&offset))
			break;
		goto return_slot;
	case R_TEDINFO:
	case R_TEPTEXT:
		if (!gem_resource_index_offset(header.rsh_tedinfo, index,
				header.rsh_nted, GEM_RESOURCE_TEDINFO_BYTES,
				&offset))
			break;
		goto return_slot;
	case R_ICONBLK:
	case R_IBPMASK:
		if (!gem_resource_index_offset(header.rsh_iconblk, index,
				header.rsh_nib, GEM_RESOURCE_ICONBLK_BYTES,
				&offset))
			break;
		goto return_slot;
	case R_BITBLK:
	case R_BIPDATA:
		if (!gem_resource_index_offset(header.rsh_bitblk, index,
				header.rsh_nbb, GEM_RESOURCE_BITBLK_BYTES,
				&offset))
			break;
		goto return_slot;
	case R_OBSPEC:
		if (!gem_resource_index_offset(header.rsh_object, index,
				header.rsh_nobs, GEM_RESOURCE_OBJECT_BYTES,
				&offset))
			break;
		offset = (UWORD) (offset + GEM_OBJECT_SPEC_OFFSET);
		goto return_slot;
	case R_TEPTMPLT:
	case R_TEPVALID:
		if (!gem_resource_index_offset(header.rsh_tedinfo, index,
				header.rsh_nted, GEM_RESOURCE_TEDINFO_BYTES,
				&offset))
			break;
		if (type == R_TEPTMPLT)
			offset = (UWORD) (offset + GEM_TED_PTMPLT_OFFSET);
		else
			offset = (UWORD) (offset + GEM_TED_PVALID_OFFSET);
		goto return_slot;
	case R_IBPDATA:
	case R_IBPTEXT:
		if (!gem_resource_index_offset(header.rsh_iconblk, index,
				header.rsh_nib, GEM_RESOURCE_ICONBLK_BYTES,
				&offset))
			break;
		if (type == R_IBPDATA)
			offset = (UWORD) (offset + GEM_ICON_PDATA_OFFSET);
		else
			offset = (UWORD) (offset + GEM_ICON_PTEXT_OFFSET);
		goto return_slot;
	case R_STRING:
		if (!gem_resource_index_offset(header.rsh_frstr, index,
				header.rsh_nstring, GEM_RESOURCE_POINTER_BYTES,
				&offset))
			break;
		stored = gem_resource_far_pair(resident, offset);
		if (gem_resource_pair_is_nil(stored))
			break;
		*address = stored;
		return TRUE;
	case R_IMAGEDATA:
		if (!gem_resource_index_offset(header.rsh_frimg, index,
				header.rsh_nimages, GEM_RESOURCE_POINTER_BYTES,
				&offset))
			break;
		stored = gem_resource_far_pair(resident, offset);
		if (gem_resource_pair_is_nil(stored))
			break;
		*address = stored;
		return TRUE;
	case R_FRSTR:
		if (!gem_resource_index_offset(header.rsh_frstr, index,
				header.rsh_nstring, GEM_RESOURCE_POINTER_BYTES,
				&offset))
			break;
		goto return_slot;
	case R_FRIMG:
		if (!gem_resource_index_offset(header.rsh_frimg, index,
				header.rsh_nimages, GEM_RESOURCE_POINTER_BYTES,
				&offset))
			break;
		goto return_slot;
	default:
		break;
	}
	gem_resource_invalid_address(address);
	return FALSE;

return_slot:
	address->lo = offset;
	address->hi = resident->storage.base.hi;
	return TRUE;
}

VOID
gem_resource_resident_init(GEM_RESOURCE_RESIDENT *resident)
{
	if (!resident)
		return;
	resident->storage.base.lo = 0;
	resident->storage.base.hi = 0;
	resident->storage.bytes = 0;
	resident->metrics.screen_width = 0;
	resident->metrics.screen_height = 0;
	resident->metrics.character_width = 0;
	resident->metrics.character_height = 0;
	resident->metrics.options = 0;
	resident->flags = 0;
}

WORD
gem_resource_resident_load(GEM_RESOURCE_RESIDENT *resident,
			   const BYTE *filename,
			   const GEM_RESOURCE_METRICS *metrics)
{
	RSHDR header;
	RSHDR *scratch_header;
	WORD descriptor;
	WORD saved_errno;
	UWORD count;
	UWORD offset;

	if (!resident || !filename || !filename[0] || !metrics) {
		errno = EINVAL;
		return FALSE;
	}
	if (resident->flags || resident->storage.base.hi
	    || resident->storage.bytes) {
		errno = EBUSY;
		return FALSE;
	}

	descriptor = (WORD) open((const char *) filename, O_RDONLY);
	if (descriptor < 0)
		return FALSE;
	if (!gem_resource_read_exact(descriptor, gem_resource_scratch.bytes,
			GEM_RESOURCE_HEADER_BYTES))
		goto fail;
	scratch_header = (RSHDR *) gem_resource_scratch.bytes;
	gem_resource_header_copy(&header, scratch_header);
	if (!gem_resource_validate_header(&header)) {
		errno = EINVAL;
		goto fail;
	}

	if (!gem_far_resource_alloc(&resident->storage, header.rsh_rssize))
		goto fail;
	resident->metrics.screen_width = metrics->screen_width;
	resident->metrics.screen_height = metrics->screen_height;
	resident->metrics.character_width = metrics->character_width;
	resident->metrics.character_height = metrics->character_height;
	resident->metrics.options = metrics->options;

	if (!gem_far_resource_copy_in(&resident->storage, 0,
			gem_resource_scratch.bytes, GEM_RESOURCE_HEADER_BYTES))
		goto fail;
	offset = GEM_RESOURCE_HEADER_BYTES;
	while (offset < header.rsh_rssize) {
		count = (UWORD) (header.rsh_rssize - offset);
		if (count > GEM_RESOURCE_COPY_BYTES)
			count = GEM_RESOURCE_COPY_BYTES;
		if (!gem_resource_read_exact(descriptor,
				gem_resource_scratch.bytes, count)
		    || !gem_far_resource_copy_in(&resident->storage, offset,
				gem_resource_scratch.bytes, count))
			goto fail;
		offset = (UWORD) (offset + count);
	}
	if (close(descriptor) != 0) {
		descriptor = -1;
		goto fail;
	}
	descriptor = -1;

	/* No pointer or coordinate byte is changed until this full pass succeeds. */
	if (!gem_resource_validate_nested(resident, &header)) {
		errno = EINVAL;
		goto fail;
	}
	if (!gem_resource_relocate(resident, &header)) {
		errno = EINVAL;
		goto fail;
	}
	resident->flags = GEM_RESOURCE_RESIDENT_LOADED;
	return TRUE;

fail:
	saved_errno = (WORD) errno;
	if (descriptor >= 0)
		(void) close(descriptor);
	if (resident->storage.base.hi && resident->storage.bytes)
		(void) gem_far_resource_free(&resident->storage);
	gem_resource_resident_init(resident);
	errno = saved_errno;
	return FALSE;
}

WORD
gem_resource_resident_free(GEM_RESOURCE_RESIDENT *resident)
{
	if (!resident
	    || !(resident->flags & GEM_RESOURCE_RESIDENT_LOADED)) {
		errno = EINVAL;
		return FALSE;
	}
	if (!gem_far_resource_free(&resident->storage))
		return FALSE;
	gem_resource_resident_init(resident);
	return TRUE;
}

WORD
gem_resource_resident_cleanup(GEM_RESOURCE_RESIDENT *resident)
{
	if (!resident) {
		errno = EINVAL;
		return FALSE;
	}
	if (!resident->storage.base.hi && !resident->storage.bytes) {
		gem_resource_resident_init(resident);
		return TRUE;
	}
	if (!gem_far_resource_free(&resident->storage))
		return FALSE;
	gem_resource_resident_init(resident);
	return TRUE;
}

WORD
gem_resource_resident_gaddr(const GEM_RESOURCE_RESIDENT *resident,
			    UWORD type, UWORD index,
			    GEM_FAR_ADDRESS *address)
{
	if (!gem_resource_get_address(resident, type, index, address)) {
		errno = EINVAL;
		return FALSE;
	}
	return TRUE;
}

WORD
gem_resource_resident_saddr(GEM_RESOURCE_RESIDENT *resident,
			    UWORD type, UWORD index,
			    GEM_FAR_ADDRESS address)
{
	GEM_FAR_ADDRESS target;

	if (!resident || !(resident->flags & GEM_RESOURCE_RESIDENT_LOADED)
	    || !gem_resource_get_address(resident, type, index, &target)
	    || gem_resource_pair_is_nil(target)
	    || (!target.lo && !target.hi)) {
		errno = EINVAL;
		return FALSE;
	}
	gem_resource_address_pair_set(target, address);
	return TRUE;
}

WORD
gem_resource_resident_obfix(GEM_RESOURCE_RESIDENT *resident,
			    GEM_FAR_ADDRESS tree, UWORD object)
{
	RSHDR header;
	UWORD available;
	UWORD offset;

	if (!resident
	    || !(resident->flags & GEM_RESOURCE_RESIDENT_LOADED)
	    || tree.hi != resident->storage.base.hi) {
		errno = EINVAL;
		return FALSE;
	}
	gem_resource_header_read(resident, &header);
	offset = header.rsh_object;
	available = header.rsh_nobs;
	while (available && offset != tree.lo) {
		offset = (UWORD) (offset + GEM_RESOURCE_OBJECT_BYTES);
		available--;
	}
	if (!available || object >= available) {
		errno = EINVAL;
		return FALSE;
	}
	while (object--)
		offset = (UWORD) (offset + GEM_RESOURCE_OBJECT_BYTES);
	gem_resource_fix_object_at(resident, offset);
	return TRUE;
}

WORD
gem_resource_resident_tree_table(const GEM_RESOURCE_RESIDENT *resident,
				 GEM_FAR_ADDRESS *tree_table)
{
	RSHDR header;
	UWORD owner_segment;

	if (!resident || !tree_table
	    || !(resident->flags & GEM_RESOURCE_RESIDENT_LOADED)) {
		errno = EINVAL;
		return FALSE;
	}

	/*
	 * A generated far-data load may temporarily use DS for the resource
	 * segment.  Preserve the exact caller DS instead of assuming that SS and
	 * DS alias: the resident AES owner intentionally has a distinct stack
	 * segment.  Both MOV instructions are available on the original 8086.
	 */
	__asm__ volatile ("movw %%ds,%0" : "=r" (owner_segment));
	gem_resource_header_read(resident, &header);
	__asm__ volatile ("movw %0,%%ds" : : "r" (owner_segment) : "memory");
	tree_table->lo = header.rsh_trindex;
	tree_table->hi = resident->storage.base.hi;
	return TRUE;
}
