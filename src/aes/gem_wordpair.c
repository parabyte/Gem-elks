/*
 * gem_wordpair.c - four-byte GEM fields using only 8086 word arithmetic.
 *
 * Classic GEM resources, GEMDOS directory records, and a few AES parameter
 * blocks contain four-byte quantities.  An IBM PC/XT still moves those
 * records as two 16-bit words; a C `long` is neither required nor desirable.
 * These helpers make carry, borrow, overflow, and rounding behavior explicit
 * and keep ia16-gcc from importing wide multiply or divide support routines.
 */

#include "aes.h"

static UWORD
gem_wordpair_data_segment(VOID)
{
	UWORD segment;

	__asm__ volatile ("movw %%ds,%0" : "=r" (segment));
	return segment;
}

GEM_U32_WORDS
gem_near_pointer_words(const void FAR *pointer)
{
	GEM_U32_WORDS field;

#if GEM_TRAP_FAR_DATA
	/*
	 * GNU ia16 represents a far data pointer as the same two adjacent words
	 * used by an original GEM resource slot.  A union copies those halves
	 * without a C long, a shift, normalization, or a compiler helper call.
	 * Near arguments are promoted with the caller's DS by the compiler.
	 */
	union gem_far_pointer_words {
		const void FAR *pointer;
		GEM_U32_WORDS words;
	} value;

	value.pointer = pointer;
	field = value.words;
#else

	/*
	 * ELKS programs use near data pointers in their hot paths.  A historical
	 * GEM pointer slot is nevertheless four bytes wide, so store the near
	 * offset in the low word and explicitly clear the segment/high word.
	 * No arithmetic, rounding, or widening is performed at this boundary.
	 */
	field.lo = (UWORD) pointer;
	field.hi = 0;
#endif
	return field;
}

LPVOID
gem_near_words_pointer(GEM_U32_WORDS field)
{
	/*
	 * A primary-data pointer is represented by its offset and, in the
	 * original-client ABI, the caller's DS.  Accept only that exact segment;
	 * resource segments must be decoded through GEM_TYPED_SLOT_POINTER.  The
	 * compact ABI continues to require a zero high word.  No normalization or
	 * wide arithmetic is performed in either case.
	 */
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
	/* A carry out of hi deliberately wraps, as in original GEM arithmetic. */
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

	/*
	 * Binary restoring division consumes one source bit per iteration.  The
	 * remainder is always 0..9, the quotient is truncated toward zero, and no
	 * hardware or compiler division is used.  Fixed 32 iterations make timing
	 * and overflow behavior independent of the input value.
	 */
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
