/*
 * gem_menu_pull_resident.h - original GEM pull-down manager for ELKS AES.
 *
 * The application menu remains the caller's relocated OBJECT tree.  The
 * resident AES does not convert it into widgets or copy it into a second
 * resource format.  Only the historical fixed M_DESK accessory tree is
 * retained locally, exactly as GEMMNLIB.C did.
 *
 * Drawing is reported as a short ordered effect list.  The physical VDI
 * owner applies SAVE, DRAW and RESTORE effects after validating the pinned
 * request.  Message delivery is likewise explicit, allowing the existing
 * APPL queue to remain the only message owner.  This makes menu tracking
 * nonblocking: one mouse/key sample enters and at most one message leaves.
 *
 * All coordinates have scale one pixel.  Names are fixed 22-byte GEM Desk
 * strings including their terminating NUL.  No value is scaled or rounded.
 */

#ifndef ELKS_GEM_MENU_PULL_RESIDENT_H
#define ELKS_GEM_MENU_PULL_RESIDENT_H

#if defined(ELKS) && ELKS
#include "gem_resource_resident.h"
#define GEM_MENU_PULL_FAR __far
typedef OBJECT GEM_MENU_PULL_OBJECT;
#else
typedef signed short WORD;
typedef unsigned short UWORD;
typedef unsigned char UBYTE;
#define VOID void
#define TRUE 1
#define FALSE 0
#define GEM_MENU_PULL_FAR

/* Exact host-side image of one original 24-byte PC GEM OBJECT record. */
typedef struct __attribute__((packed)) gem_menu_pull_object {
	WORD ob_next;
	WORD ob_head;
	WORD ob_tail;
	UWORD ob_type;
	UWORD ob_flags;
	UWORD ob_state;
	struct __attribute__((packed)) {
		UWORD lo;
		UWORD hi;
	} ob_spec;
	UWORD ob_x;
	UWORD ob_y;
	UWORD ob_width;
	UWORD ob_height;
} GEM_MENU_PULL_OBJECT;

typedef char GEM_MENU_PULL_HOST_WORD_MUST_BE_TWO_BYTES
	[(sizeof(UWORD) == 2) ? 1 : -1];
typedef char GEM_MENU_PULL_HOST_OBJECT_MUST_BE_24_BYTES
	[(sizeof(GEM_MENU_PULL_OBJECT) == 24) ? 1 : -1];
#endif

/* Direct selectors and messages from CRYSBIND.H/GEMLIB.H. */
#define GEM_MENU_PULL_ICHECK            31U
#define GEM_MENU_PULL_IENABLE           32U
#define GEM_MENU_PULL_TNORMAL           33U
#define GEM_MENU_PULL_TEXT              34U
#define GEM_MENU_PULL_REGISTER          35U
#define GEM_MENU_PULL_UNREGISTER        36U
#define GEM_MENU_PULL_CLICK             37U
#define GEM_MENU_PULL_MN_SELECTED       10U
#define GEM_MENU_PULL_AC_OPEN           40U

/* Original GEM/XM and GEMMNLIB.C fixed storage dimensions. */
#define GEM_MENU_PULL_ACCESSORIES       17U
#define GEM_MENU_PULL_NAME_BYTES        22U
#define GEM_MENU_PULL_DESK_FIXED        3U
#define GEM_MENU_PULL_DESK_OBJECTS      20U

/* At most eight ordered drawing operations arise from one input sample. */
#define GEM_MENU_PULL_DRAW_EFFECTS      8U

#define GEM_MENU_PULL_TREE_MAIN         0U
#define GEM_MENU_PULL_TREE_DESK         1U

#define GEM_MENU_PULL_DRAW_OBJECT       1U
#define GEM_MENU_PULL_SAVE_AREA         2U
#define GEM_MENU_PULL_DRAW_MENU         3U
#define GEM_MENU_PULL_RESTORE_AREA      4U
#define GEM_MENU_PULL_REDRAW_BAR        5U

/* Original OBJECT bits used without importing a hosted AES header. */
#define GEM_MENU_PULL_SELECTED          0x0001U
#define GEM_MENU_PULL_CHECKED           0x0004U
#define GEM_MENU_PULL_DISABLED          0x0008U
#define GEM_MENU_PULL_LASTOB            0x0020U
#define GEM_MENU_PULL_INDIRECT          0x0100U

/* Original BIOS key words from GEMKEYBD.H. */
#define GEM_MENU_PULL_KEY_ESCAPE        0x011bU
#define GEM_MENU_PULL_KEY_ENTER         0x1c0dU
#define GEM_MENU_PULL_KEY_F1            0x3b00U
#define GEM_MENU_PULL_KEY_F10           0x4400U
#define GEM_MENU_PULL_KEY_UP            0x4800U
#define GEM_MENU_PULL_KEY_LEFT          0x4b00U
#define GEM_MENU_PULL_KEY_RIGHT         0x4d00U
#define GEM_MENU_PULL_KEY_DOWN          0x5000U

typedef UBYTE GEM_MENU_PULL_FAR *GEM_MENU_PULL_BYTE_POINTER;
typedef const UBYTE GEM_MENU_PULL_FAR *GEM_MENU_PULL_TEXT_POINTER;

/*
 * One already-validated view of a relocated original RSC tree.
 *
 * resource points at offset zero of one paragraph segment.  segment is the
 * exact 8086 segment written by RSRC_LOAD into relocated pointer slots.
 * tree_offset and bytes are unscaled byte counts.  The implementation walks
 * object_count records with pointer increments, never multiplication.
 */
typedef struct gem_menu_pull_tree {
	GEM_MENU_PULL_BYTE_POINTER resource;
	UWORD bytes;
	UWORD segment;
	UWORD tree_offset;
	UWORD object_count;
} GEM_MENU_PULL_TREE;

#if defined(ELKS) && ELKS
/*
 * Form a view directly from the sole relocated per-PD RSC segment.  address
 * must be the tree address returned by RSRC_GADDR and object_count must be
 * the local LASTOB extent already retained by MENU_BAR validation.
 */
WORD gem_menu_pull_resident_tree_from_resource(GEM_MENU_PULL_TREE *view,
	const GEM_RESOURCE_RESIDENT *resource, GEM_FAR_ADDRESS address,
	UWORD object_count);
#endif

typedef struct gem_menu_pull_rectangle {
	WORD x;
	WORD y;
	WORD width;
	WORD height;
} GEM_MENU_PULL_RECTANGLE;

/* One draw command in the exact order required by GEMMNLIB.C mn_do(). */
typedef struct gem_menu_pull_draw_effect {
	UBYTE action;
	UBYTE tree_kind;
	WORD object;
	UWORD state;
	GEM_MENU_PULL_RECTANGLE rectangle;
} GEM_MENU_PULL_DRAW_EFFECT;

/*
 * Effects contain no client pointer.  A generated message is always eight
 * original GEM words and carries a generation-safe ELKS destination.
 */
typedef struct gem_menu_pull_effects {
	GEM_MENU_PULL_DRAW_EFFECT draw[GEM_MENU_PULL_DRAW_EFFECTS];
	UWORD target_owner;
	UWORD target_generation_lo;
	UWORD target_generation_hi;
	UWORD message[8];
	UBYTE draw_count;
	UBYTE redraw_all;
	UBYTE message_ready;
	UBYTE consume_mouse;
	UBYTE consume_key;
} GEM_MENU_PULL_EFFECTS;

/* One original AES menu call after the resident boundary resolves pointers. */
typedef struct gem_menu_pull_call {
	UWORD owner;
	UWORD generation_lo;
	UWORD generation_hi;
	const UWORD *control;
	const UWORD *int_in;
	UWORD *int_out;
	GEM_MENU_PULL_TREE tree;
	GEM_MENU_PULL_TEXT_POINTER text;
	UWORD text_limit;
} GEM_MENU_PULL_CALL;

/* One nonblocking physical input sample from the resident VDI owner. */
typedef struct gem_menu_pull_input {
	WORD mouse_x;
	WORD mouse_y;
	UWORD mouse_buttons;
	UWORD key_code;
	UBYTE key_ready;
} GEM_MENU_PULL_INPUT;

/*
 * A synthetic M_DESK object snapshot for the drawing adapter.  text is
 * copied from the original Information/separator strings or a fixed Desk
 * registration slot.  It is not a converted application resource.
 */
typedef struct gem_menu_pull_desk_object {
	WORD next;
	WORD head;
	WORD tail;
	UWORD type;
	UWORD flags;
	UWORD state;
	GEM_MENU_PULL_RECTANGLE rectangle;
	UBYTE text[GEM_MENU_PULL_NAME_BYTES];
} GEM_MENU_PULL_DESK_OBJECT;

/* Clear active tracking, click preference and all fixed accessory slots. */
VOID gem_menu_pull_resident_reset(VOID);

/* Install/remove the AES-wide gl_mntree equivalent, generation safely. */
WORD gem_menu_pull_resident_activate(const GEM_MENU_PULL_TREE *tree,
	UWORD owner, UWORD generation_lo, UWORD generation_hi,
	GEM_MENU_PULL_EFFECTS *effects);
WORD gem_menu_pull_resident_deactivate(UWORD owner, UWORD generation_lo,
	UWORD generation_hi, GEM_MENU_PULL_EFFECTS *effects);

/*
 * Process MENU_ICHECK through MENU_CLICK.  Unknown selectors set *handled
 * FALSE without touching caller arrays.  Known malformed calls return FALSE
 * and still set *handled TRUE, matching the resident event manager contract.
 */
WORD gem_menu_pull_resident_dispatch(const GEM_MENU_PULL_CALL *call,
	GEM_MENU_PULL_EFFECTS *effects, WORD *handled);

/* Feed one sample; TRUE means the AES control manager consumed it. */
WORD gem_menu_pull_resident_input(const GEM_MENU_PULL_INPUT *input,
	GEM_MENU_PULL_EFFECTS *effects);

/* Remove this exact process generation from active-menu and Desk ownership. */
VOID gem_menu_pull_resident_detach(UWORD owner, UWORD generation_lo,
	UWORD generation_hi, GEM_MENU_PULL_EFFECTS *effects);

/* Copy one current synthetic M_DESK object and its bounded display text. */
WORD gem_menu_pull_resident_desk_object(UWORD object,
	GEM_MENU_PULL_DESK_OBJECT *snapshot);
UWORD gem_menu_pull_resident_desk_count(VOID);

#endif /* ELKS_GEM_MENU_PULL_RESIDENT_H */
