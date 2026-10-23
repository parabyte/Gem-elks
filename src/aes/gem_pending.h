/* park AES calls until their socket reply is ready */

#ifndef ELKS_GEM_PENDING_H
#define ELKS_GEM_PENDING_H

#include "gem_request.h"

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

/* the null link, channel zero is real so 'none' cant be zero */
#define GEM_PENDING_NONE              0xffU

/* one parked request */
typedef struct gem_pending {
	struct gem_request request;
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

/* one channel-indexed FIFO */
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

/* park one request */
GEM_PENDING *gem_pending_park(const struct gem_request *request,
	WORD channel, UBYTE operation, WORD target,
	UWORD buffer_offset, UWORD length, UWORD output_offset);

/* finish a parked request and put its slot on the ready list */
VOID gem_pending_complete(UBYTE channel, WORD result);

/* finish with a full int_out array */
VOID gem_pending_finish_words(UBYTE channel, const UWORD *words, UWORD count);

/* return a slot to FREE without replying (its request is dead) */
VOID gem_pending_clear(GEM_PENDING * pending);

/* cancel and clear one request */
WORD gem_pending_cancel(GEM_PENDING * pending);

/* take the oldest ready reply */
WORD gem_pending_take_ready(struct gem_request *request);

/* drop one channel from the ready list (its client died) */
VOID gem_pending_ready_remove(UBYTE channel);

/* the screen-lock contenders, oldest first */
extern GEM_PENDING_LIST gem_pending_update_waiters;

#endif				/* ELKS_GEM_PENDING_H */
