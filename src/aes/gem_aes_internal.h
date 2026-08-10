/*
 * gem_aes_internal.h - shared internals of the resident AES core
 *
 * gem_aes_resident.c is the dispatcher and control loop, gem_menu_seam.c is the
 * menu half, which needs the same process descriptors, trap frame arrays and
 * drawing seams.
 *
 * nothing outside those files should include this header, the published
 * interface is gem_aes_resident.h
 */

#ifndef ELKS_GEM_AES_INTERNAL_H
#define ELKS_GEM_AES_INTERNAL_H

#include "gem_aes_resident.h"

#include "gem_pending.h"

#include "gem_event_resident.h"
#include "gem_form_resident.h"
#include "gem_graf_resident.h"
#include "gem_menu_object_resident.h"
#include "gem_menu_pull_resident.h"
#include "gem_proc.h"
#include "gem_resource_resident.h"
#include "gem_window_resident.h"

#define GEM_AES_SELECTOR              200U
#define GEM_AES_ALT_SELECTOR          201U

#define APPL_INIT                     10
#define APPL_READ                     11
#define APPL_WRITE                    12
#define APPL_FIND                     13
#define APPL_TPLAY                    14
#define APPL_TRECORD                  15
#define APPL_YIELD                    17
#define APPL_EXIT                     19

#define MENU_BAR                      30

#define RSRC_LOAD                     110
#define RSRC_FREE                     111
#define RSRC_GADDR                    112
#define RSRC_SADDR                    113
#define RSRC_OBFIX                    114

#define PROC_CREATE                   60
#define PROC_RUN                      61
#define PROC_DELETE                   62
#define PROC_INFO                     63
#define PROC_MALLOC                   64
#define PROC_MFREE                    65
#define PROC_SWITCH                   66
#define PROC_SETBLOCK                 67

/* the local-array sizes */
#define C_SIZE                        5
#define AO_SIZE                       5

#define GEM_RESIDENT_COMMAND_BYTES    256U

/* every GEM PD gets one fixed 128-byte queue */
#define GEM_RESIDENT_QUEUE_BYTES      128U

/* one-plane MFORM: five header words plus 32 row words */
#define GEM_RESIDENT_MFORM_BYTES      74U

/* physical cursor hide/show selectors */
#define GEM_RESIDENT_MOUSE_HIDE       256
#define GEM_RESIDENT_MOUSE_SHOW       257

/* native PC/EGA palette indexes for GEM logical white and black */
#define GEM_RESIDENT_NATIVE_WHITE     15U
#define GEM_RESIDENT_NATIVE_BLACK      0U

/* the application-global resource words */
#define GEM_GLOBAL_TREE_OFFSET        5U
#define GEM_GLOBAL_TREE_SEGMENT       6U
#define GEM_GLOBAL_RESOURCE_OFFSET    7U
#define GEM_GLOBAL_RESOURCE_SEGMENT   8U
#define GEM_GLOBAL_RESOURCE_BYTES     9U

/* keeps rarely-run mutex-wait code out of the nearly full near code segment */
#define GEM_RESIDENT_COLD \
	__far __attribute__((far_section, noinline, \
		section(".fartext.gemresident")))

/* the WIND_UPDATE subselectors used by GRAF trackers */
#define GEM_RESIDENT_END_UPDATE       0U
#define GEM_RESIDENT_BEG_UPDATE       1U

#define GEM_PD_FREE                   0
#define GEM_PD_ATTACHED               1

/* one record per original GEM PD, the tag is the array index */

/* APPL_INIT X_BUF: three near-pointer offsets plus the two-word abilities field. resident pointers cant fit a near offset, so those slots come back zero */
typedef struct __attribute__((packed)) gem_resident_xbuf {
	UWORD buf_len;
	WORD arch;
	UWORD color_categories;
	UWORD active_window_tree;
	UWORD information;
	GEM_U32_WORDS abilities;
} GEM_RESIDENT_XBUF;

typedef BYTE GEM_RESIDENT_XBUF_MUST_BE_14_BYTES
	[(sizeof(GEM_RESIDENT_XBUF) == 14) ? 1 : -1];

/* one offset:segment view of a MENU_TEXT/REGISTER string */
typedef union gem_resident_menu_text_pointer {
	GEM_MENU_PULL_TEXT_POINTER pointer;
	GEM_BINDINGS_POINTER_SLOT address;
} GEM_RESIDENT_MENU_TEXT_POINTER;

typedef BYTE GEM_RESIDENT_MENU_TEXT_POINTER_MUST_BE_4_BYTES
	[(sizeof(GEM_RESIDENT_MENU_TEXT_POINTER) == 4) ? 1 : -1];


/* the trap frame array sizes */
#define G_SIZE_RESIDENT               15
#define I_SIZE                        16
#define O_SIZE                        7
#define AI_SIZE                       2

/* one attached client, like the original PD */
/* one record per original GEM PD, the tag is the array index */
typedef struct gem_resident_pd {
	UWORD pid;
	UWORD segment;
	UWORD limit;
	UWORD generation_lo;
	UWORD generation_hi;
	UBYTE task_slot;
	UBYTE state;
	BYTE name[8];
	UWORD queue_index;
	BYTE queue[GEM_RESIDENT_QUEUE_BYTES];
	/* parked APPL_READ/APPL_WRITE callers waiting on this queue */
	GEM_PENDING_LIST read_waiters;
	GEM_PENDING_LIST write_waiters;
	GEM_RESOURCE_RESIDENT resource;
} GEM_RESIDENT_PD;

typedef BYTE GEM_RESIDENT_PD_MUST_BE_172_BYTES
	[(sizeof(GEM_RESIDENT_PD) == 172) ? 1 : -1];


/* --- the trap frame the dispatcher fills for every call --- */

extern UWORD control[C_SIZE];
extern UWORD aes_global[G_SIZE_RESIDENT];
extern UWORD int_in[I_SIZE];
extern UWORD int_out[O_SIZE];
extern GEM_BINDINGS_POINTER_SLOT addr_in[AI_SIZE];

/* --- the AES-wide menu and window state --- */

extern GEM_MENU_OBJECT_RESIDENT gem_resident_menu;
extern GEM_MENU_PULL_EFFECTS gem_resident_menu_effects;
extern GEM_MENU_PULL_DESK_OBJECT gem_resident_menu_desk_snapshot;
extern OBJECT gem_resident_menu_desk_object;
extern GEM_WINDOW_RESIDENT gem_resident_windows;
extern GEM_WINDOW_EFFECTS gem_resident_window_effects;

/* --- seams shared between the dispatcher, the tick and the menu --- */

/* first-use setup of every manager, safe to call repeatedly */
VOID gem_resident_initialize(VOID);

UWORD gem_resident_data_segment(VOID);
GEM_RESIDENT_PD *gem_resident_pd_for_channel(WORD channel);
GEM_RESIDENT_PD *gem_resident_pd_for_request(const struct gemtrap_request
	*request, WORD *channel);
VOID gem_resident_queue_progress(GEM_RESIDENT_PD * pd);
VOID gem_resident_enqueue_window_message(GEM_RESIDENT_PD * pd,
	const UWORD *message);
WORD gem_resident_draw_object_tree(GEM_RESIDENT_PD * pd,
	GEM_BINDINGS_POINTER_SLOT tree, WORD object, UWORD depth,
	const GRECT *clip, UWORD resident_segment, WORD system_tree);
WORD gem_resident_window_apply_effects_reserved(const GEM_WINDOW_EFFECTS
	*effects, WORD channel);
WORD gem_resident_window_apply_effects(const GEM_WINDOW_EFFECTS *effects);

/* realise one manager's declared screen effects (dispatcher-owned) */
WORD gem_resident_graf_apply_effects(GEM_RESIDENT_PD * pd, WORD channel,
	const GEM_GRAF_EFFECTS *effects);
WORD gem_resident_form_apply_effects(GEM_RESIDENT_PD * pd, WORD channel,
	const GEM_FORM_EFFECTS *effects);
WORD gem_resident_form_detach_owner(GEM_RESIDENT_PD * pd, WORD channel);

/* take or release one level of the WIND_UPDATE screen lock */
WORD gem_resident_graf_update_owner(GEM_RESIDENT_PD * pd, WORD channel,
	UWORD operation);

/* the one shared form-effects record (dispatch and tick take turns) */
extern GEM_FORM_EFFECTS gem_resident_form_effects;

/* --- the message queues, in gem_pending.c --- */

GEM_PENDING_LIST *gem_pending_waiters(GEM_RESIDENT_PD * pd, UBYTE operation);
VOID gem_resident_enqueue(GEM_RESIDENT_PD * pd, UWORD segment, UWORD offset,
	UWORD length);
VOID gem_resident_dequeue(GEM_RESIDENT_PD * pd, UWORD segment, UWORD offset,
	UWORD length);

/* --- the control tick, in gem_aes_tick.c --- */

/* deliver the sixteen-byte message an event result asks for */
WORD gem_resident_event_message(const struct gemtrap_request *request,
	GEM_RESIDENT_PD * pd, const GEM_EVENT_EFFECTS *effects);
/* hand a released WIND_UPDATE lock to the oldest live contender */
VOID GEM_RESIDENT_COLD gem_resident_update_progress(VOID);

/* --- the menu half, in gem_menu_seam.c --- */

GEM_RESIDENT_PD *gem_resident_menu_owner(VOID);
VOID gem_resident_menu_rectangle(const GEM_MENU_PULL_RECTANGLE *source,
	GRECT *destination);
VOID gem_resident_menu_grow_clip(GRECT *rectangle);
WORD gem_resident_menu_bar_rule(VOID);
WORD gem_resident_menu_draw_main(WORD object, UWORD depth, const GRECT *clip);
WORD gem_resident_menu_draw_desk_snapshot(GEM_RESIDENT_PD * pd,
	const GEM_MENU_PULL_DESK_OBJECT *snapshot, const GRECT *rectangle);
WORD gem_resident_menu_draw_desk(WORD object, UWORD complete,
	const GRECT *effect_rectangle);
WORD gem_resident_menu_restore(const GRECT *rectangle, WORD extra_owner);
WORD gem_resident_menu_opaque_backing(const GRECT *rectangle);
WORD gem_resident_menu_apply_effects(const GEM_MENU_PULL_EFFECTS *effects);
VOID gem_resident_menu_detach_owner(GEM_RESIDENT_PD * pd, WORD channel);
WORD gem_resident_menu_bar(const struct gemtrap_request *request);
WORD gem_resident_menu_text(const struct gemtrap_request *request,
	const GEM_RESIDENT_PD * pd, GEM_BINDINGS_POINTER_SLOT address,
	GEM_RESIDENT_MENU_TEXT_POINTER * pointer, UWORD *limit);
WORD gem_resident_menu_pull(const struct gemtrap_request *request);

#endif				/* ELKS_GEM_AES_INTERNAL_H */
