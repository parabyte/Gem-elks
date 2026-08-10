/*
 * gem_tape_resident.c - AES event tape for resident ELKS GEM
 *
 * records and plays back mouse and key events. one tape at a time, in
 * the caller's buffer, six bytes a record.
 *
 * recording writes a record whenever the mouse or a key changes.
 * playback feeds those records back until the tape runs out
 */

#include "gem_tape_resident.h"

#include "gem_resident_memory.h"

#define GEM_TAPE_IDLE		0U
#define GEM_TAPE_RECORDING	1U
#define GEM_TAPE_PLAYING	2U

#define GEM_TAPE_NO_OWNER	0xffffU

static UBYTE gem_tape_state;
static UWORD gem_tape_owner = GEM_TAPE_NO_OWNER;
static UWORD gem_tape_segment;
static UWORD gem_tape_offset;	/* next record in the buffer */
static UWORD gem_tape_remaining;	/* records still to read or write */
static UWORD gem_tape_written;
static WORD gem_tape_scale = 100;
static UWORD gem_tape_pending_delay;	/* milliseconds still to wait */

/* last state a recording saw, so only changes get written */
static WORD gem_tape_last_x;
static WORD gem_tape_last_y;
static UWORD gem_tape_last_buttons;
static UWORD gem_tape_elapsed;

/* the state playback is rebuilding */
static GEM_VDI_RESIDENT_INPUT gem_tape_state_input;

VOID
gem_tape_resident_reset(VOID)
{
	gem_tape_state = GEM_TAPE_IDLE;
	gem_tape_owner = GEM_TAPE_NO_OWNER;
	gem_tape_segment = 0;
	gem_tape_offset = 0;
	gem_tape_remaining = 0;
	gem_tape_written = 0;
	gem_tape_pending_delay = 0;
	gem_tape_elapsed = 0;
}

VOID
gem_tape_resident_detach(UWORD owner)
{
	if (gem_tape_state != GEM_TAPE_IDLE && gem_tape_owner == owner)
		gem_tape_resident_reset();
}

WORD
gem_tape_resident_busy(UWORD owner)
{
	return gem_tape_state != GEM_TAPE_IDLE && gem_tape_owner == owner;
}

UWORD
gem_tape_resident_written(VOID)
{
	return gem_tape_written;
}

/* one six-byte record out of, or into, the caller's buffer */
static VOID
gem_tape_put(UWORD code, UWORD high, UWORD low)
{
	UWORD record[3];

	record[0] = code;
	record[1] = low;
	record[2] = high;
	gem_resident_memory_to(record, gem_tape_segment, gem_tape_offset,
		GEM_TAPE_RECORD_BYTES);
	gem_tape_offset = (UWORD) (gem_tape_offset + GEM_TAPE_RECORD_BYTES);
	gem_tape_written++;
	gem_tape_remaining--;
}

static VOID
gem_tape_get(UWORD *code, UWORD *high, UWORD *low)
{
	UWORD record[3];

	gem_resident_memory_from(gem_tape_segment, gem_tape_offset, record,
		GEM_TAPE_RECORD_BYTES);
	*code = record[0];
	*low = record[1];
	*high = record[2];
	gem_tape_offset = (UWORD) (gem_tape_offset + GEM_TAPE_RECORD_BYTES);
	gem_tape_remaining--;
}

static WORD
gem_tape_arm(UWORD owner, UWORD segment, UWORD offset, UWORD records)
{
	if (gem_tape_state != GEM_TAPE_IDLE || !segment || !records)
		return FALSE;
	/* the whole tape has to sit in one segment */
	if (records > 0xffffU / GEM_TAPE_RECORD_BYTES)
		return FALSE;
	if ((UWORD) (records * GEM_TAPE_RECORD_BYTES)
		> (UWORD) (0xffffU - offset))
		return FALSE;
	gem_tape_owner = owner;
	gem_tape_segment = segment;
	gem_tape_offset = offset;
	gem_tape_remaining = records;
	gem_tape_written = 0;
	gem_tape_pending_delay = 0;
	gem_tape_elapsed = 0;
	return TRUE;
}

WORD
gem_tape_resident_record(UWORD owner, UWORD segment, UWORD offset,
	UWORD records)
{
	if (!gem_tape_arm(owner, segment, offset, records))
		return FALSE;
	gem_tape_state = GEM_TAPE_RECORDING;
	/* first sample always writes a position */
	gem_tape_last_x = -1;
	gem_tape_last_y = -1;
	gem_tape_last_buttons = 0;
	return TRUE;
}

WORD
gem_tape_resident_play(UWORD owner, UWORD segment, UWORD offset,
	UWORD records, WORD scale)
{
	if (!gem_tape_arm(owner, segment, offset, records))
		return FALSE;
	gem_tape_state = GEM_TAPE_PLAYING;
	gem_tape_scale = scale > 0 ? scale : 100;
	gem_tape_state_input.mouse_x = 0;
	gem_tape_state_input.mouse_y = 0;
	gem_tape_state_input.mouse_buttons = 0;
	gem_tape_state_input.key_state = 0;
	gem_tape_state_input.key_code = 0;
	gem_tape_state_input.key_ready = 0;
	gem_tape_state_input.changed = 0;
	return TRUE;
}

WORD
gem_tape_resident_sample(const GEM_VDI_RESIDENT_INPUT *input,
	UWORD elapsed_milliseconds)
{
	if (gem_tape_state != GEM_TAPE_RECORDING || !input)
		return FALSE;
	gem_tape_elapsed = (UWORD) (gem_tape_elapsed + elapsed_milliseconds);

	/* a tick record carries the wait since the last change, so it only
	 * goes in ahead of one */
	if (gem_tape_remaining
		&& (input->mouse_x != gem_tape_last_x
			|| input->mouse_y != gem_tape_last_y
			|| input->mouse_buttons != gem_tape_last_buttons
			|| input->key_ready)) {
		if (gem_tape_elapsed && gem_tape_remaining) {
			gem_tape_put(GEM_TAPE_TCHNG, 0, gem_tape_elapsed);
			gem_tape_elapsed = 0;
		}
		if (gem_tape_remaining
			&& (input->mouse_x != gem_tape_last_x
				|| input->mouse_y != gem_tape_last_y)) {
			gem_tape_put(GEM_TAPE_MCHNG, (UWORD) input->mouse_x,
				(UWORD) input->mouse_y);
			gem_tape_last_x = input->mouse_x;
			gem_tape_last_y = input->mouse_y;
		}
		if (gem_tape_remaining
			&& input->mouse_buttons != gem_tape_last_buttons) {
			gem_tape_put(GEM_TAPE_BCHNG, 0, input->mouse_buttons);
			gem_tape_last_buttons = input->mouse_buttons;
		}
		if (gem_tape_remaining && input->key_ready)
			gem_tape_put(GEM_TAPE_KCHNG, input->key_state,
				input->key_code);
	}
	if (gem_tape_remaining)
		return FALSE;
	gem_tape_state = GEM_TAPE_IDLE;
	return TRUE;
}

WORD
gem_tape_resident_replay(GEM_VDI_RESIDENT_INPUT *input,
	UWORD elapsed_milliseconds, WORD *finished)
{
	UWORD code;
	UWORD high;
	UWORD low;
	unsigned long delay;

	if (finished)
		*finished = FALSE;
	if (gem_tape_state != GEM_TAPE_PLAYING || !input)
		return FALSE;
	if (gem_tape_pending_delay) {
		if (elapsed_milliseconds >= gem_tape_pending_delay)
			gem_tape_pending_delay = 0;
		else
			gem_tape_pending_delay = (UWORD)
				(gem_tape_pending_delay - elapsed_milliseconds);
		if (gem_tape_pending_delay)
			return FALSE;
	}
	if (!gem_tape_remaining) {
		gem_tape_state = GEM_TAPE_IDLE;
		if (finished)
			*finished = TRUE;
		return FALSE;
	}

	gem_tape_state_input.key_ready = 0;
	gem_tape_state_input.changed = 1;
	while (gem_tape_remaining) {
		gem_tape_get(&code, &high, &low);
		if (code == GEM_TAPE_TCHNG) {
			/* scale the stored wait by 100/scale */
			delay = (unsigned long) low;
			delay *= 100U;
			delay /= (unsigned long) (UWORD) gem_tape_scale;
			gem_tape_pending_delay = (UWORD) delay;
			break;
		}
		if (code == GEM_TAPE_MCHNG) {
			gem_tape_state_input.mouse_x = (WORD) high;
			gem_tape_state_input.mouse_y = (WORD) low;
			continue;
		}
		if (code == GEM_TAPE_BCHNG) {
			gem_tape_state_input.mouse_buttons = low;
			continue;
		}
		if (code == GEM_TAPE_KCHNG) {
			gem_tape_state_input.key_state = high;
			gem_tape_state_input.key_code = low;
			gem_tape_state_input.key_ready = 1;
			break;
		}
	}
	*input = gem_tape_state_input;
	if (!gem_tape_remaining && !gem_tape_pending_delay) {
		gem_tape_state = GEM_TAPE_IDLE;
		if (finished)
			*finished = TRUE;
	}
	return TRUE;
}
