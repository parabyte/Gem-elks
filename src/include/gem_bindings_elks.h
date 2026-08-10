/*
 * gem_bindings_elks.h - 8086 wire ABI for the original GEM bindings
 *
 * the old bindings use far pointers for the AES/VDI param block. an ia16 near
 * pointer is one 16-bit offset which would shrink every slot, so these keep
 * the original two-word offset/segment form
 */

#ifndef ELKS_GEM_BINDINGS_ELKS_H
#define ELKS_GEM_BINDINGS_ELKS_H

#include "aes.h"

/* lo is the offset, hi the segment. storing a near pointer fills in the current data segment, 0:0 is the only null */
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

/* size checks guard the INT EF ABI against packing changes */
typedef BYTE GEM_BINDINGS_AESPB_MUST_BE_24_BYTES
	[(sizeof(GEM_BINDINGS_AESPB) == 24) ? 1 : -1];
typedef BYTE GEM_BINDINGS_VDIPB_MUST_BE_20_BYTES
	[(sizeof(GEM_BINDINGS_VDIPB) == 20) ? 1 : -1];

typedef WORD (*AESFUNC)(GEM_BINDINGS_AESPB *parameter_block);
typedef WORD (*VDIFUNC)(GEM_BINDINGS_VDIPB *parameter_block);

UWORD gem_bindings_data_segment(VOID);
VOID gem_bindings_store_pointer(GEM_BINDINGS_POINTER_SLOT *slot,
	const VOID FAR * pointer);
GEM_SLOT_POINTER gem_bindings_load_pointer(const GEM_BINDINGS_POINTER_SLOT
	*slot);
UWORD gem_bindings_far_word(UWORD segment, UWORD offset);
UBYTE gem_bindings_far_byte(UWORD segment, UWORD offset);

/* return the AES resource address without squeezing its segment into a near pointer, the old rsrc_gaddr() ABI stays as it is */
WORD rsrc_gaddr_far(WORD rstype, WORD rsid, GEM_BINDINGS_POINTER_SLOT *address);

VOID gem_bindings_vdi_ensure(VOID);
VOID gem_bindings_vdi_set_slot(GEM_BINDINGS_POINTER_SLOT *slot,
	const VOID FAR * pointer);

WORD gem(GEM_BINDINGS_AESPB *parameter_block);
WORD vdi(VOID);
WORD aescheck(VOID);
WORD gemcheck(VOID);

/* send every gem()/vdi() call to an in-process dispatcher instead of the INT EF trap, so no trap ever runs */
AESFUNC divert_aes(AESFUNC function);
VDIFUNC divert_vdi(VDIFUNC function);

/* the GEM/XM process-manager wrappers deal in DOS arena ownership, no use on ELKS, kept for reference but never compiled in */
#ifndef GEM_BINDINGS_ENABLE_DOS_PROCESS
#define GEM_BINDINGS_ENABLE_DOS_PROCESS 0
#endif

#if GEM_BINDINGS_ENABLE_DOS_PROCESS
#include "gem_proc.h"

/* opcode 64 is a GEM/XM arena call, ELKS has nothing like it */
GEM_BINDINGS_POINTER_SLOT proc_malloc(GEM_BINDINGS_POINTER_SLOT size,
	GEM_BINDINGS_POINTER_SLOT *actual_size);
#endif

#endif				/* ELKS_GEM_BINDINGS_ELKS_H */
