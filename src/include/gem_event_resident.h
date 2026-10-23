/*
 * gem_event_resident.h - GEM event/input manager for ELKS
 *
 * the old AES put a process to sleep to wait for input. ELKS owns scheduling,
 * so this keeps the per-PD event state but reports a deferred call, which parks
 * the request and finishes it later.
 *
 * timer word pairs are unsigned milliseconds, subtraction stops at zero.
 * mouse coords keep the original signed 16-bit wrap
 */

#ifndef ELKS_GEM_EVENT_RESIDENT_H
#define ELKS_GEM_EVENT_RESIDENT_H

#include "gem_bindings_elks.h"
#include "gem_channels.h"

#define GEM_EVENT_KEY_QUEUE_WORDS      8U

/* the GEM event selector values */
#define GEM_EVENT_EVNT_KEYBD           20U
#define GEM_EVENT_EVNT_BUTTON          21U
#define GEM_EVENT_EVNT_MOUSE           22U
#define GEM_EVENT_EVNT_MESAG           23U
#define GEM_EVENT_EVNT_TIMER           24U
#define GEM_EVENT_EVNT_MULTI           25U
#define GEM_EVENT_EVNT_DCLICK          26U

/* the GEM event-mask bits */
#define GEM_EVENT_MU_KEYBD             0x0001U
#define GEM_EVENT_MU_BUTTON            0x0002U
#define GEM_EVENT_MU_M1                0x0004U
#define GEM_EVENT_MU_M2                0x0008U
#define GEM_EVENT_MU_MESAG             0x0010U
#define GEM_EVENT_MU_TIMER             0x0020U
#define GEM_EVENT_MU_ALL               0x003fU

/* deferred socket reply */
#define GEM_EVENT_RESIDENT_DEFERRED    (-32768)

/* input sample with no attached client */
#define GEM_EVENT_OWNER_NONE           0xffffU

/* EVNT_MULTI has the biggest result array */
#define GEM_EVENT_OUTPUT_WORDS         7U

/* the original five-word mouse rectangle block, copied as-is */
typedef struct gem_event_rectangle {
	WORD outside;
	WORD x;
	WORD y;
	WORD width;
	WORD height;
} GEM_EVENT_RECTANGLE;

/* one nonblocking VDI input sample */
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

/* one copied client call */
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

/* one deferred eight-word message copy */
typedef struct gem_event_effects {
	GEM_BINDINGS_POINTER_SLOT message_address;
	UBYTE read_message;
} GEM_EVENT_EFFECTS;

/* one delayed event result */
typedef struct gem_event_completion {
	UWORD owner;
	UWORD generation_lo;
	UWORD generation_hi;
	UWORD output_count;
	UWORD int_out[GEM_EVENT_OUTPUT_WORDS];
	GEM_EVENT_EFFECTS effects;
} GEM_EVENT_COMPLETION;

VOID gem_event_resident_reset(VOID);

/* set the nonzero timer step in milliseconds */
WORD gem_event_resident_configure_tick(UWORD tick_milliseconds);

/* remove one exact PD generation, its key queue and pending wait */
VOID gem_event_resident_detach(UWORD owner, UWORD generation_lo,
	UWORD generation_hi);

/* feed one nonblocking mouse/keyboard sample from the physical VDI owner */
WORD gem_event_resident_input(const GEM_EVENT_INPUT *input);

/* advance armed timers, elapsed is milliseconds, never zero */
VOID gem_event_resident_tick(UWORD elapsed_milliseconds);

/* dispatch EVNT selectors 20 through 26 */
WORD gem_event_resident_dispatch(const GEM_EVENT_CALL *call,
	GEM_EVENT_EFFECTS *effects, WORD *handled);

/* recheck one parked event call */
WORD gem_event_resident_service(UWORD owner, UWORD generation_lo,
	UWORD generation_hi, UBYTE message_ready,
	GEM_EVENT_COMPLETION *completion);

#endif				/* ELKS_GEM_EVENT_RESIDENT_H */
