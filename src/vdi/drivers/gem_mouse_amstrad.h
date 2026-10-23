/*
 * gem_mouse_amstrad.h - Amstrad PC1512/PC1640 mouse-port driver
 *
 */

#ifndef ELKS_GEM_MOUSE_AMSTRAD_H
#define ELKS_GEM_MOUSE_AMSTRAD_H

#include "vdi.h"

/* clear the hardware counters and forget button state */
void gem_mouse_amstrad_reset(void);

/* read and clear the movement counters */
GEM_VDI_WORD gem_mouse_amstrad_poll(GEM_VDI_COORD *delta_x,
	GEM_VDI_COORD *delta_y);

/* consume one Amstrad mouse-button key code */
GEM_VDI_WORD gem_mouse_amstrad_key_byte(GEM_VDI_UBYTE byte);

/* button state assembled from the consumed key bytes */
GEM_VDI_WORD gem_mouse_amstrad_buttons(void);

#endif				/* ELKS_GEM_MOUSE_AMSTRAD_H */
