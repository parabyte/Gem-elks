/*
 * gem_graf_resident.h - original GEM graphics interaction manager for ELKS.
 *
 * The classic GRAF_RUBBOX, DRAGBOX, WATCHBOX, and SLIDEBOX calls wait while
 * a mouse button is held.  The resident AES must not busy-wait inside one
 * client trap: ELKS owns scheduling and delivers physical input to gemaes.
 * This interface therefore retains one original graphics wait per GEM PD,
 * draws the same XOR tracker in the resident VDI, and completes the retained
 * kernel request after button release.  No helper task, converted object
 * tree, heap allocation, or application-side polling loop is introduced.
 *
 * Every coordinate and size has scale one pixel.  Slide results keep GEM's
 * original scale of 1000, truncate toward zero, and saturate only if damaged
 * geometry escapes the validated 0..1000 range.  Animation division is done
 * with bounded word shifts/subtractions; no floating point or compiler
 * multiply/divide helper is required on an 8088/8086.
 */

#ifndef ELKS_GEM_GRAF_RESIDENT_H
#define ELKS_GEM_GRAF_RESIDENT_H

#include "gem_object_resident.h"

/* Original GEM/XM has twelve logical process channels. */
#define GEM_GRAF_PD_COUNT               12U

/* Direct selector values from original CRYSBIND.H and PPDG000.C. */
#define GEM_GRAF_RUBBOX                 70U
#define GEM_GRAF_DRAGBOX                71U
#define GEM_GRAF_MBOX                   72U
#define GEM_GRAF_GROWBOX                73U
#define GEM_GRAF_SHRINKBOX              74U
#define GEM_GRAF_WATCHBOX               75U
#define GEM_GRAF_SLIDEBOX               76U
#define GEM_GRAF_MKSTATE                79U

/* Matches the resident event manager and AES broker sleep result. */
#define GEM_GRAF_RESIDENT_DEFERRED      (-32768)

#define GEM_GRAF_OWNER_NONE             0xffffU
#define GEM_GRAF_OUTPUT_WORDS           5U

/* One unscaled physical-input sample supplied by the resident VDI owner. */
typedef struct gem_graf_input {
	UWORD owner;
	UWORD generation_lo;
	UWORD generation_hi;
	WORD mouse_x;
	WORD mouse_y;
	UWORD mouse_buttons;
	UWORD key_state;
} GEM_GRAF_INPUT;

/*
 * One dispatch after XIF has copied the client's original AES arrays.
 * resource, client_segment, and client_limit are the same validated memory
 * ownership boundary used by gem_object_resident_dispatch().
 */
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

/*
 * Original gr_rubwind()/gr_dragbox() bracket their wait with wm_update().
 * The outer resident window owner applies these two Boolean effects to its
 * existing update lock.  Both may be true only for an immediate, balanced
 * path; a deferred path begins at dispatch and ends at completion/detach.
 */
typedef struct gem_graf_effects {
	UBYTE begin_update;
	UBYTE end_update;
} GEM_GRAF_EFFECTS;

/* A completed retained request contains the exact original int_out words. */
typedef struct gem_graf_completion {
	UWORD owner;
	UWORD generation_lo;
	UWORD generation_hi;
	UWORD output_count;
	UWORD int_out[GEM_GRAF_OUTPUT_WORDS];
	GEM_GRAF_EFFECTS effects;
} GEM_GRAF_COMPLETION;

/* Clear all twelve waits and the global physical input state. */
VOID gem_graf_resident_reset(VOID);

/*
 * Feed a current physical sample.  This never blocks.  Changed coordinates
 * erase and redraw one XOR outline immediately; release erases the final
 * outline and makes one retained completion ready for service().
 */
WORD gem_graf_resident_input(const GEM_GRAF_INPUT *input);

/*
 * Dispatch selectors 70..76 and 79.  Immediate calls write int_out and
 * return their classic result.  Interactive calls return DEFERRED while the
 * original client request remains pinned in the outer AES broker.
 */
WORD gem_graf_resident_dispatch(const GEM_GRAF_CALL *call,
	GEM_GRAF_EFFECTS *effects, WORD *handled);

/* Return and clear one completed retained interaction for an exact PD. */
WORD gem_graf_resident_service(UWORD owner, UWORD generation_lo,
	UWORD generation_hi, GEM_GRAF_COMPLETION *completion);

/*
 * Cancel an exiting/reused PD generation.  Any visible XOR outline is erased
 * before returning.  end_update tells the outer window manager to release a
 * lock held by RUBBOX, DRAGBOX, or SLIDEBOX.
 */
VOID gem_graf_resident_detach(UWORD owner, UWORD generation_lo,
	UWORD generation_hi, GEM_GRAF_EFFECTS *effects);

/* Integration aid: TRUE only for this exact generation's retained wait. */
WORD gem_graf_resident_waiting(UWORD owner, UWORD generation_lo,
	UWORD generation_hi);

#endif /* ELKS_GEM_GRAF_RESIDENT_H */
