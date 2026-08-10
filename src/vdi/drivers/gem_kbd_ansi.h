/*
 * gem_kbd_ansi.h - maps ELKS tty bytes to IBM PC scan codes
 *
 * GEM apps want IBM PC/XT set-1 scan codes but an ELKS tty gives
 * ASCII and ANSI escapes, so this maps one byte (or a decoded CSI/SS3
 * key) to a scan code plus modifiers, the escape state machine and
 * buffering live in the input core
 */

#ifndef ELKS_GEM_KBD_ANSI_H
#define ELKS_GEM_KBD_ANSI_H

#include "vdi.h"

/* IBM PC/XT set-1 scan codes returned for ANSI escapes */
#define GEM_SCAN_ESCAPE		0x01
#define GEM_SCAN_BACKSPACE	0x0e
#define GEM_SCAN_TAB		0x0f
#define GEM_SCAN_ENTER		0x1c
#define GEM_SCAN_SPACE		0x39
#define GEM_SCAN_HOME		0x47
#define GEM_SCAN_UP		0x48
#define GEM_SCAN_PAGE_UP	0x49
#define GEM_SCAN_LEFT		0x4b
#define GEM_SCAN_RIGHT		0x4d
#define GEM_SCAN_END		0x4f
#define GEM_SCAN_DOWN		0x50
#define GEM_SCAN_PAGE_DOWN	0x51
#define GEM_SCAN_INSERT		0x52
#define GEM_SCAN_DELETE		0x53

/* set-1 scan code for one ASCII byte, 0 if none */
GEM_VDI_UWORD gem_kbd_ansi_scan(GEM_VDI_UBYTE character);

/* shift/control modifiers implied by one ASCII byte */
GEM_VDI_UWORD gem_kbd_ansi_modifiers(GEM_VDI_UBYTE character);

/* scan code for a decoded ANSI sequence, CSI/SS3 final byte plus first param digit (0 if none) */
GEM_VDI_UWORD gem_kbd_ansi_csi(GEM_VDI_UBYTE final, GEM_VDI_UBYTE parameter);

#endif				/* ELKS_GEM_KBD_ANSI_H */
