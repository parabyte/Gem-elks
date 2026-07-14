/*
 * gem_event_resident.h - bounded original GEM event/input manager for ELKS.
 *
 * The original AES blocked a GEM process by linking EVBs to its PD and then
 * switching UDA stacks.  ELKS already owns process state and scheduling, so
 * this interface keeps the original AES arrays and fixed twelve-PD event
 * state but reports a deferred call to the resident INT EF owner.  The owner
 * retains that delivered kernel request and later applies one completion.
 * No wrapper process, converted parameter block, or polling spin is needed.
 *
 * Timer word pairs are unsigned milliseconds with scale 1.  Tick subtraction
 * clamps at zero; it never wraps an expired timer.  Mouse coordinates and
 * rectangle edges retain original signed 16-bit modulo behavior.  No value in
 * this interface uses floating point or a scalar wider than one 8086 word.
 */

#ifndef ELKS_GEM_EVENT_RESIDENT_H
#define ELKS_GEM_EVENT_RESIDENT_H

#if defined(ELKS) && ELKS
#include "gem_bindings_elks.h"
#else
typedef signed short WORD;
typedef unsigned short UWORD;
typedef unsigned char UBYTE;
#define VOID void
#define TRUE 1
#define FALSE 0
typedef struct __attribute__((packed)) gem_event_pointer_slot {
	UWORD lo;
	UWORD hi;
} GEM_BINDINGS_POINTER_SLOT;
typedef char GEM_EVENT_HOST_WORD_MUST_BE_2_BYTES
	[(sizeof(UWORD) == 2) ? 1 : -1];
#endif

/* Original GEM/XM has twelve logical process channels. */
#define GEM_EVENT_PD_COUNT             12U
#define GEM_EVENT_KEY_QUEUE_WORDS      8U

/* Direct selector values from original CRYSBIND.H and PPDG000.C. */
#define GEM_EVENT_EVNT_KEYBD           20U
#define GEM_EVENT_EVNT_BUTTON          21U
#define GEM_EVENT_EVNT_MOUSE           22U
#define GEM_EVENT_EVNT_MESAG           23U
#define GEM_EVENT_EVNT_TIMER           24U
#define GEM_EVENT_EVNT_MULTI           25U
#define GEM_EVENT_EVNT_DCLICK          26U

/* Direct event-mask values from original GEMLIB.H. */
#define GEM_EVENT_MU_KEYBD             0x0001U
#define GEM_EVENT_MU_BUTTON            0x0002U
#define GEM_EVENT_MU_M1                0x0004U
#define GEM_EVENT_MU_M2                0x0008U
#define GEM_EVENT_MU_MESAG             0x0010U
#define GEM_EVENT_MU_TIMER             0x0020U
#define GEM_EVENT_MU_ALL               0x003fU

/* This value intentionally matches the resident AES broker's sleep result. */
#define GEM_EVENT_RESIDENT_DEFERRED    (-32768)

/* No attached process owns a physical input sample with this tag. */
#define GEM_EVENT_OWNER_NONE           0xffffU

/* EVNT_MULTI has the largest original result array. */
#define GEM_EVENT_OUTPUT_WORDS         7U

/* Original five-word mouse rectangle block, copied without conversion. */
typedef struct gem_event_rectangle {
	WORD outside;
	WORD x;
	WORD y;
	WORD width;
	WORD height;
} GEM_EVENT_RECTANGLE;

/*
 * One nonblocking VDI input sample.  owner and the two generation halves
 * identify the PD which currently owns mouse and keyboard input.  key_ready
 * is Boolean.  A ready key is copied into that PD's original eight-word
 * circular queue; a full queue drops it exactly like GEMINPUT.C nq().
 */
typedef struct gem_event_input {
	UWORD owner;
	UWORD generation_lo;
	UWORD generation_hi;
	WORD mouse_x;
	WORD mouse_y;
	UWORD mouse_buttons;
	UWORD key_state;
	UWORD key_code;
	UBYTE key_ready;
} GEM_EVENT_INPUT;

/*
 * One call after XIF has copied the original arrays from the pinned client.
 * message_ready describes that owner's existing GEM message queue; the event
 * manager never duplicates or converts the queue itself.
 */
typedef struct gem_event_call {
	UWORD owner;
	UWORD generation_lo;
	UWORD generation_hi;
	const UWORD *control;
	const UWORD *int_in;
	UWORD *int_out;
	const GEM_BINDINGS_POINTER_SLOT *addr_in;
	UBYTE message_ready;
} GEM_EVENT_CALL;

/*
 * The outer AES applies a message read before replying to the client.  Every
 * GEM message is exactly eight words (sixteen bytes); its destination remains
 * the client's original offset/segment pair.
 */
typedef struct gem_event_effects {
	GEM_BINDINGS_POINTER_SLOT message_address;
	UBYTE read_message;
} GEM_EVENT_EFFECTS;

/*
 * A delayed result contains the exact original int_out words and owner tag.
 * The outer resident core copies output_count words to its retained client's
 * int_out offset, applies effects, and then sends GEMCTL_REPLY.
 */
typedef struct gem_event_completion {
	UWORD owner;
	UWORD generation_lo;
	UWORD generation_hi;
	UWORD output_count;
	UWORD int_out[GEM_EVENT_OUTPUT_WORDS];
	GEM_EVENT_EFFECTS effects;
} GEM_EVENT_COMPLETION;

/* Reset all fixed PD, queue, mouse, click, and timer state. */
VOID gem_event_resident_reset(VOID);

/*
 * Set the physical timer quantum used only for GEM's three-tick click-window
 * extension.  The value is unscaled milliseconds and must be nonzero.
 */
WORD gem_event_resident_configure_tick(UWORD tick_milliseconds);

/* Remove one exact PD generation, including its key queue and pending wait. */
VOID gem_event_resident_detach(UWORD owner, UWORD generation_lo,
	UWORD generation_hi);

/* Feed one nonblocking mouse/keyboard sample from the physical VDI owner. */
WORD gem_event_resident_input(const GEM_EVENT_INPUT *input);

/*
 * Advance every armed timer once from ordinary process context.  elapsed is
 * scale-1 milliseconds from a low-rate ELKS timer signal and is never zero.
 */
VOID gem_event_resident_tick(UWORD elapsed_milliseconds);

/*
 * Dispatch selectors 20 through 26.  Immediate calls write the caller's
 * int_out and return their classic result.  A real wait returns
 * GEM_EVENT_RESIDENT_DEFERRED with *handled TRUE.  Unknown selectors leave
 * every array untouched and set *handled FALSE.
 */
WORD gem_event_resident_dispatch(const GEM_EVENT_CALL *call,
	GEM_EVENT_EFFECTS *effects, WORD *handled);

/*
 * Recheck one retained owner after input, timer, or message-queue progress.
 * TRUE produces one completion and clears that PD's wait.  message_ready is
 * supplied by the existing original APPL queue, so no queue is duplicated.
 */
WORD gem_event_resident_service(UWORD owner, UWORD generation_lo,
	UWORD generation_hi, UBYTE message_ready,
	GEM_EVENT_COMPLETION *completion);

/* Integration aid: TRUE only for this exact generation's retained wait. */
WORD gem_event_resident_waiting(UWORD owner, UWORD generation_lo,
	UWORD generation_hi);

#endif /* ELKS_GEM_EVENT_RESIDENT_H */
