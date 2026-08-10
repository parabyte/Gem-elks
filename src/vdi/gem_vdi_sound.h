/*
 * gem_vdi_sound.h - the resident VDI's PC speaker interface.
 *
 * the classic VDI sound escapes (61 and 62), tones go out through the ELKS
 * kernel sequencer ioctl
 */

#ifndef ELKS_GEM_VDI_SOUND_H
#define ELKS_GEM_VDI_SOUND_H

#include "aes.h"

/* play one tone (frequency in Hz, duration in twentieths of a second) */
WORD gem_vdi_sound_play(WORD frequency, WORD duration);

/* stop whatever is sounding */
WORD gem_vdi_sound_stop(VOID);

/* the mute switch (escape 62) */
VOID gem_vdi_sound_set_enabled(WORD enabled);
WORD gem_vdi_sound_is_enabled(VOID);

#endif				/* ELKS_GEM_VDI_SOUND_H */
