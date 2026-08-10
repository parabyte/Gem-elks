/*
 * gem_pending.h - parked AES requests
 *
 * the one idea that makes a resident AES work on ELKS: a classic GEM call that
 * would have blocked - an event wait, a modal form, a message read with nothing
 * queued - cant block here since one resident serves every client. instead the
 * kernel request is PARKED: its 22-byte record is saved (which keeps the
 * client's data segment pinned), the dispatcher returns without replying, and
 * the reply happens on a later tick when the wait is over.
 *
 * each client channel has exactly one slot, since a client is inside at most one
 * AES call at a time. a slot walks one path:
 *
 *   FREE ──park──> WAITING ──complete──> READY ──take_ready──> FREE
 *
 * park       saves the request and marks what its waiting for;
 * complete   writes the result into the client's pinned int_out and
 *            moves the slot onto the ready list;
 * take_ready hands one finished request back to the broker loop,
 *            which sends the actual reply.
 *
 * while WAITING, a slot may sit on one list, always FIFO, always threaded
 * through the same one-byte 'next' index:
 *
 *   READ/WRITE  on its target PD's reader/writer queue;
 *   UPDATE      on the screen-lock contenders' queue;
 *   EVENT/GRAF/FORM/TAPE on no list - the tick polls their manager.
 *
 * pure bookkeeping. it copies memory and cancels kernel requests, it never calls
 * a manager and never draws
 */

#ifndef ELKS_GEM_PENDING_H
#define ELKS_GEM_PENDING_H

#include "gemtrap.h"

#include "aes.h"

/* slot states */
#define GEM_PENDING_FREE              0
#define GEM_PENDING_WAITING           1
#define GEM_PENDING_READY             2

/* what a WAITING slot is waiting for */
#define GEM_PENDING_READ              1	/* APPL_READ, queue empty */
#define GEM_PENDING_WRITE             2	/* APPL_WRITE, queue full */
#define GEM_PENDING_EVENT             3	/* EVNT_*, condition not met */
#define GEM_PENDING_GRAF              4	/* a held GRAF_* interaction */
#define GEM_PENDING_FORM              5	/* a modal form or alert */
#define GEM_PENDING_UPDATE            6	/* WIND_UPDATE lock taken */
#define GEM_PENDING_TAPE              7	/* APPL_TRECORD/TPLAY running */

/* the null link, channel zero is real so 'none' cant be zero */
#define GEM_PENDING_NONE              0xffU

/* one parked request. 'target' is the channel whose PD queue a READ/WRITE waits on, every other op targets its own channel. UPDATE reuses buffer_offset/length for the parked generation, so a reused channel cant inherit a stale lock claim */
typedef struct gem_pending {
	struct gemtrap_request request;
	UWORD buffer_offset;
	UWORD output_offset;
	UWORD length;
	WORD result;
	UBYTE target;
	UBYTE next;
	UBYTE state;
	UBYTE operation;
} GEM_PENDING;

typedef BYTE GEM_PENDING_MUST_BE_34_BYTES[(sizeof(GEM_PENDING) == 34) ? 1 : -1];

/* a FIFO of parked requests. every list in the AES - the two waiter queues on each PD, the ready list, the screen-lock contenders - is one of these, sharing the one 'next' field a slot has, safe since a slot is never on two lists at once */
typedef struct gem_pending_list {
	UBYTE head;
	UBYTE tail;
} GEM_PENDING_LIST;

/* the slot for one channel, or null for a channel out of range */
GEM_PENDING *gem_pending_at(WORD channel);

/* all slots to FREE and every list empty, once at startup */
VOID gem_pending_reset(VOID);

/* the three list ops. append and remove keep arrival order */
VOID gem_pending_list_init(GEM_PENDING_LIST * list);
VOID gem_pending_list_append(GEM_PENDING_LIST * list, UBYTE channel);
UBYTE gem_pending_list_pop(GEM_PENDING_LIST * list);
VOID gem_pending_list_remove(GEM_PENDING_LIST * list, UBYTE channel);

/* park the request in its channel's slot. fails (returns null) when the slot isnt FREE - a client cant wait twice. the caller then returns GEM_AES_RESIDENT_DEFERRED to the broker */
GEM_PENDING *gem_pending_park(const struct gemtrap_request *request,
	WORD channel, UBYTE operation, WORD target,
	UWORD buffer_offset, UWORD length, UWORD output_offset);

/* finish a parked request with a one-word result: the word goes into the client's pinned int_out[0] and the slot joins the ready list */
VOID gem_pending_complete(UBYTE channel, WORD result);

/* finish with a full int_out array instead: COUNT words are copied to the client, int_out[0] (or FALSE when COUNT is zero) becomes the call's return value, and the slot joins the ready list */
VOID gem_pending_finish_words(UBYTE channel, const UWORD *words, UWORD count);

/* return a slot to FREE without replying (its request is dead) */
VOID gem_pending_clear(GEM_PENDING * pending);

/* cancel the parked kernel request, then clear the slot */
WORD gem_pending_cancel(GEM_PENDING * pending);

/* pop the oldest READY slot: the saved request plus its result, ready for the broker to reply with. FALSE when nothing is ready */
WORD gem_pending_take_ready(struct gemtrap_request *request);

/* drop one channel from the ready list (its client died) */
VOID gem_pending_ready_remove(UBYTE channel);

/* the screen-lock contenders, oldest first */
extern GEM_PENDING_LIST gem_pending_update_waiters;

#endif				/* ELKS_GEM_PENDING_H */
