/*
 * gem_menu_pull_resident.h - GEM pull-down manager for the ELKS AES
 *
 * the app menu stays the caller's relocated OBJECT tree, only the fixed Desk
 * accessory menu is kept locally. drawing comes out as a short ordered effect
 * list the VDI owner applies, messages go through the APPL queue, so menu
 * tracking is nonblocking: one sample in, at most one message out.
 *
 * coords are pixels. names are fixed 22-byte GEM Desk strings including the NUL
 */

#ifndef ELKS_GEM_MENU_PULL_RESIDENT_H
#define ELKS_GEM_MENU_PULL_RESIDENT_H

#include "gem_resource_resident.h"
#define GEM_MENU_PULL_FAR __far
typedef OBJECT GEM_MENU_PULL_OBJECT;

/* the GEM menu selectors and messages */
#define GEM_MENU_PULL_ICHECK            31U
#define GEM_MENU_PULL_IENABLE           32U
#define GEM_MENU_PULL_TNORMAL           33U
#define GEM_MENU_PULL_TEXT              34U
#define GEM_MENU_PULL_REGISTER          35U
#define GEM_MENU_PULL_UNREGISTER        36U
#define GEM_MENU_PULL_CLICK             37U
#define GEM_MENU_PULL_MN_SELECTED       10U
#define GEM_MENU_PULL_AC_OPEN           40U

/* fixed storage sizes */
#define GEM_MENU_PULL_ACCESSORIES       17U
#define GEM_MENU_PULL_NAME_BYTES        22U
#define GEM_MENU_PULL_DESK_FIXED        3U
#define GEM_MENU_PULL_DESK_OBJECTS      20U

/* one input sample can produce at most eight ordered drawing ops */
#define GEM_MENU_PULL_DRAW_EFFECTS      8U

#define GEM_MENU_PULL_TREE_MAIN         0U
#define GEM_MENU_PULL_TREE_DESK         1U

#define GEM_MENU_PULL_DRAW_OBJECT       1U
#define GEM_MENU_PULL_SAVE_AREA         2U
#define GEM_MENU_PULL_DRAW_MENU         3U
#define GEM_MENU_PULL_RESTORE_AREA      4U
#define GEM_MENU_PULL_REDRAW_BAR        5U

/* original OBJECT bits, so no hosted AES header is needed */
#define GEM_MENU_PULL_SELECTED          0x0001U
#define GEM_MENU_PULL_CHECKED           0x0004U
#define GEM_MENU_PULL_DISABLED          0x0008U
#define GEM_MENU_PULL_LASTOB            0x0020U
#define GEM_MENU_PULL_INDIRECT          0x0100U

/* BIOS key codes */
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

/* one checked view of a relocated RSC tree. segment is the 8086 segment RSRC_LOAD wrote into the relocated pointer slots, tree_offset and bytes are byte counts */
typedef struct gem_menu_pull_tree {
	GEM_MENU_PULL_BYTE_POINTER resource;
	UWORD bytes;
	UWORD segment;
	UWORD tree_offset;
	UWORD object_count;
} GEM_MENU_PULL_TREE;

/* build a view from the relocated per-PD RSC segment. address is the tree address RSRC_GADDR returned, object_count is the local LASTOB extent from the MENU_BAR check */
WORD gem_menu_pull_resident_tree_from_resource(GEM_MENU_PULL_TREE *view,
	const GEM_RESOURCE_RESIDENT *resource, GEM_FAR_ADDRESS address,
	UWORD object_count);

typedef struct gem_menu_pull_rectangle {
	WORD x;
	WORD y;
	WORD width;
	WORD height;
} GEM_MENU_PULL_RECTANGLE;

/* one draw command, in the order menu tracking needs */
typedef struct gem_menu_pull_draw_effect {
	UBYTE action;
	UBYTE tree_kind;
	WORD object;
	UWORD state;
	GEM_MENU_PULL_RECTANGLE rectangle;
} GEM_MENU_PULL_DRAW_EFFECT;

/* effects carry no client pointer. a generated message is eight GEM words and names a generation-safe ELKS destination */
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

/* one AES menu call, after the resident boundary resolved pointers */
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

/* one nonblocking physical input sample from the resident VDI owner */
typedef struct gem_menu_pull_input {
	WORD mouse_x;
	WORD mouse_y;
	UWORD mouse_buttons;
	UWORD key_code;
	UBYTE key_ready;
} GEM_MENU_PULL_INPUT;

/* synthetic M_DESK object snapshot for the drawing adapter. text comes from the Information/separator strings or a fixed Desk registration slot */
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

/* clear active tracking, the click preference and all fixed accessory slots */
VOID gem_menu_pull_resident_reset(VOID);

/* install or remove the AES-wide active menu, generation-safe */
WORD gem_menu_pull_resident_activate(const GEM_MENU_PULL_TREE *tree,
	UWORD owner, UWORD generation_lo, UWORD generation_hi,
	GEM_MENU_PULL_EFFECTS *effects);
WORD gem_menu_pull_resident_deactivate(UWORD owner, UWORD generation_lo,
	UWORD generation_hi, GEM_MENU_PULL_EFFECTS *effects);

/* handle MENU_ICHECK through MENU_CLICK. unknown selector sets *handled FALSE, a known call with bad args returns FALSE with *handled TRUE, like the event manager */
WORD gem_menu_pull_resident_dispatch(const GEM_MENU_PULL_CALL *call,
	GEM_MENU_PULL_EFFECTS *effects, WORD *handled);

/* feed in one sample, TRUE means the AES control manager used it up */
WORD gem_menu_pull_resident_input(const GEM_MENU_PULL_INPUT *input,
	GEM_MENU_PULL_EFFECTS *effects);

/* drop this exact process generation from active-menu and Desk ownership */
VOID gem_menu_pull_resident_detach(UWORD owner, UWORD generation_lo,
	UWORD generation_hi, GEM_MENU_PULL_EFFECTS *effects);

/* copy one current synthetic M_DESK object and its length-limited text */
WORD gem_menu_pull_resident_desk_object(UWORD object,
	GEM_MENU_PULL_DESK_OBJECT *snapshot);
UWORD gem_menu_pull_resident_desk_count(VOID);

#endif				/* ELKS_GEM_MENU_PULL_RESIDENT_H */
