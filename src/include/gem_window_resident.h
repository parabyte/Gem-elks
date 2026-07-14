/*
 * gem_window_resident.h - original GEM window manager for resident AES.
 *
 * This is the fixed-memory ELKS form of Digital Research GEMWMLIB.C and
 * GEMWRECT.C.  It keeps the original W_TREE object list, window handles,
 * current/previous/full/work rectangles, W_ACTIVE chrome tree, and ORECT
 * visibility lists.  ELKS process channels plus two generation words replace
 * historical PD pointers; no application pointer is flattened or copied.
 *
 * Geometry is unscaled signed 16-bit pixels.  There is no rounding.  Public
 * rectangles outside the configured physical screen are rejected before any
 * signed addition can overflow.  Internal word-pair addresses are copied
 * exactly and 0:0 remains the only null far address.
 */

#ifndef ELKS_GEM_WINDOW_RESIDENT_H
#define ELKS_GEM_WINDOW_RESIDENT_H

#include "gem_bindings_elks.h"

/* Original GEM/XM MULTIAPP values from MACHINE.H and GEMLIB.H. */
#define GEM_WINDOW_COUNT                 12U
#define GEM_WINDOW_RECT_COUNT            120U
#define GEM_WINDOW_ACTIVE_COUNT          19U
#define GEM_WINDOW_MESSAGE_COUNT         12U
#define GEM_WINDOW_TITLE_BYTES           81U

/* Direct opcode values from original CRYSBIND.H and GEMSUPER.C. */
#define GEM_WINDOW_WIND_CREATE           100U
#define GEM_WINDOW_WIND_OPEN             101U
#define GEM_WINDOW_WIND_CLOSE            102U
#define GEM_WINDOW_WIND_DELETE           103U
#define GEM_WINDOW_WIND_GET              104U
#define GEM_WINDOW_WIND_SET              105U
#define GEM_WINDOW_WIND_FIND             106U

/* Original GEM window kind bits. */
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

/* Original W_ACTIVE object numbers retained for controller hit testing. */
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

/* Original window fields used by the Desktop and controller. */
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

/* Original AES messages emitted by GEMCTRL.C and GEMWMLIB.C. */
#define GEM_WINDOW_WM_REDRAW             20U
#define GEM_WINDOW_WM_TOPPED             21U
#define GEM_WINDOW_WM_CLOSED             22U
#define GEM_WINDOW_WM_FULLED             23U
#define GEM_WINDOW_WM_ARROWED            24U
#define GEM_WINDOW_WM_HSLID              25U
#define GEM_WINDOW_WM_VSLID              26U
#define GEM_WINDOW_WM_SIZED              27U
#define GEM_WINDOW_WM_MOVED              28U

/* Original WM_ARROWED values from CRYSBIND.H and GEMCTRL.C. */
#define GEM_WINDOW_WA_UPPAGE             0U
#define GEM_WINDOW_WA_DNPAGE             1U
#define GEM_WINDOW_WA_UPLINE             2U
#define GEM_WINDOW_WA_DNLINE             3U
#define GEM_WINDOW_WA_LFPAGE             4U
#define GEM_WINDOW_WA_RTPAGE             5U
#define GEM_WINDOW_WA_LFLINE             6U
#define GEM_WINDOW_WA_RTLINE             7U

/* Internal window flags retain their original GEMLIB.H bit values. */
#define GEM_WINDOW_VF_INUSE              0x0001U
#define GEM_WINDOW_VF_BROKEN             0x0002U
#define GEM_WINDOW_VF_INTREE             0x0004U

/*
 * ORECT links are near pointers, exactly as in original GEMWRECT.C.  In the
 * IA-16 small-data model each link is one two-byte offset in the resident data
 * segment, so the hot clipping walk does not reload a segment or search an
 * index table.
 */
typedef struct __attribute__((packed)) gem_window_orect {
	struct gem_window_orect *next;
	GRECT rectangle;
} GEM_WINDOW_ORECT;

/* Per-handle state is the original WINDOW payload with a generation owner. */
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

/* One exact eight-word AES message plus its generation-safe destination. */
typedef struct __attribute__((packed)) gem_window_message {
	WORD owner;
	UWORD generation_lo;
	UWORD generation_hi;
	UWORD words[8];
} GEM_WINDOW_MESSAGE;

/*
 * A manager operation produces one dirty-frame union and zero or more
 * original messages.  redraw_background distinguishes geometry/z-order
 * damage from a frame-only title, information, or slider update: only the
 * former may erase client work pixels before WM_REDRAW is delivered.  The
 * outer resident AES draws W_TREE/W_ACTIVE through its object/VDI seam, then
 * enqueues messages through its event seam.  The fixed array removes
 * allocation and callback overhead from the hot path.
 */
typedef struct gem_window_effects {
	UWORD dirty_valid;
	UWORD redraw_background;
	GRECT dirty;
	UWORD message_count;
	GEM_WINDOW_MESSAGE messages[GEM_WINDOW_MESSAGE_COUNT];
} GEM_WINDOW_EFFECTS;

/* Physical metrics supplied once after the resident VDI opens. */
typedef struct gem_window_screen {
	WORD system_owner;
	WORD screen_width;
	WORD screen_height;
	WORD box_width;
	WORD box_height;
} GEM_WINDOW_SCREEN;

/* Direct original AES XIF array view for WIND_CREATE through WIND_FIND. */
typedef struct gem_window_call {
	WORD owner;
	UWORD generation_lo;
	UWORD generation_hi;
	const UWORD *control;
	const UWORD *int_in;
	UWORD *int_out;
} GEM_WINDOW_CALL;

/* One scale-one physical sample supplied by the resident PC input owner. */
typedef struct gem_window_input {
	WORD mouse_x;
	WORD mouse_y;
	UWORD mouse_buttons;
} GEM_WINDOW_INPUT;

/*
 * AES-wide state.  It is normally one static near-data object in gemaes.
 * W_TREE and W_ACTIVE are the original OBJECT representations; they are not
 * converted to another widget model.  active_name and active_info are the
 * original shared TEDINFO records rebuilt for the window being drawn.
 */
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
	/*
	 * GEMCTRL.C historically blocked in watchbox/dragbox/rubwind/slidebox.
	 * The ELKS owner retains that interaction between timer polls instead.
	 * All coordinates remain unscaled signed pixels; no client pointer is kept.
	 */
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

/* Clear all fixed state without drawing or sending a message. */
VOID gem_window_resident_init(GEM_WINDOW_RESIDENT *manager);

/* Establish root geometry: (0, box_height, width, height - box_height). */
WORD gem_window_resident_configure(GEM_WINDOW_RESIDENT *manager,
	const GEM_WINDOW_SCREEN *screen);

/*
 * Reset counts and dirty geometry before dispatch or controller work.  Only
 * records below message_count are valid; stale array slots are intentionally
 * not cleared, avoiding a 264-byte sweep on every WIND_GET hot-path call.
 */
VOID gem_window_resident_effects_init(GEM_WINDOW_EFFECTS *effects);

/*
 * Convert one exposed scale-one screen rectangle into the same bounded
 * Desktop/frame/WM_REDRAW effect used by original w_redraw().  The input is
 * clipped to the root work area below the menu bar.  This routine resets the
 * supplied effect first, so at most one message per open window can be
 * produced and the fixed twelve-record array cannot overflow.
 */
WORD gem_window_resident_damage(GEM_WINDOW_RESIDENT *manager,
	const GRECT *rectangle, GEM_WINDOW_EFFECTS *effects);

/*
 * Dispatch original WIND_CREATE..WIND_FIND arrays.  A recognized opcode sets
 * handled TRUE and writes int_out[0].  Unknown opcodes leave arrays and state
 * untouched and set handled FALSE for another original manager closure.
 */
WORD gem_window_resident_dispatch(GEM_WINDOW_RESIDENT *manager,
	const GEM_WINDOW_CALL *call, GEM_WINDOW_EFFECTS *effects,
	WORD *handled);

/* Generation-safe synthetic EXIT/APPL_EXIT cleanup for all owned windows. */
VOID gem_window_resident_detach(GEM_WINDOW_RESIDENT *manager, WORD owner,
	UWORD generation_lo, UWORD generation_hi,
	GEM_WINDOW_EFFECTS *effects);

/* Bottom-to-top window traversal used by the resident frame renderer. */
WORD gem_window_resident_first(const GEM_WINDOW_RESIDENT *manager);
WORD gem_window_resident_next(const GEM_WINDOW_RESIDENT *manager,
	WORD handle);

/*
 * Return the logical GEM process which owns the top window.  When no client
 * window is open this is the owner of W_TREE's desktop root.  NIL reports an
 * unconfigured or damaged tree.  The value is an unscaled channel word; ELKS
 * remains the scheduler and no task or memory address crosses this boundary.
 */
WORD gem_window_resident_top_owner(const GEM_WINDOW_RESIDENT *manager);

/*
 * Rebuild and return the original shared nineteen-object W_ACTIVE tree for one
 * open window.  The pointer is near and remains valid only until the next
 * build call, exactly like original gl_awind/W_ACTIVE.
 */
OBJECT *gem_window_resident_build_active(GEM_WINDOW_RESIDENT *manager,
	WORD handle);

/* Find the deepest active original frame, arrow, track, or elevator object. */
WORD gem_window_resident_gadget(GEM_WINDOW_RESIDENT *manager, WORD x,
	WORD y, WORD *handle);

/*
 * Advance GEMCTRL.C once without busy-waiting.  TRUE means the controller owns
 * this sample and generic EVNT_BUTTON delivery must be suppressed.  A press on
 * an inactive window emits WM_TOPPED; release completes closer/fuller,
 * move/size, or scale-1000 slider tracking.  Arrow and page clicks emit their
 * original WM_ARROWED value on the press.
 */
WORD gem_window_resident_input(GEM_WINDOW_RESIDENT *manager,
	const GEM_WINDOW_INPUT *input, GEM_WINDOW_EFFECTS *effects);

/*
 * Append one original GEMCTRL message.  MOVED/SIZED use rectangle in words
 * four..seven.  ARROWED/HSLID/VSLID use rectangle.g_x as the original action
 * or scale-1000 value and preserve current geometry in the remaining words.
 * Other messages use the current outer rectangle.  No window state changes
 * until the client answers through WIND_SET, preserving original GEM flow.
 */
WORD gem_window_resident_control_message(GEM_WINDOW_RESIDENT *manager,
	WORD handle, UWORD message, const GRECT *rectangle,
	GEM_WINDOW_EFFECTS *effects);

#endif /* ELKS_GEM_WINDOW_RESIDENT_H */
