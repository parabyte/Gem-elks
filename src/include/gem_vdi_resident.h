/*
 * gem_vdi_resident.h - shared VDI dispatcher for resident ELKS GEM
 *
 * the entry point takes the original CX=0473h, DS:DX parameter block.
 * application is the AES PD/channel tag, a 16-bit owner label. ELKS owns tasks
 * and memory
 */

#ifndef ELKS_GEM_VDI_RESIDENT_H
#define ELKS_GEM_VDI_RESIDENT_H

#include "gem_request.h"

#include "aes.h"
#include "gem_bindings_elks.h"
#include "vdi.h"

#define GEM_VDI_RESIDENT_SELECTOR	0x0473U
#define GEM_VDI_RESIDENT_WORKSTATIONS	8
#define GEM_VDI_RESIDENT_TEXT_MAX	80U

/* one physical GEM input sample */
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

/* open the resident physical workstation */
WORD gem_vdi_resident_startup(VOID);

/* install GEM.RSC's default mouse form */
WORD gem_vdi_resident_default_mouse(VOID);

/* return pixel and cell sizes from the physical workstation */
WORD gem_vdi_resident_get_metrics(UWORD *width, UWORD *height,
	UWORD *character_width, UWORD *character_height);

/* same-process hook for the resident AES object/menu drawing modules */
GEM_VDI_SCREEN *gem_vdi_resident_screen(VOID);

/* apply one GRAF_MOUSE change */
WORD gem_vdi_resident_mouse(const struct gem_request *request,
	WORD application, WORD number, GEM_BINDINGS_POINTER_SLOT form);

/* draw one length-limited far RSC string at a top-left cell position */
WORD gem_vdi_resident_text(GEM_BINDINGS_POINTER_SLOT text,
	UWORD max_characters, WORD x, WORD y, WORD color);

/* poll the nonblocking input devices once */
WORD gem_vdi_resident_poll_input(GEM_VDI_RESIDENT_INPUT *input);

WORD gem_vdi_resident_request(struct gem_request *request,
	WORD application);
VOID gem_vdi_resident_release(WORD application);
VOID gem_vdi_resident_shutdown(VOID);

/* release devices around a full-screen child */
WORD gem_vdi_resident_suspend(VOID);
WORD gem_vdi_resident_resume(VOID);

#endif				/* ELKS_GEM_VDI_RESIDENT_H */
