/*
 * gem_shell_resident.h - original GEM shell manager on ELKS processes.
 *
 * Calls retain GEMSUPER.C's five control words, sixteen input words, seven
 * output words, and two offset:segment address slots.  The resident owner
 * copies bytes directly between the pinned client segment and fixed storage;
 * no command structure, converted path object, heap buffer, or helper process
 * crosses this interface.
 *
 * Path and byte counts are unscaled.  Every count is checked before an 8086
 * add or copy, so failure is reported instead of wrapping.  The 2,048-byte
 * Desktop context is copied exactly: SHEL_PUT changes only the requested
 * prefix and SHEL_GET returns that same prefix without rounding or padding.
 */

#ifndef ELKS_GEM_SHELL_RESIDENT_H
#define ELKS_GEM_SHELL_RESIDENT_H

#if defined(ELKS) && ELKS
#include "gem_bindings_elks.h"
#else
typedef signed short WORD;
typedef unsigned short UWORD;
typedef unsigned char UBYTE;
#define VOID void
#define TRUE 1
#define FALSE 0
typedef struct __attribute__((packed)) gem_bindings_pointer_slot {
	UWORD lo;
	UWORD hi;
} GEM_BINDINGS_POINTER_SLOT;
typedef char GEM_SHELL_HOST_WORD_MUST_BE_TWO_BYTES
	[(sizeof(UWORD) == 2) ? 1 : -1];
#endif

/* Direct shell selector values from original CRYSBIND.H. */
#define GEM_SHELL_READ                  120U
#define GEM_SHELL_WRITE                 121U
#define GEM_SHELL_GET                   122U
#define GEM_SHELL_PUT                   123U
#define GEM_SHELL_FIND                  124U
#define GEM_SHELL_ENVRN                 125U
#define GEM_SHELL_RDEF                  126U
#define GEM_SHELL_WDEF                  127U

/* Fixed dimensions from GEMSHLIB.C, GEMLIB.H, and the original Desktop. */
#define GEM_SHELL_PD_COUNT              12U
#define GEM_SHELL_DESKTOP_OWNER         0U
#define GEM_SHELL_COMMAND_BYTES         128U
#define GEM_SHELL_TAIL_BYTES            128U
#define GEM_SHELL_CONTEXT_BYTES         2048U
#define GEM_SHELL_DEFAULT_COMMAND_BYTES 14U
#define GEM_SHELL_DEFAULT_DIR_BYTES     67U
#define GEM_SHELL_PATH_BYTES            256U
#define GEM_SHELL_ENV_SEARCH_BYTES      32U

/*
 * One call after the outer XIF has copied GEM's scalar arrays locally.
 * client_segment and client_limit describe the exact pinned ELKS data
 * segment.  All address slots must name that segment before any byte moves.
 *
 * Native smoke tests set client_memory to their synthetic segment.  The ELKS
 * build omits that member entirely and uses REP MOVSB through the resident
 * memory seam, so no host pointer enters the target record or runtime path.
 */
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
#if !defined(ELKS) || !ELKS
	UBYTE *client_memory;
#endif
} GEM_SHELL_CALL;

/* Clear all process records and restore the Desktop context marker '#'. */
VOID gem_shell_resident_reset(VOID);

/*
 * Dispatch SHEL_READ through SHEL_WDEF.  A recognized malformed selector is
 * handled and returns FALSE.  An unrelated selector leaves arrays untouched,
 * sets *handled FALSE, and returns FALSE for the next original manager.
 */
WORD gem_shell_resident_dispatch(const GEM_SHELL_CALL *call, WORD *handled);

/* Release only an exact process generation; stale EXIT records do nothing. */
VOID gem_shell_resident_detach(UWORD owner, UWORD generation_lo,
	UWORD generation_hi);

/*
 * Consume the one command recorded by SHEL_WRITE, exactly as the original
 * single-tasking AES shell did after the requesting application exited.
 * command receives the resolved executable path (128 bytes) and tail the
 * classic length-prefixed argument record (128 bytes).  FALSE when no
 * launch is pending.
 */
WORD gem_shell_resident_take_command(UBYTE *command, UWORD command_bytes,
	UBYTE *tail, UWORD tail_bytes, WORD *is_gem);

#endif /* ELKS_GEM_SHELL_RESIDENT_H */
