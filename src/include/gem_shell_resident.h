/* GEM shell calls on ELKS */

#ifndef ELKS_GEM_SHELL_RESIDENT_H
#define ELKS_GEM_SHELL_RESIDENT_H

#include "gem_bindings_elks.h"
#include "gem_channels.h"

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
#define GEM_SHELL_DESKTOP_OWNER         0U
#define GEM_SHELL_COMMAND_BYTES         128U
#define GEM_SHELL_TAIL_BYTES            128U
#define GEM_SHELL_CONTEXT_BYTES         1024U
#define GEM_SHELL_DEFAULT_COMMAND_BYTES 14U
#define GEM_SHELL_DEFAULT_DIR_BYTES     67U
#define GEM_SHELL_PATH_BYTES            256U
#define GEM_SHELL_ENV_SEARCH_BYTES      32U

/* one checked client call */
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

/* clear shell state */
VOID gem_shell_resident_reset(VOID);

/* dispatch SHEL_READ through SHEL_WDEF */
WORD gem_shell_resident_dispatch(const GEM_SHELL_CALL *call, WORD *handled);

/* release one exact client generation */
VOID gem_shell_resident_detach(UWORD owner, UWORD generation_lo,
	UWORD generation_hi);

#endif				/* ELKS_GEM_SHELL_RESIDENT_H */
