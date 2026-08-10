/*
 * gem_form_internal.h - shared internals of the AES form managers
 *
 * the form code is split across three files: gem_form_resident.c is the form
 * manager itself, gem_alert_resident.c is the alert, gem_fsel_resident.c is the
 * file selector. all three drive the same interaction records and read trees
 * the same way, so the shared pieces sit here instead of being duplicated or
 * made public.
 *
 * nothing outside those three files should include this header, the published
 * interface is gem_form_resident.h
 */

#ifndef ELKS_GEM_FORM_INTERNAL_H
#define ELKS_GEM_FORM_INTERNAL_H

#include "gem_form_resident.h"

#include "gem_aes_call.h"
#include "gem_resource_resident.h"

#include "gem_vdi_resident.h"
#define GEM_FORM_FAR __far

#define GEM_FORM_PD_FREE                 0U
#define GEM_FORM_PD_WAITING              1U
#define GEM_FORM_PD_READY                2U

#define GEM_FORM_KIND_NONE               0U
#define GEM_FORM_KIND_DO                 1U
#define GEM_FORM_KIND_ALERT              2U
#define GEM_FORM_KIND_ERROR              3U
#define GEM_FORM_KIND_FSEL               4U

#define GEM_FORM_NIL                     (-1)
#define GEM_FORM_ROOT                    0
#define GEM_FORM_MAX_DEPTH               8U
#define GEM_FORM_OBJECT_BYTES            24U
#define GEM_FORM_TEDINFO_BYTES           28U
#define GEM_FORM_MAX_EDIT_TEXT           128U
#define GEM_FORM_MAX_ALERT_SOURCE        256U

#define GEM_FORM_FORWARD                 0U
#define GEM_FORM_BACKWARD                1U
#define GEM_FORM_DEFLT                   2U

#define GEM_FORM_LEFT_BUTTON             0x0001U
#define GEM_FORM_HIGH_OBJECT             0x8000U
#define GEM_FORM_OBJECT_MASK             0x7fffU

/* the BIOS key codes the form editor uses */
#define GEM_FORM_KEY_ESCAPE              0x011bU
#define GEM_FORM_KEY_BACKSPACE           0x0e08U
#define GEM_FORM_KEY_TAB                 0x0f09U
#define GEM_FORM_KEY_BACKTAB             0x0f00U
#define GEM_FORM_KEY_RETURN              0x1c0dU
#define GEM_FORM_KEY_SPACE               0x3920U
#define GEM_FORM_KEY_UP                  0x4800U
#define GEM_FORM_KEY_LEFT                0x4b00U
#define GEM_FORM_KEY_RIGHT               0x4d00U
#define GEM_FORM_KEY_DOWN                0x5000U
#define GEM_FORM_KEY_DELETE              0x5300U

#define GEM_FORM_G_BOX                   20U
#define GEM_FORM_G_BOXCHAR               27U
#define GEM_FORM_G_IMAGE                 23U
#define GEM_FORM_G_BUTTON                26U
#define GEM_FORM_G_STRING                28U
#define GEM_FORM_G_TEXT                  21U
#define GEM_FORM_G_BOXTEXT               22U
#define GEM_FORM_G_FTEXT                 29U
#define GEM_FORM_G_FBOXTEXT              30U

#define GEM_FORM_NORMAL                  0x0000U
#define GEM_FORM_SELECTED                0x0001U
#define GEM_FORM_DISABLED                0x0008U
#define GEM_FORM_OUTLINED                0x0010U
#define GEM_FORM_SHADOWED                0x0020U

#define GEM_FORM_SELECTABLE              0x0001U
#define GEM_FORM_DEFAULT                 0x0002U
#define GEM_FORM_EXIT                    0x0004U
#define GEM_FORM_EDITABLE                0x0008U
#define GEM_FORM_RBUTTON                 0x0010U
#define GEM_FORM_LASTOB                  0x0020U
#define GEM_FORM_TOUCHEXIT               0x0040U
#define GEM_FORM_HIDETREE                0x0080U
#define GEM_FORM_INDIRECT                0x0100U
#define GEM_FORM_ESCCANCEL               0x0200U
#define GEM_FORM_SCROLLER                0x0800U
#define GEM_FORM_HIGHLIGHTED             0x0100U

#define GEM_FORM_ALERT_ICON              1U
#define GEM_FORM_ALERT_FIRST_MESSAGE     2U
#define GEM_FORM_ALERT_FIRST_BUTTON      7U

typedef struct gem_form_context {
	const GEM_RESOURCE_RESIDENT *resource;
	GEM_BINDINGS_POINTER_SLOT tree;
	OBJECT GEM_FORM_FAR *objects;
	UWORD object_count;
	UWORD client_segment;
	UWORD client_limit;
	UWORD screen_width;
	UWORD screen_height;
	UWORD character_width;
	UWORD character_height;
} GEM_FORM_CONTEXT;

typedef struct gem_form_pd {
	GEM_FORM_CONTEXT context;
	UWORD generation_lo;
	UWORD generation_hi;
	UWORD result;
	WORD edit_object;
	WORD next_object;
	WORD saved_default;
	WORD pressed_object;
	UWORD edit_index;
	UWORD pressed_state;
	UWORD previous_buttons;
	UBYTE owner;
	UBYTE state;
	UBYTE kind;
	UBYTE tree_kind;
	GEM_FORM_EFFECTS ready_effects;
} GEM_FORM_PD;

/* --- the interaction records, owned by gem_form_resident.c --- */

GEM_FORM_PD *gem_form_pd_at(UWORD owner);
VOID gem_form_clear_pd(GEM_FORM_PD * pd);
VOID gem_form_clear_effects(GEM_FORM_EFFECTS *effects);
VOID gem_form_finish(GEM_FORM_PD * pd, UWORD result);
WORD gem_form_switch_field(GEM_FORM_PD * pd, WORD object);

/* --- reading a tree, wherever it lives --- */

VOID gem_form_clear_context(GEM_FORM_CONTEXT * context);
WORD gem_form_copy_context(GEM_FORM_CONTEXT * destination,
	const GEM_FORM_CONTEXT * source);
WORD gem_form_open_tree(GEM_FORM_CONTEXT * context, const GEM_FORM_CALL *call,
	GEM_BINDINGS_POINTER_SLOT tree);
WORD gem_form_available(const GEM_FORM_CONTEXT * context,
	GEM_BINDINGS_POINTER_SLOT address, UWORD *available);
VOID GEM_FORM_FAR *gem_form_pointer(const GEM_FORM_CONTEXT * context,
	GEM_BINDINGS_POINTER_SLOT address, UWORD count);
OBJECT GEM_FORM_FAR *gem_form_object_at(const GEM_FORM_CONTEXT * context,
	UWORD object);
WORD gem_form_tedinfo(const GEM_FORM_CONTEXT * context, UWORD object_index,
	TEDINFO *tedinfo);

/* --- geometry --- */

UWORD gem_form_cells(UWORD count, UWORD size);
WORD gem_form_center_tree(GEM_FORM_CONTEXT * context, UWORD *output);
WORD gem_form_inside(WORD x, WORD y, WORD left, WORD top, WORD width,
	WORD height);
VOID gem_form_metrics(const GEM_FORM_CALL *call, UWORD *screen_width,
	UWORD *screen_height, UWORD *character_width, UWORD *character_height);

/* --- the alert, in gem_alert_resident.c --- */

WORD gem_form_begin_alert(const GEM_FORM_CALL *call, UWORD kind,
	UWORD default_button, GEM_FORM_EFFECTS *effects);
VOID gem_form_alert_input(GEM_FORM_PD * pd, const GEM_FORM_INPUT *input,
	GEM_FORM_EFFECTS *effects);
VOID gem_form_alert_rectangle(GEM_FORM_RECTANGLE *rectangle);
WORD gem_form_alert_source_from_call(const GEM_FORM_CALL *call,
	GEM_BINDINGS_POINTER_SLOT address);
WORD gem_form_alert_source_string(UWORD index, UWORD parameter);
OBJECT GEM_FORM_FAR *gem_form_alert_object_at(UWORD object);
UBYTE *gem_form_alert_string_at(UWORD index);
VOID gem_form_clear_alert_strings(VOID);
/* TRUE when this exact generation owns the one alert */
WORD gem_form_alert_owns(UWORD owner, UWORD generation_lo, UWORD generation_hi);
VOID gem_form_alert_release(VOID);
VOID gem_form_alert_reset(VOID);

/* --- the file selector, in gem_fsel_resident.c --- */

WORD gem_form_begin_fsel(const GEM_FORM_CALL *call, GEM_FORM_EFFECTS *effects);
VOID gem_form_fsel_progress(GEM_FORM_PD * pd, GEM_FORM_EFFECTS *effects);
VOID gem_form_fsel_result(BYTE *path, UWORD path_size, BYTE *name,
	UWORD name_size);
/* where the last sample was, for the slider's page-by-a-windowful */
VOID gem_form_fsel_pointer(WORD y);
/* give up the one selector and report where its answer goes */
VOID gem_form_fsel_release(GEM_BINDINGS_POINTER_SLOT *path,
	GEM_BINDINGS_POINTER_SLOT *name);
VOID gem_form_fsel_reset(VOID);

#endif				/* ELKS_GEM_FORM_INTERNAL_H */
