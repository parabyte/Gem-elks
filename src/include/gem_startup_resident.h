/*
 * gem_startup_resident.h - GEM Desktop startup calls
 *
 * sits right below the AES's param-block copy, using the original five-word
 * control array, integer arrays and offset/segment slots directly. WIND_CALC
 * keeps the original wrap-at-65536 word behavior
 */

#ifndef ELKS_GEM_STARTUP_RESIDENT_H
#define ELKS_GEM_STARTUP_RESIDENT_H

#include "gem_bindings_elks.h"
#include "gem_channels.h"

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

/* mouse effects preserve hide/show nesting */
#define GEM_STARTUP_MOUSE_NONE           0U
#define GEM_STARTUP_MOUSE_FORM           1U
#define GEM_STARTUP_MOUSE_HIDE           2U
#define GEM_STARTUP_MOUSE_SHOW           3U

/* SHEL_GET needs at most one zero fill plus one marker-byte fill */
#define GEM_STARTUP_MAX_FILLS            2U

/* physical AES geometry */
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

/* one copied client call */
typedef struct gem_startup_call {
	UWORD owner;
	UWORD generation_lo;
	UWORD generation_hi;
	const UWORD *control;
	const UWORD *int_in;
	UWORD *int_out;
	const GEM_BINDINGS_POINTER_SLOT *addr_in;
} GEM_STARTUP_CALL;

/* one checked fill in the live client segment */
typedef struct gem_startup_fill {
	GEM_BINDINGS_POINTER_SLOT address;
	UWORD count;
	UBYTE value;
} GEM_STARTUP_FILL;

/* effects applied after dispatch */
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

/* remove one exact client generation */
VOID gem_startup_resident_detach(UWORD owner, UWORD generation_lo,
	UWORD generation_hi);

/* dispatch one startup call */
WORD gem_startup_resident_dispatch(const GEM_STARTUP_CALL *call,
	GEM_STARTUP_EFFECTS *effects, WORD *handled);

#endif				/* ELKS_GEM_STARTUP_RESIDENT_H */
