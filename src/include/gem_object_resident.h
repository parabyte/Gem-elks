/*
 * gem_object_resident.h - GEM object manager for the resident AES
 *
 * takes the classic AES control/int_in/int_out/addr_in arrays directly. an
 * OBJECT tree stays in its relocated RSC segment or the calling task's pinned
 * data segment. coord addition wraps as an 8086 word, pointer ranges never
 * wrap and are rejected before any far access
 */

#ifndef ELKS_GEM_OBJECT_RESIDENT_H
#define ELKS_GEM_OBJECT_RESIDENT_H

#include "gem_resource_resident.h"
#include "gem_bindings_elks.h"

/* the GEM object opcodes */
#define GEM_OBJECT_OBJC_ADD            40U
#define GEM_OBJECT_OBJC_DELETE         41U
#define GEM_OBJECT_OBJC_DRAW           42U
#define GEM_OBJECT_OBJC_FIND           43U
#define GEM_OBJECT_OBJC_OFFSET         44U
#define GEM_OBJECT_OBJC_ORDER          45U
#define GEM_OBJECT_OBJC_CHANGE         47U

/* object types the direct renderer handles */
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

/* one dispatch, after the arrays were copied. client_segment and client_limit are the pinned request DS and its exclusive byte limit. resident_segment is zero for client calls, the AES passes its own DS only for the resident W_ACTIVE tree and that segment is never accepted from an app trap */
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

/* dispatch OBJC_ADD, DRAW, FIND, OFFSET, ORDER and CHANGE. a known call with bad args returns FALSE with handled TRUE, an unknown selector leaves handled FALSE so the next manager can take it */
WORD gem_object_resident_dispatch(const GEM_OBJECT_RESIDENT_CALL *call,
	WORD *handled);

#endif				/* ELKS_GEM_OBJECT_RESIDENT_H */
