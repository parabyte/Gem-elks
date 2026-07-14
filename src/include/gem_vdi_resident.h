/*
 * gem_vdi_resident.h - classic shared VDI dispatcher for resident ELKS GEM.
 *
 * The entry point consumes the original CX=0473h, DS:DX parameter block.
 * Application is the original AES PD/channel tag.  It is retained only as a
 * 16-bit owner identity; ELKS remains responsible for tasks and memory.
 */

#ifndef ELKS_GEM_VDI_RESIDENT_H
#define ELKS_GEM_VDI_RESIDENT_H

#include <linuxmt/gemtrap.h>

#include "aes.h"
#include "gem_bindings_elks.h"
#include "vdi.h"

#define GEM_VDI_RESIDENT_SELECTOR	0x0473U
#define GEM_VDI_RESIDENT_WORKSTATIONS	8
#define GEM_VDI_RESIDENT_TEXT_MAX	80U

/*
 * One physical input sample retained entirely inside the resident owner.
 * Coordinates are unscaled pixels.  key_code keeps GEM's original BIOS
 * layout: ASCII in the low byte and the IBM PC scan code in the high byte.
 * No pointer or converted event structure crosses the client boundary.
 */
typedef struct gem_vdi_resident_input {
	WORD mouse_x;
	WORD mouse_y;
	UWORD mouse_buttons;
	UWORD key_state;
	UWORD key_code;
	UBYTE key_ready;
	UBYTE changed;
} GEM_VDI_RESIDENT_INPUT;

/*
 * The activity byte occupies the structure's former alignment byte.  Thus an
 * idle-sample hint costs no resident stack or data space in the 16-bit ABI.
 */
typedef BYTE GEM_VDI_RESIDENT_INPUT_MUST_BE_12_BYTES
	[(sizeof(GEM_VDI_RESIDENT_INPUT) == 12) ? 1 : -1];

/*
 * Open the one physical GSX workstation owned by gemaes.  The display and
 * input devices remain resident until gem_vdi_resident_shutdown(); opening
 * or closing a client virtual workstation never transfers hardware ownership.
 */
WORD gem_vdi_resident_startup(VOID);

/*
 * Return unscaled pixel and cell dimensions from that physical workstation.
 * All four outputs are required near pointers in the resident owner's data
 * segment.  Values are copied exactly; no rounding or saturation is needed.
 */
WORD gem_vdi_resident_get_metrics(UWORD *width, UWORD *height,
	UWORD *character_width, UWORD *character_height);

/* Same-process seam for resident original AES object/menu drawing modules. */
GEM_VDI_SCREEN *gem_vdi_resident_screen(VOID);

/*
 * Apply one original GRAF_MOUSE effect.  USER_DEF (255) reads one exact
 * 74-byte MFORM from the calling task's pinned DS; built-in forms and the
 * hide/show numbers 256/257 do not read the supplied slot.
 */
WORD gem_vdi_resident_mouse(const struct gemtrap_request *request,
	WORD application, WORD number, GEM_BINDINGS_POINTER_SLOT form);

/* Draw one bounded original far RSC string at a top-left cell coordinate. */
WORD gem_vdi_resident_text(GEM_BINDINGS_POINTER_SLOT text,
	UWORD max_characters, WORD x, WORD y, WORD color);

/*
 * Poll the already-open nonblocking PC input devices once.  A changed mouse
 * packet moves the native cursor with the currently selected GEM hot spot.
 * The returned state is still valid when no packet changed, allowing the AES
 * quick-event checks to observe the current buttons without a busy loop.
 * changed is one only for a state-changing mouse packet or a complete key;
 * it lets the periodic owner tick avoid repeating every menu, GRAF, window,
 * and event quick check when the physical state is unchanged.
 */
WORD gem_vdi_resident_poll_input(GEM_VDI_RESIDENT_INPUT *input);

WORD gem_vdi_resident_request(struct gemtrap_request *request,
	WORD application);
VOID gem_vdi_resident_release(WORD application);
VOID gem_vdi_resident_shutdown(VOID);

#endif /* ELKS_GEM_VDI_RESIDENT_H */
