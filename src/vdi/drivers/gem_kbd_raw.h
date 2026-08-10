/*
 * gem_kbd_raw.h - turns raw XT set-1 scancodes into GEM key events
 *
 * when the console is in DCSET_KRAW mode the keyboard fd gives raw
 * set-1 scancodes, make and break, not ascii. GEM already wants set-1
 * scan codes so this just tracks shift/ctrl/alt/caps and fills in the
 * ascii, its the real way DOS GEM read the keyboard
 */

#ifndef ELKS_GEM_KBD_RAW_H
#define ELKS_GEM_KBD_RAW_H

#include "vdi.h"

/* forget any held modifiers, call when the keyboard is re-opened */
void gem_kbd_raw_reset(void);

/*
 * feed one raw scancode. returns GEM_VDI_KEY_PRESS with character,
 * modifiers and scan_code filled when a key should go to GEM, or
 * GEM_VDI_KEY_NONE for a modifier change, a key release, an E0 prefix
 * or an unmapped code
 */
GEM_VDI_WORD gem_kbd_raw_scancode(GEM_VDI_UBYTE code, GEM_VDI_UWORD *character,
	GEM_VDI_UWORD *modifiers, GEM_VDI_UWORD *scan_code);

#endif				/* ELKS_GEM_KBD_RAW_H */
