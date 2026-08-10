/*
 * gem_event_resident.c - GEM event/input waits on ELKS processes
 *
 * the EVNT_* event and input waits, ELKS does the blocking and waking
 * so we just record what a caller waits for, the resident owner wakes it
 */

#include "gem_event_resident.h"
#include "gem_aes_call.h"

#define GEM_EVENT_PD_FREE              0U
#define GEM_EVENT_PD_ACTIVE            1U
#define GEM_EVENT_WAIT_NONE            0U
#define GEM_EVENT_WAIT_ACTIVE          1U

#define GEM_EVENT_DCLICK_RATES         5U
#define GEM_EVENT_DEFAULT_TICK_MS      20U

/* button param word, click count in low byte, enter/leave flag in high byte */
#define GEM_EVENT_BYTE_MASK            0x00ffU

typedef struct gem_event_word_pair {
	UWORD lo;
	UWORD hi;
} GEM_EVENT_WORD_PAIR;

typedef struct gem_event_pd {
	UWORD generation_lo;
	UWORD generation_hi;
	UWORD flags;
	UWORD button_click_word;
	UWORD button_mask;
	UWORD button_state;
	UWORD button_transition_serial;
	UWORD button_batch_serial;
	GEM_EVENT_RECTANGLE mouse_one;
	GEM_EVENT_RECTANGLE mouse_two;
	GEM_EVENT_WORD_PAIR timer;
	GEM_BINDINGS_POINTER_SLOT message_address;
	UWORD keys[GEM_EVENT_KEY_QUEUE_WORDS];
	UBYTE key_front;
	UBYTE key_rear;
	UBYTE key_count;
	UBYTE timer_ready;
	UBYTE state;
	UBYTE wait_state;
	UBYTE opcode;
} GEM_EVENT_PD;

typedef UBYTE GEM_EVENT_PD_MUST_BE_68_BYTES
	[(sizeof(GEM_EVENT_PD) == 68) ? 1 : -1];

static GEM_EVENT_PD gem_event_pds[GEM_EVENT_PD_COUNT];

/* double-click rate table, milliseconds */
static const UWORD gem_event_dclick_rates[GEM_EVENT_DCLICK_RATES] = {
	450U, 330U, 275U, 220U, 165U
};

static UWORD gem_event_dclick_index;
static UWORD gem_event_tick_ms;

static UWORD gem_event_input_owner;
static UWORD gem_event_input_generation_lo;
static UWORD gem_event_input_generation_hi;
static WORD gem_event_mouse_x;
static WORD gem_event_mouse_y;
static UWORD gem_event_mouse_buttons;
static UWORD gem_event_key_state;
static UWORD gem_event_mouse_clicks;
static UWORD gem_event_button_transition_serial;
static UWORD gem_event_button_batch_serial;
static UBYTE gem_event_input_valid;

/*
 * the last mouse button transition kept so a wait armed after the edge
 * still sees it, only valid while the input owner matches, an owner
 * change clears the transition count
 */
static WORD gem_event_previous_mouse_x;
static WORD gem_event_previous_mouse_y;
static UWORD gem_event_previous_mouse_buttons;
static UWORD gem_event_previous_mouse_clicks;
static UWORD gem_event_mouse_transitions;

/* click batching state */
static UWORD gem_event_click_owner;
static UWORD gem_event_click_generation_lo;
static UWORD gem_event_click_generation_hi;
static UWORD gem_event_click_desired;
static UWORD gem_event_click_count;
static UWORD gem_event_click_delay_ms;

/* a finished batch stays visible till someone has seen its serial */
static UWORD gem_event_batch_owner;
static UWORD gem_event_batch_generation_lo;
static UWORD gem_event_batch_generation_hi;
static UWORD gem_event_batch_state;
static UWORD gem_event_batch_clicks;

static GEM_EVENT_PD *
gem_event_pd_at(UWORD owner)
{
	GEM_EVENT_PD *pd;
	UWORD index;

	if (owner >= GEM_EVENT_PD_COUNT)
		return (GEM_EVENT_PD *) 0;
	pd = gem_event_pds;
	index = owner;
	while (index--)
		pd++;
	return pd;
}

static VOID
gem_event_clear_rectangle(GEM_EVENT_RECTANGLE *rectangle)
{
	rectangle->outside = 0;
	rectangle->x = 0;
	rectangle->y = 0;
	rectangle->width = 0;
	rectangle->height = 0;
}

static VOID
gem_event_copy_rectangle_words(GEM_EVENT_RECTANGLE *destination,
	const GEM_EVENT_RECTANGLE *source)
{
	destination->outside = source->outside;
	destination->x = source->x;
	destination->y = source->y;
	destination->width = source->width;
	destination->height = source->height;
}

static VOID
gem_event_copy_slot(GEM_BINDINGS_POINTER_SLOT *destination,
	const GEM_BINDINGS_POINTER_SLOT *source)
{
	destination->lo = source->lo;
	destination->hi = source->hi;
}

static VOID
gem_event_clear_pd(GEM_EVENT_PD *pd)
{
	UWORD *key;
	UWORD count;

	pd->generation_lo = 0;
	pd->generation_hi = 0;
	pd->flags = 0;
	pd->button_click_word = 0;
	pd->button_mask = 0;
	pd->button_state = 0;
	pd->button_transition_serial = 0;
	pd->button_batch_serial = 0;
	gem_event_clear_rectangle(&pd->mouse_one);
	gem_event_clear_rectangle(&pd->mouse_two);
	pd->timer.lo = 0;
	pd->timer.hi = 0;
	pd->message_address.lo = 0;
	pd->message_address.hi = 0;
	key = pd->keys;
	count = GEM_EVENT_KEY_QUEUE_WORDS;
	while (count--)
		*key++ = 0;
	pd->key_front = 0;
	pd->key_rear = 0;
	pd->key_count = 0;
	pd->timer_ready = FALSE;
	pd->state = GEM_EVENT_PD_FREE;
	pd->wait_state = GEM_EVENT_WAIT_NONE;
	pd->opcode = 0;
}

/* a reused channel drops the old generations input and wait state */
static GEM_EVENT_PD *
gem_event_bind_pd(UWORD owner, UWORD generation_lo, UWORD generation_hi)
{
	GEM_EVENT_PD *pd;

	pd = gem_event_pd_at(owner);
	if (!pd)
		return (GEM_EVENT_PD *) 0;
	if (pd->state == GEM_EVENT_PD_ACTIVE
		&& pd->generation_lo == generation_lo
		&& pd->generation_hi == generation_hi)
		return pd;
	gem_event_clear_pd(pd);
	pd->generation_lo = generation_lo;
	pd->generation_hi = generation_hi;
	pd->state = GEM_EVENT_PD_ACTIVE;
	return pd;
}

static WORD
gem_event_pd_matches(const GEM_EVENT_PD *pd, UWORD generation_lo,
	UWORD generation_hi)
{
	return pd && pd->state == GEM_EVENT_PD_ACTIVE
		&& pd->generation_lo == generation_lo
		&& pd->generation_hi == generation_hi;
}

static VOID
gem_event_clear_effects(GEM_EVENT_EFFECTS *effects)
{
	effects->read_message = FALSE;
	effects->message_address.lo = 0;
	effects->message_address.hi = 0;
}

static VOID
gem_event_clear_completion(GEM_EVENT_COMPLETION *completion)
{
	UWORD *output;
	UWORD count;

	completion->owner = GEM_EVENT_OWNER_NONE;
	completion->generation_lo = 0;
	completion->generation_hi = 0;
	completion->output_count = 0;
	output = completion->int_out;
	count = GEM_EVENT_OUTPUT_WORDS;
	while (count--)
		*output++ = 0;
	gem_event_clear_effects(&completion->effects);
}

/* check the array counts before touching caller arrays */
static WORD
gem_event_call_shape(const GEM_EVENT_CALL *call, UWORD input_count,
	UWORD output_count, UWORD address_count)
{
	if (!call)
		return FALSE;
	return gem_aes_call_counts(call->control, input_count, output_count,
		address_count)
		&& gem_aes_call_arrays(call->int_in, input_count,
		call->int_out, output_count, call->addr_in, address_count);
}

static WORD
gem_event_malformed(const GEM_EVENT_CALL *call, WORD *handled)
{
	return gem_aes_call_malformed(call ? call->control : (const UWORD *) 0,
		call ? call->int_out : (UWORD *) 0, handled);
}

/* enqueue a key, a full queue drops it */
static VOID
gem_event_enqueue_key(GEM_EVENT_PD *pd, UWORD key_code)
{
	if (pd->key_count >= GEM_EVENT_KEY_QUEUE_WORDS)
		return;
	pd->keys[pd->key_rear] = key_code;
	pd->key_rear++;
	if (pd->key_rear == GEM_EVENT_KEY_QUEUE_WORDS)
		pd->key_rear = 0;
	pd->key_count++;
}

/* dequeue a key, caller already checked one is ready */
static UWORD
gem_event_dequeue_key(GEM_EVENT_PD *pd)
{
	UWORD key_code;

	key_code = pd->keys[pd->key_front];
	pd->key_front++;
	if (pd->key_front == GEM_EVENT_KEY_QUEUE_WORDS)
		pd->key_front = 0;
	pd->key_count--;
	return key_code;
}

/* point-in-rectangle, unsigned adds so the compiler cant assume no overflow */
static WORD
gem_event_inside(WORD x, WORD y, const GEM_EVENT_RECTANGLE *rectangle)
{
	WORD right;
	WORD bottom;

	right = (WORD) ((UWORD) rectangle->x + (UWORD) rectangle->width);
	bottom = (WORD) ((UWORD) rectangle->y + (UWORD) rectangle->height);
	return x >= rectangle->x && y >= rectangle->y && x < right
		&& y < bottom;
}

/* mouse-rectangle wait, zero waits to enter, nonzero waits to leave */
static WORD
gem_event_rectangle_ready(const GEM_EVENT_RECTANGLE *rectangle)
{
	WORD inside;

	inside = gem_event_inside(gem_event_mouse_x, gem_event_mouse_y,
		rectangle);
	return (rectangle->outside != 0) != (inside != 0);
}

/* is the button in the state this wait wants */
static WORD
gem_event_button_ready(UWORD buttons, UWORD click_word, UWORD mask,
	UWORD desired)
{
	UWORD leave;
	UWORD matches;

	leave = (click_word >> 8) & GEM_EVENT_BYTE_MASK;
	matches = (((mask & GEM_EVENT_BYTE_MASK)
			& ((desired ^ buttons) & GEM_EVENT_BYTE_MASK)) == 0);
	return (matches != 0) != (leave != 0);
}

static UWORD
gem_event_requested_clicks(UWORD click_word)
{
	return click_word & GEM_EVENT_BYTE_MASK;
}

/* saturating millisecond add */
static UWORD
gem_event_millisecond_add(UWORD value, UWORD addition)
{
	if ((UWORD) (0xffffU - value) < addition)
		return 0xffffU;
	return (UWORD) (value + addition);
}

/* two-word timer subtract, clamps at zero instead of wrapping */
static WORD
gem_event_timer_subtract(GEM_EVENT_WORD_PAIR *timer, UWORD elapsed)
{
	UWORD old_low;

	if (!timer->hi && timer->lo <= elapsed) {
		timer->lo = 0;
		timer->hi = 0;
		return TRUE;
	}
	old_low = timer->lo;
	timer->lo = (UWORD) (timer->lo - elapsed);
	if (old_low < elapsed)
		timer->hi--;
	return FALSE;
}

static WORD
gem_event_same_input_owner(UWORD owner, UWORD generation_lo,
	UWORD generation_hi)
{
	return gem_event_input_valid && gem_event_input_owner == owner
		&& gem_event_input_generation_lo == generation_lo
		&& gem_event_input_generation_hi == generation_hi;
}

static VOID
gem_event_clear_previous_transition(VOID)
{
	gem_event_mouse_transitions = 0;
}

/* true when this wait wants more than one click, so batching turns on */
static WORD
gem_event_has_multiclick_wait(UWORD owner, UWORD generation_lo,
	UWORD generation_hi)
{
	GEM_EVENT_PD *pd;

	pd = gem_event_pd_at(owner);
	if (!gem_event_pd_matches(pd, generation_lo, generation_hi)
		|| pd->wait_state != GEM_EVENT_WAIT_ACTIVE
		|| !(pd->flags & GEM_EVENT_MU_BUTTON))
		return FALSE;
	return gem_event_requested_clicks(pd->button_click_word) > 1U;
}

/* publish the delayed button transition once the batch delay runs out */
static VOID
gem_event_finish_click_batch(VOID)
{
	if (!gem_event_click_delay_ms)
		return;
	gem_event_click_delay_ms = 0;
	gem_event_button_batch_serial++;
	gem_event_batch_owner = gem_event_click_owner;
	gem_event_batch_generation_lo = gem_event_click_generation_lo;
	gem_event_batch_generation_hi = gem_event_click_generation_hi;
	gem_event_batch_state = gem_event_click_desired;
	gem_event_batch_clicks = gem_event_click_count;
	/*
	 * publish the delayed state with its click count, then if the
	 * button has already moved on, publish the current state as one
	 * click
	 */
	if (gem_event_click_desired != gem_event_mouse_buttons) {
		gem_event_previous_mouse_x = gem_event_mouse_x;
		gem_event_previous_mouse_y = gem_event_mouse_y;
		gem_event_previous_mouse_buttons = gem_event_click_desired;
		gem_event_previous_mouse_clicks = gem_event_click_count;
		gem_event_mouse_transitions = 2U;
		gem_event_mouse_clicks = 1U;
	} else
		gem_event_mouse_clicks = gem_event_click_count;
}

static WORD
gem_event_moved_more_than_two(WORD old_value, WORD new_value)
{
	WORD difference;

	difference = (WORD) ((UWORD) old_value - (UWORD) new_value);
	return difference > 2 || difference < -2;
}

VOID
gem_event_resident_reset(VOID)
{
	GEM_EVENT_PD *pd;
	UWORD count;

	pd = gem_event_pds;
	count = GEM_EVENT_PD_COUNT;
	while (count--)
		gem_event_clear_pd(pd++);
	gem_event_dclick_index = 0;
	gem_event_tick_ms = GEM_EVENT_DEFAULT_TICK_MS;
	gem_event_input_owner = GEM_EVENT_OWNER_NONE;
	gem_event_input_generation_lo = 0;
	gem_event_input_generation_hi = 0;
	gem_event_mouse_x = 0;
	gem_event_mouse_y = 0;
	gem_event_mouse_buttons = 0;
	gem_event_key_state = 0;
	gem_event_mouse_clicks = 0;
	gem_event_button_transition_serial = 0;
	gem_event_button_batch_serial = 0;
	gem_event_input_valid = FALSE;
	gem_event_clear_previous_transition();
	gem_event_click_owner = GEM_EVENT_OWNER_NONE;
	gem_event_click_generation_lo = 0;
	gem_event_click_generation_hi = 0;
	gem_event_click_desired = 0;
	gem_event_click_count = 0;
	gem_event_click_delay_ms = 0;
	gem_event_batch_owner = GEM_EVENT_OWNER_NONE;
	gem_event_batch_generation_lo = 0;
	gem_event_batch_generation_hi = 0;
	gem_event_batch_state = 0;
	gem_event_batch_clicks = 0;
}

WORD
gem_event_resident_configure_tick(UWORD tick_milliseconds)
{
	if (!tick_milliseconds)
		return FALSE;
	gem_event_tick_ms = tick_milliseconds;
	return TRUE;
}

VOID
gem_event_resident_detach(UWORD owner, UWORD generation_lo, UWORD generation_hi)
{
	GEM_EVENT_PD *pd;

	pd = gem_event_pd_at(owner);
	if (!gem_event_pd_matches(pd, generation_lo, generation_hi))
		return;
	gem_event_clear_pd(pd);
	if (gem_event_same_input_owner(owner, generation_lo, generation_hi)) {
		gem_event_input_owner = GEM_EVENT_OWNER_NONE;
		gem_event_input_valid = FALSE;
		gem_event_clear_previous_transition();
	}
	if (gem_event_click_delay_ms && gem_event_click_owner == owner
		&& gem_event_click_generation_lo == generation_lo
		&& gem_event_click_generation_hi == generation_hi)
		gem_event_click_delay_ms = 0;
}

WORD
gem_event_resident_input(const GEM_EVENT_INPUT *input)
{
	GEM_EVENT_PD *pd;
	WORD owner_changed;
	WORD moved;

	if (!input || input->owner >= GEM_EVENT_PD_COUNT
		|| input->key_ready > 1U)
		return FALSE;
	pd = gem_event_pd_at(input->owner);
	if (!gem_event_pd_matches(pd, input->generation_lo,
			input->generation_hi))
		return FALSE;

	owner_changed = !gem_event_same_input_owner(input->owner,
		input->generation_lo, input->generation_hi);
	moved = gem_event_input_valid
		&& (gem_event_moved_more_than_two(gem_event_mouse_x,
			input->mouse_x)
		|| gem_event_moved_more_than_two(gem_event_mouse_y,
			input->mouse_y));
	if (moved && gem_event_click_delay_ms)
		gem_event_finish_click_batch();

	if (owner_changed) {
		/* a new owner gets the current mouse state reposted */
		gem_event_button_transition_serial++;
		gem_event_click_delay_ms = 0;
		gem_event_clear_previous_transition();
	} else if (gem_event_input_valid
		&& input->mouse_buttons != gem_event_mouse_buttons) {
		gem_event_button_transition_serial++;
		if (gem_event_click_delay_ms) {
			if (input->mouse_buttons == gem_event_click_desired) {
				if (gem_event_click_count != 0xffffU)
					gem_event_click_count++;
				/* a repeat click extends the batch by three ticks */
				gem_event_click_delay_ms =
					gem_event_millisecond_add
					(gem_event_click_delay_ms,
					gem_event_tick_ms);
				gem_event_click_delay_ms =
					gem_event_millisecond_add
					(gem_event_click_delay_ms,
					gem_event_tick_ms);
				gem_event_click_delay_ms =
					gem_event_millisecond_add
					(gem_event_click_delay_ms,
					gem_event_tick_ms);
			}
		} else if (input->mouse_buttons
			&& gem_event_has_multiclick_wait(input->owner,
				input->generation_lo, input->generation_hi)) {
			gem_event_click_owner = input->owner;
			gem_event_click_generation_lo = input->generation_lo;
			gem_event_click_generation_hi = input->generation_hi;
			gem_event_click_desired = input->mouse_buttons;
			gem_event_click_count = 1;
			gem_event_click_delay_ms =
				gem_event_dclick_rates[gem_event_dclick_index];
		} else {
			/* no multi-click wait, publish the transition right away */
			gem_event_previous_mouse_x = input->mouse_x;
			gem_event_previous_mouse_y = input->mouse_y;
			gem_event_previous_mouse_buttons =
				gem_event_mouse_buttons;
			gem_event_previous_mouse_clicks =
				gem_event_mouse_clicks;
			if (gem_event_mouse_transitions != 0xffffU)
				gem_event_mouse_transitions++;
			gem_event_mouse_clicks = 1;
		}
	}

	gem_event_input_owner = input->owner;
	gem_event_input_generation_lo = input->generation_lo;
	gem_event_input_generation_hi = input->generation_hi;
	gem_event_mouse_x = input->mouse_x;
	gem_event_mouse_y = input->mouse_y;
	gem_event_mouse_buttons = input->mouse_buttons;
	gem_event_key_state = input->key_state;
	gem_event_input_valid = TRUE;
	if (input->key_ready)
		gem_event_enqueue_key(pd, input->key_code);
	return TRUE;
}

VOID
gem_event_resident_tick(UWORD elapsed_milliseconds)
{
	GEM_EVENT_PD *pd;
	UWORD count;

	if (!elapsed_milliseconds)
		return;
	if (gem_event_click_delay_ms) {
		if (gem_event_click_delay_ms <= elapsed_milliseconds)
			gem_event_finish_click_batch();
		else
			gem_event_click_delay_ms = (UWORD)
				(gem_event_click_delay_ms -
				elapsed_milliseconds);
	}

	pd = gem_event_pds;
	count = GEM_EVENT_PD_COUNT;
	while (count--) {
		if (pd->state == GEM_EVENT_PD_ACTIVE
			&& pd->wait_state == GEM_EVENT_WAIT_ACTIVE
			&& (pd->flags & GEM_EVENT_MU_TIMER)
			&& !pd->timer_ready
			&& gem_event_timer_subtract(&pd->timer,
				elapsed_milliseconds))
			pd->timer_ready = TRUE;
		pd++;
	}
}

static VOID
gem_event_copy_rectangle(GEM_EVENT_RECTANGLE *rectangle, const UWORD *words)
{
	rectangle->outside = (WORD) words[0];
	rectangle->x = (WORD) words[1];
	rectangle->y = (WORD) words[2];
	rectangle->width = (WORD) words[3];
	rectangle->height = (WORD) words[4];
}

/* a wait only gets installed after its quick checks fail */
static VOID
gem_event_begin_wait(GEM_EVENT_PD *pd, UWORD opcode, UWORD flags,
	UWORD click_word, UWORD mask, UWORD state,
	const GEM_EVENT_RECTANGLE *mouse_one,
	const GEM_EVENT_RECTANGLE *mouse_two, UWORD timer_lo, UWORD timer_hi,
	GEM_BINDINGS_POINTER_SLOT message_address)
{
	pd->opcode = (UBYTE) opcode;
	pd->flags = flags;
	pd->button_click_word = click_word;
	pd->button_mask = mask;
	pd->button_state = state;
	pd->button_transition_serial = gem_event_button_transition_serial;
	pd->button_batch_serial = gem_event_button_batch_serial;
	if (mouse_one)
		gem_event_copy_rectangle_words(&pd->mouse_one, mouse_one);
	else
		gem_event_clear_rectangle(&pd->mouse_one);
	if (mouse_two)
		gem_event_copy_rectangle_words(&pd->mouse_two, mouse_two);
	else
		gem_event_clear_rectangle(&pd->mouse_two);
	pd->timer.lo = timer_lo;
	pd->timer.hi = timer_hi;
	pd->timer_ready = FALSE;
	gem_event_copy_slot(&pd->message_address, &message_address);
	pd->wait_state = GEM_EVENT_WAIT_ACTIVE;
}

/*
 * the ready checks for a wait, with quick FALSE a button wait only fires
 * on a transition after it was armed
 */
static UWORD
gem_event_ready_mask(GEM_EVENT_PD *pd, UWORD owner, UBYTE message_ready,
	WORD quick, UWORD *key_return, UWORD *button_return)
{
	UWORD what;
	UWORD requested;
	WORD owns_mouse;

	what = 0;
	*key_return = 0;
	*button_return = 0;
	if ((pd->flags & GEM_EVENT_MU_KEYBD) && pd->key_count) {
		what |= GEM_EVENT_MU_KEYBD;
		*key_return = gem_event_dequeue_key(pd);
	}

	owns_mouse = gem_event_same_input_owner(owner, pd->generation_lo,
		pd->generation_hi);
	if (owns_mouse && (pd->flags & GEM_EVENT_MU_BUTTON)) {
		requested = gem_event_requested_clicks(pd->button_click_word);
		if (quick && gem_event_mouse_transitions > 1U
			&&
			gem_event_button_ready(gem_event_previous_mouse_buttons,
				pd->button_click_word, pd->button_mask,
				pd->button_state)) {
			/* the button already moved this sample, use that transition */
			what |= GEM_EVENT_MU_BUTTON;
			*button_return = gem_event_previous_mouse_clicks;
		} else if (quick
			&& gem_event_button_ready(gem_event_mouse_buttons,
				pd->button_click_word, pd->button_mask,
				pd->button_state)) {
			what |= GEM_EVENT_MU_BUTTON;
			*button_return = gem_event_mouse_clicks;
		} else if (!quick && requested > 1U
			&& pd->button_batch_serial
			!= gem_event_button_batch_serial
			&& gem_event_batch_owner == owner
			&& gem_event_batch_generation_lo == pd->generation_lo
			&& gem_event_batch_generation_hi == pd->generation_hi) {
			/*
			 * the batch publishes the delayed state first, then
			 * the current state with one click
			 */
			if (gem_event_button_ready(gem_event_batch_state,
					pd->button_click_word, pd->button_mask,
					pd->button_state)) {
				what |= GEM_EVENT_MU_BUTTON;
				if (gem_event_batch_clicks > requested)
					*button_return = requested;
				else
					*button_return = gem_event_batch_clicks;
			} else if (gem_event_button_ready
				(gem_event_mouse_buttons, pd->button_click_word,
					pd->button_mask, pd->button_state)) {
				what |= GEM_EVENT_MU_BUTTON;
				*button_return = 1U;
			}
		} else if (!quick && requested <= 1U
			&& pd->button_transition_serial
			!= gem_event_button_transition_serial
			&& gem_event_button_ready(gem_event_mouse_buttons,
				pd->button_click_word, pd->button_mask,
				pd->button_state)) {
			what |= GEM_EVENT_MU_BUTTON;
			if (gem_event_mouse_clicks > requested)
				*button_return = requested;
			else
				*button_return = gem_event_mouse_clicks;
		}
	}
	if (owns_mouse && (pd->flags & GEM_EVENT_MU_M1)
		&& gem_event_rectangle_ready(&pd->mouse_one))
		what |= GEM_EVENT_MU_M1;
	if (owns_mouse && (pd->flags & GEM_EVENT_MU_M2)
		&& gem_event_rectangle_ready(&pd->mouse_two))
		what |= GEM_EVENT_MU_M2;
	if ((pd->flags & GEM_EVENT_MU_MESAG) && message_ready)
		what |= GEM_EVENT_MU_MESAG;
	if ((pd->flags & GEM_EVENT_MU_TIMER)
		&& (pd->timer_ready || (!pd->timer.lo && !pd->timer.hi)))
		what |= GEM_EVENT_MU_TIMER;
	return what;
}

static UWORD
gem_event_output_count(UWORD opcode)
{
	switch (opcode) {
	case GEM_EVENT_EVNT_BUTTON:
	case GEM_EVENT_EVNT_MOUSE:
		return 5U;
	case GEM_EVENT_EVNT_MULTI:
		return 7U;
	default:
		return 1U;
	}
}

/* fill the result words after a wait is satisfied */
static WORD
gem_event_build_result(GEM_EVENT_PD *pd, UWORD owner, UWORD what,
	UWORD key_return, UWORD button_return, UWORD *output,
	GEM_EVENT_EFFECTS *effects)
{
	WORD return_x;
	WORD return_y;
	WORD previous_ready;
	WORD result;

	/* report the saved mouse position while a transition is pending */
	previous_ready = gem_event_mouse_transitions
		&& gem_event_same_input_owner(owner, pd->generation_lo,
		pd->generation_hi);
	if (previous_ready) {
		return_x = gem_event_previous_mouse_x;
		return_y = gem_event_previous_mouse_y;
	} else {
		return_x = gem_event_mouse_x;
		return_y = gem_event_mouse_y;
	}
	result = TRUE;
	switch (pd->opcode) {
	case GEM_EVENT_EVNT_KEYBD:
		result = (WORD) key_return;
		break;
	case GEM_EVENT_EVNT_BUTTON:
		result = (WORD) button_return;
		output[1] = (UWORD) return_x;
		output[2] = (UWORD) return_y;
		output[3] = gem_event_mouse_buttons;
		output[4] = gem_event_key_state;
		break;
	case GEM_EVENT_EVNT_MOUSE:
		/* EVNT_MOUSE always returns zero */
		result = FALSE;
		output[1] = (UWORD) return_x;
		output[2] = (UWORD) return_y;
		output[3] = gem_event_mouse_buttons;
		output[4] = gem_event_key_state;
		break;
	case GEM_EVENT_EVNT_MESAG:
		effects->read_message = TRUE;
		gem_event_copy_slot(&effects->message_address,
			&pd->message_address);
		break;
	case GEM_EVENT_EVNT_TIMER:
		break;
	case GEM_EVENT_EVNT_MULTI:
		result = (WORD) what;
		output[1] = (UWORD) return_x;
		output[2] = (UWORD) return_y;
		output[3] = gem_event_mouse_buttons;
		output[4] = gem_event_key_state;
		output[5] = key_return;
		output[6] = button_return;
		if (what & GEM_EVENT_MU_MESAG) {
			effects->read_message = TRUE;
			gem_event_copy_slot(&effects->message_address,
				&pd->message_address);
		}
		break;
	default:
		result = FALSE;
		break;
	}
	output[0] = (UWORD) result;
	/* clear the pending transition after every completed wait */
	if (previous_ready)
		gem_event_clear_previous_transition();
	return result;
}

static WORD
gem_event_shape_for_opcode(const GEM_EVENT_CALL *call, UWORD opcode)
{
	switch (opcode) {
	case GEM_EVENT_EVNT_KEYBD:
		return gem_event_call_shape(call, 0U, 1U, 0U);
	case GEM_EVENT_EVNT_BUTTON:
		return gem_event_call_shape(call, 3U, 5U, 0U);
	case GEM_EVENT_EVNT_MOUSE:
		return gem_event_call_shape(call, 5U, 5U, 0U);
	case GEM_EVENT_EVNT_MESAG:
		return gem_event_call_shape(call, 0U, 1U, 1U);
	case GEM_EVENT_EVNT_TIMER:
		return gem_event_call_shape(call, 2U, 1U, 0U);
	case GEM_EVENT_EVNT_MULTI:
		return gem_event_call_shape(call, 16U, 7U, 1U);
	case GEM_EVENT_EVNT_DCLICK:
		return gem_event_call_shape(call, 2U, 1U, 0U);
	default:
		return FALSE;
	}
}

WORD
gem_event_resident_dispatch(const GEM_EVENT_CALL *call,
	GEM_EVENT_EFFECTS *effects, WORD *handled)
{
	GEM_EVENT_PD *pd;
	GEM_EVENT_RECTANGLE mouse_one;
	GEM_EVENT_RECTANGLE mouse_two;
	GEM_BINDINGS_POINTER_SLOT message_address;
	UWORD opcode;
	UWORD flags;
	UWORD click_word;
	UWORD mask;
	UWORD state;
	UWORD timer_lo;
	UWORD timer_hi;
	UWORD key_return;
	UWORD button_return;
	UWORD what;
	WORD result;

	if (!handled)
		return FALSE;
	*handled = FALSE;
	if (!call || !call->control || !effects)
		return FALSE;
	opcode = call->control[0];
	if (opcode < GEM_EVENT_EVNT_KEYBD || opcode > GEM_EVENT_EVNT_DCLICK)
		return FALSE;
	gem_event_clear_effects(effects);
	if (!gem_event_shape_for_opcode(call, opcode))
		return gem_event_malformed(call, handled);
	pd = gem_event_bind_pd(call->owner, call->generation_lo,
		call->generation_hi);
	if (!pd || pd->wait_state == GEM_EVENT_WAIT_ACTIVE)
		return gem_event_malformed(call, handled);

	/* EVNT_DCLICK sets a global preference and returns right away */
	if (opcode == GEM_EVENT_EVNT_DCLICK) {
		if (call->int_in[1]) {
			if (call->int_in[0] >= GEM_EVENT_DCLICK_RATES)
				return gem_event_malformed(call, handled);
			gem_event_dclick_index = call->int_in[0];
		}
		call->int_out[0] = gem_event_dclick_index;
		*handled = TRUE;
		return (WORD) gem_event_dclick_index;
	}

	flags = 0;
	click_word = 0;
	mask = 0;
	state = 0;
	timer_lo = 0;
	timer_hi = 0;
	message_address.lo = 0;
	message_address.hi = 0;
	gem_event_clear_rectangle(&mouse_one);
	gem_event_clear_rectangle(&mouse_two);

	switch (opcode) {
	case GEM_EVENT_EVNT_KEYBD:
		flags = GEM_EVENT_MU_KEYBD;
		break;
	case GEM_EVENT_EVNT_BUTTON:
		flags = GEM_EVENT_MU_BUTTON;
		click_word = call->int_in[0];
		mask = call->int_in[1];
		state = call->int_in[2];
		break;
	case GEM_EVENT_EVNT_MOUSE:
		flags = GEM_EVENT_MU_M1;
		gem_event_copy_rectangle(&mouse_one, call->int_in);
		break;
	case GEM_EVENT_EVNT_MESAG:
		flags = GEM_EVENT_MU_MESAG;
		gem_event_copy_slot(&message_address, &call->addr_in[0]);
		if (!message_address.lo && !message_address.hi)
			return gem_event_malformed(call, handled);
		break;
	case GEM_EVENT_EVNT_TIMER:
		flags = GEM_EVENT_MU_TIMER;
		timer_lo = call->int_in[0];
		timer_hi = call->int_in[1];
		/* a zero timer becomes one real tick */
		if (!timer_lo && !timer_hi)
			timer_lo = gem_event_tick_ms;
		break;
	case GEM_EVENT_EVNT_MULTI:
		flags = call->int_in[0];
		if (!flags || (flags & (UWORD) ~GEM_EVENT_MU_ALL))
			return gem_event_malformed(call, handled);
		click_word = call->int_in[1];
		mask = call->int_in[2];
		state = call->int_in[3];
		/* two-button mouse compatibility case */
		if (mask == 3U && state == 1U)
			state = 0;
		gem_event_copy_rectangle(&mouse_one, &call->int_in[4]);
		gem_event_copy_rectangle(&mouse_two, &call->int_in[9]);
		timer_lo = call->int_in[14];
		timer_hi = call->int_in[15];
		gem_event_copy_slot(&message_address, &call->addr_in[0]);
		if ((flags & GEM_EVENT_MU_MESAG)
			&& !message_address.lo && !message_address.hi)
			return gem_event_malformed(call, handled);
		break;
	default:
		return FALSE;
	}

	gem_event_begin_wait(pd, opcode, flags, click_word, mask, state,
		&mouse_one, &mouse_two, timer_lo, timer_hi, message_address);
	what = gem_event_ready_mask(pd, call->owner, call->message_ready, TRUE,
		&key_return, &button_return);
	if (!what) {
		*handled = TRUE;
		return GEM_EVENT_RESIDENT_DEFERRED;
	}
	/* EVNT_BUTTON satisfied right away returns one click */
	if (opcode == GEM_EVENT_EVNT_BUTTON && (what & GEM_EVENT_MU_BUTTON))
		button_return = 1U;
	result = gem_event_build_result(pd, call->owner, what, key_return,
		button_return, call->int_out, effects);
	pd->wait_state = GEM_EVENT_WAIT_NONE;
	*handled = TRUE;
	return result;
}

WORD
gem_event_resident_service(UWORD owner, UWORD generation_lo,
	UWORD generation_hi, UBYTE message_ready,
	GEM_EVENT_COMPLETION *completion)
{
	GEM_EVENT_PD *pd;
	UWORD key_return;
	UWORD button_return;
	UWORD what;

	if (!completion)
		return FALSE;
	gem_event_clear_completion(completion);
	pd = gem_event_pd_at(owner);
	if (!gem_event_pd_matches(pd, generation_lo, generation_hi)
		|| pd->wait_state != GEM_EVENT_WAIT_ACTIVE)
		return FALSE;
	what = gem_event_ready_mask(pd, owner, message_ready, FALSE,
		&key_return, &button_return);
	if (!what)
		return FALSE;
	completion->owner = owner;
	completion->generation_lo = generation_lo;
	completion->generation_hi = generation_hi;
	completion->output_count = gem_event_output_count(pd->opcode);
	gem_event_build_result(pd, owner, what, key_return, button_return,
		completion->int_out, &completion->effects);
	pd->wait_state = GEM_EVENT_WAIT_NONE;
	return TRUE;
}
