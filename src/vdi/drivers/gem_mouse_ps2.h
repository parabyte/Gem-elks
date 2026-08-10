/*
 * gem_mouse_ps2.h - PS/2 mouse protocol driver.
 *
 */

#ifndef ELKS_GEM_MOUSE_PS2_H
#define ELKS_GEM_MOUSE_PS2_H

#include "vdi.h"

/* forget any half-collected packet, called when the port opens */
void gem_mouse_ps2_reset(void);

/* feed one byte from the device stream, nonzero when a full three-byte packet decodes, movement in delta_x and delta_y (flipped to screen), buttons in buttons */
GEM_VDI_WORD gem_mouse_ps2_parse(GEM_VDI_UBYTE byte,
	GEM_VDI_COORD *delta_x, GEM_VDI_COORD *delta_y, GEM_VDI_WORD *buttons);

#endif				/* ELKS_GEM_MOUSE_PS2_H */
