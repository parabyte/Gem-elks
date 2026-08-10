/*
 * gem_mouse_amstrad.h - Amstrad PC1512/PC1640 mouse-port driver
 *
 */

#ifndef ELKS_GEM_MOUSE_AMSTRAD_H
#define ELKS_GEM_MOUSE_AMSTRAD_H

#include "vdi.h"

/* clear the hardware counters and forget button state */
void gem_mouse_amstrad_reset(void);

/* read and clear the movement counters, nonzero if anything moved, delta_x and delta_y in screen coords, call once per tick */
GEM_VDI_WORD gem_mouse_amstrad_poll(GEM_VDI_COORD *delta_x,
	GEM_VDI_COORD *delta_y);

/* Amstrad buttons arrive as keyboard codes not via the mouse port, nonzero when the byte was a button code and consumed */
GEM_VDI_WORD gem_mouse_amstrad_key_byte(GEM_VDI_UBYTE byte);

/* button state assembled from the consumed key bytes */
GEM_VDI_WORD gem_mouse_amstrad_buttons(void);

#endif				/* ELKS_GEM_MOUSE_AMSTRAD_H */
