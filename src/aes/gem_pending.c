/*
 * gem_pending.c - parked AES requests
 *
 * the bookkeeping for parked requests, park a request that cant finish,
 * keep it on the right FIFO, hand the finished ones back to the broker,
 * see gem_pending.h for the state machine
 *
 * the message queues live here too, a parked APPL_READ finishes when a
 * write fills the queue, a parked APPL_WRITE when a read drains it
 */

#include "gem_aes_internal.h"

#include <errno.h>

/* one slot per channel, the array index is the channel number */
static GEM_PENDING gem_pending_slots[GEM_PROC_CHANNELS];

/* finished requests waiting for the broker to reply, oldest first */
static GEM_PENDING_LIST gem_pending_ready;

/* WIND_UPDATE lock contenders, oldest first */
GEM_PENDING_LIST gem_pending_update_waiters;

GEM_PENDING *
gem_pending_at(WORD channel)
{
	GEM_PENDING *pending;
	UWORD count;

	if (channel < 0 || channel >= GEM_PROC_CHANNELS)
		return NULL;
	pending = gem_pending_slots;
	count = (UWORD) channel;
	while (count--)
		pending++;
	return pending;
}

VOID
gem_pending_clear(GEM_PENDING *pending)
{
	pending->buffer_offset = 0;
	pending->output_offset = 0;
	pending->length = 0;
	pending->result = 0;
	pending->target = 0;
	pending->next = GEM_PENDING_NONE;
	pending->state = GEM_PENDING_FREE;
	pending->operation = 0;
}

VOID
gem_pending_reset(VOID)
{
	GEM_PENDING *pending;
	UWORD count;

	pending = gem_pending_slots;
	count = GEM_PROC_CHANNELS;
	while (count--) {
		gem_pending_clear(pending);
		pending++;
	}
	gem_pending_list_init(&gem_pending_ready);
	gem_pending_list_init(&gem_pending_update_waiters);
}

/*
 * the one FIFO discipline, a list holds channel numbers, the links are
 * each slots 'next' byte, C startup zeroes statics but channel zero is
 * real so lists must be set to NONE by hand
 */

VOID
gem_pending_list_init(GEM_PENDING_LIST *list)
{
	list->head = GEM_PENDING_NONE;
	list->tail = GEM_PENDING_NONE;
}

VOID
gem_pending_list_append(GEM_PENDING_LIST *list, UBYTE channel)
{
	GEM_PENDING *pending;
	GEM_PENDING *previous;

	pending = gem_pending_at((WORD) channel);
	pending->next = GEM_PENDING_NONE;
	if (list->tail != GEM_PENDING_NONE) {
		previous = gem_pending_at((WORD) list->tail);
		previous->next = channel;
	} else {
		list->head = channel;
	}
	list->tail = channel;
}

UBYTE
gem_pending_list_pop(GEM_PENDING_LIST *list)
{
	GEM_PENDING *pending;
	UBYTE channel;

	channel = list->head;
	if (channel == GEM_PENDING_NONE)
		return channel;
	pending = gem_pending_at((WORD) channel);
	list->head = pending->next;
	if (list->head == GEM_PENDING_NONE)
		list->tail = GEM_PENDING_NONE;
	pending->next = GEM_PENDING_NONE;
	return channel;
}

VOID
gem_pending_list_remove(GEM_PENDING_LIST *list, UBYTE channel)
{
	GEM_PENDING *pending;
	GEM_PENDING *previous;
	UBYTE current;
	UBYTE previous_channel;

	previous_channel = GEM_PENDING_NONE;
	current = list->head;
	while (current != GEM_PENDING_NONE) {
		pending = gem_pending_at((WORD) current);
		if (current == channel) {
			if (previous_channel == GEM_PENDING_NONE)
				list->head = pending->next;
			else {
				previous = gem_pending_at(
					(WORD) previous_channel);
				previous->next = pending->next;
			}
			if (list->tail == channel)
				list->tail = previous_channel;
			pending->next = GEM_PENDING_NONE;
			return;
		}
		previous_channel = current;
		current = pending->next;
	}
}

/*
 * parking and finishing
 */

GEM_PENDING *
gem_pending_park(const struct gemtrap_request *request, WORD channel,
	UBYTE operation, WORD target, UWORD buffer_offset, UWORD length,
	UWORD output_offset)
{
	GEM_PENDING *pending;

	pending = gem_pending_at(channel);
	if (!pending || pending->state != GEM_PENDING_FREE)
		return NULL;
	pending->request = *request;
	pending->buffer_offset = buffer_offset;
	pending->output_offset = output_offset;
	pending->length = length;
	pending->result = 0;
	pending->target = (UBYTE) target;
	pending->next = GEM_PENDING_NONE;
	pending->operation = operation;
	pending->state = GEM_PENDING_WAITING;
	return pending;
}

/*
 * the binding reads int_out[0] not the AX the trap returned, so the
 * result must land in the clients pinned array before the reply
 */
VOID
gem_pending_complete(UBYTE channel, WORD result)
{
	GEM_PENDING *pending;

	pending = gem_pending_at((WORD) channel);
	pending->result = result;
	gem_resident_memory_to(&pending->result, pending->request.ds,
		pending->output_offset, 2U);
	pending->state = GEM_PENDING_READY;
	gem_pending_list_append(&gem_pending_ready, channel);
}

VOID
gem_pending_finish_words(UBYTE channel, const UWORD *words, UWORD count)
{
	GEM_PENDING *pending;
	UWORD bytes;

	pending = gem_pending_at((WORD) channel);
	bytes = (UWORD) (count + count);
	if (bytes)
		gem_resident_memory_to(words, pending->request.ds,
			pending->output_offset, bytes);
	pending->result = count ? (WORD) words[0] : FALSE;
	pending->state = GEM_PENDING_READY;
	gem_pending_list_append(&gem_pending_ready, channel);
}

WORD
gem_pending_cancel(GEM_PENDING *pending)
{
	WORD result;

	result = gemctl(GEMCTL_CANCEL, &pending->request);
	if (result != 0 && errno != ESRCH)
		return FALSE;
	gem_pending_clear(pending);
	return TRUE;
}

WORD
gem_pending_take_ready(struct gemtrap_request *request)
{
	GEM_PENDING *pending;
	UBYTE channel;

	channel = gem_pending_list_pop(&gem_pending_ready);
	if (channel == GEM_PENDING_NONE)
		return FALSE;
	pending = gem_pending_at((WORD) channel);
	*request = pending->request;
	request->ax = (UWORD) pending->result;
	gem_pending_clear(pending);
	return TRUE;
}

VOID
gem_pending_ready_remove(UBYTE channel)
{
	gem_pending_list_remove(&gem_pending_ready, channel);
}

/*
 * the message queues, each PD owns one 128 byte queue, APPL_WRITE
 * appends to the targets queue and APPL_READ drains it, a call that dont
 * fit is parked on the target PDs reader or writer FIFO and the pump
 * below finishes it once the queue has drained or filled enough
 */

/* the FIFO a parked READ or WRITE waits on */
GEM_PENDING_LIST *
gem_pending_waiters(GEM_RESIDENT_PD *pd, UBYTE operation)
{
	if (operation == GEM_PENDING_WRITE)
		return &pd->write_waiters;
	return &pd->read_waiters;
}

VOID
gem_resident_enqueue(GEM_RESIDENT_PD *pd, UWORD segment, UWORD offset,
	UWORD length)
{
	gem_resident_memory_from(segment, offset, &pd->queue[pd->queue_index],
		length);
	pd->queue_index = (UWORD) (pd->queue_index + length);
}

VOID
gem_resident_dequeue(GEM_RESIDENT_PD *pd, UWORD segment, UWORD offset,
	UWORD length)
{
	BYTE *destination;
	BYTE *source;
	UWORD remaining;

	gem_resident_memory_to(pd->queue, segment, offset, length);
	remaining = (UWORD) (pd->queue_index - length);
	destination = pd->queue;
	source = &pd->queue[length];
	while (remaining--)
		*destination++ = *source++;
	pd->queue_index = (UWORD) (pd->queue_index - length);
}

/* copy one 8 word AES message into the PD queue */
VOID
gem_resident_enqueue_window_message(GEM_RESIDENT_PD *pd, const UWORD *words)
{
	BYTE *destination;
	const BYTE *source;
	UWORD count;

	destination = &pd->queue[pd->queue_index];
	source = (const BYTE *) words;
	count = 16U;
	while (count--)
		*destination++ = *source++;
	pd->queue_index = (UWORD) (pd->queue_index + 16U);
}

/*
 * the rendezvous pump, a finished write may have given the oldest
 * parked reader enough bytes, a finished read may have made room for
 * the oldest parked writer, keep alternating until neither can move,
 * pass cap so a corrupt list cant loop forever
 */
VOID
gem_resident_queue_progress(GEM_RESIDENT_PD *pd)
{
	GEM_PENDING *pending;
	UBYTE channel;
	UWORD passes;

	passes = GEM_PROC_CHANNELS;
	while (passes--) {
		channel = pd->read_waiters.head;
		if (channel != GEM_PENDING_NONE) {
			pending = gem_pending_at((WORD) channel);
			if (pending->length <= pd->queue_index) {
				(void) gem_pending_list_pop(&pd->read_waiters);
				gem_resident_dequeue(pd, pending->request.ds,
					pending->buffer_offset,
					pending->length);
				gem_pending_complete(channel, TRUE);
				continue;
			}
		}

		channel = pd->write_waiters.head;
		if (channel != GEM_PENDING_NONE) {
			pending = gem_pending_at((WORD) channel);
			if (pending->length <= (UWORD)
				(GEM_RESIDENT_QUEUE_BYTES - pd->queue_index)) {
				(void) gem_pending_list_pop(&pd->write_waiters);
				gem_resident_enqueue(pd, pending->request.ds,
					pending->buffer_offset,
					pending->length);
				gem_pending_complete(channel, TRUE);
				continue;
			}
		}
		break;
	}
}
