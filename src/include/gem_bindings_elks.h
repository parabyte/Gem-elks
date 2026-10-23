/* direct stock ELKS binding ABI */

#ifndef ELKS_GEM_BINDINGS_ELKS_H
#define ELKS_GEM_BINDINGS_ELKS_H

#include "aes.h"

/* offset first, segment second */
typedef GEM_U32_WORDS GEM_BINDINGS_POINTER_SLOT;

typedef struct __attribute__((packed)) gem_bindings_aespb {
	GEM_BINDINGS_POINTER_SLOT control;
	GEM_BINDINGS_POINTER_SLOT global;
	GEM_BINDINGS_POINTER_SLOT intin;
	GEM_BINDINGS_POINTER_SLOT intout;
	GEM_BINDINGS_POINTER_SLOT addrin;
	GEM_BINDINGS_POINTER_SLOT addrout;
} GEM_BINDINGS_AESPB;

typedef struct __attribute__((packed)) gem_bindings_vdipb {
	GEM_BINDINGS_POINTER_SLOT contrl;
	GEM_BINDINGS_POINTER_SLOT intin;
	GEM_BINDINGS_POINTER_SLOT ptsin;
	GEM_BINDINGS_POINTER_SLOT intout;
	GEM_BINDINGS_POINTER_SLOT ptsout;
} GEM_BINDINGS_VDIPB;

/* keep the wire layout fixed */
typedef BYTE GEM_BINDINGS_AESPB_MUST_BE_24_BYTES
	[(sizeof(GEM_BINDINGS_AESPB) == 24) ? 1 : -1];
typedef BYTE GEM_BINDINGS_VDIPB_MUST_BE_20_BYTES
	[(sizeof(GEM_BINDINGS_VDIPB) == 20) ? 1 : -1];

UWORD gem_bindings_data_segment(VOID);
VOID gem_bindings_store_pointer(GEM_BINDINGS_POINTER_SLOT *slot,
	const VOID FAR * pointer);
GEM_SLOT_POINTER gem_bindings_load_pointer(const GEM_BINDINGS_POINTER_SLOT
	*slot);
/* keep the full resource pointer */
WORD rsrc_gaddr_far(WORD rstype, WORD rsid, GEM_BINDINGS_POINTER_SLOT *address);

VOID gem_bindings_vdi_ensure(VOID);
VOID gem_bindings_vdi_set_slot(GEM_BINDINGS_POINTER_SLOT *slot,
	const VOID FAR * pointer);

WORD gem(GEM_BINDINGS_AESPB *parameter_block);
WORD vdi(VOID);
WORD aescheck(VOID);
WORD gemcheck(VOID);

#endif				/* ELKS_GEM_BINDINGS_ELKS_H */
