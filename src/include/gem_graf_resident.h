/*
 * gem_graf_resident.h - GEM graphics interaction manager for ELKS
 *
 * classic GRAF_RUBBOX, DRAGBOX, WATCHBOX and SLIDEBOX wait while a mouse button
 * is held. the resident AES cant busy-wait on one client, so each PD
 * gets one parked graphics wait, the XOR tracker draws in the resident VDI, and
 * the parked request finishes when the button is released.
 *
 * coords and sizes are pixels. slide results keep GEM's 0..1000 scale and
 * truncate toward zero
 */

#ifndef ELKS_GEM_GRAF_RESIDENT_H
#define ELKS_GEM_GRAF_RESIDENT_H

#include "gem_channels.h"
#include "gem_object_resident.h"

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

/* deferred socket reply */
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

/* one checked GRAF call */
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

/* deferred window-lock effects */
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

/* feed one nonblocking physical sample */
WORD gem_graf_resident_input(const GEM_GRAF_INPUT *input);

/* dispatch GRAF selectors 70 through 76 and 79 */
WORD gem_graf_resident_dispatch(const GEM_GRAF_CALL *call,
	GEM_GRAF_EFFECTS *effects, WORD *handled);

/* hand back and clear one finished parked interaction for an exact PD */
WORD gem_graf_resident_service(UWORD owner, UWORD generation_lo,
	UWORD generation_hi, GEM_GRAF_COMPLETION *completion);

/* cancel one client generation and its XOR outline */
VOID gem_graf_resident_detach(UWORD owner, UWORD generation_lo,
	UWORD generation_hi, GEM_GRAF_EFFECTS *effects);

/* TRUE only for this exact generation's parked wait */
WORD gem_graf_resident_waiting(UWORD owner, UWORD generation_lo,
	UWORD generation_hi);

#endif				/* ELKS_GEM_GRAF_RESIDENT_H */
