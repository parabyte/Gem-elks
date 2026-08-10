/*	OPTIMIZE.C	1/25/84 - 06/05/85	Lee Jay Lorenzen	*/
/*	merge source	5/28/87			mdf			*/

/*
*       Copyright 1999, Caldera Thin Clients, Inc.                      
*       This software is licenced under the GNU Public License.         
*       Please see LICENSE.TXT for further information.                 
*                                                                       
*                  Historical Copyright                                 
*	-------------------------------------------------------------
*	GEM Application Environment Services		  Version 2.3
*	Serial No.  XXXX-0000-654321		  All Rights Reserved
*	Copyright (C) 1985 - 1987		Digital Research Inc.
*	-------------------------------------------------------------
*/

#include "ppddesk.h"

EXTERN UWORD	intin[];
EXTERN UWORD	intout[];
EXTERN UWORD	contrl[];


/*
 * Return the low 16 bits of an unsigned product.
 *
 * A GEM WORD is exactly one 8086 word.  Addition therefore wraps modulo
 * 65,536, which is the behaviour of the historical small-model compiler.
 * The loop always executes sixteen times, so its cost is bounded and it never
 * asks the compiler for the very slow 8088 MUL instruction.
 */
UWORD
desk_uword_multiply(left, right)
	UWORD left, right;
{
	UWORD product;
	UBYTE count;

	product = 0;
	for (count = 0; count < 16; count++)
	{
	  if (right & 1U)
	    product += left;
	  left <<= 1;
	  right >>= 1;
	}
	return(product);
}


/* Preserve the original two's-complement low-word multiplication result. */
WORD
desk_word_multiply(left, right)
	WORD left, right;
{
	return((WORD)desk_uword_multiply((UWORD)left, (UWORD)right));
}


/*
 * Divide one unsigned word using sixteen restoring-division steps.
 *
 * The returned quotient is rounded down.  `remainder`, when non-null, is in
 * the range zero through denominator minus one.  A zero denominator returns
 * zero and leaves the complete numerator as the remainder; valid Desktop
 * geometry never uses that defensive case.  `carry` represents the
 * seventeenth bit created while shifting the remainder, so no wider C scalar
 * or compiler helper is required.
 */
UWORD
desk_uword_divide(numerator, denominator, remainder_out)
	UWORD numerator, denominator, *remainder_out;
{
	UWORD quotient;
	UWORD remainder;
	UWORD old_numerator;
	UWORD old_remainder;
	UBYTE numerator_bit;
	UBYTE carry;
	UBYTE count;

	if (!denominator)
	{
	  if (remainder_out)
	    *remainder_out = numerator;
	  return(0);
	}

	quotient = 0;
	remainder = 0;
	for (count = 0; count < 16; count++)
	{
	  old_numerator = numerator;
	  numerator += numerator;
	  numerator_bit = numerator < old_numerator;
	  old_remainder = remainder;
	  remainder += remainder;
	  carry = remainder < old_remainder;
	  remainder |= numerator_bit;
	  quotient <<= 1;
	  if (carry || remainder >= denominator)
	  {
	    remainder -= denominator;
	    quotient |= 1U;
	  }
	}
	if (remainder_out)
	  *remainder_out = remainder;
	return(quotient);
}


/*
 * Signed division truncates toward zero, matching 8086 IDIV and original GEM
 * C.  The remainder has the numerator's sign.  Magnitudes are formed as
 * unsigned words, so -32768 is handled without signed overflow.  The one
 * mathematically unrepresentable quotient (-32768 / -1) retains its native
 * low-word value 0x8000; Desktop callers do not present that pair.
 */
WORD
desk_word_divide(numerator, denominator, remainder_out)
	WORD numerator, denominator, *remainder_out;
{
	UWORD numerator_magnitude;
	UWORD denominator_magnitude;
	UWORD quotient;
	UWORD remainder;
	UWORD numerator_sign;
	UWORD denominator_sign;

	if (!denominator)
	{
	  if (remainder_out)
	    *remainder_out = numerator;
	  return(0);
	}

	numerator_sign = (UWORD)numerator & 0x8000U;
	denominator_sign = (UWORD)denominator & 0x8000U;
	numerator_magnitude = numerator_sign
		? (UWORD)(0U - (UWORD)numerator) : (UWORD)numerator;
	denominator_magnitude = denominator_sign
		? (UWORD)(0U - (UWORD)denominator) : (UWORD)denominator;
	quotient = desk_uword_divide(numerator_magnitude,
				     denominator_magnitude, &remainder);
	if (remainder_out)
	  *remainder_out = numerator_sign
		? (WORD)(0U - remainder) : (WORD)remainder;
	return(numerator_sign != denominator_sign
		? (WORD)(0U - quotient) : (WORD)quotient);
}


/* Form a near byte address using the same modulo-65,536 offset as GEM C. */
BYTE *
desk_byte_index(base, index, record_size)
	BYTE *base;
	UWORD index, record_size;
{
	return(base + desk_uword_multiply(index, record_size));
}


/*
 * OBJECT records are exactly 24 bytes at the AES boundary.  Compute
 * index * 24 as index * 8 plus index * 16 with four one-bit shifts.  This is
 * materially cheaper than the general helper in mouse/menu paths and keeps
 * the hot object lookup free of MUL.
 */
LPOBJ
desk_object_at(tree, obj)
	LPTREE tree;
	WORD obj;
{
	UWORD scaled;
	UWORD offset;
	union desk_far_object_pointer {
		LPOBJ pointer;
		GEM_U32_WORDS words;
	} result;

	scaled = (UWORD)obj;
	scaled <<= 1;
	scaled <<= 1;
	scaled <<= 1;
	offset = scaled;
	scaled <<= 1;
	offset += scaled;
	/*
	 * Resource trees occupy one validated segment and never cross its end.
	 * Add only the byte offset so ia16-gcc cannot normalize a far pointer or
	 * import a wide helper in this hot object lookup.
	 */
	result.pointer = tree;
	result.words.lo = (UWORD) (result.words.lo + offset);
	return(result.pointer);
}


	WORD
bit_num(flag)
	UWORD		flag;
{
	WORD		i;
	UWORD		test;

	if ( !flag )
	  return(-1);
	for (i=0,test=1; !(flag & test); test <<= 1,i++);
	return(i);
}

VOID rc_constrain(LPGRECT pc, LPGRECT pt)
{
	  if (pt->g_x < pc->g_x)
	    pt->g_x = pc->g_x;
	  if (pt->g_y < pc->g_y)
	    pt->g_y = pc->g_y;
	  if ((pt->g_x + pt->g_w) > (pc->g_x + pc->g_w))
	    pt->g_x = (pc->g_x + pc->g_w) - pt->g_w;
	  if ((pt->g_y + pt->g_h) > (pc->g_y + pc->g_h))
	    pt->g_y = (pc->g_y + pc->g_h) - pt->g_h;
}
/*

VOID rc_union(LPGRECT p1, LPGRECT p2)
{
	WORD		tx, ty, tw, th;

	tw = wmax(p1->g_x + p1->g_w, p2->g_x + p2->g_w);
	th = wmax(p1->g_y + p1->g_h, p2->g_y + p2->g_h);
	tx = wmin(p1->g_x, p2->g_x);
	ty = wmin(p1->g_y, p2->g_y);
	p2->g_x = tx;
	p2->g_y = ty;
	p2->g_w = tw - tx;
	p2->g_h = th - ty;
}


	WORD
rc_intersect(p1, p2)
	GRECT		*p1, *p2;
{
	WORD		tx, ty, tw, th;

	tw = wmin(p2->g_x + p2->g_w, p1->g_x + p1->g_w);
	th = wmin(p2->g_y + p2->g_h, p1->g_y + p1->g_h);
	tx = wmax(p2->g_x, p1->g_x);
	ty = wmax(p2->g_y, p1->g_y);
	p2->g_x = tx;
	p2->g_y = ty;
	p2->g_w = tw - tx;
	p2->g_h = th - ty;
	return( (tw > tx) && (th > ty) );
}
*/

	WORD
mid(lo, val, hi)
	WORD		lo, val, hi;
{
	if (val < lo)
	  return(lo);
	if (val > hi)
	  return(hi);
	return(val);
}

	BYTE
*strscn(ps, pd, stop)
	BYTE		*ps, *pd, stop;
{
	while ( (*ps) &&
		(*ps != stop) )
	  *pd++ = *ps++;
	return(pd);
}




/*
*	Strip out period and turn into raw data.
*/
	VOID
fmt_str(instr, outstr)
	BYTE		*instr, *outstr;
{
	WORD		count;
	BYTE		*pstr;

	pstr = instr;
	while( (*pstr) && (*pstr != '.') )
	  *outstr++ = *pstr++;
	if (*pstr)
	{
	  count = 8 - (pstr - instr);
	  while ( count-- )
	    *outstr++ = ' ';
	  pstr++;
	  while (*pstr)
	    *outstr++ = *pstr++;
	}
	*outstr = 0;
}


/*
*	Insert in period and make into true data.
*/
	VOID
unfmt_str(instr, outstr)
	BYTE		*instr, *outstr;
{
	BYTE		*pstr, temp;

	pstr = instr;
	while( (*pstr) && ((pstr - instr) < 8) )
	{
	  temp = *pstr++;
	  if (temp != ' ')
	    *outstr++ = temp;
	}
	if (*pstr)
	{
	  *outstr++ = '.';
	  while (*pstr)
	  {
	    temp = *pstr++;
	    if (temp != ' ')
	      *outstr++ = temp;
	  }
	}
	*outstr = 0;
}


VOID fs_sset(LPTREE tree, WORD obj, LPBYTE pstr,
	     GEM_SLOT_BYTE_POINTER *ptext, WORD *ptxtlen)
{
	GEM_TYPED_SLOT_POINTER slot;
	LPTEDI spec;
	LPOBJ object;
/*	char buf[82]; */

	object = desk_object_at(tree, obj);
	slot.words = object->ob_spec;
	spec = slot.tedinfo;
	if (!spec)
	{
	  *ptext = (GEM_SLOT_BYTE_POINTER) 0;
	  *ptxtlen = 0;
	  return;
	}

	/* Why are we copying these three in turn to the same buffer? 
	 * Debugging? 
	lstlcpy(ADDR(buf), spec->te_ptext,   sizeof(buf));
	lstlcpy(ADDR(buf), spec->te_ptmplt,  sizeof(buf));
	lstlcpy(ADDR(buf), spec->te_pvalid,  sizeof(buf));
*/	
	
	slot.words = spec->te_ptext;
	*ptext = slot.bytes;
	if (!*ptext)
	{
	  *ptxtlen = 0;
	  return;
	}
	*ptxtlen = spec->te_txtlen;
	LSTCPY(*ptext, pstr);

/*	lstlcpy(ADDR(buf), spec->te_ptext, sizeof(buf)); */
}


VOID inf_sset(LPTREE tree, WORD obj, BYTE *pstr)
{
	GEM_SLOT_BYTE_POINTER text;
	WORD		txtlen;

	fs_sset(tree, obj, ADDR(pstr), &text, &txtlen);
}


VOID fs_sget(LPTREE tree, WORD obj, LPBYTE pstr, WORD maxlen)
{
	GEM_SLOT_BYTE_POINTER ptext;

	{
	  LPOBJ object;
	  LPTEDI spec;
	  GEM_TYPED_SLOT_POINTER slot;

	  object = desk_object_at(tree, obj);
	  slot.words = object->ob_spec;
	  spec = slot.tedinfo;
	  if (spec) {
	    slot.words = spec->te_ptext;
	    ptext = slot.bytes;
	  } else {
	    ptext = (GEM_SLOT_BYTE_POINTER) 0;
	  }
	}
	if (ptext)
	  lstlcpy(pstr, ptext, maxlen);
	else if (maxlen)
	  *pstr = 0;
}


VOID inf_sget(LPTREE tree, WORD obj, BYTE *pstr, WORD maxlen)
{
	fs_sget(tree, obj, ADDR(pstr), maxlen);
}


/* v3.2: Allow proper field states, not the rather blunt methods of DR GEM */
VOID inf_fldset(LPTREE tree, WORD obj,
				UWORD testfld, UWORD testbit,
				UWORD truestate, UWORD falsestate)
{
	LPOBJ object;

	object = desk_object_at(tree, obj);
	if (testfld & testbit)
	{
		object->ob_state &= ~falsestate;
		object->ob_state |= truestate;
	}
	else
	{
		object->ob_state &= ~truestate;
		object->ob_state |= falsestate;
	}
}


WORD inf_gindex(LPTREE tree, WORD baseobj, WORD numobj)
{
	WORD		retobj;
	LPOBJ		object;

	object = desk_object_at(tree, baseobj);
	for (retobj=0; retobj < numobj; retobj++)
	{
	  if (object->ob_state & SELECTED)
	    return(retobj);
	  object++;
	}
	return(-1);
}


/*
*	Return 0 if cancel was selected, 1 if okay was selected, -1 if
*	nothing was selected.
*/

WORD inf_what(LPTREE tree, WORD ok, WORD cncl)
{
/* [JCE] Rewritten to avoid "dangerous" assumptions of object order */

	WORD		field = -1;
	LPOBJ		ok_object;
	LPOBJ		cancel_object;

	ok_object = desk_object_at(tree, ok);
	cancel_object = desk_object_at(tree, cncl);
	if (ok_object->ob_state & SELECTED)
	{
		field  = 1;
		ok_object->ob_state &= ~SELECTED;
	}
	if (cancel_object->ob_state & SELECTED)
	{
		field  = 0;
		cancel_object->ob_state &= ~SELECTED;
	}
	return(field);
}


VOID merge_str(BYTE *pdst, BYTE *ptmp, ...)
{
	va_list		ap;

	va_start(ap, ptmp);
	merge_v(pdst, ptmp, ap);
	va_end(ap);
	
}

VOID merge_v(BYTE *pdst, BYTE *ptmp, va_list ap)
{
	WORD		do_value;
	BYTE		lholder[12];
	BYTE		*pnum, *psrc;
	GEM_U32_WORDS	lvalue;
	UWORD		digit;
	
	while(*ptmp)
	{
		if (*ptmp != '%') 
		{
			*pdst++ = *ptmp++;
			continue;
		}
	    ptmp++;
	    do_value = FALSE;
	    switch(*ptmp++)
	    {
	      case '%':
			*pdst++ = '%'; break;
	      case 'L':
			lvalue = va_arg(ap, GEM_U32_WORDS);
			do_value = TRUE;
			break;
	      case 'W':
			lvalue = gem_u32_words(va_arg(ap, UWORD), 0);
			do_value = TRUE;
			break;
	      case 'S':
			psrc = va_arg(ap, BYTE *);
			while(*psrc)
		  		*pdst++ = *psrc++;
			break;
	    }
	    if (do_value)
	    {
	    	pnum = &lholder[0];
	      while(lvalue.lo || lvalue.hi)
	      {
				lvalue = gem_u32_div10(lvalue, &digit);
				*pnum++ = '0' + ((BYTE) digit);
	      }
	      	if ( pnum == ((BYTE *) &lholder[0]) ) *pdst++ = '0';
	      	else while(pnum != ((BYTE *) &lholder[0]) )
		  		*pdst++ = *--pnum;
	    }
	}
	*pdst = 0;
}

/*
*	Routine to see if the test filename matches one of a set of 
*	comma delimited wildcard strings.
*		e.g.,	pwld = "*.COM,*.EXE,*.BAT"
*		 	ptst = "MYFILE.BAT"
*/
WORD wildcmp(BYTE *pwld, BYTE *ptst)
{
	BYTE		*pwild;
	BYTE		*ptest;
						/* skip over *.*, and	*/
						/*   *.ext faster	*/
	while(*pwld)
	{
	  ptest = ptst;
	  pwild = pwld;
						/* move on to next 	*/
						/*   set of wildcards	*/
	  pwld = scasb(pwld, ',');
	  if (*pwld)
	    pwld++;
						/* start the checking	*/
	  if (pwild[0] == '*')
	  {
	    if (pwild[2] == '*')
	      return(TRUE);
	    else
	    {
	      pwild = &pwild[2];
	      ptest = scasb(ptest, '.');
	      if (*ptest)
	        ptest++;
	    }
	  }
						/* finish off comparison*/
	  while( (*ptest) && 
	         (*pwild) &&
		 (*pwild != ',') )
	  {
	    if (*pwild == '?')
	    {
	       pwild++;
	       if (*ptest != '.')
	         ptest++;
	    }
	    else
	    {
	      if (*pwild == '*')
	      {
	        if (*ptest != '.')
		  ptest++;
	        else		
		  pwild++;
	      }
	      else
	      {
	        if (*ptest == *pwild)
	        {
	          pwild++;
	          ptest++;
	        }
	        else
	          break;
	      }
	    }
	  }
						/* eat up remaining 	*/
						/*   wildcard chars	*/
	  while( (*pwild == '*') ||
	         (*pwild == '?') ||
	         (*pwild == '.') )
	    pwild++;
						/* if any part of wild-	*/
						/*   card or test is	*/
						/*   left then no match	*/
	  if ( ((*pwild == 0) || (*pwild == ',')) && 
	       (!*ptest) )
	    return( TRUE );
	}
	return(FALSE);
}



/*
*	Routine to insert a character in a string by
*/
	VOID
ins_char(str, pos, chr, tot_len)
	REG BYTE	*str;
	WORD		pos;
	BYTE		chr;
	REG WORD	tot_len;
{
	REG WORD	ii, len;

	len = strlen(str);

	for (ii = len; ii > pos; ii--)
	  str[ii] = str[ii-1];
	str[ii] = chr;
	if (len+1 < tot_len)
	  str[len+1] = 0;
	else
	  str[tot_len-1] = 0;
}





