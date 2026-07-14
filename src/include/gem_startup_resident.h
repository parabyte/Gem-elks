/*
 * gem_startup_resident.h - bounded original GEM Desktop startup closure.
 *
 * This interface sits immediately below the resident AES XIF copy.  It uses
 * the original five-word AES control array, integer arrays, and four-byte
 * offset/segment slots directly.  There is no RPC structure conversion and
 * no application pointer is flattened into resident data space.
 *
 * All coordinates, sizes, drive vectors, generations, and counters are
 * unscaled 16-bit values.  WIND_CALC follows the original GEM modulo-65536
 * word behavior; it performs no rounding, multiplication, or division.
 */

#ifndef ELKS_GEM_STARTUP_RESIDENT_H
#define ELKS_GEM_STARTUP_RESIDENT_H

/*
 * The ELKS build consumes the project's exact AES wire types.  A native host
 * smoke has no 16-bit GEM object model, so it uses only this module's scalar
 * subset.  `short` is checked below to remain exactly two bytes; the fallback
 * never crosses INT EF and is not part of the target ABI.
 */
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
typedef char GEM_STARTUP_HOST_WORD_MUST_BE_2_BYTES
	[(sizeof(UWORD) == 2) ? 1 : -1];
#endif

/* Original GEM/XM MULTIAPP has twelve PD slots, numbered zero through eleven. */
#define GEM_STARTUP_PD_COUNT             12U

/* Direct opcode values from original CRYSBIND.H and GEMSUPER.C. */
#define GEM_STARTUP_APPL_BVSET           16U
#define GEM_STARTUP_APPL_BVEXT           18U
#define GEM_STARTUP_GRAF_HANDLE          77U
#define GEM_STARTUP_GRAF_MOUSE           78U
#define GEM_STARTUP_WIND_GET             104U
#define GEM_STARTUP_WIND_UPDATE          107U
#define GEM_STARTUP_WIND_CALC            108U
#define GEM_STARTUP_SHEL_GET             122U

/* This bounded SHEL_GET supplies the Desktop's exact 2048-byte save area. */
#define GEM_STARTUP_SHELL_BYTES          2048U

/*
 * Mouse effects preserve GEMGSXIF.C gsx_moff()/gsx_mon() nesting.  NONE is
 * intentional for a nested hide or a partial show: the logical count changes,
 * but the physical cursor does not need another driver call.
 */
#define GEM_STARTUP_MOUSE_NONE           0U
#define GEM_STARTUP_MOUSE_FORM           1U
#define GEM_STARTUP_MOUSE_HIDE           2U
#define GEM_STARTUP_MOUSE_SHOW           3U

/* SHEL_GET needs at most one zero fill followed by one marker-byte fill. */
#define GEM_STARTUP_MAX_FILLS            2U

/*
 * Physical AES geometry established after the resident VDI opens its screen.
 * Width and height are pixel counts, not maximum coordinates.  Character and
 * box metrics are pixel counts returned by the original GRAF_HANDLE call.
 * frame_3d is a Boolean; every other value is an exact unscaled GEM word.
 */
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

/*
 * One invocation uses the original local arrays after XIF has copied them
 * from the pinned client.  owner is the original zero-through-eleven PD tag.
 * The two generation words are compared separately and are never combined.
 */
typedef struct gem_startup_call {
	UWORD owner;
	UWORD generation_lo;
	UWORD generation_hi;
	const UWORD *control;
	const UWORD *int_in;
	UWORD *int_out;
	const GEM_BINDINGS_POINTER_SLOT *addr_in;
} GEM_STARTUP_CALL;

/*
 * A client fill is an integration instruction, not a copied client buffer.
 * The resident owner validates the exact address range against the pinned DS,
 * then fills count bytes with value.  The address is not advanced here, so no
 * far-pointer arithmetic or offset wrap can be hidden in this module.
 */
typedef struct gem_startup_fill {
	GEM_BINDINGS_POINTER_SLOT address;
	UWORD count;
	UBYTE value;
} GEM_STARTUP_FILL;

/*
 * Side effects which the outer resident service applies after dispatch.
 * Fill records must be applied in array order.  mouse_form_address is used
 * only for USER_DEF (255); built-in forms carry the null 0:0 slot.
 */
typedef struct gem_startup_effects {
	UBYTE fill_count;
	UBYTE mouse_action;
	WORD mouse_number;
	GEM_BINDINGS_POINTER_SLOT mouse_form_address;
	GEM_STARTUP_FILL fills[GEM_STARTUP_MAX_FILLS];
} GEM_STARTUP_EFFECTS;

/* Clear all fixed state, including physical-screen configuration. */
VOID gem_startup_resident_reset(VOID);

/* Install physical geometry after VDI open; invalid geometry is rejected. */
WORD gem_startup_resident_configure(const GEM_STARTUP_SCREEN *screen);

/*
 * Remove state only when both owner and generation match.  This releases an
 * abandoned startup WIND_UPDATE lock without letting a stale EXIT record tear
 * down a newly reused PD slot.
 */
VOID gem_startup_resident_detach(UWORD owner, UWORD generation_lo,
	UWORD generation_hi);

/*
 * Dispatch one bounded startup call.  A supported call sets *handled TRUE,
 * writes int_out[0], and returns that same original AES result.  An opcode or
 * subselector outside this bounded closure leaves the arrays and fixed state
 * untouched, sets *handled FALSE, and returns FALSE so a later full original
 * manager can receive it.  Malformed parameters to a recognized call are
 * handled as an AES failure and are never reported as success.
 */
WORD gem_startup_resident_dispatch(const GEM_STARTUP_CALL *call,
	GEM_STARTUP_EFFECTS *effects, WORD *handled);

#endif /* ELKS_GEM_STARTUP_RESIDENT_H */
