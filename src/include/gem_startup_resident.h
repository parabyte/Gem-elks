/*
 * gem_startup_resident.h - GEM Desktop startup calls
 *
 * sits right below the AES's param-block copy, using the original five-word
 * control array, integer arrays and offset/segment slots directly. WIND_CALC
 * keeps the original wrap-at-65536 word behavior
 */

#ifndef ELKS_GEM_STARTUP_RESIDENT_H
#define ELKS_GEM_STARTUP_RESIDENT_H

/* fallback types for the native smoke test, never crosses the trap */
#include "gem_bindings_elks.h"

/* GEM/XM has twelve PD slots */
#define GEM_STARTUP_PD_COUNT             6U

/* the GEM startup opcodes */
#define GEM_STARTUP_APPL_BVSET           16U
#define GEM_STARTUP_APPL_BVEXT           18U
#define GEM_STARTUP_GRAF_HANDLE          77U
#define GEM_STARTUP_GRAF_MOUSE           78U
#define GEM_STARTUP_WIND_GET             104U
#define GEM_STARTUP_WIND_UPDATE          107U
#define GEM_STARTUP_WIND_CALC            108U
#define GEM_STARTUP_SHEL_GET             122U

/* SHEL_GET serves the Desktop's 2048-byte save area */
#define GEM_STARTUP_SHELL_BYTES          2048U

/* mouse effects keep the hide/show nesting. NONE covers a nested hide or partial show: the logical count changes but the physical cursor needs no driver call */
#define GEM_STARTUP_MOUSE_NONE           0U
#define GEM_STARTUP_MOUSE_FORM           1U
#define GEM_STARTUP_MOUSE_HIDE           2U
#define GEM_STARTUP_MOUSE_SHOW           3U

/* SHEL_GET needs at most one zero fill plus one marker-byte fill */
#define GEM_STARTUP_MAX_FILLS            2U

/* physical AES geometry, set once the resident VDI opens its screen. width and height are pixel counts not max coords, character and box sizes are what GRAF_HANDLE returns */
typedef struct gem_startup_screen {
	WORD vdi_handle;
	WORD character_width;
	WORD character_height;
	WORD box_width;
	WORD box_height;
	WORD screen_width;
	WORD screen_height;
	UBYTE frame_3d;
} GEM_STARTUP_SCREEN;

/* one call, using the arrays copied from the pinned client. owner is the zero-through-eleven PD tag */
typedef struct gem_startup_call {
	UWORD owner;
	UWORD generation_lo;
	UWORD generation_hi;
	const UWORD *control;
	const UWORD *int_in;
	UWORD *int_out;
	const GEM_BINDINGS_POINTER_SLOT *addr_in;
} GEM_STARTUP_CALL;

/* a client fill is an instruction for the outer service, not a copied buffer. the resident owner checks the range against the pinned DS, then fills count bytes with value */
typedef struct gem_startup_fill {
	GEM_BINDINGS_POINTER_SLOT address;
	UWORD count;
	UBYTE value;
} GEM_STARTUP_FILL;

/* side effects applied after dispatch, fills in array order. mouse_form_address is used only for USER_DEF (255), built-in forms carry the null 0:0 slot */
typedef struct gem_startup_effects {
	UBYTE fill_count;
	UBYTE mouse_action;
	WORD mouse_number;
	GEM_BINDINGS_POINTER_SLOT mouse_form_address;
	GEM_STARTUP_FILL fills[GEM_STARTUP_MAX_FILLS];
} GEM_STARTUP_EFFECTS;

/* clear all fixed state, including the physical-screen setup */
VOID gem_startup_resident_reset(VOID);

/* install the physical geometry after VDI open, bad geometry is refused */
WORD gem_startup_resident_configure(const GEM_STARTUP_SCREEN *screen);

/* remove state only when owner and generation match, so a stale EXIT record cant tear down a freshly reused PD slot */
VOID gem_startup_resident_detach(UWORD owner, UWORD generation_lo,
	UWORD generation_hi);

/* dispatch one startup call. a supported call sets *handled TRUE, writes int_out[0], returns that AES result. an uncovered opcode sets *handled FALSE so a later manager can take it */
WORD gem_startup_resident_dispatch(const GEM_STARTUP_CALL *call,
	GEM_STARTUP_EFFECTS *effects, WORD *handled);

#endif				/* ELKS_GEM_STARTUP_RESIDENT_H */
