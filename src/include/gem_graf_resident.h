/*
 * gem_graf_resident.h - GEM graphics interaction manager for ELKS
 *
 * classic GRAF_RUBBOX, DRAGBOX, WATCHBOX and SLIDEBOX wait while a mouse button
 * is held. the resident AES cant busy-wait inside one client trap, so each PD
 * gets one parked graphics wait, the XOR tracker draws in the resident VDI, and
 * the parked request finishes when the button is released.
 *
 * coords and sizes are pixels. slide results keep GEM's 0..1000 scale and
 * truncate toward zero
 */

#ifndef ELKS_GEM_GRAF_RESIDENT_H
#define ELKS_GEM_GRAF_RESIDENT_H

#include "gem_object_resident.h"

/* GEM/XM has twelve logical process channels */
#define GEM_GRAF_PD_COUNT               6U

/* the GEM graphics selectors */
#define GEM_GRAF_RUBBOX                 70U
#define GEM_GRAF_DRAGBOX                71U
#define GEM_GRAF_MBOX                   72U
/* the two extended graphics entry points */
#define GEM_XGRF_STEPCALC               130U
#define GEM_XGRF_2BOX                   131U
#define GEM_GRAF_GROWBOX                73U
#define GEM_GRAF_SHRINKBOX              74U
#define GEM_GRAF_WATCHBOX               75U
#define GEM_GRAF_SLIDEBOX               76U
#define GEM_GRAF_MKSTATE                79U

/* same sleep result the event manager and AES broker use */
#define GEM_GRAF_RESIDENT_DEFERRED      (-32768)

#define GEM_GRAF_OWNER_NONE             0xffffU
#define GEM_GRAF_OUTPUT_WORDS           5U

/* one physical-input sample handed in by the resident VDI owner */
typedef struct gem_graf_input {
	UWORD owner;
	UWORD generation_lo;
	UWORD generation_hi;
	WORD mouse_x;
	WORD mouse_y;
	UWORD mouse_buttons;
	UWORD key_state;
} GEM_GRAF_INPUT;

/* one dispatch, after the client's AES arrays were copied. resource, client_segment and client_limit mark the same checked memory boundary gem_object_resident_dispatch() uses */
typedef struct gem_graf_call {
	UWORD owner;
	UWORD generation_lo;
	UWORD generation_hi;
	const GEM_RESOURCE_RESIDENT *resource;
	UWORD client_segment;
	UWORD client_limit;
	const UWORD *control;
	const UWORD *int_in;
	UWORD *int_out;
	const GEM_BINDINGS_POINTER_SLOT *addr_in;
} GEM_GRAF_CALL;

/* the wait takes the window update lock, the outer window owner applies these flags. a deferred path locks at dispatch and unlocks at completion or detach */
typedef struct gem_graf_effects {
	UBYTE begin_update;
	UBYTE end_update;
} GEM_GRAF_EFFECTS;

/* a finished parked request carries the original int_out words */
typedef struct gem_graf_completion {
	UWORD owner;
	UWORD generation_lo;
	UWORD generation_hi;
	UWORD output_count;
	UWORD int_out[GEM_GRAF_OUTPUT_WORDS];
	GEM_GRAF_EFFECTS effects;
} GEM_GRAF_COMPLETION;

/* clear all waits and the global physical-input state */
VOID gem_graf_resident_reset(VOID);

/* feed in the current physical sample, never blocks. movement redraws the XOR outline, a button release erases it and leaves one completion waiting for service() */
WORD gem_graf_resident_input(const GEM_GRAF_INPUT *input);

/* dispatch selectors 70..76 and 79. immediate calls write int_out and return the classic result, interactive calls return DEFERRED */
WORD gem_graf_resident_dispatch(const GEM_GRAF_CALL *call,
	GEM_GRAF_EFFECTS *effects, WORD *handled);

/* hand back and clear one finished parked interaction for an exact PD */
WORD gem_graf_resident_service(UWORD owner, UWORD generation_lo,
	UWORD generation_hi, GEM_GRAF_COMPLETION *completion);

/* cancel an exiting or reused PD generation, erasing any visible XOR outline. end_update drops a lock held by RUBBOX, DRAGBOX or SLIDEBOX */
VOID gem_graf_resident_detach(UWORD owner, UWORD generation_lo,
	UWORD generation_hi, GEM_GRAF_EFFECTS *effects);

/* TRUE only for this exact generation's parked wait */
WORD gem_graf_resident_waiting(UWORD owner, UWORD generation_lo,
	UWORD generation_hi);

#endif				/* ELKS_GEM_GRAF_RESIDENT_H */
