/*****************************************************************************
 *
 *	-------------------------------------------------------------
 *	GEM Desktop					  Version 3.1
 *	Serial No.  XXXX-0000-654321		  All Rights Reserved
 *	Copyright (C) 1985 - 1989		Digital Research Inc.
 *	-------------------------------------------------------------
 *
 *****************************************************************************/

#include "portab.h"
#include "xfmlib.h"

EXTERN WORD x_xmul;              /* scaling factors for x transformation      */
EXTERN WORD x_xdiv;
EXTERN WORD x_xtrans;            /* translate factor for x transformation     */
EXTERN WORD x_ymul;              /* scaling factors for x transformation     */
EXTERN WORD x_ydiv;
EXTERN WORD x_ytrans;            /* translate factor for x transformation     */
EXTERN WORD x_wmicrons;
EXTERN WORD x_hmicrons;

/******************************************************************************
 *
 * Original GEM used an intermediate wider than one machine word here.  The
 * ELKS seam forms the product as explicit low/high words, then uses restoring
 * division one bit at a time.  The result is an unscaled integer truncated
 * toward zero.  A zero divisor returns zero.  Overflow saturates to -32768 or
 * 32767 at this signed-word interface.  Keeping the helper self-contained also
 * lets a trap-linked Desktop use this original closure without pulling in the
 * resident AES implementation and its duplicate public binding symbols.
 *
 ******************************************************************************/

WORD
x_mul_div(m1, m2, d1)
	WORD m1, m2, d1;
{
	UWORD left;
	UWORD right;
	UWORD divisor;
	UWORD multiplicand_high;
	UWORD product_low;
	UWORD product_high;
	UWORD quotient_low;
	UWORD quotient_high;
	UWORD remainder;
	UWORD old_low;
	UBYTE carry;
	UBYTE numerator_bit;
	UBYTE subtract;
	UBYTE count;
	WORD negative;

	if (!d1)
		return 0;

	negative = (m1 < 0) ^ (m2 < 0) ^ (d1 < 0);
	left = m1 < 0 ? (UWORD) (0U - (UWORD) m1) : (UWORD) m1;
	right = m2 < 0 ? (UWORD) (0U - (UWORD) m2) : (UWORD) m2;
	divisor = d1 < 0 ? (UWORD) (0U - (UWORD) d1) : (UWORD) d1;

	/* Form the unsigned 16-by-16 product in two explicit 16-bit halves. */
	product_low = 0;
	product_high = 0;
	multiplicand_high = 0;
	for (count = 0; count < 16; count++) {
		if (right & 1) {
			old_low = product_low;
			product_low += left;
			product_high += multiplicand_high;
			if (product_low < old_low)
				product_high++;
		}
		multiplicand_high = (UWORD) ((multiplicand_high << 1)
			| (left >> 15));
		left <<= 1;
		right >>= 1;
	}

	/*
	 * Consume the two-word numerator from its most significant bit.  The
	 * one-bit `carry` is the seventeenth remainder bit; it guarantees that
	 * subtraction remains correct when a 16-bit remainder shift overflows.
	 */
	remainder = 0;
	quotient_low = 0;
	quotient_high = 0;
	for (count = 0; count < 32; count++) {
		numerator_bit = (UBYTE) (product_high >> 15);
		product_high = (UWORD) ((product_high << 1)
			| (product_low >> 15));
		product_low <<= 1;
		carry = (UBYTE) (remainder >> 15);
		remainder = (UWORD) ((remainder << 1) | numerator_bit);
		subtract = carry || remainder >= divisor;
		if (subtract)
			remainder -= divisor;
		quotient_high = (UWORD) ((quotient_high << 1)
			| (quotient_low >> 15));
		quotient_low = (UWORD) ((quotient_low << 1) | subtract);
	}

	if (!negative) {
		if (quotient_high || quotient_low > 32767U)
			return 32767;
		return (WORD) quotient_low;
	}
	if (quotient_high || quotient_low > 32768U)
		return (WORD) 0x8000U;
	return (WORD) (0U - quotient_low);
}
