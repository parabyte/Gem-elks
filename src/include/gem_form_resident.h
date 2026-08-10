/*
 * gem_form_resident.h - GEM form manager for the resident ELKS AES
 *
 * DOS GEM blocked inside FORM_DO and alerts. the ELKS owner cant block inside
 * one request, so each PD's form stays parked and is pushed along one input
 * sample at a time, the request itself is what waits.
 *
 * app forms stay as their OBJECT/TEDINFO records in the caller's segment.
 * alert text is parsed into a fixed ten-object alert tree. effects tell the
 * outer owner to draw, lock and repaint
 */

#ifndef ELKS_GEM_FORM_RESIDENT_H
#define ELKS_GEM_FORM_RESIDENT_H

#include "gem_object_resident.h"

/* GEM/XM has twelve logical process channels */
#define GEM_FORM_PD_COUNT                 6U

/* the GEM form selectors */
#define GEM_FORM_OBJC_EDIT                46U
#define GEM_FORM_DO                       50U
#define GEM_FORM_DIAL                     51U
#define GEM_FORM_ALERT                    52U
#define GEM_FORM_ERROR                    53U
#define GEM_FORM_CENTER                   54U
#define GEM_FORM_KEYBD                    55U
#define GEM_FORM_BUTTON                   56U
/* the file selector manager, served on the AES's own GEM.RSC tree */
#define GEM_FORM_FSEL_INPUT               90U
#define GEM_FORM_FSEL_EXINPUT             91U

/* same value the event and GRAF parked-request protocols use */
#define GEM_FORM_RESIDENT_DEFERRED        (-32768)
#define GEM_FORM_OWNER_NONE               0xffffU

/* the original form-dial and object-edit kinds */
#define GEM_FORM_FMD_START                0U
#define GEM_FORM_FMD_GROW                 1U
#define GEM_FORM_FMD_SHRINK               2U
#define GEM_FORM_FMD_FINISH               3U
#define GEM_FORM_EDSTART                  0U
#define GEM_FORM_EDINIT                   1U
#define GEM_FORM_EDCHAR                   2U
#define GEM_FORM_EDEND                    3U

/* the fixed alert shape: root, icon, five rows, three buttons */
#define GEM_FORM_ALERT_OBJECTS            10U
#define GEM_FORM_ALERT_MESSAGE_ROWS       5U
#define GEM_FORM_ALERT_BUTTONS            3U
#define GEM_FORM_ALERT_TEXT_BYTES         41U

#define GEM_FORM_TREE_NONE                0U
#define GEM_FORM_TREE_CALLER              1U
#define GEM_FORM_TREE_ALERT               2U
#define GEM_FORM_TREE_SYSTEM              3U

/* the longest path and file name the selector hands back */
#define GEM_FORM_FSEL_PATH_BYTES          80U
#define GEM_FORM_FSEL_NAME_BYTES_OUT      16U

/* FORM_DO and FORM_ALERT return one int_out result word */
#define GEM_FORM_OUTPUT_WORDS             2U

typedef struct gem_form_rectangle {
	WORD x;
	WORD y;
	WORD width;
	WORD height;
} GEM_FORM_RECTANGLE;

/* one call, after the arrays were copied from a pinned client. resident_segment is the trusted AES DS, used only for the fixed alert tree, an app addr_in value is never accepted there */
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

/* one nonblocking physical input sample from the resident VDI owner */
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

/* draw_tree names the caller or fixed alert tree for gem_resident_draw_object_tree(). redraw_background asks for a Desktop/window/menu repaint after FMD_FINISH or a dismissed alert. begin/end_update use the one WIND_UPDATE nesting count owned by gem_startup_resident */
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
	/* FSEL_INPUT also hands back two strings. the caller does the copy since only it still holds the live request, and the two slots were checked against that client when the selector opened */
	UBYTE fsel;
	GEM_BINDINGS_POINTER_SLOT fsel_path;
	GEM_BINDINGS_POINTER_SLOT fsel_name;
} GEM_FORM_COMPLETION;

/* clear all fixed PD interaction state and the alert tree */
VOID gem_form_resident_reset(VOID);

/* the path and file name a finished FSEL_INPUT settled on. only valid for the completion that reported fsel */
VOID gem_form_resident_fsel_result(BYTE *path, UWORD path_size,
	BYTE *name, UWORD name_size);

/* dispatch OBJC_EDIT and FORM_DO through FORM_BUTTON. interactive FORM_DO, FORM_ALERT and FORM_ERROR return DEFERRED, the rest finish at once. unknown selector leaves handled FALSE, a known call with bad args sets handled TRUE and returns FALSE */
WORD gem_form_resident_dispatch(const GEM_FORM_CALL *call,
	GEM_FORM_EFFECTS *effects, WORD *handled);

/* feed in one physical sample. alert highlighting may ask for one fixed-tree redraw in effects. TRUE means an active form used up the sample */
WORD gem_form_resident_input(const GEM_FORM_INPUT *input,
	GEM_FORM_EFFECTS *effects);

/* hand back and clear one ready parked call for this exact PD generation */
WORD gem_form_resident_service(UWORD owner, UWORD generation_lo,
	UWORD generation_hi, GEM_FORM_COMPLETION *completion);

/* cancel a reused or exiting generation and balance any form-held update lock */
VOID gem_form_resident_detach(UWORD owner, UWORD generation_lo,
	UWORD generation_hi, GEM_FORM_EFFECTS *effects);

/* TRUE only while this exact generation owns a deferred form interaction */
WORD gem_form_resident_waiting(UWORD owner, UWORD generation_lo,
	UWORD generation_hi);

#endif				/* ELKS_GEM_FORM_RESIDENT_H */
