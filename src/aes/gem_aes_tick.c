/* timers, input ownership and parked AES calls */

#include "gem_aes_internal.h"

#include "gem_event_resident.h"
#include "gem_form_resident.h"
#include "gem_graf_resident.h"
#include "gem_menu_pull_resident.h"
#include "gem_proc.h"
#include "gem_resident_memory.h"
#include "gem_vdi_resident.h"
#include "gem_window_resident.h"

/* copy one checked 16 byte message */
WORD
gem_resident_event_message(const struct gemtrap_request *request,
	GEM_RESIDENT_PD *pd, const GEM_EVENT_EFFECTS *effects)
{
	if (!effects->read_message)
		return TRUE;
	if (effects->message_address.hi != request->ds
		|| !gem_resident_memory_pointer(request,
			effects->message_address, 16U)
		|| pd->queue_index < 16U)
		return FALSE;
	gem_resident_dequeue(pd, request->ds, effects->message_address.lo, 16U);
	gem_resident_queue_progress(pd);
	return TRUE;
}

/* give WIND_UPDATE to the oldest live waiter */
VOID GEM_RESIDENT_COLD
gem_resident_update_progress(VOID)
{
	GEM_PENDING *pending;
	GEM_RESIDENT_PD *pd;
	UBYTE channel;

	for (;;) {
		channel = gem_pending_update_waiters.head;
		if (channel == GEM_PENDING_NONE)
			return;
		pending = gem_pending_at((WORD) channel);
		if (!pending || pending->state != GEM_PENDING_WAITING
			|| pending->operation != GEM_PENDING_UPDATE) {
			/* drop a stale link */
			(void) gem_pending_list_pop
				(&gem_pending_update_waiters);
			continue;
		}
		pd = gem_resident_pd_for_channel((WORD) channel);
		if (!pd || pd->state != GEM_PD_ATTACHED
			|| pd->pid != pending->request.pid
			|| pd->segment != pending->request.ds
			|| pd->task_slot != (UBYTE) pending->request.slot
			|| pd->generation_lo != pending->buffer_offset
			|| pd->generation_hi != pending->length) {
			/* fail a reused channel */
			(void) gem_pending_list_pop
				(&gem_pending_update_waiters);
			gem_pending_complete(channel, -1);
			continue;
		}
		if (!gem_resident_graf_update_owner(pd, (WORD) channel,
				GEM_RESIDENT_BEG_UPDATE))
			return;	/* still locked */
		(void) gem_pending_list_pop(&gem_pending_update_waiters);
		gem_pending_complete(channel, TRUE);
		return;
	}
}

/* finish one GRAF request */
static VOID
gem_resident_graf_complete(UBYTE channel, const GEM_GRAF_COMPLETION *completion)
{
	GEM_PENDING *pending;
	GEM_RESIDENT_PD *pd;

	pending = gem_pending_at((WORD) channel);
	pd = gem_resident_pd_for_channel((WORD) channel);
	if (!pending || pending->operation != GEM_PENDING_GRAF || !pd
		|| completion->output_count > GEM_GRAF_OUTPUT_WORDS
		|| !gem_resident_graf_apply_effects(pd, (WORD) channel,
			&completion->effects)) {
		if (pending)
			gem_pending_complete(channel, -1);
		return;
	}
	gem_pending_finish_words(channel, completion->int_out,
		completion->output_count);
}

/* give each held GRAF channel one look, nothing ever polls */
static VOID
gem_resident_graf_progress(VOID)
{
	GEM_GRAF_COMPLETION completion;
	GEM_PENDING *pending;
	GEM_RESIDENT_PD *pd;
	UWORD owner_segment;
	UBYTE channel;

	owner_segment = gem_resident_data_segment();
	channel = 0;
	while (channel < GEM_PROC_CHANNELS) {
		pending = gem_pending_at((WORD) channel);
		if (pending->state == GEM_PENDING_WAITING
			&& pending->operation == GEM_PENDING_GRAF) {
			pd = gem_resident_pd_for_channel((WORD) channel);
			if (pd && pd->state == GEM_PD_ATTACHED
				&& gem_graf_resident_service((UWORD) channel,
					pd->generation_lo, pd->generation_hi,
					&completion)) {
				__asm__ volatile ("movw %0,%%ds"::"r"
					(owner_segment):"memory");
				gem_resident_graf_complete(channel,
					&completion);
			}
		}
		channel++;
	}
}

/*
 * one finished modal form, do its redraw effects, hand back the file
 * selectors strings if it was one, copy the outputs, ready
 */
static VOID
gem_resident_form_complete(UBYTE channel, const GEM_FORM_COMPLETION *completion)
{
	GEM_PENDING *pending;
	GEM_RESIDENT_PD *pd;

	pending = gem_pending_at((WORD) channel);
	pd = gem_resident_pd_for_channel((WORD) channel);
	if (!pending || pending->operation != GEM_PENDING_FORM || !pd
		|| completion->output_count > GEM_FORM_OUTPUT_WORDS
		|| !gem_resident_form_apply_effects(pd, (WORD) channel,
			&completion->effects)) {
		if (pending)
			gem_pending_complete(channel, -1);
		return;
	}
	if (completion->fsel) {
		BYTE path[GEM_FORM_FSEL_PATH_BYTES];
		BYTE name[GEM_FORM_FSEL_NAME_BYTES_OUT];

		gem_form_resident_fsel_result(path, (UWORD) sizeof(path),
			name, (UWORD) sizeof(name));
		if (completion->fsel_path.hi == pending->request.ds)
			gem_resident_memory_to(path, pending->request.ds,
				completion->fsel_path.lo, (UWORD) sizeof(path));
		if (completion->fsel_name.hi == pending->request.ds)
			gem_resident_memory_to(name, pending->request.ds,
				completion->fsel_name.lo, (UWORD) sizeof(name));
	}
	gem_pending_finish_words(channel, completion->int_out,
		completion->output_count);
}

/* give each held FORM channel one look, no client-side polling */
static VOID
gem_resident_form_progress(VOID)
{
	GEM_FORM_COMPLETION completion;
	GEM_PENDING *pending;
	GEM_RESIDENT_PD *pd;
	UWORD owner_segment;
	UBYTE channel;

	owner_segment = gem_resident_data_segment();
	channel = 0;
	while (channel < GEM_PROC_CHANNELS) {
		pending = gem_pending_at((WORD) channel);
		if (pending->state == GEM_PENDING_WAITING
			&& pending->operation == GEM_PENDING_FORM) {
			pd = gem_resident_pd_for_channel((WORD) channel);
			if (pd && pd->state == GEM_PD_ATTACHED
				&& gem_form_resident_service((UWORD) channel,
					pd->generation_lo, pd->generation_hi,
					&completion)) {
				__asm__ volatile ("movw %0,%%ds"::"r"
					(owner_segment):"memory");
				gem_resident_form_complete(channel,
					&completion);
			}
		}
		channel++;
	}
}

/* one finished EVNT_* wait, copy the event words to the client */
static VOID
gem_resident_event_complete(UBYTE channel,
	const GEM_EVENT_COMPLETION *completion)
{
	GEM_PENDING *pending;

	pending = gem_pending_at((WORD) channel);
	if (!pending || completion->output_count > GEM_EVENT_OUTPUT_WORDS) {
		if (pending)
			gem_pending_complete(channel, -1);
		return;
	}
	gem_pending_finish_words(channel, completion->int_out,
		completion->output_count);
}

/* recheck each parked EVNT_* wait once */
static VOID
gem_resident_event_progress(VOID)
{
	GEM_EVENT_COMPLETION completion;
	GEM_PENDING *pending;
	GEM_RESIDENT_PD *pd;
	UBYTE channel;

	channel = 0;
	while (channel < GEM_PROC_CHANNELS) {
		pending = gem_pending_at((WORD) channel);
		if (pending->state == GEM_PENDING_WAITING
			&& pending->operation == GEM_PENDING_EVENT) {
			pd = gem_resident_pd_for_channel((WORD) channel);
			if (pd && pd->state == GEM_PD_ATTACHED
				&& gem_event_resident_service((UWORD) channel,
					pd->generation_lo, pd->generation_hi,
					pd->queue_index >= 16U, &completion)) {
				if (!gem_resident_event_message
					(&pending->request, pd,
						&completion.effects))
					gem_pending_complete(channel, -1);
				else
					gem_resident_event_complete(channel,
						&completion);
			}
		}
		channel++;
	}
}

/*
 * step 3a of the tick, a held modal form owns every sample outright,
 * its button or key mustnt also reach the menu or EVNT_* state so TRUE
 * here means the sample got used up
 */
static WORD
gem_resident_form_input_sample(GEM_RESIDENT_PD *pd, WORD channel,
	const GEM_VDI_RESIDENT_INPUT *sample)
{
	GEM_FORM_INPUT input;
	GEM_PENDING *pending;
	UWORD owner_segment;
	WORD consumed;

	if (!pd || !sample
		|| !gem_form_resident_waiting((UWORD) channel,
			pd->generation_lo, pd->generation_hi))
		return FALSE;
	input.owner = (UWORD) channel;
	input.generation_lo = pd->generation_lo;
	input.generation_hi = pd->generation_hi;
	input.mouse_x = sample->mouse_x;
	input.mouse_y = sample->mouse_y;
	input.mouse_buttons = sample->mouse_buttons;
	input.key_code = sample->key_code;
	input.key_state = sample->key_state;
	input.clicks = 1U;
	input.key_ready = sample->key_ready;
	owner_segment = gem_resident_data_segment();
	consumed = gem_form_resident_input(&input, &gem_resident_form_effects);
	__asm__ volatile ("movw %0,%%ds"::"r" (owner_segment):"memory");
	if (consumed && gem_resident_form_apply_effects(pd, channel,
			&gem_resident_form_effects)) {
		gem_resident_form_progress();
		return TRUE;
	}

	/* a broken held state must drop its update lock and client pin */
	(void) gem_resident_form_detach_owner(pd, channel);
	pending = gem_pending_at(channel);
	if (pending && pending->state == GEM_PENDING_WAITING
		&& pending->operation == GEM_PENDING_FORM)
		gem_pending_complete((UBYTE) channel, -1);
	return TRUE;
}

/*
 * the tick itself, ELAPSED is milliseconds since the last call, zero
 * means an AES request just finished so give parked calls one extra
 * look, a freshly armed wait must see the current mouse state once so
 * zero never skips the sample
 */
VOID
gem_aes_resident_poll(UWORD elapsed_milliseconds)
{
	GEM_EVENT_INPUT event_input;
	GEM_GRAF_INPUT graf_input;
	GEM_MENU_PULL_INPUT menu_input;
	GEM_RESIDENT_PD *pd;
	GEM_VDI_RESIDENT_INPUT vdi_input;
	GEM_WINDOW_INPUT window_input;
	UWORD owner_segment;
	WORD channel;
	WORD consumed;
	WORD graf_waiting;

	gem_resident_initialize();

	/* 1. timers */
	if (elapsed_milliseconds)
		gem_event_resident_tick(elapsed_milliseconds);

	/* send shared input to the foreground PD */
	channel = gem_proc_foreground_channel();
	pd = gem_resident_pd_for_channel(channel);
	if (!pd || pd->state != GEM_PD_ATTACHED) {
		channel = GEM_PROC_DESKTOP;
		pd = gem_resident_pd_for_channel(channel);
	}
	if (pd && pd->state == GEM_PD_ATTACHED
		&& gem_vdi_resident_poll_input(&vdi_input)) {
		/* idle tick with no input change, timers only */
		if (elapsed_milliseconds && !vdi_input.changed) {
			gem_resident_event_progress();
			return;
		}

		/* one owner gets the sample */

		/* forms and alerts go first */
		if (gem_resident_form_input_sample(pd, channel, &vdi_input)) {
			gem_resident_graf_progress();
			gem_resident_event_progress();
			return;
		}

		/* then the menu */
		menu_input.mouse_x = vdi_input.mouse_x;
		menu_input.mouse_y = vdi_input.mouse_y;
		menu_input.mouse_buttons = vdi_input.mouse_buttons;
		menu_input.key_code = vdi_input.key_code;
		menu_input.key_ready = vdi_input.key_ready;
		consumed = gem_menu_pull_resident_input(&menu_input,
			&gem_resident_menu_effects);
		if (consumed) {
			/*
			 * the menu owns this sample until tracking ends,
			 * apply effects first so a duplicate button or key
			 * event dont reach the Desktop
			 */
			(void) gem_resident_menu_apply_effects
				(&gem_resident_menu_effects);
			gem_resident_graf_progress();
			gem_resident_event_progress();
			return;
		}

		/* then a held GRAF tracker */
		graf_waiting = gem_graf_resident_waiting((UWORD) channel,
			pd->generation_lo, pd->generation_hi);
		graf_input.owner = (UWORD) channel;
		graf_input.generation_lo = pd->generation_lo;
		graf_input.generation_hi = pd->generation_hi;
		graf_input.mouse_x = vdi_input.mouse_x;
		graf_input.mouse_y = vdi_input.mouse_y;
		graf_input.mouse_buttons = vdi_input.mouse_buttons;
		graf_input.key_state = vdi_input.key_state;
		owner_segment = gem_resident_data_segment();
		(void) gem_graf_resident_input(&graf_input);
		__asm__ volatile ("movw %0,%%ds"::"r" (owner_segment)
			:"memory");
		gem_resident_graf_progress();
		if (!graf_waiting) {
			/* 3d. window gadgets, title drags, closers, sliders */
			window_input.mouse_x = vdi_input.mouse_x;
			window_input.mouse_y = vdi_input.mouse_y;
			window_input.mouse_buttons = vdi_input.mouse_buttons;
			consumed =
				gem_window_resident_input(&gem_resident_windows,
				&window_input, &gem_resident_window_effects);
			if (consumed) {
				(void) gem_resident_window_apply_effects
					(&gem_resident_window_effects);
				gem_resident_graf_progress();
				gem_resident_event_progress();
				return;
			}

			/* 3e. whatever is left feeds the EVNT_* state */
			event_input.owner = (UWORD) channel;
			event_input.generation_lo = pd->generation_lo;
			event_input.generation_hi = pd->generation_hi;
			event_input.mouse_x = vdi_input.mouse_x;
			event_input.mouse_y = vdi_input.mouse_y;
			event_input.mouse_buttons = vdi_input.mouse_buttons;
			event_input.key_state = vdi_input.key_state;
			event_input.key_code = vdi_input.key_code;
			event_input.key_ready = vdi_input.key_ready;
			(void) gem_event_resident_input(&event_input);
		}
	}

	/* 4. let anything finishable finish now */
	gem_resident_graf_progress();
	gem_resident_event_progress();
}
