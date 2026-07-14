/*
 * gem_object_resident.h - original GEM object manager for resident AES.
 *
 * The resident entry point consumes the classic AES control, int_in,
 * int_out, and addr_in arrays directly.  An OBJECT tree remains either in
 * its relocated RSC segment or in the calling ELKS task's pinned data
 * segment.  No tree, string, icon, or image is converted into a second UI
 * representation.
 *
 * Coordinates and sizes have scale one pixel.  Object-state and color words
 * retain their original bit encodings.  Coordinate accumulation wraps as an
 * 8086 word, matching GEMOBLIB.C; pointer ranges never wrap and are rejected
 * before a far access.  Every scalar stored by the target interface is one
 * byte or one 8086 word.
 */

#ifndef ELKS_GEM_OBJECT_RESIDENT_H
#define ELKS_GEM_OBJECT_RESIDENT_H

#if defined(ELKS) && ELKS
#include "gem_resource_resident.h"
#include "gem_bindings_elks.h"
#else
/*
 * The native smoke uses layout-identical records.  These definitions are a
 * test seam only; the ELKS build above consumes the project's authoritative
 * GEM ABI declarations.
 */
typedef signed short WORD;
typedef unsigned short UWORD;
typedef signed char BYTE;
typedef unsigned char UBYTE;
#define VOID void
#define TRUE 1
#define FALSE 0
#define NIL (-1)

typedef struct __attribute__((packed)) gem_object_words {
	UWORD lo;
	UWORD hi;
} GEM_FAR_ADDRESS;
typedef GEM_FAR_ADDRESS GEM_BINDINGS_POINTER_SLOT;

typedef struct __attribute__((packed)) gem_object_record {
	WORD ob_next;
	WORD ob_head;
	WORD ob_tail;
	UWORD ob_type;
	UWORD ob_flags;
	UWORD ob_state;
	GEM_FAR_ADDRESS ob_spec;
	UWORD ob_x;
	UWORD ob_y;
	UWORD ob_width;
	UWORD ob_height;
} OBJECT;

typedef struct __attribute__((packed)) gem_object_tedinfo {
	GEM_FAR_ADDRESS te_ptext;
	GEM_FAR_ADDRESS te_ptmplt;
	GEM_FAR_ADDRESS te_pvalid;
	WORD te_font;
	WORD te_junk1;
	WORD te_just;
	WORD te_color;
	WORD te_junk2;
	WORD te_thickness;
	WORD te_txtlen;
	WORD te_tmplen;
} TEDINFO;

typedef struct __attribute__((packed)) gem_object_iconblk {
	GEM_FAR_ADDRESS ib_pmask;
	GEM_FAR_ADDRESS ib_pdata;
	GEM_FAR_ADDRESS ib_ptext;
	WORD ib_char;
	WORD ib_xchar;
	WORD ib_ychar;
	WORD ib_xicon;
	WORD ib_yicon;
	WORD ib_wicon;
	WORD ib_hicon;
	WORD ib_xtext;
	WORD ib_ytext;
	WORD ib_wtext;
	WORD ib_htext;
} ICONBLK;

typedef struct __attribute__((packed)) gem_object_bitblk {
	GEM_FAR_ADDRESS bi_pdata;
	WORD bi_wb;
	WORD bi_hl;
	WORD bi_x;
	WORD bi_y;
	WORD bi_color;
} BITBLK;

typedef struct __attribute__((packed)) gem_object_rshdr {
	UWORD rsh_vrsn;
	UWORD rsh_object;
	UWORD rsh_tedinfo;
	UWORD rsh_iconblk;
	UWORD rsh_bitblk;
	UWORD rsh_frstr;
	UWORD rsh_string;
	UWORD rsh_imdata;
	UWORD rsh_frimg;
	UWORD rsh_trindex;
	UWORD rsh_nobs;
	UWORD rsh_ntree;
	UWORD rsh_nted;
	UWORD rsh_nib;
	UWORD rsh_nbb;
	UWORD rsh_nstring;
	UWORD rsh_nimages;
	UWORD rsh_rssize;
} RSHDR;

typedef struct gem_object_metrics {
	UWORD screen_width;
	UWORD screen_height;
	UWORD character_width;
	UWORD character_height;
	UWORD options;
} GEM_RESOURCE_METRICS;

typedef struct gem_object_storage {
	GEM_FAR_ADDRESS base;
	UWORD bytes;
} GEM_FAR_RESOURCE;

typedef struct gem_object_resource {
	GEM_FAR_RESOURCE storage;
	GEM_RESOURCE_METRICS metrics;
	UWORD flags;
	UBYTE *host_bytes;
} GEM_RESOURCE_RESIDENT;

#define GEM_RESOURCE_RESIDENT_LOADED 0x0001U

typedef struct gem_vdi_rect {
	WORD x;
	WORD y;
	WORD width;
	WORD height;
} GEM_VDI_RECT;

typedef struct gem_vdi_screen {
	WORD xres;
	WORD yres;
	UWORD planes;
	UWORD colors;
	const VOID *driver;
} GEM_VDI_SCREEN;

typedef UWORD GEM_VDI_COLOR;
typedef UWORD GEM_VDI_BITS;
typedef UBYTE GEM_VDI_UBYTE;

#define GEM_VDI_REPLACE 0
#define GEM_VDI_XOR 1
#define GEM_VDI_OR 2
#define GEM_VDI_AND 3

GEM_VDI_SCREEN *gem_vdi_resident_screen(VOID);
WORD gem_vdi_resident_text(GEM_BINDINGS_POINTER_SLOT text,
	UWORD max_characters, WORD x, WORD y, WORD color);
VOID gem_vdi_set_clip(GEM_VDI_SCREEN *screen, WORD count,
	const GEM_VDI_RECT *rectangle);
WORD gem_vdi_set_mode(WORD mode);
GEM_VDI_COLOR gem_vdi_set_foreground(GEM_VDI_SCREEN *screen,
	GEM_VDI_COLOR color);
GEM_VDI_COLOR gem_vdi_set_background(GEM_VDI_SCREEN *screen,
	GEM_VDI_COLOR color);
WORD gem_vdi_set_use_background(WORD enabled);
VOID gem_vdi_rect(GEM_VDI_SCREEN *screen, WORD x, WORD y,
	WORD width, WORD height);
VOID gem_vdi_fill_rect(GEM_VDI_SCREEN *screen, WORD x, WORD y,
	WORD width, WORD height);
VOID gem_vdi_fill_pattern(GEM_VDI_SCREEN *screen, WORD x, WORD y,
	WORD width, WORD height, const GEM_VDI_UBYTE *pattern);
VOID gem_vdi_bitmap(GEM_VDI_SCREEN *screen, WORD x, WORD y,
	WORD width, WORD height, const GEM_VDI_BITS *bits);
VOID gem_vdi_line(GEM_VDI_SCREEN *screen, WORD x1, WORD y1,
	WORD x2, WORD y2, WORD draw_last);
VOID gem_vdi_hide_cursor(GEM_VDI_SCREEN *screen);
VOID gem_vdi_show_cursor(GEM_VDI_SCREEN *screen);
VOID gem_vdi_flush(GEM_VDI_SCREEN *screen);
#endif

/* Direct opcode values and array contracts from PPDG000.C/CRYSBIND.H. */
#define GEM_OBJECT_OBJC_ADD            40U
#define GEM_OBJECT_OBJC_DRAW           42U
#define GEM_OBJECT_OBJC_FIND           43U
#define GEM_OBJECT_OBJC_OFFSET         44U
#define GEM_OBJECT_OBJC_ORDER          45U
#define GEM_OBJECT_OBJC_CHANGE         47U

/* Original object types used by the direct renderer. */
#ifndef G_BOX
#define G_BOX       20
#define G_TEXT      21
#define G_BOXTEXT   22
#define G_IMAGE     23
#define G_USERDEF   24
#define G_IBOX      25
#define G_BUTTON    26
#define G_BOXCHAR   27
#define G_STRING    28
#define G_FTEXT     29
#define G_FBOXTEXT  30
#define G_ICON      31
#define G_TITLE     32
#define G_CICON     33
#define G_DTMFDB    34
#endif

#ifndef ROOT
#define ROOT 0
#endif
#ifndef LASTOB
#define LASTOB      0x0020U
#define HIDETREE    0x0080U
#define INDIRECT    0x0100U
#define DEFAULT     0x0002U
#define EXIT        0x0004U
#define NORMAL      0x0000U
#define SELECTED    0x0001U
#define CROSSED     0x0002U
#define CHECKED     0x0004U
#define DISABLED    0x0008U
#define OUTLINED    0x0010U
#define SHADOWED    0x0020U
#define WHITEBAK    0x0040U
#define DRAW3D      0x0080U
#endif

typedef BYTE GEM_OBJECT_RECORD_MUST_BE_24_BYTES
	[(sizeof(OBJECT) == 24) ? 1 : -1];
typedef BYTE GEM_OBJECT_TEDINFO_MUST_BE_28_BYTES
	[(sizeof(TEDINFO) == 28) ? 1 : -1];
typedef BYTE GEM_OBJECT_ICONBLK_MUST_BE_34_BYTES
	[(sizeof(ICONBLK) == 34) ? 1 : -1];
typedef BYTE GEM_OBJECT_BITBLK_MUST_BE_14_BYTES
	[(sizeof(BITBLK) == 14) ? 1 : -1];

/*
 * One dispatch after XIF has copied the original arrays.  client_segment and
 * client_limit are the pinned request DS and its exclusive byte limit.  They
 * permit ordinary application-built OBJECT trees and dynamically loaded ICN
 * blocks without trusting any unrelated segment.  resident_segment is zero
 * for client calls.  The AES itself may supply its current DS when drawing
 * the original resident W_ACTIVE tree; that trusted segment is bounded at
 * offset ffff and is never accepted from an application trap.
 */
typedef struct gem_object_resident_call {
	const GEM_RESOURCE_RESIDENT *resource;
	UWORD client_segment;
	UWORD client_limit;
	UWORD resident_segment;
	const UWORD *control;
	const UWORD *int_in;
	UWORD *int_out;
	const GEM_BINDINGS_POINTER_SLOT *addr_in;
} GEM_OBJECT_RESIDENT_CALL;

/*
 * Dispatch OBJC_ADD, DRAW, FIND, OFFSET, ORDER, and CHANGE.  A recognized
 * selector sets handled TRUE and writes the exact classic int_out contract.
 * Malformed recognized calls return FALSE.  Unknown selectors are untouched
 * and leave handled FALSE for the next original-manager closure.
 */
WORD gem_object_resident_dispatch(const GEM_OBJECT_RESIDENT_CALL *call,
	WORD *handled);

#endif /* ELKS_GEM_OBJECT_RESIDENT_H */
