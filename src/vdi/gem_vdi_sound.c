/*
 * gem_vdi_sound.c - the resident VDI's PC speaker tones.
 *
 * GEM's sound escape gives a frequency in Hz and a duration in twentieths of
 * a second, both converted to what the ELKS kernel sequencer wants - a PIT
 * divisor and 100 Hz ticks
 */

#include "gem_vdi_sound.h"

#include <linuxmt/kd.h>
#include <sys/ioctl.h>

static UBYTE gem_vdi_sound_enabled = TRUE;

static UWORD __attribute__((optimize("Os")))
	gem_vdi_sound_divisor(UWORD frequency)
{
	UWORD remaining_high;
	UWORD remaining_low;
	UWORD step;
	UWORD multiple;
	UWORD divisor;
	UWORD old_low;

	/* PIT channel two runs at 1,193,181 Hz, held as the word pair
	 * 0012h:34ddh, the PIT takes one 16-bit reload word, a quotient below
	 * one rounds up to one, one above 65535 stops at 65535 */
	if (!frequency)
		return 0;
	remaining_high = 0x0012U;
	remaining_low = 0x34ddU;
	step = frequency;
	multiple = 1U;
	while (step <= 0x7fffU && multiple <= 0x7fffU) {
		step += step;
		multiple += multiple;
	}
	divisor = 0;
	while (multiple) {
		while (remaining_high || remaining_low >= step) {
			old_low = remaining_low;
			remaining_low -= step;
			if (old_low < step)
				remaining_high--;
			if (divisor > (UWORD) (0xffffU - multiple))
				return 0xffffU;
			divisor += multiple;
		}
		step >>= 1;
		multiple >>= 1;
	}
	return divisor ? divisor : 1U;
}

static UWORD __attribute__((optimize("Os")))
	gem_vdi_sound_ticks(WORD duration)
{
	UWORD ticks;

	/* one GEM twentieth-of-a-second unit is five 100 Hz kernel ticks,
	 * values above 13107 stop at the full 16-bit tick word */
	if (duration <= 0)
		return 1U;
	ticks = (UWORD) duration;
	if (ticks > 13107U)
		return 0xffffU;
	duration = (WORD) ticks;
	ticks += (UWORD) duration;
	ticks += (UWORD) duration;
	ticks += (UWORD) duration;
	ticks += (UWORD) duration;
	return ticks;
}

WORD __attribute__((optimize("Os")))
	gem_vdi_sound_stop(VOID)
{
	struct audio_seq sequence;

	sequence.events = NULL;
	sequence.count = 0;
	sequence.rate_hz = 0;
	sequence.flags = AUDIO_SEQ_F_STOP;
	return ioctl(0, KIOCSNDSEQ, &sequence) >= 0 ? TRUE : FALSE;
}

WORD __attribute__((optimize("Os")))
	gem_vdi_sound_play(WORD frequency, WORD duration)
{
	struct audio_event event;
	struct audio_seq sequence;
	WORD result;

	if (!gem_vdi_sound_enabled)
		return TRUE;
	if (frequency < 0)
		return FALSE;
	event.divisor = gem_vdi_sound_divisor((UWORD) frequency);
	event.ticks = gem_vdi_sound_ticks(duration);
	event.flags = event.divisor ? AUDIO_F_TONE : AUDIO_F_REST;
	event.priority = 0;
	sequence.events = &event;
	sequence.count = 1;
	sequence.rate_hz = 0;
	sequence.flags = 0;
	result = (WORD) ioctl(0, KIOCSNDSEQ, &sequence);
	return result == 1 ? TRUE : FALSE;
}

VOID
gem_vdi_sound_set_enabled(WORD enabled)
{
	gem_vdi_sound_enabled = enabled ? TRUE : FALSE;
}

WORD
gem_vdi_sound_is_enabled(VOID)
{
	return gem_vdi_sound_enabled ? TRUE : FALSE;
}
