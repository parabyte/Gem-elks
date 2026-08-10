/*
 * gem_wordpair.c - four-byte GEM fields as two 16-bit words
 *
 * some GEM fields hold four-byte values. these move and do math on them
 * as word pairs so ia16-gcc dont pull in its own routines
 * i tried using ia16's functions but they are not as fast as some of these original gem functions
 */

#include "aes.h"

static UWORD
gem_wordpair_data_segment(VOID)
{
	UWORD segment;

	__asm__ volatile ("movw %%ds,%0":"=r" (segment));
	return segment;
}

GEM_U32_WORDS
gem_near_pointer_words(const void FAR *pointer)
{
	GEM_U32_WORDS field;

#if GEM_TRAP_FAR_DATA
	/* ia16 stores a far data pointer as the same two adjacent words a
	 * GEM resource slot uses */
	union gem_far_pointer_words {
		const void FAR *pointer;
		GEM_U32_WORDS words;
	} value;

	value.pointer = pointer;
	field = value.words;
#else
	/* near offset in the low word, zero segment */
	field.lo = (UWORD) pointer;
	field.hi = 0;
#endif
	return field;
}

LPVOID
gem_near_words_pointer(GEM_U32_WORDS field)
{
	/* only the caller's exact DS (or a zero high word in the compact
	 * ABI) is accepted; resource segments go through GEM_TYPED_SLOT_POINTER */
#if GEM_TRAP_FAR_DATA
	if (field.hi != gem_wordpair_data_segment())
		return NULL;
#else
	if (field.hi)
		return NULL;
#endif
	return (LPVOID) field.lo;
}

GEM_U32_WORDS
gem_u32_words(UWORD lo, UWORD hi)
{
	GEM_U32_WORDS value;

	value.lo = lo;
	value.hi = hi;
	return value;
}

VOID
gem_u32_add_to(GEM_U32_WORDS *value, GEM_U32_WORDS amount)
{
	UWORD old_lo;

	old_lo = value->lo;
	value->lo += amount.lo;
	value->hi += amount.hi;
	if (value->lo < old_lo)
		value->hi++;
	/* a carry out of hi just wraps */
}

GEM_U32_WORDS
gem_u32_mul_u16(UWORD left, UWORD right)
{
	GEM_U32_WORDS result;
	GEM_U32_WORDS addend;
	UWORD carry;
	UWORD count;

	result = gem_u32_words(0, 0);
	addend = gem_u32_words(left, 0);
	count = 16;
	while (count--) {
		if (right & 1)
			gem_u32_add_to(&result, addend);
		right >>= 1;
		carry = (addend.lo & 0x8000U) ? 1 : 0;
		addend.lo <<= 1;
		addend.hi = (UWORD) ((addend.hi << 1) | carry);
	}
	return result;
}

UWORD
gem_u32_to_u16_sat(GEM_U32_WORDS value)
{
	if (value.hi)
		return 0xffffU;
	return value.lo;
}

GEM_U32_WORDS
gem_u32_div10(GEM_U32_WORDS value, UWORD *remainder)
{
	GEM_U32_WORDS quotient;
	UWORD rem;
	UWORD bit;
	UWORD count;

	/* binary restoring division, one source bit per step */
	quotient = gem_u32_words(0, 0);
	rem = 0;
	count = 16;
	while (count--) {
		bit = (value.hi & 0x8000U) ? 1 : 0;
		value.hi <<= 1;
		quotient.hi <<= 1;
		rem = (UWORD) ((rem << 1) | bit);
		if (rem >= 10) {
			rem -= 10;
			quotient.hi |= 1;
		}
	}
	count = 16;
	while (count--) {
		bit = (value.lo & 0x8000U) ? 1 : 0;
		value.lo <<= 1;
		quotient.lo <<= 1;
		rem = (UWORD) ((rem << 1) | bit);
		if (rem >= 10) {
			rem -= 10;
			quotient.lo |= 1;
		}
	}
	if (remainder)
		*remainder = rem;
	return quotient;
}
