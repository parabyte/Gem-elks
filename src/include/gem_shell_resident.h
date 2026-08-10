/*
 * gem_shell_resident.h - GEM shell manager on ELKS processes
 *
 * calls keep the classic five control words, sixteen input words, seven output
 * words and two offset:segment address slots. bytes move straight between the
 * pinned client segment and fixed storage. SHEL_PUT changes only the requested
 * prefix of the 2048-byte Desktop context, SHEL_GET returns that same prefix
 */

#ifndef ELKS_GEM_SHELL_RESIDENT_H
#define ELKS_GEM_SHELL_RESIDENT_H

#include "gem_bindings_elks.h"

/* shell selector values */
#define GEM_SHELL_READ                  120U
#define GEM_SHELL_WRITE                 121U
#define GEM_SHELL_GET                   122U
#define GEM_SHELL_PUT                   123U
#define GEM_SHELL_FIND                  124U
#define GEM_SHELL_ENVRN                 125U
#define GEM_SHELL_RDEF                  126U
#define GEM_SHELL_WDEF                  127U

/* fixed dimensions from the original Desktop */
#define GEM_SHELL_PD_COUNT              6U
#define GEM_SHELL_DESKTOP_OWNER         0U
#define GEM_SHELL_COMMAND_BYTES         128U
#define GEM_SHELL_TAIL_BYTES            128U
#define GEM_SHELL_CONTEXT_BYTES         1024U
#define GEM_SHELL_DEFAULT_COMMAND_BYTES 14U
#define GEM_SHELL_DEFAULT_DIR_BYTES     67U
#define GEM_SHELL_PATH_BYTES            256U
#define GEM_SHELL_ENV_SEARCH_BYTES      32U

/* one call, after GEM's scalar arrays were copied. every address slot must name the pinned client segment before any byte moves, the bytes go through the resident memory helpers */
typedef struct gem_shell_call {
	UWORD owner;
	UWORD generation_lo;
	UWORD generation_hi;
	UWORD client_segment;
	UWORD client_limit;
	const UWORD *control;
	const UWORD *int_in;
	UWORD *int_out;
	const GEM_BINDINGS_POINTER_SLOT *addr_in;
} GEM_SHELL_CALL;

/* clear every process record and put back the Desktop context marker '#' */
VOID gem_shell_resident_reset(VOID);

/* dispatch SHEL_READ through SHEL_WDEF. a shell selector with bad args is still handled and returns FALSE, an unknown selector sets *handled FALSE so the next manager can try it */
WORD gem_shell_resident_dispatch(const GEM_SHELL_CALL *call, WORD *handled);

/* release only an exact process generation, a stale EXIT record does nothing */
VOID gem_shell_resident_detach(UWORD owner, UWORD generation_lo,
	UWORD generation_hi);

/* take the one command SHEL_WRITE recorded. command gets the resolved executable path and tail the length-prefixed argument record, 128 bytes each. FALSE when no launch is pending */
WORD gem_shell_resident_take_command(UBYTE *command, UWORD command_bytes,
	UBYTE *tail, UWORD tail_bytes, WORD *is_gem);

#endif				/* ELKS_GEM_SHELL_RESIDENT_H */
