/*
 * gem_bindings_elks.h - 8086 wire ABI for the original GEM bindings.
 *
 * The Pacific C bindings describe an AES or VDI parameter block with C far
 * pointers.  A GNU ia16 near data pointer is only one 16-bit offset, so using
 * the host C pointer type in the parameter block would silently shrink every
 * slot and move all following fields.  These structures instead retain the
 * original two-word offset/segment representation exactly.
 */

#ifndef ELKS_GEM_BINDINGS_ELKS_H
#define ELKS_GEM_BINDINGS_ELKS_H

#include "aes.h"

/*
 * A pointer slot is an unscaled byte address.  `lo` is the 16-bit offset and
 * `hi` is the 16-bit segment.  Storing a near pointer supplies the current
 * data segment.  0:0 is the sole null representation.  There is no rounding
 * or saturation; each half is copied exactly and 16-bit offset wrap retains
 * normal 8086 segment semantics.
 */
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

/*
 * Compile-time byte-size checks protect the INT EF ABI.  A negative array
 * bound deliberately stops the build if compiler packing ever changes.
 */
typedef BYTE GEM_BINDINGS_AESPB_MUST_BE_24_BYTES
	[(sizeof(GEM_BINDINGS_AESPB) == 24) ? 1 : -1];
typedef BYTE GEM_BINDINGS_VDIPB_MUST_BE_20_BYTES
	[(sizeof(GEM_BINDINGS_VDIPB) == 20) ? 1 : -1];

typedef WORD (*AESFUNC)(GEM_BINDINGS_AESPB *parameter_block);
typedef WORD (*VDIFUNC)(GEM_BINDINGS_VDIPB *parameter_block);

UWORD gem_bindings_data_segment(VOID);
VOID gem_bindings_store_pointer(GEM_BINDINGS_POINTER_SLOT *slot,
				const VOID FAR *pointer);
GEM_SLOT_POINTER gem_bindings_load_pointer(
	const GEM_BINDINGS_POINTER_SLOT *slot);
UWORD gem_bindings_far_word(UWORD segment, UWORD offset);
UBYTE gem_bindings_far_byte(UWORD segment, UWORD offset);

/*
 * Return the original AES resource address without attempting to flatten its
 * segment into a near C pointer.  This additive entry point leaves the legacy
 * near-only rsrc_gaddr() ABI unchanged while original callers are migrated.
 */
WORD rsrc_gaddr_far(WORD rstype, WORD rsid,
		     GEM_BINDINGS_POINTER_SLOT *address);

VOID gem_bindings_vdi_ensure(VOID);
VOID gem_bindings_vdi_set_slot(GEM_BINDINGS_POINTER_SLOT *slot,
			       const VOID FAR *pointer);

WORD gem(GEM_BINDINGS_AESPB *parameter_block);
WORD vdi(VOID);
WORD aescheck(VOID);
WORD gemcheck(VOID);

/*
 * Divert every gem()/vdi() call to an in-process dispatcher instead of the
 * INT EF trap.  The stock-ELKS direct-linked port installs its dispatchers
 * here at startup, so no trap instruction ever executes.
 */
AESFUNC divert_aes(AESFUNC function);
VDIFUNC divert_vdi(VDIFUNC function);

/*
 * GEM/XM process-manager wrappers describe DOS arena ownership.  ELKS owns
 * process address spaces outright and program launch goes through the
 * original single-tasking SHEL_WRITE record, so the imported wrappers
 * remain available for source audit but are never compiled in.
 */
#ifndef GEM_BINDINGS_ENABLE_DOS_PROCESS
#define GEM_BINDINGS_ENABLE_DOS_PROCESS 0
#endif

#if GEM_BINDINGS_ENABLE_DOS_PROCESS
#include "gem_proc.h"

/* Opcode 64 is a GEM/XM arena call and has no ELKS process-seam analogue. */
GEM_BINDINGS_POINTER_SLOT proc_malloc(GEM_BINDINGS_POINTER_SLOT size,
	GEM_BINDINGS_POINTER_SLOT *actual_size);
#endif

#endif /* ELKS_GEM_BINDINGS_ELKS_H */
