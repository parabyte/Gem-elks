/*
 * gem_vdi_resident.h - shared VDI dispatcher for resident ELKS GEM
 *
 * the entry point takes the original CX=0473h, DS:DX parameter block.
 * application is the AES PD/channel tag, a 16-bit owner label. ELKS owns tasks
 * and memory
 */

#ifndef ELKS_GEM_VDI_RESIDENT_H
#define ELKS_GEM_VDI_RESIDENT_H

#include "gemtrap.h"

#include "aes.h"
#include "gem_bindings_elks.h"
#include "vdi.h"

#define GEM_VDI_RESIDENT_SELECTOR	0x0473U
#define GEM_VDI_RESIDENT_WORKSTATIONS	8
#define GEM_VDI_RESIDENT_TEXT_MAX	80U

/* one physical input sample. key_code keeps GEM's BIOS layout: ASCII in the low byte, scan code in the high byte */
typedef struct gem_vdi_resident_input {
	WORD mouse_x;
	WORD mouse_y;
	UWORD mouse_buttons;
	UWORD key_state;
	UWORD key_code;
	UBYTE key_ready;
	UBYTE changed;
} GEM_VDI_RESIDENT_INPUT;

typedef BYTE GEM_VDI_RESIDENT_INPUT_MUST_BE_12_BYTES
	[(sizeof(GEM_VDI_RESIDENT_INPUT) == 12) ? 1 : -1];

/* open the one physical VDI workstation gemaes owns. the devices stay open until gem_vdi_resident_shutdown(), a client workstation never takes over the hardware */
WORD gem_vdi_resident_startup(VOID);

/* install GEM.RSC's first mouse form and balance the hide the workstation open leaves outstanding. call once the AES's own resource is loaded, the shapes live in it not here */
WORD gem_vdi_resident_default_mouse(VOID);

/* return pixel and cell sizes from the physical workstation */
WORD gem_vdi_resident_get_metrics(UWORD *width, UWORD *height,
	UWORD *character_width, UWORD *character_height);

/* same-process hook for the resident AES object/menu drawing modules */
GEM_VDI_SCREEN *gem_vdi_resident_screen(VOID);

/* apply one GRAF_MOUSE change. USER_DEF (255) reads a 74-byte MFORM from the pinned DS, built-in forms and hide/show 256/257 never read the slot */
WORD gem_vdi_resident_mouse(const struct gemtrap_request *request,
	WORD application, WORD number, GEM_BINDINGS_POINTER_SLOT form);

/* draw one length-limited far RSC string at a top-left cell position */
WORD gem_vdi_resident_text(GEM_BINDINGS_POINTER_SLOT text,
	UWORD max_characters, WORD x, WORD y, WORD color);

/* poll the nonblocking input devices once. the returned state is valid when nothing changed, changed is one only for a real mouse change or a complete key, so the owner tick can skip the quick checks when the physical state hasnt moved */
WORD gem_vdi_resident_poll_input(GEM_VDI_RESIDENT_INPUT *input);

WORD gem_vdi_resident_request(struct gemtrap_request *request,
	WORD application);
VOID gem_vdi_resident_release(WORD application);
VOID gem_vdi_resident_shutdown(VOID);

/* release the adapter and input devices around one synchronous full-screen child launch. client workstation records survive, the caller drives the redraw cascade after resume */
WORD gem_vdi_resident_suspend(VOID);
WORD gem_vdi_resident_resume(VOID);

#endif				/* ELKS_GEM_VDI_RESIDENT_H */
