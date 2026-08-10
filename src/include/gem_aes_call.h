/*
 * gem_aes_call.h - shared arg checks for every AES manager
 *
 * the resident cant trust the client arrays, so each manager checks the
 * block shape before reading it. two halves: counts() for the control[1..3]
 * word promise, arrays() for the arrays this manager reads
 */

#ifndef ELKS_GEM_AES_CALL_H
#define ELKS_GEM_AES_CALL_H

#include "aes.h"
#include "gem_bindings_elks.h"

/* control block promises at least this many int_in, int_out and addr_in entries */
WORD gem_aes_call_counts(const UWORD *control, UWORD input_count,
	UWORD output_count, UWORD address_count);

/* every array the manager reads is present, pass null and zero count for one it dont carry */
WORD gem_aes_call_arrays(const UWORD *int_in, UWORD input_count,
	UWORD *int_out, UWORD output_count,
	const GEM_BINDINGS_POINTER_SLOT *addr_in, UWORD address_count);

/* known selector with bad args, mark it handled and answer FALSE in int_out[0] if theres room */
WORD gem_aes_call_malformed(const UWORD *control, UWORD *int_out,
	WORD *handled);

#endif				/* ELKS_GEM_AES_CALL_H */
