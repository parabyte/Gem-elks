/*
 * gem_mouse_ms.h - Microsoft serial mouse protocol driver.
 *
 */

#ifndef ELKS_GEM_MOUSE_MS_H
#define ELKS_GEM_MOUSE_MS_H

#include "vdi.h"

/* forget any half-collected packet, called when the port opens */
void gem_mouse_ms_reset(void);

/* feed one byte from the serial stream, nonzero when a full three-byte packet decodes, movement in delta_x and delta_y, buttons in buttons */
GEM_VDI_WORD gem_mouse_ms_parse(GEM_VDI_UBYTE byte,
	GEM_VDI_COORD *delta_x, GEM_VDI_COORD *delta_y, GEM_VDI_WORD *buttons);

#endif				/* ELKS_GEM_MOUSE_MS_H */
