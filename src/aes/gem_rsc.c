/*
 * gem_rsc.c - reads a loaded GEM .RSC file
 *
 * looks up an object, string or image by its number and hands back where
 * it sits. keeps no memory of its own so any resource can share it
 */

#include "gem_rsc.h"

typedef union gem_rsc_far_pointer {
	VOID __far *pointer;
	UBYTE __far *bytes;
	UWORD __far *words;
	GEM_U32_WORDS __far *pair;
	RSHDR __far *header;
	GEM_FAR_ADDRESS address;
} GEM_RSC_FAR_POINTER;

typedef BYTE GEM_RSC_FAR_POINTER_MUST_BE_FOUR_BYTES
	[(sizeof(GEM_RSC_FAR_POINTER) == 4) ? 1 : -1];

VOID __far *
gem_rsc_pointer(UWORD segment, UWORD offset)
{
	GEM_RSC_FAR_POINTER value;

	value.address.lo = offset;
	value.address.hi = segment;
	return value.pointer;
}

WORD
gem_rsc_range(UWORD offset, UWORD count, UWORD limit)
{
	if (offset > limit)
		return FALSE;
	return count <= (UWORD) (limit - offset);
}

UBYTE
gem_rsc_byte(const GEM_RSC_IMAGE *image, UWORD offset)
{
	UBYTE __far *pointer;

	pointer = (UBYTE __far *) gem_rsc_pointer(image->segment, offset);
	return *pointer;
}

VOID
gem_rsc_byte_set(const GEM_RSC_IMAGE *image, UWORD offset, UBYTE value)
{
	UBYTE __far *pointer;

	pointer = (UBYTE __far *) gem_rsc_pointer(image->segment, offset);
	*pointer = value;
}

GEM_U32_WORDS
gem_rsc_pair(const GEM_RSC_IMAGE *image, UWORD offset)
{
	GEM_U32_WORDS result;
	GEM_U32_WORDS __far *pointer;

	pointer = (GEM_U32_WORDS __far *) gem_rsc_pointer(image->segment,
		offset);
	result.lo = pointer->lo;
	result.hi = pointer->hi;
	return result;
}

VOID
gem_rsc_pair_set(const GEM_RSC_IMAGE *image, UWORD offset, GEM_U32_WORDS value)
{
	GEM_U32_WORDS __far *pointer;

	pointer = (GEM_U32_WORDS __far *) gem_rsc_pointer(image->segment,
		offset);
	pointer->lo = value.lo;
	pointer->hi = value.hi;
}

/* writes a four-byte address into a slot that can sit outside the
 * resource, so build the destination from its own words */
VOID
gem_rsc_address_set(GEM_FAR_ADDRESS target, GEM_FAR_ADDRESS value)
{
	GEM_U32_WORDS __far *pointer;

	pointer = (GEM_U32_WORDS __far *) gem_rsc_pointer(target.hi, target.lo);
	pointer->lo = value.lo;
	pointer->hi = value.hi;
}

WORD
gem_rsc_pair_is_nil(GEM_U32_WORDS value)
{
	return value.lo == 0xffffU && value.hi == 0xffffU;
}

VOID
gem_rsc_invalid_address(GEM_FAR_ADDRESS *address)
{
	address->lo = 0xffffU;
	address->hi = 0xffffU;
}

VOID
gem_rsc_header_copy(RSHDR *destination, const RSHDR *source)
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

VOID
gem_rsc_header(const GEM_RSC_IMAGE *image, RSHDR *header)
{
	RSHDR __far *source;

	source = (RSHDR __far *) gem_rsc_pointer(image->segment, 0);
	header->rsh_vrsn = source->rsh_vrsn;
	header->rsh_object = source->rsh_object;
	header->rsh_tedinfo = source->rsh_tedinfo;
	header->rsh_iconblk = source->rsh_iconblk;
	header->rsh_bitblk = source->rsh_bitblk;
	header->rsh_frstr = source->rsh_frstr;
	header->rsh_string = source->rsh_string;
	header->rsh_imdata = source->rsh_imdata;
	header->rsh_frimg = source->rsh_frimg;
	header->rsh_trindex = source->rsh_trindex;
	header->rsh_nobs = source->rsh_nobs;
	header->rsh_ntree = source->rsh_ntree;
	header->rsh_nted = source->rsh_nted;
	header->rsh_nib = source->rsh_nib;
	header->rsh_nbb = source->rsh_nbb;
	header->rsh_nstring = source->rsh_nstring;
	header->rsh_nimages = source->rsh_nimages;
	header->rsh_rssize = source->rsh_rssize;
}

/* repeated adds so ia16-gcc dont call a multiply helper */
static UWORD
gem_rsc_repeat_add(UWORD value, UWORD count, UWORD addend)
{
	if (!count)
		return value;
	__asm__ volatile ("1:\n\t"
		"addw %2,%0\n\t" "loop 1b":"+&r" (value), "+c"(count)
		:"r"(addend)
		:"cc");
	return value;
}

WORD
gem_rsc_index_offset(UWORD base, UWORD index, UWORD count, UWORD size,
	UWORD *offset)
{
	if (index >= count)
		return FALSE;
	*offset = gem_rsc_repeat_add(base, index, size);
	return TRUE;
}

WORD
gem_rsc_address(const GEM_RSC_IMAGE *image, UWORD type, UWORD index,
	GEM_FAR_ADDRESS *address)
{
	RSHDR header;
	GEM_U32_WORDS stored;
	UWORD offset;

	if (!image || !image->segment || !address) {
		if (address)
			gem_rsc_invalid_address(address);
		return FALSE;
	}
	gem_rsc_header(image, &header);

	switch (type) {
	case R_TREE:
		if (!gem_rsc_index_offset(header.rsh_trindex, index,
				header.rsh_ntree, GEM_RSC_POINTER_BYTES,
				&offset))
			break;
		goto stored_pointer;
	case R_OBJECT:
		if (!gem_rsc_index_offset(header.rsh_object, index,
				header.rsh_nobs, GEM_RSC_OBJECT_BYTES, &offset))
			break;
		goto return_slot;
	case R_TEDINFO:
	case R_TEPTEXT:
		if (!gem_rsc_index_offset(header.rsh_tedinfo, index,
				header.rsh_nted, GEM_RSC_TEDINFO_BYTES,
				&offset))
			break;
		goto return_slot;
	case R_ICONBLK:
	case R_IBPMASK:
		if (!gem_rsc_index_offset(header.rsh_iconblk, index,
				header.rsh_nib, GEM_RSC_ICONBLK_BYTES, &offset))
			break;
		goto return_slot;
	case R_BITBLK:
	case R_BIPDATA:
		if (!gem_rsc_index_offset(header.rsh_bitblk, index,
				header.rsh_nbb, GEM_RSC_BITBLK_BYTES, &offset))
			break;
		goto return_slot;
	case R_OBSPEC:
		if (!gem_rsc_index_offset(header.rsh_object, index,
				header.rsh_nobs, GEM_RSC_OBJECT_BYTES, &offset))
			break;
		offset = (UWORD) (offset + GEM_RSC_OB_SPEC);
		goto return_slot;
	case R_TEPTMPLT:
	case R_TEPVALID:
		if (!gem_rsc_index_offset(header.rsh_tedinfo, index,
				header.rsh_nted, GEM_RSC_TEDINFO_BYTES,
				&offset))
			break;
		offset = (UWORD) (offset + ((type == R_TEPTMPLT)
				? GEM_RSC_TE_PTMPLT : GEM_RSC_TE_PVALID));
		goto return_slot;
	case R_IBPDATA:
	case R_IBPTEXT:
		if (!gem_rsc_index_offset(header.rsh_iconblk, index,
				header.rsh_nib, GEM_RSC_ICONBLK_BYTES, &offset))
			break;
		offset = (UWORD) (offset + ((type == R_IBPDATA)
				? GEM_RSC_IB_PDATA : GEM_RSC_IB_PTEXT));
		goto return_slot;
	case R_STRING:
		if (!gem_rsc_index_offset(header.rsh_frstr, index,
				header.rsh_nstring, GEM_RSC_POINTER_BYTES,
				&offset))
			break;
		goto stored_pointer;
	case R_IMAGEDATA:
		if (!gem_rsc_index_offset(header.rsh_frimg, index,
				header.rsh_nimages, GEM_RSC_POINTER_BYTES,
				&offset))
			break;
		goto stored_pointer;
	case R_FRSTR:
		if (!gem_rsc_index_offset(header.rsh_frstr, index,
				header.rsh_nstring, GEM_RSC_POINTER_BYTES,
				&offset))
			break;
		goto return_slot;
	case R_FRIMG:
		if (!gem_rsc_index_offset(header.rsh_frimg, index,
				header.rsh_nimages, GEM_RSC_POINTER_BYTES,
				&offset))
			break;
		goto return_slot;
	default:
		break;
	}
	gem_rsc_invalid_address(address);
	return FALSE;

stored_pointer:
	/* table holds the address, hand back what it points at */
	stored = gem_rsc_pair(image, offset);
	if (gem_rsc_pair_is_nil(stored)) {
		gem_rsc_invalid_address(address);
		return FALSE;
	}
	*address = stored;
	return TRUE;

return_slot:
	/* caller wants the slot itself, not what it holds */
	address->lo = offset;
	address->hi = image->segment;
	return TRUE;
}

WORD
gem_rsc_string(const GEM_RSC_IMAGE *image, UWORD offset, BYTE *buffer,
	UWORD size)
{
	UWORD length;

	if (!image || !image->segment || !buffer || !size)
		return -1;
	buffer[0] = 0;
	length = 0;
	while (length + 1U < size) {
		if (!gem_rsc_range(offset, (UWORD) (length + 1U), image->bytes))
			return -1;
		buffer[length] = (BYTE) gem_rsc_byte(image,
			(UWORD) (offset + length));
		if (!buffer[length])
			return (WORD) length;
		length++;
	}
	buffer[length] = 0;
	return (WORD) length;
}
