/*
 * gem_form_resident.h - original GEM form manager for resident ELKS AES.
 *
 * FORM_DO and alerts historically waited inside AES while GEM's control
 * manager owned the mouse, keyboard and update lock.  The ELKS owner cannot
 * block inside one delivered INT EF request, so this interface retains the
 * original form criteria for one logical GEM PD and advances it from one
 * physical input sample at a time.  The delivered kernel request remains the
 * wait block; no helper process, wrapper, converted widget tree or heap object
 * is introduced.
 *
 * Application forms remain their exact original OBJECT/TEDINFO/string records
 * in either the caller's pinned data segment or its relocated RSC segment.
 * Alert text is parsed into GEMFMALT.C's historical fixed ten-object alert
 * tree.  Ordered effects tell the outer owner to use its existing object
 * renderer, update lock and desktop/window repaint path.
 *
 * Coordinates and dimensions have scale one pixel.  Every scalar is one byte
 * or one 8086 word.  Centering truncates halves toward the upper/left edge,
 * exactly as integer GEM arithmetic did; no multiply, divide, floating point,
 * recursion or dynamic allocation crosses this interface.
 */

#ifndef ELKS_GEM_FORM_RESIDENT_H
#define ELKS_GEM_FORM_RESIDENT_H

#include "gem_object_resident.h"

/* Original GEM/XM has twelve logical process channels. */
#define GEM_FORM_PD_COUNT                 12U

/* Direct selectors from CRYSBIND.H and the PPDG000.C array contracts. */
#define GEM_FORM_OBJC_EDIT                46U
#define GEM_FORM_DO                       50U
#define GEM_FORM_DIAL                     51U
#define GEM_FORM_ALERT                    52U
#define GEM_FORM_ERROR                    53U
#define GEM_FORM_CENTER                   54U
#define GEM_FORM_KEYBD                    55U
#define GEM_FORM_BUTTON                   56U

/* This value is shared with the event/GRAF retained-request protocols. */
#define GEM_FORM_RESIDENT_DEFERRED        (-32768)
#define GEM_FORM_OWNER_NONE               0xffffU

/* Original form-dial and object-edit kinds. */
#define GEM_FORM_FMD_START                0U
#define GEM_FORM_FMD_GROW                 1U
#define GEM_FORM_FMD_SHRINK               2U
#define GEM_FORM_FMD_FINISH               3U
#define GEM_FORM_EDSTART                  0U
#define GEM_FORM_EDINIT                   1U
#define GEM_FORM_EDCHAR                   2U
#define GEM_FORM_EDEND                    3U

/* The fixed GEMFMALT.C alert shape: root, icon, five rows, three buttons. */
#define GEM_FORM_ALERT_OBJECTS            10U
#define GEM_FORM_ALERT_MESSAGE_ROWS       5U
#define GEM_FORM_ALERT_BUTTONS            3U
#define GEM_FORM_ALERT_TEXT_BYTES         41U

#define GEM_FORM_TREE_NONE                0U
#define GEM_FORM_TREE_CALLER              1U
#define GEM_FORM_TREE_ALERT               2U

/* FORM_DO/FORM_ALERT return one original int_out result word. */
#define GEM_FORM_OUTPUT_WORDS             1U

typedef struct gem_form_rectangle {
	WORD x;
	WORD y;
	WORD width;
	WORD height;
} GEM_FORM_RECTANGLE;

/*
 * One call after XIF copied the classic arrays from a pinned client.
 * resident_segment is the trusted AES DS used only for the module's fixed
 * alert tree; application addr_in values are never accepted in that segment.
 */
typedef struct gem_form_call {
	UWORD owner;
	UWORD generation_lo;
	UWORD generation_hi;
	const GEM_RESOURCE_RESIDENT *resource;
	UWORD client_segment;
	UWORD client_limit;
	UWORD resident_segment;
	const UWORD *control;
	const UWORD *int_in;
	UWORD *int_out;
	const GEM_BINDINGS_POINTER_SLOT *addr_in;
} GEM_FORM_CALL;

/* One nonblocking physical input sample supplied by the resident VDI owner. */
typedef struct gem_form_input {
	UWORD owner;
	UWORD generation_lo;
	UWORD generation_hi;
	WORD mouse_x;
	WORD mouse_y;
	UWORD mouse_buttons;
	UWORD key_code;
	UWORD key_state;
	UWORD clicks;
	UBYTE key_ready;
} GEM_FORM_INPUT;

/*
 * Drawing remains centralized.  draw_tree names the caller or fixed alert
 * tree for gem_resident_draw_object_tree(); redraw_background asks the outer
 * owner to repaint the exposed Desktop, W_ACTIVE frames and menu bar after
 * FMD_FINISH or alert dismissal.  begin/end_update use the authoritative
 * WIND_UPDATE recursion owned by gem_startup_resident.
 */
typedef struct gem_form_effects {
	GEM_BINDINGS_POINTER_SLOT tree;
	GEM_FORM_RECTANGLE rectangle;
	UWORD resident_segment;
	UBYTE tree_kind;
	UBYTE begin_update;
	UBYTE end_update;
	UBYTE draw_tree;
	UBYTE redraw_background;
} GEM_FORM_EFFECTS;

typedef struct gem_form_completion {
	UWORD owner;
	UWORD generation_lo;
	UWORD generation_hi;
	UWORD output_count;
	UWORD int_out[GEM_FORM_OUTPUT_WORDS];
	GEM_FORM_EFFECTS effects;
} GEM_FORM_COMPLETION;

/* Clear all fixed PD interaction state and the one historical alert tree. */
VOID gem_form_resident_reset(VOID);

/*
 * Dispatch OBJC_EDIT and FORM_DO through FORM_BUTTON.  Interactive FORM_DO,
 * FORM_ALERT and FORM_ERROR calls return DEFERRED; all other selectors finish
 * immediately.  Unknown selectors leave handled FALSE and caller arrays
 * untouched.  Known malformed calls set handled TRUE and return FALSE.
 */
WORD gem_form_resident_dispatch(const GEM_FORM_CALL *call,
	GEM_FORM_EFFECTS *effects, WORD *handled);

/*
 * Feed one physical sample.  Alert press/release highlighting may request one
 * fixed-tree redraw in effects; caller forms redraw through the authoritative
 * object manager directly.  TRUE means an active form consumed the sample.
 */
WORD gem_form_resident_input(const GEM_FORM_INPUT *input,
	GEM_FORM_EFFECTS *effects);

/* Return and clear one ready retained call for this exact PD generation. */
WORD gem_form_resident_service(UWORD owner, UWORD generation_lo,
	UWORD generation_hi, GEM_FORM_COMPLETION *completion);

/* Cancel a reused/exiting generation and balance any form-owned update lock. */
VOID gem_form_resident_detach(UWORD owner, UWORD generation_lo,
	UWORD generation_hi, GEM_FORM_EFFECTS *effects);

/* TRUE only while this exact generation owns a deferred form interaction. */
WORD gem_form_resident_waiting(UWORD owner, UWORD generation_lo,
	UWORD generation_hi);

/*
 * Focused proof/introspection seam for GEMFMALT.C's fixed resident alert tree.
 * The returned records are the records rendered by the target, not a second
 * representation.  Text is copied only when the caller supplies a buffer.
 */
WORD gem_form_resident_alert_object(UWORD object, OBJECT *snapshot);
WORD gem_form_resident_alert_text(UWORD string_index, UBYTE *text,
	UWORD text_bytes);

#endif /* ELKS_GEM_FORM_RESIDENT_H */
