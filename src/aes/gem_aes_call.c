/*
 * gem_aes_call.c - the argument checks every AES manager shares
 *
 * one copy of the shape test the eight managers each used to carry,
 * nothing here reads client memory, it just decides whether the
 * control block and the arrays behind it are worth reading at all
 */

#include "gem_aes_call.h"

WORD
gem_aes_call_counts(const UWORD *control, UWORD input_count,
	UWORD output_count, UWORD address_count)
{
	if (!control)
		return FALSE;
	return control[1] >= input_count && control[2] >= output_count
		&& control[3] >= address_count;
}

WORD
gem_aes_call_arrays(const UWORD *int_in, UWORD input_count,
	UWORD *int_out, UWORD output_count,
	const GEM_BINDINGS_POINTER_SLOT *addr_in, UWORD address_count)
{
	if (input_count && !int_in)
		return FALSE;
	if (output_count && !int_out)
		return FALSE;
	return !address_count
		|| addr_in != (const GEM_BINDINGS_POINTER_SLOT *) 0;
}

WORD
gem_aes_call_malformed(const UWORD *control, UWORD *int_out, WORD *handled)
{
	if (handled)
		*handled = TRUE;
	if (control && int_out && control[2])
		int_out[0] = FALSE;
	return FALSE;
}
