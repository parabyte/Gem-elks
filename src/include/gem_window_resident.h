/*
 * gem_window_resident.h - GEM window manager for the resident AES
 *
 * the resident window manager: the window object list, window handles,
 * rectangles, the W_ACTIVE chrome tree and ORECT visibility lists. ELKS process
 * channels plus two generation words stand in for the old PD pointers.
 *
 * geometry is signed 16-bit pixels. rectangles outside the physical screen are
 * rejected before signed addition can overflow
 */

#ifndef ELKS_GEM_WINDOW_RESIDENT_H
#define ELKS_GEM_WINDOW_RESIDENT_H

#include "gem_bindings_elks.h"

/* GEM/XM window limits */
#define GEM_WINDOW_COUNT                 12U
#define GEM_WINDOW_RECT_COUNT            120U
#define GEM_WINDOW_ACTIVE_COUNT          19U
#define GEM_WINDOW_MESSAGE_COUNT         12U
#define GEM_WINDOW_TITLE_BYTES           81U

/* the GEM window opcodes */
#define GEM_WINDOW_WIND_CREATE           100U
#define GEM_WINDOW_WIND_OPEN             101U
#define GEM_WINDOW_WIND_CLOSE            102U
#define GEM_WINDOW_WIND_DELETE           103U
#define GEM_WINDOW_WIND_GET              104U
#define GEM_WINDOW_WIND_SET              105U
#define GEM_WINDOW_WIND_FIND             106U

/* original GEM window kind bits */
#define GEM_WINDOW_NAME                  0x0001U
#define GEM_WINDOW_CLOSER                0x0002U
#define GEM_WINDOW_FULLER                0x0004U
#define GEM_WINDOW_MOVER                 0x0008U
#define GEM_WINDOW_INFO                  0x0010U
#define GEM_WINDOW_SIZER                 0x0020U
#define GEM_WINDOW_UPARROW               0x0040U
#define GEM_WINDOW_DNARROW               0x0080U
#define GEM_WINDOW_VSLIDE                0x0100U
#define GEM_WINDOW_LFARROW               0x0200U
#define GEM_WINDOW_RTARROW               0x0400U
#define GEM_WINDOW_HSLIDE                0x0800U
#define GEM_WINDOW_HOTCLOSE              0x1000U

/* W_ACTIVE object numbers, kept for controller hit testing */
#define GEM_WINDOW_W_BOX                 0
#define GEM_WINDOW_W_TITLE               1
#define GEM_WINDOW_W_CLOSER              2
#define GEM_WINDOW_W_NAME                3
#define GEM_WINDOW_W_FULLER              4
#define GEM_WINDOW_W_INFO                5
#define GEM_WINDOW_W_DATA                6
#define GEM_WINDOW_W_WORK                7
#define GEM_WINDOW_W_SIZER               8
#define GEM_WINDOW_W_VBAR                9
#define GEM_WINDOW_W_UPARROW             10
#define GEM_WINDOW_W_DNARROW             11
#define GEM_WINDOW_W_VSLIDE              12
#define GEM_WINDOW_W_VELEV               13
#define GEM_WINDOW_W_HBAR                14
#define GEM_WINDOW_W_LFARROW             15
#define GEM_WINDOW_W_RTARROW             16
#define GEM_WINDOW_W_HSLIDE              17
#define GEM_WINDOW_W_HELEV               18

/* window fields the Desktop and controller use */
#define GEM_WINDOW_WF_KIND               1U
#define GEM_WINDOW_WF_NAME               2U
#define GEM_WINDOW_WF_INFO               3U
#define GEM_WINDOW_WF_WXYWH              4U
#define GEM_WINDOW_WF_CXYWH              5U
#define GEM_WINDOW_WF_PXYWH              6U
#define GEM_WINDOW_WF_FXYWH              7U
#define GEM_WINDOW_WF_HSLIDE             8U
#define GEM_WINDOW_WF_VSLIDE             9U
#define GEM_WINDOW_WF_TOP                10U
#define GEM_WINDOW_WF_FIRSTXYWH          11U
#define GEM_WINDOW_WF_NEXTXYWH           12U
#define GEM_WINDOW_WF_NEWDESK            14U
#define GEM_WINDOW_WF_HSLSIZ             15U
#define GEM_WINDOW_WF_VSLSIZ             16U
#define GEM_WINDOW_WF_TATTRB             18U
#define GEM_WINDOW_WF_SIZTOP             19U

/* AES messages the window manager sends */
#define GEM_WINDOW_WM_REDRAW             20U
#define GEM_WINDOW_WM_TOPPED             21U
#define GEM_WINDOW_WM_CLOSED             22U
#define GEM_WINDOW_WM_FULLED             23U
#define GEM_WINDOW_WM_ARROWED            24U
#define GEM_WINDOW_WM_HSLID              25U
#define GEM_WINDOW_WM_VSLID              26U
#define GEM_WINDOW_WM_SIZED              27U
#define GEM_WINDOW_WM_MOVED              28U

/* WM_ARROWED values */
#define GEM_WINDOW_WA_UPPAGE             0U
#define GEM_WINDOW_WA_DNPAGE             1U
#define GEM_WINDOW_WA_UPLINE             2U
#define GEM_WINDOW_WA_DNLINE             3U
#define GEM_WINDOW_WA_LFPAGE             4U
#define GEM_WINDOW_WA_RTPAGE             5U
#define GEM_WINDOW_WA_LFLINE             6U
#define GEM_WINDOW_WA_RTLINE             7U

/* internal window flags keep their original bit values */
#define GEM_WINDOW_VF_INUSE              0x0001U
#define GEM_WINDOW_VF_BROKEN             0x0002U
#define GEM_WINDOW_VF_INTREE             0x0004U

/* ORECT links are near pointers */
typedef struct __attribute__((packed)) gem_window_orect {
	struct gem_window_orect *next;
	GRECT rectangle;
} GEM_WINDOW_ORECT;

/* per-handle state: the original WINDOW payload plus a generation owner */
typedef struct __attribute__((packed)) gem_window_slot {
	UWORD flags;
	UWORD kind;
	WORD owner;
	UWORD generation_lo;
	UWORD generation_hi;
	GRECT full;
	GRECT work;
	GRECT previous;
	WORD hslide;
	WORD vslide;
	WORD hslsiz;
	WORD vslsiz;
	GEM_BINDINGS_POINTER_SLOT name;
	GEM_BINDINGS_POINTER_SLOT info;
	GEM_WINDOW_ORECT *first_rect;
	GEM_WINDOW_ORECT *next_rect;
} GEM_WINDOW_SLOT;

/* one eight-word AES message and its generation-safe destination */
typedef struct __attribute__((packed)) gem_window_message {
	WORD owner;
	UWORD generation_lo;
	UWORD generation_hi;
	UWORD words[8];
} GEM_WINDOW_MESSAGE;

/* one merged dirty area and its messages */
typedef struct gem_window_effects {
	UWORD dirty_valid;
	UWORD redraw_background;
	GRECT dirty;
	UWORD message_count;
	GEM_WINDOW_MESSAGE messages[GEM_WINDOW_MESSAGE_COUNT];
} GEM_WINDOW_EFFECTS;

/* physical metrics, given once after the resident VDI opens */
typedef struct gem_window_screen {
	WORD system_owner;
	WORD screen_width;
	WORD screen_height;
	WORD box_width;
	WORD box_height;
} GEM_WINDOW_SCREEN;

/* the AES array view for WIND_CREATE through WIND_FIND */
typedef struct gem_window_call {
	WORD owner;
	UWORD generation_lo;
	UWORD generation_hi;
	const UWORD *control;
	const UWORD *int_in;
	UWORD *int_out;
} GEM_WINDOW_CALL;

/* one physical sample from the resident PC input owner */
typedef struct gem_window_input {
	WORD mouse_x;
	WORD mouse_y;
	UWORD mouse_buttons;
} GEM_WINDOW_INPUT;

/* shared resident window state */
typedef struct gem_window_resident {
	GEM_WINDOW_SLOT windows[GEM_WINDOW_COUNT];
	OBJECT tree[GEM_WINDOW_COUNT];
	GEM_WINDOW_ORECT rectangles[GEM_WINDOW_RECT_COUNT];
	OBJECT active[GEM_WINDOW_ACTIVE_COUNT];
	TEDINFO active_name;
	TEDINFO active_info;
	GEM_BINDINGS_POINTER_SLOT desktop;
	UWORD desktop_root;
	GEM_WINDOW_ORECT *free_rect;
	WORD top;
	WORD system_owner;
	WORD screen_width;
	WORD screen_height;
	WORD box_width;
	WORD box_height;
	/* one parked window-control interaction */
	GRECT control_start;
	GRECT control_track;
	WORD control_start_x;
	WORD control_start_y;
	WORD control_handle;
	WORD control_gadget;
	UWORD control_state;
	UWORD ready;
	UWORD rect_overflow;
} GEM_WINDOW_RESIDENT;

typedef BYTE GEM_WINDOW_ORECT_MUST_BE_10_BYTES
	[(sizeof(GEM_WINDOW_ORECT) == 10) ? 1 : -1];
typedef BYTE GEM_WINDOW_MESSAGE_MUST_BE_22_BYTES
	[(sizeof(GEM_WINDOW_MESSAGE) == 22) ? 1 : -1];

/* clear all fixed state without drawing anything or sending a message */
VOID gem_window_resident_init(GEM_WINDOW_RESIDENT *manager);

/* set the root geometry: (0, box_height, width, height - box_height) */
WORD gem_window_resident_configure(GEM_WINDOW_RESIDENT *manager,
	const GEM_WINDOW_SCREEN *screen);

/* reset dirty geometry and message count */
VOID gem_window_resident_effects_init(GEM_WINDOW_EFFECTS *effects);

/* turn one exposed area into redraw effects */
WORD gem_window_resident_damage(GEM_WINDOW_RESIDENT *manager,
	const GRECT *rectangle, GEM_WINDOW_EFFECTS *effects);

/* dispatch WIND_CREATE through WIND_FIND */
WORD gem_window_resident_dispatch(GEM_WINDOW_RESIDENT *manager,
	const GEM_WINDOW_CALL *call, GEM_WINDOW_EFFECTS *effects,
	WORD *handled);

/* generation-safe EXIT/APPL_EXIT cleanup for every window this owner has */
VOID gem_window_resident_detach(GEM_WINDOW_RESIDENT *manager, WORD owner,
	UWORD generation_lo, UWORD generation_hi, GEM_WINDOW_EFFECTS *effects);

/* bottom-to-top window walk used by the resident frame renderer */
WORD gem_window_resident_first(const GEM_WINDOW_RESIDENT *manager);
WORD gem_window_resident_next(const GEM_WINDOW_RESIDENT *manager, WORD handle);
/* input owner of the top window */
WORD gem_window_resident_top_owner(const GEM_WINDOW_RESIDENT *manager);

/* rebuild the shared W_ACTIVE tree */
OBJECT *gem_window_resident_build_active(GEM_WINDOW_RESIDENT *manager,
	WORD handle);

/* find the deepest live frame, arrow, track or elevator object */
WORD gem_window_resident_gadget(GEM_WINDOW_RESIDENT *manager, WORD x,
	WORD y, WORD *handle);

/* advance one window-control interaction */
WORD gem_window_resident_input(GEM_WINDOW_RESIDENT *manager,
	const GEM_WINDOW_INPUT *input, GEM_WINDOW_EFFECTS *effects);

/* append one window-control message */
WORD gem_window_resident_control_message(GEM_WINDOW_RESIDENT *manager,
	WORD handle, UWORD message, const GRECT *rectangle,
	GEM_WINDOW_EFFECTS *effects);

#endif				/* ELKS_GEM_WINDOW_RESIDENT_H */
