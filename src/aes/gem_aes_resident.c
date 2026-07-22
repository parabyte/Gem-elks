/*
 * gem_aes_resident.c - first bounded resident FreeGEM AES unit for ELKS.
 *
 * The dispatcher shape and local XIF arrays are ported directly from
 * FreeGEM AES GEMSUPER.C (Digital Research, 1984-1987; GPL release by
 * Caldera Thin Clients, Inc., 1999).  The resident closure now retains the
 * application/process/resource core together with original event, menu,
 * object, FORM, GRAF, window, shell, startup and shared-screen managers.
 * Selectors not named by those bounded managers still return -1 immediately;
 * this file does not claim unported AES selectors merely because the owner is
 * graphical and multi-application capable.
 *
 * The original XIF copied the caller's control, integer, and address arrays
 * into local buffers.  ELKS clients have distinct data segments, so the same
 * copy is performed with two 8086 REP MOVSB helpers.  Pointer slots remain
 * their original offset/segment word pairs; there is no pointer flattening,
 * RPC representation, structure conversion, C long, or wide arithmetic.
 */

#include <errno.h>

#include "gem_aes_resident.h"
#include "gem_bindings_elks.h"
#include "gem_event_resident.h"
#include "gem_form_resident.h"
#include "gem_graf_resident.h"
#include "gem_menu_object_resident.h"
#include "gem_menu_pull_resident.h"
#include "gem_object_resident.h"
#include "gem_proc.h"
#include "gem_resident_memory.h"
#include "gem_resource_resident.h"
#include "gem_shell_resident.h"
#include "gem_startup_resident.h"
#include "gem_vdi_resident.h"
#include "gem_window_resident.h"

#define GEM_AES_SELECTOR              200U
#define GEM_AES_ALT_SELECTOR          201U

#define APPL_INIT                     10
#define APPL_READ                     11
#define APPL_WRITE                    12
#define APPL_FIND                     13
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

/* Original MULTIAPP GEMSUPER.C local-array bounds. */
#define C_SIZE                        5
#define G_SIZE_RESIDENT               15
#define I_SIZE                        16
#define O_SIZE                        7
#define AI_SIZE                       2
#define AO_SIZE                       5

#define GEM_RESIDENT_COMMAND_BYTES    256U
#define GEM_RESIDENT_TAIL_BYTES       128U

/* Original STRUCT.H gives every GEM PD one fixed 128-byte message queue. */
#define GEM_RESIDENT_QUEUE_BYTES      128U

/* The classic one-plane MFORM is five words followed by 32 row words. */
#define GEM_RESIDENT_MFORM_BYTES      74U

/* Physical cursor selectors used by GEMGSXIF.C gsx_moff()/gsx_mon(). */
#define GEM_RESIDENT_MOUSE_HIDE       256
#define GEM_RESIDENT_MOUSE_SHOW       257

/* Native PC/EGA palette index used for GEM logical white. */
#define GEM_RESIDENT_NATIVE_WHITE     15U

/* Native PC/EGA palette index used for GEM logical black. */
#define GEM_RESIDENT_NATIVE_BLACK      0U

/* Original GEMSUPER/GEMRSLIB application-global resource words. */
#define GEM_GLOBAL_TREE_OFFSET        5U
#define GEM_GLOBAL_TREE_SEGMENT       6U
#define GEM_GLOBAL_RESOURCE_OFFSET    7U
#define GEM_GLOBAL_RESOURCE_SEGMENT   8U
#define GEM_GLOBAL_RESOURCE_BYTES     9U

/* One byte names a channel; ff is outside the original zero-through-eleven. */
#define GEM_RESIDENT_INDEX_NONE       0xffU

#define GEM_PENDING_FREE              0
#define GEM_PENDING_WAITING           1
#define GEM_PENDING_READY             2

#define GEM_PENDING_READ              1
#define GEM_PENDING_WRITE             2
#define GEM_PENDING_EVENT             3
#define GEM_PENDING_GRAF              4
#define GEM_PENDING_FORM              5
#define GEM_PENDING_UPDATE            6

/*
 * Keep the cold mutex-wait list outside the nearly full first 64 KiB code
 * segment.  The resident owner uses the medium code model, so calls to these
 * far functions are ordinary 8086 CALL/RETF pairs.  Data remains near and the
 * list itself uses only one-byte channel indexes.
 */
#define GEM_RESIDENT_COLD \
	__far __attribute__((far_section, noinline, \
		section(".fartext.gemresident")))

/* Original GEMWMLIB.C wm_update() subselectors used by GRAF trackers. */
#define GEM_RESIDENT_END_UPDATE       0U
#define GEM_RESIDENT_BEG_UPDATE       1U

#define GEM_PD_FREE                   0
#define GEM_PD_ATTACHED               1

/*
 * One record corresponds to one original GEM PD tag.  The tag itself is the
 * array index, so it consumes no field.  All address and lifecycle values are
 * unscaled 16-bit words.  The two generation halves are copied, never joined.
 */
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
	UBYTE read_head;
	UBYTE read_tail;
	UBYTE write_head;
	UBYTE write_tail;
	GEM_RESOURCE_RESIDENT resource;
} GEM_RESIDENT_PD;

typedef BYTE GEM_RESIDENT_PD_MUST_BE_172_BYTES
	[(sizeof(GEM_RESIDENT_PD) == 172) ? 1 : -1];

/*
 * Original GEM used one EVB plus one QPB when APPL_READ or APPL_WRITE had to
 * wait.  An ELKS client already has exactly one delivered INT EF request, so
 * its logical channel is also a fixed event-block index.  The saved 22-byte
 * broker request keeps that client's DS pinned.  Buffer and output offsets
 * remain unscaled 16-bit offsets in that DS; no pointer conversion occurs.
 */
typedef struct gem_resident_pending {
	struct gemtrap_request request;
	UWORD buffer_offset;
	UWORD output_offset;
	UWORD length;
	WORD result;
	UBYTE target;
	UBYTE next;
	UBYTE state;
	UBYTE operation;
} GEM_RESIDENT_PENDING;

typedef BYTE GEM_RESIDENT_PENDING_MUST_BE_34_BYTES
	[(sizeof(GEM_RESIDENT_PENDING) == 34) ? 1 : -1];

/*
 * The current original-binding client is a small-data ELKS program.  Its
 * X_BUF has three near-pointer offsets, followed by the original two-word
 * abilities field.  Resident pointers cannot be represented by those near
 * offsets, so this nucleus returns zero for them and returns only safe scalar
 * information.  No graphical ability is advertised by this bounded unit.
 */
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

/* One exact offset:segment view for bounded MENU_TEXT/REGISTER strings. */
typedef union gem_resident_menu_text_pointer {
	GEM_MENU_PULL_TEXT_POINTER pointer;
	GEM_BINDINGS_POINTER_SLOT address;
} GEM_RESIDENT_MENU_TEXT_POINTER;

typedef BYTE GEM_RESIDENT_MENU_TEXT_POINTER_MUST_BE_4_BYTES
	[(sizeof(GEM_RESIDENT_MENU_TEXT_POINTER) == 4) ? 1 : -1];

static GEM_RESIDENT_PD gem_resident_pds[GEM_PROC_CHANNELS];
static GEM_RESIDENT_PENDING gem_resident_pending[GEM_PROC_CHANNELS];
static UBYTE gem_resident_ready_head;
static UBYTE gem_resident_ready_tail;
static UBYTE gem_resident_update_head;
static UBYTE gem_resident_update_tail;
static UBYTE gem_resident_initialized;
static GEM_MENU_OBJECT_RESIDENT gem_resident_menu;
static GEM_MENU_PULL_EFFECTS gem_resident_menu_effects;
static GEM_MENU_PULL_DESK_OBJECT gem_resident_menu_desk_snapshot;
static OBJECT gem_resident_menu_desk_object;
static GEM_FORM_EFFECTS gem_resident_form_effects;
static GEM_WINDOW_RESIDENT gem_resident_windows;
static GEM_WINDOW_EFFECTS gem_resident_window_effects;

/*
 * GRAF_RUBBOX, DRAGBOX, and SLIDEBOX acquire the same recursive update lock
 * as WIND_UPDATE.  Keep one private original-array-shaped call record so the
 * completion poll does not borrow a client's XIF scratch or grow the 8086
 * stack.  Every value is an unscaled 16-bit GEM word.
 */
static GEM_STARTUP_CALL gem_resident_graf_update_call;
static GEM_STARTUP_EFFECTS gem_resident_graf_update_effects;
static UWORD gem_resident_graf_update_control[C_SIZE];
static UWORD gem_resident_graf_update_input[1];
static UWORD gem_resident_graf_update_output[1];

/*
 * XIF is non-reentrant: the sole registered broker owner dispatches one
 * request at a time, and the kernel rejects an owner trapping into itself.
 * Static scratch avoids a 114-byte automatic frame on an 8088 while retaining
 * the original fixed array sizes exactly.
 */
static UWORD control[C_SIZE];
static UWORD aes_global[G_SIZE_RESIDENT];
static UWORD int_in[I_SIZE];
static UWORD int_out[O_SIZE];
static GEM_BINDINGS_POINTER_SLOT addr_in[AI_SIZE];
static GEM_BINDINGS_POINTER_SLOT addr_out[AO_SIZE];

/*
 * W_ACTIVE is the original resident nine-object window-frame tree.  These
 * arrays are private XIF-shaped scratch so drawing a frame never damages the
 * application arrays which still have to be copied back after dispatch.
 */
static UWORD gem_resident_frame_control[C_SIZE];
static UWORD gem_resident_frame_input[6];
static UWORD gem_resident_frame_output[1];
static GEM_BINDINGS_POINTER_SLOT gem_resident_frame_address[1];

static BYTE gem_resident_command[GEM_RESIDENT_COMMAND_BYTES];
static BYTE gem_resident_tail[GEM_RESIDENT_TAIL_BYTES];

static VOID GEM_RESIDENT_COLD gem_resident_update_progress(VOID);

/* Return the near-data segment containing W_ACTIVE and its shared TEDINFO. */
static UWORD
gem_resident_data_segment(VOID)
{
	UWORD segment;

	__asm__ volatile ("movw %%ds,%0" : "=r" (segment));
	return segment;
}

/*
 * GEMSUPER.C names fields through opcode-specific macros.  Preserve that
 * contract explicitly so a truncated control record cannot make a case read
 * stale static scratch which belonged to the preceding client.
 */
static WORD
gem_resident_control_valid(UWORD opcode)
{
	switch (opcode) {
	case APPL_INIT:
		return control[2] >= 1;
	case GEM_STARTUP_APPL_BVSET:
		return control[1] >= 2 && control[2] >= 1;
	case GEM_STARTUP_APPL_BVEXT:
		return control[1] >= 1 && control[2] >= 5
		       && control[3] >= 2;
	case APPL_READ:
	case APPL_WRITE:
		return control[1] >= 2 && control[2] >= 1
		       && control[3] >= 1;
	case APPL_FIND:
		return control[2] >= 1 && control[3] >= 1;
	case APPL_YIELD:
		return TRUE;
	case APPL_EXIT:
		return control[2] >= 1;
	case GEM_EVENT_EVNT_KEYBD:
		return control[2] >= 1;
	case GEM_EVENT_EVNT_BUTTON:
		return control[1] >= 3 && control[2] >= 5;
	case GEM_EVENT_EVNT_MOUSE:
		return control[1] >= 5 && control[2] >= 5;
	case GEM_EVENT_EVNT_MESAG:
		return control[2] >= 1 && control[3] >= 1;
	case GEM_EVENT_EVNT_TIMER:
		return control[1] >= 2 && control[2] >= 1;
	case GEM_EVENT_EVNT_MULTI:
		return control[1] >= 16 && control[2] >= 7
		       && control[3] >= 1;
	case GEM_EVENT_EVNT_DCLICK:
		return control[1] >= 2 && control[2] >= 1;
	case MENU_BAR:
		return control[1] >= 1 && control[2] >= 1
		       && control[3] >= 1;
	case GEM_MENU_PULL_ICHECK:
	case GEM_MENU_PULL_IENABLE:
	case GEM_MENU_PULL_TNORMAL:
		return control[1] >= 2 && control[2] >= 1
		       && control[3] >= 1;
	case GEM_MENU_PULL_TEXT:
		return control[1] >= 1 && control[2] >= 1
		       && control[3] >= 2;
	case GEM_MENU_PULL_REGISTER:
		return control[1] >= 1 && control[2] >= 1
		       && control[3] >= 1;
	case GEM_MENU_PULL_UNREGISTER:
		return control[1] >= 1 && control[2] >= 1;
	case GEM_MENU_PULL_CLICK:
		return control[1] >= 2 && control[2] >= 1;
	case GEM_OBJECT_OBJC_ADD:
	case GEM_OBJECT_OBJC_ORDER:
		return control[1] >= 2 && control[2] >= 1
		       && control[3] >= 1;
	case GEM_OBJECT_OBJC_DRAW:
		return control[1] >= 6 && control[2] >= 1
		       && control[3] >= 1;
	case GEM_OBJECT_OBJC_FIND:
		return control[1] >= 4 && control[2] >= 1
		       && control[3] >= 1;
	case GEM_OBJECT_OBJC_OFFSET:
		return control[1] >= 1 && control[2] >= 3
		       && control[3] >= 1;
	case GEM_OBJECT_OBJC_CHANGE:
		return control[1] >= 8 && control[2] >= 1
		       && control[3] >= 1;
	case GEM_FORM_OBJC_EDIT:
		return control[1] >= 4 && control[2] >= 2
		       && control[3] >= 1;
	case GEM_FORM_DO:
		return control[1] >= 1 && control[2] >= 1
		       && control[3] >= 1;
	case GEM_FORM_DIAL:
		return control[1] >= 9 && control[2] >= 1;
	case GEM_FORM_ALERT:
		return control[1] >= 1 && control[2] >= 1
		       && control[3] >= 1;
	case GEM_FORM_ERROR:
		return control[1] >= 1 && control[2] >= 1;
	case GEM_FORM_CENTER:
		return control[2] >= 5 && control[3] >= 1;
	case GEM_FORM_KEYBD:
		return control[1] >= 3 && control[2] >= 3
		       && control[3] >= 1;
	case GEM_GRAF_RUBBOX:
		return control[1] >= 4 && control[2] >= 3;
	case GEM_GRAF_DRAGBOX:
		return control[1] >= 8 && control[2] >= 3;
	case GEM_GRAF_MBOX:
		return control[1] >= 6 && control[2] >= 1;
	case GEM_GRAF_GROWBOX:
	case GEM_GRAF_SHRINKBOX:
		return control[1] >= 8 && control[2] >= 1;
	case GEM_GRAF_WATCHBOX:
		return control[1] >= 4 && control[2] >= 1
		       && control[3] >= 1;
	case GEM_GRAF_SLIDEBOX:
		return control[1] >= 3 && control[2] >= 1
		       && control[3] >= 1;
	case GEM_STARTUP_GRAF_HANDLE:
		return control[2] >= 5;
	case GEM_STARTUP_GRAF_MOUSE:
		return control[1] >= 1 && control[2] >= 1
		       && control[3] >= 1;
	case GEM_GRAF_MKSTATE:
		return control[2] >= 5;
	case GEM_STARTUP_WIND_UPDATE:
		return control[1] >= 1 && control[2] >= 1;
	case GEM_STARTUP_WIND_CALC:
		return control[1] >= 6 && control[2] >= 5;
	case GEM_WINDOW_WIND_CREATE:
	case GEM_WINDOW_WIND_OPEN:
		return control[1] >= 5 && control[2] >= 1;
	case GEM_WINDOW_WIND_CLOSE:
	case GEM_WINDOW_WIND_DELETE:
		return control[1] >= 1 && control[2] >= 1;
	case GEM_WINDOW_WIND_GET:
		return control[1] >= 2 && control[2] >= 5;
	case GEM_WINDOW_WIND_SET:
		return control[1] >= 6 && control[2] >= 1;
	case GEM_WINDOW_WIND_FIND:
		return control[1] >= 2 && control[2] >= 1;
	case RSRC_LOAD:
		return control[2] >= 1 && control[3] >= 1;
	case RSRC_FREE:
		return control[2] >= 1;
	case RSRC_GADDR:
		return control[1] >= 2 && control[2] >= 1;
	case RSRC_SADDR:
		return control[1] >= 2 && control[2] >= 1
		       && control[3] >= 1;
	case RSRC_OBFIX:
		return control[1] >= 1 && control[2] >= 1
		       && control[3] >= 1;
	case GEM_SHELL_READ:
		return control[2] >= 1 && control[3] >= 2;
	case GEM_SHELL_WRITE:
		return control[1] >= 3 && control[2] >= 1
		       && control[3] >= 2;
	case GEM_SHELL_GET:
	case GEM_SHELL_PUT:
		return control[1] >= 1 && control[2] >= 1
		       && control[3] >= 1;
	case GEM_SHELL_FIND:
		return control[2] >= 1 && control[3] >= 1;
	case GEM_SHELL_ENVRN:
	case GEM_SHELL_RDEF:
	case GEM_SHELL_WDEF:
		return control[2] >= 1 && control[3] >= 2;
	case PROC_CREATE:
		return control[1] >= 2 && control[2] >= 2
		       && control[3] >= 2;
	case PROC_RUN:
		return control[1] >= 3 && control[2] >= 1
		       && control[3] >= 2;
	case PROC_DELETE:
		return control[1] >= 1 && control[2] >= 1;
	case PROC_INFO:
		return control[1] >= 1 && control[2] >= 3
		       && control[4] >= 5;
	case PROC_MALLOC:
	case PROC_MFREE:
		return TRUE;
	case PROC_SWITCH:
	case PROC_SETBLOCK:
		return control[1] >= 1 && control[2] >= 1;
	default:
		/* Unsupported opcodes are allowed only to return a defined -1. */
		return TRUE;
	}
}

static GEM_RESIDENT_PD *
gem_resident_pd_for_channel(WORD channel)
{
	GEM_RESIDENT_PD *pd;
	UWORD count;

	if (channel < 0 || channel >= GEM_PROC_CHANNELS)
		return NULL;
	pd = gem_resident_pds;
	count = (UWORD) channel;
	while (count--) {
		/* A pointer increment is cheaper than channel * sizeof (*pd). */
		pd++;
	}
	return pd;
}

static GEM_RESIDENT_PD *
gem_resident_pd_for_request(const struct gemtrap_request *request,
			    WORD *channel)
{
	GEM_RESIDENT_PD *pd;
	WORD number;

	pd = gem_resident_pds;
	number = 0;
	while (number < GEM_PROC_CHANNELS) {
		if (pd->state == GEM_PD_ATTACHED
		    && pd->pid == request->pid
		    && pd->segment == request->ds
		    && pd->task_slot == (UBYTE) request->slot) {
			if (channel)
				*channel = number;
			return pd;
		}
		pd++;
		number++;
	}
	return NULL;
}

static VOID
gem_resident_clear_pd(GEM_RESIDENT_PD *pd)
{
	UWORD index;

	pd->pid = 0;
	pd->segment = 0;
	pd->limit = 0;
	pd->generation_lo = 0;
	pd->generation_hi = 0;
	pd->task_slot = 0;
	pd->state = GEM_PD_FREE;
	index = 0;
	while (index < 8U)
		pd->name[index++] = ' ';
	pd->queue_index = 0;
	pd->read_head = GEM_RESIDENT_INDEX_NONE;
	pd->read_tail = GEM_RESIDENT_INDEX_NONE;
	pd->write_head = GEM_RESIDENT_INDEX_NONE;
	pd->write_tail = GEM_RESIDENT_INDEX_NONE;
	gem_resource_resident_init(&pd->resource);
}

static GEM_RESIDENT_PENDING *
gem_resident_pending_for_channel(WORD channel)
{
	GEM_RESIDENT_PENDING *pending;
	UWORD count;

	if (channel < 0 || channel >= GEM_PROC_CHANNELS)
		return NULL;
	pending = gem_resident_pending;
	count = (UWORD) channel;
	while (count--)
		pending++;
	return pending;
}

/*
 * C startup supplies zero, while GEM's null event link is ff because channel
 * zero is a real PD.  Initialize every fixed link explicitly on first use.
 */
static VOID
gem_resident_initialize(VOID)
{
	GEM_RESIDENT_PENDING *pending;
	GEM_RESIDENT_PD *pd;
	GEM_STARTUP_SCREEN startup_screen;
	GEM_WINDOW_SCREEN window_screen;
	UWORD character_height;
	UWORD character_width;
	UWORD count;
	UWORD screen_height;
	UWORD screen_width;

	if (gem_resident_initialized)
		return;
	pd = gem_resident_pds;
	pending = gem_resident_pending;
	count = GEM_PROC_CHANNELS;
	while (count--) {
		gem_resident_clear_pd(pd++);
		pending->state = GEM_PENDING_FREE;
		pending->next = GEM_RESIDENT_INDEX_NONE;
		pending++;
	}
	gem_resident_ready_head = GEM_RESIDENT_INDEX_NONE;
	gem_resident_ready_tail = GEM_RESIDENT_INDEX_NONE;
	gem_resident_update_head = GEM_RESIDENT_INDEX_NONE;
	gem_resident_update_tail = GEM_RESIDENT_INDEX_NONE;
	gem_menu_object_resident_init(&gem_resident_menu);
	gem_menu_pull_resident_reset();
	gem_event_resident_reset();
	(void) gem_event_resident_configure_tick(20U);
	gem_form_resident_reset();
	gem_shell_resident_reset();
	gem_startup_resident_reset();
	gem_graf_resident_reset();
	gem_window_resident_init(&gem_resident_windows);
	if (gem_vdi_resident_get_metrics(&screen_width, &screen_height,
					 &character_width, &character_height)) {
		/*
		 * GEMGRAF.C defines hbox as hchar plus three pixels.  The native
		 * PC driver reports the original square 1:1 pixel metric, so wbox
		 * is the same exact unscaled word; no ratio multiply or divide is
		 * needed at this ELKS interface.
		 */
		startup_screen.vdi_handle = 1;
		startup_screen.character_width = (WORD) character_width;
		startup_screen.character_height = (WORD) character_height;
		startup_screen.box_height = (WORD) (character_height + 3U);
		startup_screen.box_width = startup_screen.box_height;
		startup_screen.screen_width = (WORD) screen_width;
		startup_screen.screen_height = (WORD) screen_height;
		startup_screen.frame_3d = 0;
		(void) gem_startup_resident_configure(&startup_screen);
		window_screen.system_owner = GEM_PROC_AES;
		window_screen.screen_width = (WORD) screen_width;
		window_screen.screen_height = (WORD) screen_height;
		window_screen.box_width = startup_screen.box_width;
		window_screen.box_height = startup_screen.box_height;
		(void) gem_window_resident_configure(&gem_resident_windows,
			&window_screen);
	}
	gem_resident_initialized = TRUE;
}

/* Direct 8-byte adaptation of GEMPD.C p_nameit(). */
static VOID
gem_resident_nameit(GEM_RESIDENT_PD *pd, const BYTE *path)
{
	const BYTE *name;
	const BYTE *scan;
	UWORD index;

	index = 0;
	while (index < 8U)
		pd->name[index++] = ' ';
	if (!path)
		return;

	name = path;
	scan = path;
	while (*scan) {
		if (*scan == '/' || *scan == '\\')
			name = scan + 1;
		scan++;
	}
	index = 0;
	while (index < 8U && name[index] && name[index] != '.') {
		pd->name[index] = name[index];
		index++;
	}
}

static VOID
gem_resident_wait_links(GEM_RESIDENT_PD *pd, UBYTE operation,
			UBYTE **head, UBYTE **tail)
{
	if (operation == GEM_PENDING_WRITE) {
		*head = &pd->write_head;
		*tail = &pd->write_tail;
	} else {
		*head = &pd->read_head;
		*tail = &pd->read_tail;
	}
}

/* Append one original-order event using byte indexes instead of EVB pointers. */
static VOID
gem_resident_wait_append(GEM_RESIDENT_PD *pd, UBYTE operation, UBYTE index)
{
	GEM_RESIDENT_PENDING *previous;
	GEM_RESIDENT_PENDING *pending;
	UBYTE *head;
	UBYTE *tail;

	gem_resident_wait_links(pd, operation, &head, &tail);
	pending = gem_resident_pending_for_channel((WORD) index);
	pending->next = GEM_RESIDENT_INDEX_NONE;
	if (*tail != GEM_RESIDENT_INDEX_NONE) {
		previous = gem_resident_pending_for_channel((WORD) *tail);
		previous->next = index;
	} else {
		*head = index;
	}
	*tail = index;
}

static UBYTE
gem_resident_wait_pop(GEM_RESIDENT_PD *pd, UBYTE operation)
{
	GEM_RESIDENT_PENDING *pending;
	UBYTE *head;
	UBYTE *tail;
	UBYTE index;

	gem_resident_wait_links(pd, operation, &head, &tail);
	index = *head;
	if (index == GEM_RESIDENT_INDEX_NONE)
		return index;
	pending = gem_resident_pending_for_channel((WORD) index);
	*head = pending->next;
	if (*head == GEM_RESIDENT_INDEX_NONE)
		*tail = GEM_RESIDENT_INDEX_NONE;
	pending->next = GEM_RESIDENT_INDEX_NONE;
	return index;
}

static VOID
gem_resident_wait_remove(GEM_RESIDENT_PD *pd, UBYTE operation,
			 UBYTE wanted)
{
	GEM_RESIDENT_PENDING *pending;
	GEM_RESIDENT_PENDING *previous;
	UBYTE *head;
	UBYTE *tail;
	UBYTE index;
	UBYTE previous_index;

	gem_resident_wait_links(pd, operation, &head, &tail);
	previous_index = GEM_RESIDENT_INDEX_NONE;
	index = *head;
	while (index != GEM_RESIDENT_INDEX_NONE) {
		pending = gem_resident_pending_for_channel((WORD) index);
		if (index == wanted) {
			if (previous_index == GEM_RESIDENT_INDEX_NONE)
				*head = pending->next;
			else {
				previous = gem_resident_pending_for_channel(
					(WORD) previous_index);
				previous->next = pending->next;
			}
			if (*tail == wanted)
				*tail = previous_index;
			pending->next = GEM_RESIDENT_INDEX_NONE;
			return;
		}
		previous_index = index;
		index = pending->next;
	}
}

static VOID
gem_resident_ready_append(UBYTE index)
{
	GEM_RESIDENT_PENDING *pending;
	GEM_RESIDENT_PENDING *previous;

	pending = gem_resident_pending_for_channel((WORD) index);
	pending->next = GEM_RESIDENT_INDEX_NONE;
	if (gem_resident_ready_tail != GEM_RESIDENT_INDEX_NONE) {
		previous = gem_resident_pending_for_channel(
			(WORD) gem_resident_ready_tail);
		previous->next = index;
	} else {
		gem_resident_ready_head = index;
	}
	gem_resident_ready_tail = index;
}

static VOID
gem_resident_ready_remove(UBYTE wanted)
{
	GEM_RESIDENT_PENDING *pending;
	GEM_RESIDENT_PENDING *previous;
	UBYTE index;
	UBYTE previous_index;

	previous_index = GEM_RESIDENT_INDEX_NONE;
	index = gem_resident_ready_head;
	while (index != GEM_RESIDENT_INDEX_NONE) {
		pending = gem_resident_pending_for_channel((WORD) index);
		if (index == wanted) {
			if (previous_index == GEM_RESIDENT_INDEX_NONE)
				gem_resident_ready_head = pending->next;
			else {
				previous = gem_resident_pending_for_channel(
					(WORD) previous_index);
				previous->next = pending->next;
			}
			if (gem_resident_ready_tail == wanted)
				gem_resident_ready_tail = previous_index;
			pending->next = GEM_RESIDENT_INDEX_NONE;
			return;
		}
		previous_index = index;
		index = pending->next;
	}
}

/*
 * Original GEMFLAG.C links competing wind_spb event blocks in arrival order.
 * The ELKS broker already gives each PD one pinned pending request and one byte
 * link, so retain that exact FIFO without allocating a second event object.
 */
static VOID GEM_RESIDENT_COLD
gem_resident_update_append(UBYTE index)
{
	GEM_RESIDENT_PENDING *pending;
	GEM_RESIDENT_PENDING *previous;

	pending = gem_resident_pending_for_channel((WORD) index);
	pending->next = GEM_RESIDENT_INDEX_NONE;
	if (gem_resident_update_tail != GEM_RESIDENT_INDEX_NONE) {
		previous = gem_resident_pending_for_channel(
			(WORD) gem_resident_update_tail);
		previous->next = index;
	} else {
		gem_resident_update_head = index;
	}
	gem_resident_update_tail = index;
}

static UBYTE GEM_RESIDENT_COLD
gem_resident_update_pop(VOID)
{
	GEM_RESIDENT_PENDING *pending;
	UBYTE index;

	index = gem_resident_update_head;
	if (index == GEM_RESIDENT_INDEX_NONE)
		return index;
	pending = gem_resident_pending_for_channel((WORD) index);
	gem_resident_update_head = pending->next;
	if (gem_resident_update_head == GEM_RESIDENT_INDEX_NONE)
		gem_resident_update_tail = GEM_RESIDENT_INDEX_NONE;
	pending->next = GEM_RESIDENT_INDEX_NONE;
	return index;
}

/* Remove a dying waiter without disturbing the arrival order of survivors. */
static VOID GEM_RESIDENT_COLD
gem_resident_update_remove(UBYTE wanted)
{
	GEM_RESIDENT_PENDING *pending;
	GEM_RESIDENT_PENDING *previous;
	UBYTE index;
	UBYTE previous_index;

	previous_index = GEM_RESIDENT_INDEX_NONE;
	index = gem_resident_update_head;
	while (index != GEM_RESIDENT_INDEX_NONE) {
		pending = gem_resident_pending_for_channel((WORD) index);
		if (index == wanted) {
			if (previous_index == GEM_RESIDENT_INDEX_NONE)
				gem_resident_update_head = pending->next;
			else {
				previous = gem_resident_pending_for_channel(
					(WORD) previous_index);
				previous->next = pending->next;
			}
			if (gem_resident_update_tail == wanted)
				gem_resident_update_tail = previous_index;
			pending->next = GEM_RESIDENT_INDEX_NONE;
			return;
		}
		previous_index = index;
		index = pending->next;
	}
}

/*
 * Complete one deferred XIF.  The original binding reads int_out[0], not the
 * CPU AX returned by INT EF, so write the same signed word into the still
 * pinned client array before the service wakes that task with GEMCTL_REPLY.
 */
static VOID
gem_resident_complete(UBYTE index, WORD result)
{
	GEM_RESIDENT_PENDING *pending;

	pending = gem_resident_pending_for_channel((WORD) index);
	pending->result = result;
	gem_resident_memory_to(&pending->result, pending->request.ds,
			     pending->output_offset, 2U);
	pending->state = GEM_PENDING_READY;
	gem_resident_ready_append(index);
}

/*
 * Retain one competing WIND_UPDATE(BEG_UPDATE) exactly like an original
 * MU_MUTEX EVB.  The delivered INT EF record pins the caller segment until the
 * current owner releases its final recursive depth.  Every count and link is
 * an unscaled 8- or 16-bit value; no heap or pointer conversion is involved.
 */
static WORD GEM_RESIDENT_COLD
gem_resident_update_defer(const struct gemtrap_request *request, WORD channel,
	UWORD output_offset)
{
	GEM_RESIDENT_PENDING *pending;
	GEM_RESIDENT_PD *pd;

	pending = gem_resident_pending_for_channel(channel);
	pd = gem_resident_pd_for_channel(channel);
	if (!pending || !pd || pd->state != GEM_PD_ATTACHED
	    || pending->state != GEM_PENDING_FREE)
		return -1;
	pending->request = *request;
	/*
	 * A trap generation changes on every INT EF request, whereas the PD
	 * generation is the APPL_INIT attachment generation.  UPDATE needs no
	 * message buffer or length, so retain those two attachment halves in the
	 * otherwise-unused fields.  This prevents a stale waiter from acquiring on
	 * behalf of a channel reused by a new application image without growing the
	 * fixed pending record.
	 */
	pending->buffer_offset = pd->generation_lo;
	pending->output_offset = output_offset;
	pending->length = pd->generation_hi;
	pending->result = 0;
	pending->target = (UBYTE) channel;
	pending->next = GEM_RESIDENT_INDEX_NONE;
	pending->operation = GEM_PENDING_UPDATE;
	pending->state = GEM_PENDING_WAITING;
	gem_resident_update_append((UBYTE) channel);
	return GEM_AES_RESIDENT_DEFERRED;
}

static VOID
gem_resident_enqueue(GEM_RESIDENT_PD *pd, UWORD segment, UWORD offset,
		     UWORD length)
{
	gem_resident_memory_from(segment, offset, &pd->queue[pd->queue_index],
			       length);
	pd->queue_index = (UWORD) (pd->queue_index + length);
}

static VOID
gem_resident_dequeue(GEM_RESIDENT_PD *pd, UWORD segment, UWORD offset,
		     UWORD length)
{
	BYTE *destination;
	BYTE *source;
	UWORD remaining;

	gem_resident_memory_to(pd->queue, segment, offset, length);
	remaining = (UWORD) (pd->queue_index - length);
	destination = pd->queue;
	source = &pd->queue[length];
	while (remaining--)
		*destination++ = *source++;
	pd->queue_index = (UWORD) (pd->queue_index - length);
}

/*
 * This is GEMQUEUE.C aqueue()/doq() with the EVB storage replaced by the
 * fixed delivered-request table.  A successful write wakes the oldest read;
 * a successful read frees room for the oldest write.  At most twelve events
 * can complete, so the bounded pass count cannot wrap or spin.
 */
static VOID
gem_resident_queue_progress(GEM_RESIDENT_PD *pd)
{
	GEM_RESIDENT_PENDING *pending;
	UBYTE index;
	UWORD passes;

	passes = GEM_PROC_CHANNELS;
	while (passes--) {
		index = pd->read_head;
		if (index != GEM_RESIDENT_INDEX_NONE) {
			pending = gem_resident_pending_for_channel((WORD) index);
			if (pending->length <= pd->queue_index) {
				(void) gem_resident_wait_pop(pd, GEM_PENDING_READ);
				gem_resident_dequeue(pd, pending->request.ds,
					pending->buffer_offset, pending->length);
				gem_resident_complete(index, TRUE);
				continue;
			}
		}

		index = pd->write_head;
		if (index != GEM_RESIDENT_INDEX_NONE) {
			pending = gem_resident_pending_for_channel((WORD) index);
			if (pending->length <= (UWORD)
			    (GEM_RESIDENT_QUEUE_BYTES - pd->queue_index)) {
				(void) gem_resident_wait_pop(pd, GEM_PENDING_WRITE);
				gem_resident_enqueue(pd, pending->request.ds,
					pending->buffer_offset, pending->length);
				gem_resident_complete(index, TRUE);
				continue;
			}
		}
		break;
	}
}

/* Copy one native eight-word AES message into an original 128-byte PD queue. */
static VOID
gem_resident_enqueue_window_message(GEM_RESIDENT_PD *pd,
	const UWORD *words)
{
	BYTE *destination;
	const BYTE *source;
	UWORD count;

	destination = &pd->queue[pd->queue_index];
	source = (const BYTE *) words;
	count = 16U;
	while (count--)
		*destination++ = *source++;
	pd->queue_index = (UWORD) (pd->queue_index + 16U);
}

/*
 * Invoke the original object renderer with a private, resident XIF array.
 * resident_segment is zero for application/RSC trees and is the owner DS for
 * trusted AES trees such as W_ACTIVE and M_DESK.  The clip is scale-one
 * pixels; negative origins remain their original two's-complement words.
 */
static WORD
gem_resident_draw_object_tree(GEM_RESIDENT_PD *pd,
	GEM_BINDINGS_POINTER_SLOT tree, WORD object, UWORD depth,
	const GRECT *clip, UWORD resident_segment)
{
	GEM_OBJECT_RESIDENT_CALL call;
	WORD handled;

	if (!pd || pd->state != GEM_PD_ATTACHED || !tree.hi || !clip
	    || clip->g_w <= 0 || clip->g_h <= 0)
		return FALSE;
	gem_resident_frame_control[0] = GEM_OBJECT_OBJC_DRAW;
	gem_resident_frame_control[1] = 6U;
	gem_resident_frame_control[2] = 1U;
	gem_resident_frame_control[3] = 1U;
	gem_resident_frame_control[4] = 0;
	gem_resident_frame_input[0] = (UWORD) object;
	gem_resident_frame_input[1] = depth;
	gem_resident_frame_input[2] = (UWORD) clip->g_x;
	gem_resident_frame_input[3] = (UWORD) clip->g_y;
	gem_resident_frame_input[4] = (UWORD) clip->g_w;
	gem_resident_frame_input[5] = (UWORD) clip->g_h;
	gem_resident_frame_address[0] = tree;
	gem_resident_frame_output[0] = FALSE;
	call.resource = &pd->resource;
	call.client_segment = pd->segment;
	call.client_limit = pd->limit;
	call.resident_segment = resident_segment;
	call.control = gem_resident_frame_control;
	call.int_in = gem_resident_frame_input;
	call.int_out = gem_resident_frame_output;
	call.addr_in = gem_resident_frame_address;
	handled = FALSE;
	return gem_object_resident_dispatch(&call, &handled)
		&& handled && gem_resident_frame_output[0];
}

/*
 * Intersect two scale-one screen rectangles using only signed GEM WORDs.
 * Every caller supplies a rectangle already clipped to the physical screen,
 * so x + width is at most 640 and y + height is at most 480 on this target.
 * Those sums therefore cannot overflow a signed sixteen-bit word.  Empty
 * intersections return FALSE and are never sent to VDI or the object walker.
 */
static WORD
gem_resident_screen_intersection(const GRECT *first, const GRECT *second,
	GRECT *result)
{
	WORD left;
	WORD top;
	WORD right;
	WORD bottom;
	WORD edge;

	if (!first || !second || !result || first->g_w <= 0
	    || first->g_h <= 0 || second->g_w <= 0 || second->g_h <= 0)
		return FALSE;
	left = first->g_x > second->g_x ? first->g_x : second->g_x;
	top = first->g_y > second->g_y ? first->g_y : second->g_y;
	right = first->g_x + first->g_w;
	edge = second->g_x + second->g_w;
	if (edge < right)
		right = edge;
	bottom = first->g_y + first->g_h;
	edge = second->g_y + second->g_h;
	if (edge < bottom)
		bottom = edge;
	if (right <= left || bottom <= top)
		return FALSE;
	result->g_x = left;
	result->g_y = top;
	result->g_w = right - left;
	result->g_h = bottom - top;
	return TRUE;
}

/*
 * Repaint the Desktop background inside one scale-one pixel rectangle before
 * any surviving window frames are drawn.  Original GEM w_drawdesk() enters
 * do_walk(DESKWH), intersects the damage with every root ORECT, and draws only
 * those pieces which the Desktop still owns.  Retain that important rule here:
 * clearing the complete damage union would erase covered client work and make
 * it flash blank until the client's later WM_REDRAW message is dispatched.
 *
 * The installed WF_NEWDESK tree's selected DROOT may legitimately be a hollow
 * G_BOX.  Clear each exposed ORECT piece first so that such a tree can add its
 * icons and pattern without preserving old window, menu, or form pixels.
 * During early startup, or after the owner exits, that logical-white clear is
 * also the complete bounded fallback.  No framebuffer copy, backing bitmap,
 * heap allocation, multiplication, division, or value wider than WORD is used.
 */
static WORD
gem_resident_desktop_restore_rectangle(const GRECT *rectangle)
{
	GEM_RESIDENT_PD *pd;
	const GEM_WINDOW_ORECT *owned;
	GEM_WINDOW_SLOT *root;
	GEM_VDI_SCREEN *screen;
	GRECT clipped;
	WORD draw_desktop;

	if (!rectangle || rectangle->g_w <= 0 || rectangle->g_h <= 0)
		return FALSE;
	screen = gem_vdi_resident_screen();
	if (!screen)
		return FALSE;

	root = gem_resident_windows.windows;
	pd = gem_resident_pd_for_channel(root->owner);
	draw_desktop = gem_resident_windows.desktop.hi && pd
	    && pd->state == GEM_PD_ATTACHED
	    && pd->generation_lo == root->generation_lo
	    && pd->generation_hi == root->generation_hi;

	/*
	 * DROOT=1 is commonly an IBOX or a pattern-owning child rather than an
	 * opaque full-screen object.  Erase each owned piece before drawing it.
	 * Cursor hide/show protects the driver's saved background, while disabling
	 * an inherited clip is safe because the fill itself receives exact bounded
	 * coordinates.  Native color 15 is GEM logical white.
	 */
	owned = root->first_rect;
	while (owned) {
		if (gem_resident_screen_intersection(rectangle,
			&owned->rectangle, &clipped)) {
			gem_vdi_set_clip(screen, 0, NULL);
			gem_vdi_hide_cursor(screen);
			gem_vdi_set_mode(GEM_VDI_REPLACE);
			gem_vdi_set_foreground(screen,
				GEM_RESIDENT_NATIVE_WHITE);
			gem_vdi_fill_rect(screen, clipped.g_x, clipped.g_y,
				clipped.g_w, clipped.g_h);
			gem_vdi_show_cursor(screen);
			gem_vdi_flush(screen);
			if (draw_desktop
			    && !gem_resident_draw_object_tree(pd,
				gem_resident_windows.desktop,
				(WORD) gem_resident_windows.desktop_root, 8U,
				&clipped, 0))
				return FALSE;
		}
		owned = owned->next;
	}
	return TRUE;
}

/*
 * Draw every open original W_ACTIVE frame from bottom to top.  Original
 * w_cpwalk() enters do_walk() and intersects the dirty rectangle with every
 * ORECT owned by that window.  A single dirty-union clip is not sufficient:
 * the frame of a partly covered lower window would otherwise paint through a
 * higher window's work area.  Title and information strings remain in their
 * owner's pinned client or direct RSC segment; only the frame tree itself and
 * its shared TEDINFO records use the explicitly trusted resident DS.
 */
static WORD
gem_resident_window_draw_frames_rectangle(const GRECT *rectangle)
{
	GEM_RESIDENT_PD *pd;
	const GEM_WINDOW_ORECT *owned;
	GEM_WINDOW_SLOT *slot;
	OBJECT *objects;
	GEM_BINDINGS_POINTER_SLOT tree;
	GRECT clipped;
	UWORD count;
	UWORD resident_segment;
	WORD handle;

	if (!rectangle || rectangle->g_w <= 0 || rectangle->g_h <= 0)
		return FALSE;
	resident_segment = gem_resident_data_segment();

	/*
	 * Original 2-D W_ACTIVE places two adjacent one-pixel outlines between
	 * an active closer and its name: W_CLOSER's right edge followed by
	 * W_NAME's left edge.  A clipped root draw can reject those child edges
	 * after a pull-down has covered only the rightmost two pixels, even though
	 * the rest of the retained frame is still valid.  Complete those original
	 * child outlines explicitly after each object draw.  A menu can cover a
	 * non-top window, so this must follow every frame in bottom-to-top W_TREE
	 * order rather than only manager->top.  The retained object clip makes the
	 * two 17-pixel lines a no-op outside the exact dirty/ORECT intersection.
	 */
	handle = gem_window_resident_first(&gem_resident_windows);
	while (handle >= 0) {
		slot = gem_resident_windows.windows;
		count = (UWORD) handle;
		while (count--)
			slot++;
		/*
		 * The window manager's ORECT chain is the authoritative visible
		 * region.  A fully covered W_ACTIVE window has no first rectangle;
		 * original w_redraw() therefore never paints that frame.  Skipping it
		 * also prevents a covered Desktop frame from overwriting a top
		 * application during multi-application activation.
		 */
		if (!slot->first_rect) {
			handle = gem_window_resident_next(&gem_resident_windows,
				handle);
			continue;
		}
		pd = gem_resident_pd_for_channel(slot->owner);
		if (!pd || pd->state != GEM_PD_ATTACHED
		    || pd->generation_lo != slot->generation_lo
		    || pd->generation_hi != slot->generation_hi)
			return FALSE;
		objects = gem_window_resident_build_active(&gem_resident_windows,
			handle);
		if (!objects)
			return FALSE;
		tree.lo = (UWORD) objects;
		tree.hi = resident_segment;
		owned = slot->first_rect;
		while (owned) {
			if (gem_resident_screen_intersection(rectangle,
				&owned->rectangle, &clipped)) {
				if (!gem_resident_draw_object_tree(pd, tree, ROOT, 8U,
					&clipped, resident_segment))
					return FALSE;

				/*
				 * Complete the original closer/name double edge while the
				 * object renderer's exact ORECT clip remains active.  W_BOX
				 * contains the current absolute outer position just built by
				 * gem_window_resident_build_active().  WS_PREV deliberately
				 * retains the old position after a move or resize and therefore
				 * must not be used for current chrome.
				 */
				if (slot->kind & GEM_WINDOW_CLOSER) {
					gem_vdi_set_mode(GEM_VDI_REPLACE);
					gem_vdi_set_foreground(
						gem_vdi_resident_screen(),
						GEM_RESIDENT_NATIVE_BLACK);
					gem_vdi_fill_rect(
						gem_vdi_resident_screen(),
						(WORD) ((WORD) objects[ROOT].ob_x
						    + gem_resident_windows.box_width
						    - 1),
						(WORD) ((WORD) objects[ROOT].ob_y + 1),
						(slot->kind & GEM_WINDOW_NAME)
						    ? 2 : 1,
						(WORD) (gem_resident_windows.box_height
						    - 2));
				}
			}
			owned = owned->next;
		}
		handle = gem_window_resident_next(&gem_resident_windows, handle);
	}
	return TRUE;
}

static WORD
gem_resident_window_draw_frames(const GEM_WINDOW_EFFECTS *effects)
{
	if (!effects || !effects->dirty_valid)
		return TRUE;
	return gem_resident_window_draw_frames_rectangle(&effects->dirty);
}

/*
 * Commit one window-manager effect atomically with respect to queue space.
 * Reserved byte counts are preflighted for all twelve target PDs before the
 * first message is copied, so a full queue cannot leave half an effect graph.
 */
static WORD
gem_resident_window_apply_effects_reserved(const GEM_WINDOW_EFFECTS *effects,
	WORD extra_owner)
{
	GEM_RESIDENT_PD *pd;
	const GEM_WINDOW_MESSAGE *message;
	UWORD reserved[GEM_PROC_CHANNELS];
	UWORD index;
	UWORD owner;

	if (!effects || effects->message_count > GEM_WINDOW_MESSAGE_COUNT)
		return FALSE;
	index = 0;
	while (index < GEM_PROC_CHANNELS)
		reserved[index++] = 0;
	if (extra_owner != NIL) {
		if (extra_owner < 0 || extra_owner >= GEM_PROC_CHANNELS)
			return FALSE;
		reserved[(UWORD) extra_owner] = 16U;
	}
	message = effects->messages;
	index = effects->message_count;
	while (index--) {
		if (message->owner < 0 || message->owner >= GEM_PROC_CHANNELS)
			return FALSE;
		owner = (UWORD) message->owner;
		pd = gem_resident_pd_for_channel(message->owner);
		if (!pd || pd->state != GEM_PD_ATTACHED
		    || pd->generation_lo != message->generation_lo
		    || pd->generation_hi != message->generation_hi)
			return FALSE;
		reserved[owner] = (UWORD) (reserved[owner] + 16U);
		if (reserved[owner]
		    > (UWORD) (GEM_RESIDENT_QUEUE_BYTES - pd->queue_index))
			return FALSE;
		message++;
	}
	/*
	 * The queue preflight above remains first: no framebuffer mutation occurs
	 * when a client cannot accept every promised WM_REDRAW.  Once it succeeds,
	 * clear a geometry/z-order dirty union with the Desktop OBJECT tree and then
	 * rebuild surviving W_ACTIVE frames before clients repaint work areas.
	 * Title, information, and slider changes retain client work pixels and draw
	 * only frames because those operations deliberately send no WM_REDRAW.
	 */
	if (effects->dirty_valid && effects->redraw_background
	    && (!gem_resident_desktop_restore_rectangle(&effects->dirty)
		|| !gem_resident_window_draw_frames(effects)))
		return FALSE;
	if (effects->dirty_valid && !effects->redraw_background
	    && !gem_resident_window_draw_frames(effects))
		return FALSE;

	message = effects->messages;
	index = effects->message_count;
	while (index--) {
		pd = gem_resident_pd_for_channel(message->owner);
		gem_resident_enqueue_window_message(pd, message->words);
		message++;
	}
	pd = gem_resident_pds;
	index = 0;
	while (index < GEM_PROC_CHANNELS) {
		if (reserved[index])
			gem_resident_queue_progress(pd);
		pd++;
		index++;
	}
	return TRUE;
}

/* Ordinary window effects have no later message sharing their queue space. */
static WORD
gem_resident_window_apply_effects(const GEM_WINDOW_EFFECTS *effects)
{
	return gem_resident_window_apply_effects_reserved(effects, NIL);
}

/* Resolve the generation which owns the AES-wide original menu tree. */
static GEM_RESIDENT_PD *
gem_resident_menu_owner(VOID)
{
	GEM_RESIDENT_PD *pd;

	if (gem_resident_menu.visible != GEM_MENU_OBJECT_RESIDENT_VISIBLE)
		return (GEM_RESIDENT_PD *) 0;
	pd = gem_resident_pd_for_channel(gem_resident_menu.owner);
	if (!pd || pd->state != GEM_PD_ATTACHED
	    || pd->generation_lo != gem_resident_menu.generation_lo
	    || pd->generation_hi != gem_resident_menu.generation_hi)
		return (GEM_RESIDENT_PD *) 0;
	return pd;
}

static VOID
gem_resident_menu_rectangle(const GEM_MENU_PULL_RECTANGLE *source,
	GRECT *destination)
{
	destination->g_x = source->x;
	destination->g_y = source->y;
	destination->g_w = source->width;
	destination->g_h = source->height;
}

/* Draw one object or subtree directly from the active relocated menu RSC. */
static WORD
gem_resident_menu_draw_main(WORD object, UWORD depth, const GRECT *clip)
{
	GEM_RESIDENT_PD *pd;

	pd = gem_resident_menu_owner();
	if (!pd)
		return FALSE;
	return gem_resident_draw_object_tree(pd, gem_resident_menu.tree,
		object, depth, clip, 0);
}

/*
 * Materialize one member of original GEM's fixed resident M_DESK tree only
 * for the duration of its object draw.  The source state remains the fixed
 * tree in gem_menu_pull_resident; this one-record view merely supplies the
 * existing object renderer with its native OBJECT ABI and resident string.
 */
static WORD
gem_resident_menu_draw_desk_snapshot(GEM_RESIDENT_PD *pd,
	const GEM_MENU_PULL_DESK_OBJECT *snapshot, const GRECT *rectangle)
{
	GEM_BINDINGS_POINTER_SLOT tree;
	UWORD segment;

	if (!pd || !snapshot || !rectangle)
		return FALSE;
	gem_resident_menu_desk_object.ob_next = NIL;
	gem_resident_menu_desk_object.ob_head = NIL;
	gem_resident_menu_desk_object.ob_tail = NIL;
	gem_resident_menu_desk_object.ob_type = snapshot->type;
	gem_resident_menu_desk_object.ob_flags = snapshot->flags | LASTOB;
	gem_resident_menu_desk_object.ob_state = snapshot->state;
	gem_resident_menu_desk_object.ob_x = (UWORD) rectangle->g_x;
	gem_resident_menu_desk_object.ob_y = (UWORD) rectangle->g_y;
	gem_resident_menu_desk_object.ob_width = (UWORD) rectangle->g_w;
	gem_resident_menu_desk_object.ob_height = (UWORD) rectangle->g_h;
	segment = gem_resident_data_segment();
	if ((snapshot->type & 0x00ffU) == G_BOX) {
		/* Exact M_DESK root box specification from GEMMNLIB.C. */
		gem_resident_menu_desk_object.ob_spec.lo = 0x1100U;
		gem_resident_menu_desk_object.ob_spec.hi = 0x00ffU;
	} else {
		gem_resident_menu_desk_object.ob_spec.lo =
			(UWORD) snapshot->text;
		gem_resident_menu_desk_object.ob_spec.hi = segment;
	}
	tree.lo = (UWORD) &gem_resident_menu_desk_object;
	tree.hi = segment;
	return gem_resident_draw_object_tree(pd, tree, ROOT, 0,
		rectangle, segment);
}

/* Draw one M_DESK row or the complete fixed root/row list. */
static WORD
gem_resident_menu_draw_desk(WORD object, UWORD complete,
	const GRECT *effect_rectangle)
{
	GEM_RESIDENT_PD *pd;
	GRECT rectangle;
	WORD root_x;
	WORD root_y;
	UWORD index;
	UWORD last;

	pd = gem_resident_menu_owner();
	if (!pd)
		return FALSE;
	if (!complete) {
		if (object < ROOT
		    || !gem_menu_pull_resident_desk_object((UWORD) object,
			&gem_resident_menu_desk_snapshot))
			return FALSE;
		return gem_resident_menu_draw_desk_snapshot(pd,
			&gem_resident_menu_desk_snapshot, effect_rectangle);
	}
	last = gem_menu_pull_resident_desk_count();
	if (!last || !gem_menu_pull_resident_desk_object(ROOT,
		&gem_resident_menu_desk_snapshot))
		return FALSE;
	gem_resident_menu_rectangle(&gem_resident_menu_desk_snapshot.rectangle,
		&rectangle);
	root_x = rectangle.g_x;
	root_y = rectangle.g_y;
	if (!gem_resident_menu_draw_desk_snapshot(pd,
		&gem_resident_menu_desk_snapshot, &rectangle))
		return FALSE;
	index = 1U;
	while (index <= last) {
		if (!gem_menu_pull_resident_desk_object(index,
			&gem_resident_menu_desk_snapshot))
			return FALSE;
		gem_resident_menu_rectangle(
			&gem_resident_menu_desk_snapshot.rectangle, &rectangle);
		rectangle.g_x = (WORD) ((UWORD) rectangle.g_x
			+ (UWORD) root_x);
		rectangle.g_y = (WORD) ((UWORD) rectangle.g_y
			+ (UWORD) root_y);
		if (!gem_resident_menu_draw_desk_snapshot(pd,
			&gem_resident_menu_desk_snapshot, &rectangle))
			return FALSE;
		index++;
	}
	return TRUE;
}

/*
 * Restore a dismissed pull-down without a large bitmap save.  The Desktop
 * OBJECT tree is repainted only in the exposed rectangle, followed by every
 * original W_ACTIVE frame, original WM_REDRAW delivery, and the menu bar.
 * This is the same bounded redraw strategy used by the existing direct GEM
 * implementation and costs no heap.
 */
static WORD
gem_resident_menu_restore(const GRECT *rectangle, WORD extra_owner)
{
	GRECT bar;

	if (!rectangle || rectangle->g_w <= 0 || rectangle->g_h <= 0)
		return FALSE;
	/*
	 * SAVE_AREA has no bitmap in this XT port.  Recreate original w_redraw():
	 * clear/Desktop/frame drawing is synchronous, then every intersecting
	 * client receives WM_REDRAW for its visible work area.  Reserve one extra
	 * queue record when MN_SELECTED will be appended by the menu manager after
	 * this restoration, preserving the existing all-or-nothing preflight.
	 */
	if (!gem_window_resident_damage(&gem_resident_windows, rectangle,
		&gem_resident_window_effects)
	    || !gem_resident_window_apply_effects_reserved(
		&gem_resident_window_effects,
		extra_owner))
		return FALSE;
	bar.g_x = 0;
	bar.g_y = 0;
	bar.g_w = gem_resident_windows.screen_width;
	bar.g_h = gem_resident_windows.box_height;
	return gem_resident_menu_draw_main(1, 8U, &bar);
}

/*
 * Paint the one opaque backing rectangle which the resident pull-down needs.
 * Classic Desktop pull-downs are visually opaque even though their retained
 * root is the deliberately hollow 00ff:1100 G_BOX.  The resident OBJECT
 * renderer correctly preserves that generic hollow-box meaning, while the
 * G_STRING glyphs are transparent.  Without a menu-owned backing fill, old
 * window text therefore remains visible between those glyphs.
 *
 * SAVE_AREA intentionally owns no bitmap in this ELKS port; dismissal uses a
 * bounded Desktop/window repaint instead.  Keep the original OBJECT and VDI
 * hollow-fill semantics unchanged, and establish the menu surface here with
 * one scale-one native-white rectangle immediately before ob_draw.  The
 * following object draw resets the exact menu clip, redraws the black outer
 * border and rows, and performs the only required flush.  Cursor hide/show
 * protects its saved background during this direct framebuffer fill.
 */
static WORD
gem_resident_menu_opaque_backing(const GRECT *rectangle)
{
	GEM_VDI_SCREEN *screen;

	if (!rectangle || rectangle->g_w <= 0 || rectangle->g_h <= 0)
		return FALSE;
	screen = gem_vdi_resident_screen();
	if (!screen)
		return FALSE;
	gem_vdi_set_clip(screen, 0, NULL);
	gem_vdi_hide_cursor(screen);
	gem_vdi_set_mode(GEM_VDI_REPLACE);
	gem_vdi_set_foreground(screen, GEM_RESIDENT_NATIVE_WHITE);
	gem_vdi_fill_rect(screen, rectangle->g_x, rectangle->g_y,
		rectangle->g_w, rectangle->g_h);
	gem_vdi_show_cursor(screen);
	return TRUE;
}

/* Apply ordered GEMMNLIB drawing and one optional original AES message. */
static WORD
gem_resident_menu_apply_effects(const GEM_MENU_PULL_EFFECTS *effects)
{
	const GEM_MENU_PULL_DRAW_EFFECT *draw;
	GEM_RESIDENT_PD *target;
	GRECT rectangle;
	UWORD index;

	if (!effects || effects->draw_count > GEM_MENU_PULL_DRAW_EFFECTS)
		return FALSE;
	target = (GEM_RESIDENT_PD *) 0;
	if (effects->message_ready) {
		if (effects->target_owner >= GEM_PROC_CHANNELS)
			return FALSE;
		target = gem_resident_pd_for_channel(
			(WORD) effects->target_owner);
		if (!target || target->state != GEM_PD_ATTACHED
		    || target->generation_lo != effects->target_generation_lo
		    || target->generation_hi != effects->target_generation_hi)
			return FALSE;
		gem_resident_queue_progress(target);
		if (target->queue_index
		    > GEM_RESIDENT_QUEUE_BYTES - 16U)
			return FALSE;
	}
	draw = effects->draw;
	index = effects->draw_count;
	while (index--) {
		gem_resident_menu_rectangle(&draw->rectangle, &rectangle);
		switch (draw->action) {
		case GEM_MENU_PULL_SAVE_AREA:
			/* Restore is an exact clipped repaint; no backing bitmap. */
			break;
		case GEM_MENU_PULL_DRAW_OBJECT:
			/*
			 * G_TITLE and G_STRING glyphs are transparent.  Original
			 * do_chg() toggled SELECTED with XOR, so deselection restored
			 * the white pixels underneath.  This retained-tree path draws
			 * an explicit state instead; clear its small object rectangle
			 * first so a normal title or row cannot leave the old black
			 * SELECTED fill behind.
			 */
			if (!gem_resident_menu_opaque_backing(&rectangle))
				return FALSE;
			if (draw->tree_kind == GEM_MENU_PULL_TREE_DESK) {
				if (!gem_resident_menu_draw_desk(draw->object,
					FALSE, &rectangle))
					return FALSE;
			} else if (!gem_resident_menu_draw_main(draw->object,
				0, &rectangle))
				return FALSE;
			break;
		case GEM_MENU_PULL_DRAW_MENU:
			if (!gem_resident_menu_opaque_backing(&rectangle))
				return FALSE;
			if (draw->tree_kind == GEM_MENU_PULL_TREE_DESK) {
				if (!gem_resident_menu_draw_desk(draw->object,
					TRUE, &rectangle))
					return FALSE;
			} else if (!gem_resident_menu_draw_main(draw->object,
				8U, &rectangle))
				return FALSE;
			break;
		case GEM_MENU_PULL_RESTORE_AREA:
			if (!gem_resident_menu_restore(&rectangle,
				target ? (WORD) effects->target_owner : NIL))
				return FALSE;
			break;
		case GEM_MENU_PULL_REDRAW_BAR:
			if (!gem_resident_menu_draw_main(draw->object,
				8U, &rectangle))
				return FALSE;
			break;
		default:
			return FALSE;
		}
		draw++;
	}
	if (effects->redraw_all) {
		rectangle.g_x = 0;
		rectangle.g_y = 0;
		rectangle.g_w = gem_resident_windows.screen_width;
		rectangle.g_h = gem_resident_windows.box_height;
		/*
		 * A newly active application may install fewer transparent G_TITLE
		 * objects than the previous owner.  Clear the complete menu strip first
		 * so retired title glyphs cannot survive an ownership switch.
		 */
		if (!gem_resident_menu_opaque_backing(&rectangle)
		    || !gem_resident_menu_draw_main(1, 8U, &rectangle))
			return FALSE;
	}
	if (target) {
		gem_resident_enqueue_window_message(target, effects->message);
		gem_resident_queue_progress(target);
	}
	return TRUE;
}

static VOID
gem_resident_window_detach_owner(GEM_RESIDENT_PD *pd, WORD channel)
{
	gem_window_resident_detach(&gem_resident_windows, channel,
		pd->generation_lo, pd->generation_hi,
		&gem_resident_window_effects);
	/* A dying owner must be detached even if another PD queue is already full. */
	(void) gem_resident_window_apply_effects(&gem_resident_window_effects);
}

/*
 * Acquire or release one level of the original recursive update lock through
 * the already-authoritative WIND_UPDATE manager.  GRAF never owns a parallel
 * lock: this exact PD/generation is therefore ordered with window management
 * and any update nesting already held by the same application.
 */
static WORD
gem_resident_graf_update_owner(GEM_RESIDENT_PD *pd, WORD channel,
	UWORD operation)
{
	WORD handled;
	WORD result;

	if (!pd || channel < 0 || channel >= GEM_PROC_CHANNELS)
		return FALSE;
	gem_resident_graf_update_control[0] = GEM_STARTUP_WIND_UPDATE;
	gem_resident_graf_update_control[1] = 1U;
	gem_resident_graf_update_control[2] = 1U;
	gem_resident_graf_update_control[3] = 0;
	gem_resident_graf_update_control[4] = 0;
	gem_resident_graf_update_input[0] = operation;
	gem_resident_graf_update_output[0] = FALSE;
	gem_resident_graf_update_call.owner = (UWORD) channel;
	gem_resident_graf_update_call.generation_lo = pd->generation_lo;
	gem_resident_graf_update_call.generation_hi = pd->generation_hi;
	gem_resident_graf_update_call.control = gem_resident_graf_update_control;
	gem_resident_graf_update_call.int_in = gem_resident_graf_update_input;
	gem_resident_graf_update_call.int_out = gem_resident_graf_update_output;
	gem_resident_graf_update_call.addr_in =
		(const GEM_BINDINGS_POINTER_SLOT *) 0;
	result = gem_startup_resident_dispatch(&gem_resident_graf_update_call,
		&gem_resident_graf_update_effects, &handled);
	result = handled && result;
	if (result && operation == GEM_RESIDENT_END_UPDATE)
		gem_resident_update_progress();
	return result;
}

/*
 * Transfer a released update semaphore to the oldest live waiter before that
 * waiter is replied.  Acquiring through the authoritative startup manager
 * establishes depth one first, matching GEMFLAG.C unsync(); only then is the
 * retained INT EF call completed.  A still-recursive owner leaves the FIFO
 * untouched.  Stale generations are failed and skipped without acquiring the
 * lock on behalf of a reused channel.
 */
static VOID GEM_RESIDENT_COLD
gem_resident_update_progress(VOID)
{
	GEM_RESIDENT_PENDING *pending;
	GEM_RESIDENT_PD *pd;
	UBYTE index;

	for (;;) {
		index = gem_resident_update_head;
		if (index == GEM_RESIDENT_INDEX_NONE)
			return;
		pending = gem_resident_pending_for_channel((WORD) index);
		if (!pending || pending->state != GEM_PENDING_WAITING
		    || pending->operation != GEM_PENDING_UPDATE) {
			(void) gem_resident_update_pop();
			continue;
		}
		pd = gem_resident_pd_for_channel((WORD) index);
		if (!pd || pd->state != GEM_PD_ATTACHED
		    || pd->pid != pending->request.pid
		    || pd->segment != pending->request.ds
		    || pd->task_slot != (UBYTE) pending->request.slot
		    || pd->generation_lo != pending->buffer_offset
		    || pd->generation_hi != pending->length) {
			(void) gem_resident_update_pop();
			gem_resident_complete(index, -1);
			continue;
		}
		if (!gem_resident_graf_update_owner(pd, (WORD) index,
			GEM_RESIDENT_BEG_UPDATE))
			return;
		(void) gem_resident_update_pop();
		gem_resident_complete(index, TRUE);
		return;
	}
}

/* Apply a dispatch/completion pair in original begin-then-end order. */
static WORD
gem_resident_graf_apply_effects(GEM_RESIDENT_PD *pd, WORD channel,
	const GEM_GRAF_EFFECTS *effects)
{
	if (!effects)
		return TRUE;
	if (effects->begin_update
	    && !gem_resident_graf_update_owner(pd, channel,
		GEM_RESIDENT_BEG_UPDATE))
		return FALSE;
	if (effects->end_update
	    && !gem_resident_graf_update_owner(pd, channel,
		GEM_RESIDENT_END_UPDATE))
		return FALSE;
	return TRUE;
}

/*
 * Apply one original GEM form-manager effect without copying or converting
 * its OBJECT tree.  Rectangle values are scale-one pixels.  A caller tree is
 * accepted only through the pinned client/RSC ranges checked by the object
 * manager; the fixed alert tree is accepted only in this resident AES data
 * segment.  The final END_UPDATE is attempted even if a repaint fails, so a
 * dying or completed form cannot strand the recursive screen lock.
 */
static WORD
gem_resident_form_apply_effects(GEM_RESIDENT_PD *pd, WORD channel,
	const GEM_FORM_EFFECTS *effects)
{
	GRECT rectangle;
	UWORD resident_segment;
	WORD result;

	if (!effects)
		return TRUE;
	result = TRUE;
	if (effects->begin_update
	    && !gem_resident_graf_update_owner(pd, channel,
		GEM_RESIDENT_BEG_UPDATE))
		result = FALSE;

	rectangle.g_x = effects->rectangle.x;
	rectangle.g_y = effects->rectangle.y;
	rectangle.g_w = effects->rectangle.width;
	rectangle.g_h = effects->rectangle.height;
	if (result && effects->draw_tree) {
		resident_segment = 0;
		if (effects->tree_kind == GEM_FORM_TREE_ALERT) {
			resident_segment = gem_resident_data_segment();
			if (effects->resident_segment != resident_segment
			    || effects->tree.hi != resident_segment)
				result = FALSE;
		} else if (effects->tree_kind != GEM_FORM_TREE_CALLER
			   || effects->resident_segment) {
			result = FALSE;
		}
		if (result && !gem_resident_draw_object_tree(pd, effects->tree,
			ROOT, 8U, &rectangle, resident_segment))
			result = FALSE;
	}
	if (result && effects->redraw_background) {
		if (rectangle.g_w <= 0 || rectangle.g_h <= 0
		    || !gem_resident_menu_restore(&rectangle, NIL))
			result = FALSE;
	}
	if (effects->end_update
	    && !gem_resident_graf_update_owner(pd, channel,
		GEM_RESIDENT_END_UPDATE))
		result = FALSE;
	return result;
}

/* Balance a generation's retained FORM_DO/alert before its RSC is released. */
static WORD
gem_resident_form_detach_owner(GEM_RESIDENT_PD *pd, WORD channel)
{
	UWORD owner_segment;

	if (!pd)
		return FALSE;
	owner_segment = gem_resident_data_segment();
	gem_form_resident_detach((UWORD) channel, pd->generation_lo,
		pd->generation_hi, &gem_resident_form_effects);
	__asm__ volatile ("movw %0,%%ds" : : "r" (owner_segment) : "memory");
	return gem_resident_form_apply_effects(pd, channel,
		&gem_resident_form_effects);
}

/* Erase a dying generation's XOR outline before its pinned RSC can vanish. */
static WORD
gem_resident_graf_detach_owner(GEM_RESIDENT_PD *pd, WORD channel)
{
	GEM_GRAF_EFFECTS effects;

	if (!pd)
		return FALSE;
	gem_graf_resident_detach((UWORD) channel, pd->generation_lo,
		pd->generation_hi, &effects);
	return gem_resident_graf_apply_effects(pd, channel, &effects);
}

static VOID
gem_resident_menu_detach_owner(GEM_RESIDENT_PD *pd, WORD channel)
{
	gem_menu_pull_resident_detach((UWORD) channel,
		pd->generation_lo, pd->generation_hi,
		&gem_resident_menu_effects);
	/* EXIT cleanup is generation-final; drawing failure cannot retain it. */
	(void) gem_resident_menu_apply_effects(&gem_resident_menu_effects);
	gem_menu_object_resident_detach(&gem_resident_menu, channel,
		pd->generation_lo, pd->generation_hi);
}

static WORD
gem_resident_attach(const struct gemtrap_request *request, WORD channel)
{
	struct gemtrap_request attachment;
	GEM_RESIDENT_PD *pd;

	if (channel < 0 || channel >= GEM_PROC_CHANNELS)
		return FALSE;
	pd = gem_resident_pd_for_channel(channel);
	if (!pd)
		return FALSE;
	if (pd->state != GEM_PD_FREE)
		return FALSE;

	attachment = *request;
	attachment.ax = (UWORD) channel;
	if (gemctl(GEMCTL_ATTACH, &attachment) != 0)
		return FALSE;

	pd->pid = request->pid;
	pd->segment = request->ds;
	pd->limit = request->data_limit;
	pd->generation_lo = request->generation_lo;
	pd->generation_hi = request->generation_hi;
	pd->task_slot = (UBYTE) request->slot;
	pd->state = GEM_PD_ATTACHED;
	return TRUE;
}

static WORD
gem_resident_detach(const struct gemtrap_request *request,
		    GEM_RESIDENT_PD *pd, WORD channel)
{
	struct gemtrap_request attachment;

	/* Restore form/tracker/menu/window pixels while every RSC is resident. */
	if (!gem_resident_form_detach_owner(pd, channel))
		return FALSE;
	if (!gem_resident_graf_detach_owner(pd, channel))
		return FALSE;
	gem_resident_menu_detach_owner(pd, channel);
	gem_resident_window_detach_owner(pd, channel);
	gem_event_resident_detach((UWORD) channel, pd->generation_lo,
		pd->generation_hi);
	gem_shell_resident_detach((UWORD) channel, pd->generation_lo,
		pd->generation_hi);
	gem_startup_resident_detach((UWORD) channel, pd->generation_lo,
		pd->generation_hi);
	gem_resident_update_progress();
	/* The far RSC segment belongs to this exact attachment generation. */
	if (!gem_resource_resident_cleanup(&pd->resource))
		return FALSE;
	attachment = *request;
	attachment.ax = (UWORD) channel;
	attachment.generation_lo = pd->generation_lo;
	attachment.generation_hi = pd->generation_hi;
	if (gemctl(GEMCTL_DETACH, &attachment) != 0)
		return FALSE;
	gem_resident_clear_pd(pd);
	return TRUE;
}

static WORD
gem_resident_xbuf_init(const struct gemtrap_request *request,
		       GEM_BINDINGS_POINTER_SLOT pointer)
{
	GEM_RESIDENT_XBUF xbuf;
	UWORD caller_bytes;
	UWORD result_bytes;

	if (!pointer.lo && !pointer.hi)
		return TRUE;
	if (pointer.hi != request->ds
	    || !gem_resident_memory_range(pointer.lo, 2, request->data_limit))
		return FALSE;

	gem_resident_memory_from(request->ds, pointer.lo, &caller_bytes, 2);
	result_bytes = caller_bytes;
	if (result_bytes > (UWORD) sizeof(xbuf))
		result_bytes = (UWORD) sizeof(xbuf);
	if (!gem_resident_memory_range(pointer.lo, result_bytes,
				request->data_limit))
		return FALSE;

	xbuf.buf_len = result_bytes;
	xbuf.arch = 16;
	xbuf.color_categories = 0;
	xbuf.active_window_tree = 0;
	xbuf.information = 0;
	xbuf.abilities.lo = 0;
	xbuf.abilities.hi = 0;
	gem_resident_memory_to(&xbuf, request->ds, pointer.lo, result_bytes);
	return TRUE;
}

static WORD
gem_resident_appl_init(const struct gemtrap_request *request)
{
	GEM_RESIDENT_PD *pd;
	WORD channel;
	UWORD index;

	pd = gem_resident_pd_for_request(request, &channel);
	if (!pd) {
		/*
		 * The direct-linked build has exactly one client, the Desktop
		 * linked into this executable, so APPL_INIT always binds the
		 * original channel zero.  No adoption, PID map, or logical
		 * process table exists; ELKS alone owns every real process.
		 */
		channel = GEM_PROC_DESKTOP;
		if (!gem_resident_attach(request, channel))
			return -1;
	}
	if (channel == GEM_PROC_DESKTOP)
		gem_resident_nameit(gem_resident_pd_for_channel(channel),
				    "DESKTOP");

	/* Direct MULTIAPP GEMSUPER.C global initialization, ELKS channel bound. */
	aes_global[0] = 0x0110U;
	aes_global[1] = GEM_PROC_CHANNELS - 1;
	aes_global[2] = (UWORD) channel;
	index = 3;
	while (index < G_SIZE_RESIDENT)
		aes_global[index++] = 0;

	if (!gem_resident_xbuf_init(request, addr_in[0])) {
		pd = gem_resident_pd_for_channel(channel);
		if (pd)
			(void) gem_resident_detach(request, pd, channel);
		return -1;
	}
	return channel;
}

/* Direct bounded adaptation of GEMAPLIB.C ap_find(). */
static WORD
gem_resident_appl_find(const struct gemtrap_request *request)
{
	GEM_RESIDENT_PD *pd;
	BYTE name[9];
	UWORD available;
	UWORD count;
	UWORD index;
	WORD channel;

	if (addr_in[0].hi != request->ds
	    || addr_in[0].lo >= request->data_limit)
		return -1;
	available = (UWORD) (request->data_limit - addr_in[0].lo);
	count = available;
	if (count > 9U)
		count = 9U;
	gem_resident_memory_from(request->ds, addr_in[0].lo, name, count);
	index = 0;
	while (index < count && name[index])
		index++;
	if (index == count)
		return -1;

	pd = gem_resident_pds;
	channel = 0;
	while (channel < GEM_PROC_CHANNELS) {
		if (pd->state == GEM_PD_ATTACHED) {
			index = 0;
			while (index < 8U && name[index] == pd->name[index])
				index++;
			if (index == 8U && name[8] == '\0')
				return channel;
		}
		pd++;
		channel++;
	}
	return -1;
}

static WORD
gem_resident_appl_defer(const struct gemtrap_request *request,
			GEM_RESIDENT_PD *target_pd, WORD caller_channel,
			WORD target_channel, UBYTE operation,
			UWORD length, UWORD output_offset)
{
	GEM_RESIDENT_PENDING *pending;

	pending = gem_resident_pending_for_channel(caller_channel);
	if (!pending || pending->state != GEM_PENDING_FREE)
		return -1;
	pending->request = *request;
	pending->buffer_offset = addr_in[0].lo;
	pending->output_offset = output_offset;
	pending->length = length;
	pending->result = 0;
	pending->target = (UBYTE) target_channel;
	pending->operation = operation;
	pending->state = GEM_PENDING_WAITING;
	gem_resident_wait_append(target_pd, operation,
				 (UBYTE) caller_channel);
	return GEM_AES_RESIDENT_DEFERRED;
}

/*
 * GEMAPLIB.C ap_rdwr() and GEMQUEUE.C aqueue()/doq() retained one queue per
 * target PD.  Immediate copies use that same queue.  A non-ready operation
 * keeps the delivered kernel request as its event block so the sole resident
 * owner can dequeue another real ELKS client and establish the rendezvous.
 */
static WORD
gem_resident_appl_rdwr(const struct gemtrap_request *request,
		       UBYTE operation, UWORD output_offset)
{
	GEM_RESIDENT_PENDING *caller_pending;
	GEM_RESIDENT_PD *target_pd;
	WORD caller_channel;
	WORD target_channel;
	UWORD length;

	if (!gem_resident_pd_for_request(request, &caller_channel))
		return -1;
	target_channel = (WORD) int_in[0];
	target_pd = gem_resident_pd_for_channel(target_channel);
	if (!target_pd || target_pd->state != GEM_PD_ATTACHED)
		return -1;
	length = int_in[1];
	if (length > GEM_RESIDENT_QUEUE_BYTES
	    || !gem_resident_memory_pointer(request, addr_in[0], length))
		return -1;
	if (!length)
		return TRUE;

	caller_pending = gem_resident_pending_for_channel(caller_channel);
	if (!caller_pending || caller_pending->state != GEM_PENDING_FREE)
		return -1;

	if (operation == GEM_PENDING_WRITE) {
		if (length <= (UWORD)
		    (GEM_RESIDENT_QUEUE_BYTES - target_pd->queue_index)) {
			gem_resident_enqueue(target_pd, request->ds,
					     addr_in[0].lo, length);
			gem_resident_queue_progress(target_pd);
			return TRUE;
		}
	} else if (length <= target_pd->queue_index) {
		gem_resident_dequeue(target_pd, request->ds,
				     addr_in[0].lo, length);
		gem_resident_queue_progress(target_pd);
		return TRUE;
	}

	return gem_resident_appl_defer(request, target_pd, caller_channel,
				       target_channel, operation, length,
				       output_offset);
}

static VOID
gem_resident_clear_pending(GEM_RESIDENT_PENDING *pending)
{
	pending->buffer_offset = 0;
	pending->output_offset = 0;
	pending->length = 0;
	pending->result = 0;
	pending->target = 0;
	pending->next = GEM_RESIDENT_INDEX_NONE;
	pending->state = GEM_PENDING_FREE;
	pending->operation = 0;
}

static WORD
gem_resident_cancel_pending(GEM_RESIDENT_PENDING *pending)
{
	WORD result;

	result = gemctl(GEMCTL_CANCEL, &pending->request);
	if (result != 0 && errno != ESRCH)
		return FALSE;
	gem_resident_clear_pending(pending);
	return TRUE;
}

/*
 * Remove one dying original PD from the event graph.  Its own delivered trap
 * is cancelled, releasing the request segment pin.  Writers waiting for its
 * queue receive the original signed failure and are allowed to run; no event
 * may retain a link to a channel after its attachment is detached.
 */
static WORD
gem_resident_drop_channel(WORD channel)
{
	GEM_RESIDENT_PENDING *pending;
	GEM_RESIDENT_PD *pd;
	GEM_RESIDENT_PD *target_pd;
	WORD index;
	WORD result;

	result = TRUE;
	pd = gem_resident_pd_for_channel(channel);
	/*
	 * A retained FORM or GRAF request may still own visible pixels and one
	 * WIND_UPDATE depth.  Restore/release both before GEMCTL_CANCEL unpins the
	 * request DS or later cleanup releases its direct RSC segment.
	 */
	if (pd && !gem_resident_form_detach_owner(pd, channel))
		result = FALSE;
	if (pd && !gem_resident_graf_detach_owner(pd, channel))
		result = FALSE;
	pending = gem_resident_pending;
	index = 0;
	while (index < GEM_PROC_CHANNELS) {
		if (pending->state == GEM_PENDING_WAITING
		    && (index == channel || pending->target == (UBYTE) channel)) {
			if (pending->operation == GEM_PENDING_UPDATE) {
				gem_resident_update_remove((UBYTE) index);
			} else if (pending->operation != GEM_PENDING_EVENT
			    && pending->operation != GEM_PENDING_GRAF
			    && pending->operation != GEM_PENDING_FORM) {
				target_pd = gem_resident_pd_for_channel(
					(WORD) pending->target);
				if (target_pd)
					gem_resident_wait_remove(target_pd,
						pending->operation, (UBYTE) index);
			}
			if (index == channel) {
				if (!gem_resident_cancel_pending(pending))
					result = FALSE;
			} else {
				gem_resident_complete((UBYTE) index, -1);
			}
		} else if (pending->state == GEM_PENDING_READY
			   && index == channel) {
			gem_resident_ready_remove((UBYTE) index);
			if (!gem_resident_cancel_pending(pending))
				result = FALSE;
		}
		pending++;
		index++;
	}
	if (pd) {
		gem_event_resident_detach((UWORD) channel,
			pd->generation_lo, pd->generation_hi);
		pd->queue_index = 0;
		pd->read_head = GEM_RESIDENT_INDEX_NONE;
		pd->read_tail = GEM_RESIDENT_INDEX_NONE;
		pd->write_head = GEM_RESIDENT_INDEX_NONE;
		pd->write_tail = GEM_RESIDENT_INDEX_NONE;
	}
	return result;
}

static WORD
gem_resident_copy_command(const struct gemtrap_request *request,
			  GEM_BINDINGS_POINTER_SLOT pointer)
{
	UWORD available;
	UWORD count;
	UWORD index;

	if (pointer.hi != request->ds || pointer.lo >= request->data_limit)
		return FALSE;
	available = (UWORD) (request->data_limit - pointer.lo);
	count = available;
	if (count > GEM_RESIDENT_COMMAND_BYTES)
		count = GEM_RESIDENT_COMMAND_BYTES;
	gem_resident_memory_from(request->ds, pointer.lo,
			       gem_resident_command, count);
	index = 0;
	while (index < count) {
		if (!gem_resident_command[index])
			return TRUE;
		index++;
	}
	return FALSE;
}

static WORD
gem_resident_copy_tail(const struct gemtrap_request *request,
		       GEM_BINDINGS_POINTER_SLOT pointer, BYTE **tail)
{
	UBYTE length;
	UWORD bytes;

	if (!pointer.lo && !pointer.hi) {
		*tail = NULL;
		return TRUE;
	}
	if (pointer.hi != request->ds
	    || !gem_resident_memory_range(pointer.lo, 1, request->data_limit))
		return FALSE;
	gem_resident_memory_from(request->ds, pointer.lo, &length, 1);
	if (length > 127U)
		return FALSE;
	bytes = (UWORD) length + 1U;
	if (!gem_resident_memory_range(pointer.lo, bytes, request->data_limit))
		return FALSE;
	gem_resident_memory_from(request->ds, pointer.lo,
			       gem_resident_tail, bytes);
	*tail = gem_resident_tail;
	return TRUE;
}

/*
 * Preserve GEMRSLIB's application-global ownership words.  The authoritative
 * data remains the one per-generation resident descriptor; these five words
 * are only the original client-visible view copied by XIF on every request.
 */
static WORD
gem_resident_resource_global(GEM_RESIDENT_PD *pd)
{
	GEM_FAR_ADDRESS tree_table;

	if (!(pd->resource.flags & GEM_RESOURCE_RESIDENT_LOADED)) {
		aes_global[GEM_GLOBAL_TREE_OFFSET] = 0;
		aes_global[GEM_GLOBAL_TREE_SEGMENT] = 0;
		aes_global[GEM_GLOBAL_RESOURCE_OFFSET] = 0;
		aes_global[GEM_GLOBAL_RESOURCE_SEGMENT] = 0;
		aes_global[GEM_GLOBAL_RESOURCE_BYTES] = 0;
		return TRUE;
	}
	if (!gem_resource_resident_tree_table(&pd->resource, &tree_table))
		return FALSE;
	aes_global[GEM_GLOBAL_TREE_OFFSET] = tree_table.lo;
	aes_global[GEM_GLOBAL_TREE_SEGMENT] = tree_table.hi;
	aes_global[GEM_GLOBAL_RESOURCE_OFFSET] = pd->resource.storage.base.lo;
	aes_global[GEM_GLOBAL_RESOURCE_SEGMENT] = pd->resource.storage.base.hi;
	aes_global[GEM_GLOBAL_RESOURCE_BYTES] = pd->resource.storage.bytes;
	return TRUE;
}

/*
 * A replacement address may name only the calling application's pinned data
 * segment or its own resident resource segment.  Null and GEM's ffff:ffff
 * sentinel are copied exactly.  Counts are unscaled bytes and subtraction is
 * performed before comparison so a wrapped offset can never pass.
 */
static WORD
gem_resident_resource_address_valid(const struct gemtrap_request *request,
				    const GEM_RESIDENT_PD *pd,
				    GEM_FAR_ADDRESS address)
{
	if ((!address.lo && !address.hi)
	    || (address.lo == 0xffffU && address.hi == 0xffffU))
		return TRUE;
	if (address.hi == request->ds)
		return gem_resident_memory_range(address.lo, 1U,
			request->data_limit);
	if ((pd->resource.flags & GEM_RESOURCE_RESIDENT_LOADED)
	    && address.hi == pd->resource.storage.base.hi)
		return gem_resident_memory_range(address.lo, 1U,
			pd->resource.storage.bytes);
	return FALSE;
}

/* Direct resident-PD adaptation of GEMSUPER.C's RSRC cases 110 through 114. */
static WORD
gem_resident_resource(const struct gemtrap_request *request, WORD opcode)
{
	GEM_RESOURCE_METRICS metrics;
	GEM_RESIDENT_PD *pd;
	UWORD owner_segment;
	WORD channel;
	WORD result;

	pd = gem_resident_pd_for_request(request, &channel);
	if (!pd)
		return -1;
	__asm__ volatile ("movw %%ds,%0" : "=r" (owner_segment));
	result = FALSE;
	switch (opcode) {
	case RSRC_LOAD:
		if (!gem_resident_copy_command(request, addr_in[0])
		    || !gem_vdi_resident_get_metrics(&metrics.screen_width,
			&metrics.screen_height, &metrics.character_width,
			&metrics.character_height))
			break;
		/* GEMINIT.C selected GEM/2 outlined-root compatibility by default. */
		metrics.options = GEM_RESOURCE_OPTION_OUTLINED_ROOT;
		result = gem_resource_resident_load(&pd->resource,
			gem_resident_command, &metrics);
		break;
	case RSRC_FREE:
		/* An active original menu may not retain a freed RSC tree. */
		if (gem_resident_menu.visible
		    == GEM_MENU_OBJECT_RESIDENT_VISIBLE
		    && gem_resident_menu.owner == channel
		    && gem_resident_menu.generation_lo == pd->generation_lo
		    && gem_resident_menu.generation_hi == pd->generation_hi) {
			if (!gem_menu_pull_resident_deactivate((UWORD) channel,
				pd->generation_lo, pd->generation_hi,
				&gem_resident_menu_effects)
			    || !gem_resident_menu_apply_effects(
				&gem_resident_menu_effects))
				break;
		}
		gem_menu_object_resident_detach(&gem_resident_menu, channel,
			pd->generation_lo, pd->generation_hi);
		result = gem_resource_resident_free(&pd->resource);
		break;
	case RSRC_GADDR:
		result = gem_resource_resident_gaddr(&pd->resource, int_in[0],
			int_in[1], &addr_out[0]);
		break;
	case RSRC_SADDR:
		if (!gem_resident_resource_address_valid(request, pd, addr_in[0]))
			break;
		result = gem_resource_resident_saddr(&pd->resource, int_in[0],
			int_in[1], addr_in[0]);
		break;
	case RSRC_OBFIX:
		if (addr_in[0].hi != pd->resource.storage.base.hi)
			break;
		result = gem_resource_resident_obfix(&pd->resource, addr_in[0],
			int_in[0]);
		break;
	default:
		return -1;
	}

	/*
	 * Resource records live in an independent paragraph segment.  GNU ia16
	 * normally restores DS after each generated far-data access, but this
	 * owner boundary must never let a resource DS reach XIF's client-global
	 * copy.  Restore the exact DS captured before dispatch; no DS/SS alias is
	 * assumed.  MOV Sreg,r16 is an original 8086 instruction and costs no
	 * arithmetic or helper call.
	 */
	__asm__ volatile ("movw %0,%%ds" : : "r" (owner_segment) : "memory");
	if (!gem_resident_resource_global(pd))
		return -1;
	return result;
}

/* Direct resident adaptation of original GEMSUPER.C MENU_BAR dispatch. */
static WORD
gem_resident_menu_bar(const struct gemtrap_request *request)
{
	GEM_MENU_PULL_TREE view;
	GEM_RESIDENT_PD *pd;
	WORD old_owner;
	UWORD old_generation_lo;
	UWORD old_generation_hi;
	WORD channel;
	WORD result;

	pd = gem_resident_pd_for_request(request, &channel);
	if (!pd)
		return -1;
	if (!int_in[0]) {
		if (gem_menu_pull_resident_deactivate((UWORD) channel,
			pd->generation_lo, pd->generation_hi,
			&gem_resident_menu_effects)
		    && !gem_resident_menu_apply_effects(
			&gem_resident_menu_effects))
			return -1;
		return gem_menu_object_resident_bar(&gem_resident_menu,
			&pd->resource, addr_in[0], FALSE, channel,
			pd->generation_lo, pd->generation_hi);
	}

	/* Close any prior generation's tracking while its tree is still active. */
	if (gem_resident_menu.visible == GEM_MENU_OBJECT_RESIDENT_VISIBLE) {
		old_owner = gem_resident_menu.owner;
		old_generation_lo = gem_resident_menu.generation_lo;
		old_generation_hi = gem_resident_menu.generation_hi;
		if (gem_menu_pull_resident_deactivate((UWORD) old_owner,
			old_generation_lo, old_generation_hi,
			&gem_resident_menu_effects)
		    && !gem_resident_menu_apply_effects(
			&gem_resident_menu_effects))
			return -1;
	}
	result = gem_menu_object_resident_bar(&gem_resident_menu,
		&pd->resource, addr_in[0], TRUE, channel,
		pd->generation_lo, pd->generation_hi);
	if (!result)
		return FALSE;
	if (!gem_menu_pull_resident_tree_from_resource(&view, &pd->resource,
		addr_in[0], gem_resident_menu.object_count)
	    || !gem_menu_pull_resident_activate(&view, (UWORD) channel,
		pd->generation_lo, pd->generation_hi,
		&gem_resident_menu_effects)
	    || !gem_resident_menu_apply_effects(&gem_resident_menu_effects)) {
		(void) gem_menu_object_resident_bar(&gem_resident_menu,
			&pd->resource, addr_in[0], FALSE, channel,
			pd->generation_lo, pd->generation_hi);
		return FALSE;
	}
	return TRUE;
}

/* Validate a menu text pointer and retain its exact remaining byte count. */
static WORD
gem_resident_menu_text(const struct gemtrap_request *request,
	const GEM_RESIDENT_PD *pd, GEM_BINDINGS_POINTER_SLOT address,
	GEM_RESIDENT_MENU_TEXT_POINTER *pointer, UWORD *limit)
{
	if (!request || !pd || !pointer || !limit || !address.hi)
		return FALSE;
	if (address.hi == request->ds) {
		if (!gem_resident_memory_range(address.lo, 1U,
			request->data_limit))
			return FALSE;
		*limit = (UWORD) (request->data_limit - address.lo);
	} else if ((pd->resource.flags & GEM_RESOURCE_RESIDENT_LOADED)
		   && address.hi == pd->resource.storage.base.hi
		   && gem_resident_memory_range(address.lo, 1U,
			pd->resource.storage.bytes)) {
		*limit = (UWORD) (pd->resource.storage.bytes - address.lo);
	} else
		return FALSE;
	pointer->address.lo = address.lo;
	pointer->address.hi = address.hi;
	return TRUE;
}

/* Direct resident adaptation of GEMSUPER.C MENU_ICHECK through MENU_CLICK. */
static WORD
gem_resident_menu_pull(const struct gemtrap_request *request)
{
	GEM_MENU_PULL_CALL call;
	GEM_RESIDENT_MENU_TEXT_POINTER text;
	GEM_RESIDENT_PD *pd;
	GEM_MENU_PULL_TREE tree;
	GEM_BINDINGS_POINTER_SLOT text_address;
	UWORD object_count;
	UWORD owner_segment;
	UWORD text_limit;
	UWORD opcode;
	WORD channel;
	WORD handled;
	WORD result;

	pd = gem_resident_pd_for_request(request, &channel);
	if (!pd)
		return -1;
	opcode = control[0];
	owner_segment = gem_resident_data_segment();
	tree.resource = (GEM_MENU_PULL_BYTE_POINTER) 0;
	tree.bytes = 0;
	tree.segment = 0;
	tree.tree_offset = 0;
	tree.object_count = 0;
	if (opcode >= GEM_MENU_PULL_ICHECK
	    && opcode <= GEM_MENU_PULL_TEXT) {
		result = gem_menu_object_resident_tree_count(&pd->resource,
			addr_in[0], &object_count);
		__asm__ volatile ("movw %0,%%ds" : : "r" (owner_segment)
			: "memory");
		if (!result)
			return FALSE;
		result = gem_menu_pull_resident_tree_from_resource(&tree,
			&pd->resource, addr_in[0], object_count);
		__asm__ volatile ("movw %0,%%ds" : : "r" (owner_segment)
			: "memory");
		if (!result)
			return FALSE;
	}
	text.pointer = (GEM_MENU_PULL_TEXT_POINTER) 0;
	text_limit = 0;
	text_address.lo = 0;
	text_address.hi = 0;
	if (opcode == GEM_MENU_PULL_TEXT)
		text_address = addr_in[1];
	else if (opcode == GEM_MENU_PULL_REGISTER)
		text_address = addr_in[0];
	if (text_address.hi
	    && !gem_resident_menu_text(request, pd, text_address,
		&text, &text_limit))
		return FALSE;

	call.owner = (UWORD) channel;
	call.generation_lo = pd->generation_lo;
	call.generation_hi = pd->generation_hi;
	call.control = control;
	call.int_in = int_in;
	call.int_out = int_out;
	call.tree = tree;
	call.text = text.pointer;
	call.text_limit = text_limit;
	handled = FALSE;
	result = gem_menu_pull_resident_dispatch(&call,
		&gem_resident_menu_effects, &handled);
	__asm__ volatile ("movw %0,%%ds" : : "r" (owner_segment)
		: "memory");
	if (!handled)
		return -1;
	if (!gem_resident_menu_apply_effects(&gem_resident_menu_effects))
		return -1;
	return result;
}

/* Direct resident adaptation of GEMSUPER.C OBJC_* manager dispatch. */
static WORD
gem_resident_object(const struct gemtrap_request *request)
{
	GEM_OBJECT_RESIDENT_CALL call;
	GEM_RESIDENT_PD *pd;
	WORD channel;
	WORD handled;
	WORD result;

	pd = gem_resident_pd_for_request(request, &channel);
	if (!pd)
		return -1;
	call.resource = &pd->resource;
	call.client_segment = request->ds;
	call.client_limit = request->data_limit;
	call.resident_segment = 0;
	call.control = control;
	call.int_in = int_in;
	call.int_out = int_out;
	call.addr_in = addr_in;
	result = gem_object_resident_dispatch(&call, &handled);
	return handled ? result : -1;
}

/* Retain one modal FORM_DO/FORM_ALERT call in its delivered ELKS trap slot. */
static WORD
gem_resident_form_defer(const struct gemtrap_request *request,
	WORD channel, UWORD output_offset)
{
	GEM_RESIDENT_PENDING *pending;

	pending = gem_resident_pending_for_channel(channel);
	if (!pending || pending->state != GEM_PENDING_FREE)
		return -1;
	pending->request = *request;
	pending->buffer_offset = 0;
	pending->output_offset = output_offset;
	pending->length = 0;
	pending->result = 0;
	pending->target = (UBYTE) channel;
	pending->next = GEM_RESIDENT_INDEX_NONE;
	pending->operation = GEM_PENDING_FORM;
	pending->state = GEM_PENDING_WAITING;
	return GEM_AES_RESIDENT_DEFERRED;
}

/*
 * Direct resident adaptation of GEMSUPER.C's OBJC_EDIT and FORM selectors.
 * Original pointer slots remain in the pinned client DS or relocated RSC
 * segment.  Interactive calls retain the delivered request; no event wrapper,
 * converted tree, heap record, or process-local AES instance is introduced.
 */
static WORD
gem_resident_form(const struct gemtrap_request *request, UWORD output_offset)
{
	GEM_FORM_CALL call;
	GEM_RESIDENT_PD *pd;
	UWORD owner_segment;
	WORD channel;
	WORD handled;
	WORD result;

	pd = gem_resident_pd_for_request(request, &channel);
	if (!pd)
		return -1;
	owner_segment = gem_resident_data_segment();
	call.owner = (UWORD) channel;
	call.generation_lo = pd->generation_lo;
	call.generation_hi = pd->generation_hi;
	call.resource = &pd->resource;
	call.client_segment = request->ds;
	call.client_limit = request->data_limit;
	call.resident_segment = owner_segment;
	call.control = control;
	call.int_in = int_in;
	call.int_out = int_out;
	call.addr_in = addr_in;
	handled = FALSE;
	result = gem_form_resident_dispatch(&call, &gem_resident_form_effects,
		&handled);
	__asm__ volatile ("movw %0,%%ds" : : "r" (owner_segment) : "memory");
	if (!handled)
		return -1;
	if (!gem_resident_form_apply_effects(pd, channel,
		&gem_resident_form_effects)) {
		(void) gem_resident_form_detach_owner(pd, channel);
		return -1;
	}
	if (result != GEM_FORM_RESIDENT_DEFERRED)
		return result;

	result = gem_resident_form_defer(request, channel, output_offset);
	if (result == GEM_AES_RESIDENT_DEFERRED)
		return result;
	(void) gem_resident_form_detach_owner(pd, channel);
	return -1;
}

/*
 * Apply the original startup manager's bounded effects only after every
 * client range has been validated.  That keeps SHEL_GET and USER_DEF mouse
 * changes transactional at the pinned ELKS segment boundary.  Counts are
 * exact bytes, coordinates are exact pixels, and no scaling occurs here.
 */
static WORD
gem_resident_startup(const struct gemtrap_request *request,
	UWORD output_offset)
{
	GEM_STARTUP_CALL call;
	GEM_STARTUP_EFFECTS effects;
	GEM_STARTUP_FILL *fill;
	GEM_RESIDENT_PD *pd;
	UWORD index;
	WORD channel;
	WORD handled;
	WORD mouse_number;
	WORD result;

	pd = gem_resident_pd_for_request(request, &channel);
	if (!pd)
		return -1;
	call.owner = (UWORD) channel;
	call.generation_lo = pd->generation_lo;
	call.generation_hi = pd->generation_hi;
	call.control = control;
	call.int_in = int_in;
	call.int_out = int_out;
	call.addr_in = addr_in;

	/*
	 * USER_DEF and SHEL_GET are the only startup calls which can touch the
	 * client segment.  Validate their original inputs before the manager
	 * changes cursor nesting or per-PD startup state.
	 */
	if (control[0] == GEM_STARTUP_GRAF_MOUSE
	    && (WORD) int_in[0] == 255
	    && !gem_resident_memory_pointer(request, addr_in[0],
			GEM_RESIDENT_MFORM_BYTES))
		return -1;
	if (control[0] == GEM_STARTUP_SHEL_GET && int_in[0]
	    && !gem_resident_memory_pointer(request, addr_in[0], int_in[0]))
		return -1;
	result = gem_startup_resident_dispatch(&call, &effects, &handled);
	if (!handled) {
		/*
		 * A recognized competing begin is the original wind_spb mutex wait,
		 * not an AES failure.  Retain this exact request until unsync transfers
		 * ownership; every other unhandled selector remains unsupported.
		 */
		if (control[0] == GEM_STARTUP_WIND_UPDATE
		    && int_in[0] == GEM_RESIDENT_BEG_UPDATE)
			return gem_resident_update_defer(request, channel,
				output_offset);
		return -1;
	}
	if (effects.fill_count > GEM_STARTUP_MAX_FILLS)
		return -1;
	if (result && control[0] == GEM_STARTUP_WIND_UPDATE
	    && int_in[0] == GEM_RESIDENT_END_UPDATE)
		gem_resident_update_progress();

	/* Recheck every emitted fill before applying the first client write. */
	index = 0;
	while (index < effects.fill_count) {
		fill = &effects.fills[index++];
		if (fill->address.hi != request->ds
		    || !gem_resident_memory_range(fill->address.lo, fill->count,
			request->data_limit))
			return -1;
	}
	if (effects.mouse_action == GEM_STARTUP_MOUSE_FORM
	    && effects.mouse_number == 255
	    && !gem_resident_memory_pointer(request,
			effects.mouse_form_address, GEM_RESIDENT_MFORM_BYTES))
		return -1;

	index = 0;
	while (index < effects.fill_count) {
		fill = &effects.fills[index++];
		gem_resident_memory_fill(request->ds, fill->address.lo,
			fill->value, fill->count);
	}
	if (effects.mouse_action == GEM_STARTUP_MOUSE_FORM)
		mouse_number = effects.mouse_number;
	else if (effects.mouse_action == GEM_STARTUP_MOUSE_HIDE)
		mouse_number = GEM_RESIDENT_MOUSE_HIDE;
	else if (effects.mouse_action == GEM_STARTUP_MOUSE_SHOW)
		mouse_number = GEM_RESIDENT_MOUSE_SHOW;
	else
		return result;

	if (!gem_vdi_resident_mouse(request, channel, mouse_number,
			effects.mouse_form_address))
		return -1;
	return result;
}

/*
 * Preserve GEM's original SHEL_READ through SHEL_WDEF selector interface,
 * but perform path lookup and program launch with ELKS system calls in the
 * resident owner.  Address slots remain exact offset:segment word pairs;
 * the shell manager validates every client range before copying any byte.
 */
static WORD
gem_resident_shell(const struct gemtrap_request *request)
{
	GEM_SHELL_CALL call;
	GEM_RESIDENT_PD *pd;
	WORD channel;
	WORD handled;
	WORD result;

	pd = gem_resident_pd_for_request(request, &channel);
	if (!pd)
		return -1;
	call.owner = (UWORD) channel;
	call.generation_lo = pd->generation_lo;
	call.generation_hi = pd->generation_hi;
	call.client_segment = request->ds;
	call.client_limit = request->data_limit;
	call.control = control;
	call.int_in = int_in;
	call.int_out = int_out;
	call.addr_in = addr_in;
	handled = FALSE;
	result = gem_shell_resident_dispatch(&call, &handled);
	return handled ? result : -1;
}

/* Direct resident adaptation of GEMSUPER.C WIND_CREATE through WIND_FIND. */
static WORD
gem_resident_window(const struct gemtrap_request *request)
{
	GEM_FAR_ADDRESS address;
	GEM_RESIDENT_PD *pd;
	GEM_WINDOW_CALL call;
	WORD channel;
	WORD handled;
	WORD result;
	WORD top_owner;

	pd = gem_resident_pd_for_request(request, &channel);
	if (!pd)
		return -1;
	if (control[0] == GEM_WINDOW_WIND_SET
	    && (int_in[1] == GEM_WINDOW_WF_NAME
		|| int_in[1] == GEM_WINDOW_WF_INFO
		|| (int_in[0] == 0
		    && int_in[1] == GEM_WINDOW_WF_NEWDESK))) {
		address.lo = int_in[2];
		address.hi = int_in[3];
		if (!gem_resident_resource_address_valid(request, pd, address))
			return -1;
	}
	call.owner = channel;
	call.generation_lo = pd->generation_lo;
	call.generation_hi = pd->generation_hi;
	call.control = control;
	call.int_in = int_in;
	call.int_out = int_out;
	result = gem_window_resident_dispatch(&gem_resident_windows, &call,
		&gem_resident_window_effects, &handled);
	if (!handled)
		return -1;
	if (!gem_resident_window_apply_effects(&gem_resident_window_effects))
		return -1;
	/*
	 * Multi-application GEM re-routed raw input here whenever the top of
	 * the shared window tree changed owner.  The direct-linked build has
	 * one client, so the Desktop is always the logical foreground and no
	 * switch exists.
	 */
	return result;
}

/* Retain one classic held-button GRAF call in its existing ELKS trap slot. */
static WORD
gem_resident_graf_defer(const struct gemtrap_request *request,
	WORD channel, UWORD output_offset)
{
	GEM_RESIDENT_PENDING *pending;

	pending = gem_resident_pending_for_channel(channel);
	if (!pending || pending->state != GEM_PENDING_FREE)
		return -1;
	pending->request = *request;
	pending->buffer_offset = 0;
	pending->output_offset = output_offset;
	pending->length = 0;
	pending->result = 0;
	pending->target = (UBYTE) channel;
	pending->next = GEM_RESIDENT_INDEX_NONE;
	pending->operation = GEM_PENDING_GRAF;
	pending->state = GEM_PENDING_WAITING;
	return GEM_AES_RESIDENT_DEFERRED;
}

/* Direct resident adaptation of GEMSUPER.C GRAF selectors 70..76 and 79. */
static WORD
gem_resident_graf(const struct gemtrap_request *request, UWORD output_offset)
{
	GEM_GRAF_CALL call;
	GEM_GRAF_EFFECTS cleanup_effects;
	GEM_GRAF_EFFECTS effects;
	GEM_RESIDENT_PD *pd;
	UWORD owner_segment;
	WORD channel;
	WORD handled;
	WORD result;

	pd = gem_resident_pd_for_request(request, &channel);
	if (!pd)
		return -1;
	call.owner = (UWORD) channel;
	call.generation_lo = pd->generation_lo;
	call.generation_hi = pd->generation_hi;
	call.resource = &pd->resource;
	call.client_segment = request->ds;
	call.client_limit = request->data_limit;
	call.control = control;
	call.int_in = int_in;
	call.int_out = int_out;
	call.addr_in = addr_in;
	owner_segment = gem_resident_data_segment();
	result = gem_graf_resident_dispatch(&call, &effects, &handled);
	__asm__ volatile ("movw %0,%%ds" : : "r" (owner_segment) : "memory");
	if (!handled)
		return -1;
	if (!gem_resident_graf_apply_effects(pd, channel, &effects)) {
		/* Dispatch may already have drawn one XOR outline; erase it now. */
		gem_graf_resident_detach((UWORD) channel, pd->generation_lo,
			pd->generation_hi, &cleanup_effects);
		__asm__ volatile ("movw %0,%%ds" : : "r" (owner_segment)
			: "memory");
		return -1;
	}
	if (result != GEM_GRAF_RESIDENT_DEFERRED)
		return result;

	result = gem_resident_graf_defer(request, channel, output_offset);
	if (result == GEM_AES_RESIDENT_DEFERRED)
		return result;

	/* A channel should have no second trap, but unwind pixels/lock if damaged. */
	gem_graf_resident_detach((UWORD) channel, pd->generation_lo,
		pd->generation_hi, &cleanup_effects);
	__asm__ volatile ("movw %0,%%ds" : : "r" (owner_segment) : "memory");
	(void) gem_resident_graf_apply_effects(pd, channel, &cleanup_effects);
	return -1;
}

/*
 * Copy one button-up GRAF result to the retained original int_out array.
 * output_count is bounded to five words; doubling by addition is exact and
 * does not introduce an 8086 multiplication helper.
 */
static VOID
gem_resident_graf_complete(UBYTE index,
	const GEM_GRAF_COMPLETION *completion)
{
	GEM_RESIDENT_PENDING *pending;
	GEM_RESIDENT_PD *pd;
	UWORD output_bytes;

	pending = gem_resident_pending_for_channel((WORD) index);
	pd = gem_resident_pd_for_channel((WORD) index);
	if (!pending || pending->operation != GEM_PENDING_GRAF || !pd
	    || completion->output_count > GEM_GRAF_OUTPUT_WORDS
	    || !gem_resident_graf_apply_effects(pd, (WORD) index,
		&completion->effects)) {
		if (pending)
			gem_resident_complete(index, -1);
		return;
	}
	output_bytes = (UWORD) (completion->output_count
		+ completion->output_count);
	if (output_bytes)
		gem_resident_memory_to(completion->int_out, pending->request.ds,
			pending->output_offset, output_bytes);
	pending->result = completion->output_count
		? (WORD) completion->int_out[0] : FALSE;
	pending->state = GEM_PENDING_READY;
	gem_resident_ready_append(index);
}

/* Service each fixed retained GRAF channel once without application polling. */
static VOID
gem_resident_graf_progress(VOID)
{
	GEM_GRAF_COMPLETION completion;
	GEM_RESIDENT_PENDING *pending;
	GEM_RESIDENT_PD *pd;
	UWORD owner_segment;
	UBYTE index;

	owner_segment = gem_resident_data_segment();
	pending = gem_resident_pending;
	index = 0;
	while (index < GEM_PROC_CHANNELS) {
		if (pending->state == GEM_PENDING_WAITING
		    && pending->operation == GEM_PENDING_GRAF) {
			pd = gem_resident_pd_for_channel((WORD) index);
			if (pd && pd->state == GEM_PD_ATTACHED
			    && gem_graf_resident_service((UWORD) index,
				pd->generation_lo, pd->generation_hi, &completion)) {
				__asm__ volatile ("movw %0,%%ds" : : "r" (owner_segment)
					: "memory");
				gem_resident_graf_complete(index, &completion);
			}
		}
		pending++;
		index++;
	}
}

/* Copy one completed modal FORM result to its retained original int_out. */
static VOID
gem_resident_form_complete(UBYTE index,
	const GEM_FORM_COMPLETION *completion)
{
	GEM_RESIDENT_PENDING *pending;
	GEM_RESIDENT_PD *pd;
	UWORD output_bytes;

	pending = gem_resident_pending_for_channel((WORD) index);
	pd = gem_resident_pd_for_channel((WORD) index);
	if (!pending || pending->operation != GEM_PENDING_FORM || !pd
	    || completion->output_count > GEM_FORM_OUTPUT_WORDS
	    || !gem_resident_form_apply_effects(pd, (WORD) index,
		&completion->effects)) {
		if (pending)
			gem_resident_complete(index, -1);
		return;
	}
	output_bytes = (UWORD) (completion->output_count
		+ completion->output_count);
	if (output_bytes)
		gem_resident_memory_to(completion->int_out, pending->request.ds,
			pending->output_offset, output_bytes);
	pending->result = completion->output_count
		? (WORD) completion->int_out[0] : FALSE;
	pending->state = GEM_PENDING_READY;
	gem_resident_ready_append(index);
}

/* Service each fixed retained FORM channel once without client-side polling. */
static VOID
gem_resident_form_progress(VOID)
{
	GEM_FORM_COMPLETION completion;
	GEM_RESIDENT_PENDING *pending;
	GEM_RESIDENT_PD *pd;
	UWORD owner_segment;
	UBYTE index;

	owner_segment = gem_resident_data_segment();
	pending = gem_resident_pending;
	index = 0;
	while (index < GEM_PROC_CHANNELS) {
		if (pending->state == GEM_PENDING_WAITING
		    && pending->operation == GEM_PENDING_FORM) {
			pd = gem_resident_pd_for_channel((WORD) index);
			if (pd && pd->state == GEM_PD_ATTACHED
			    && gem_form_resident_service((UWORD) index,
				pd->generation_lo, pd->generation_hi, &completion)) {
				__asm__ volatile ("movw %0,%%ds" : : "r" (owner_segment)
					: "memory");
				gem_resident_form_complete(index, &completion);
			}
		}
		pending++;
		index++;
	}
}

/*
 * Give modal forms first refusal of one physical sample.  A retained form is
 * the original GEM control-manager owner, so its button/key must not also
 * enter menu or EVNT_* state.  clicks is one because this VDI seam reports
 * transitions rather than the event manager's independent multi-click batch;
 * the form module preserves TOUCHEXIT's explicit count when called through
 * FORM_BUTTON and otherwise defaults a physical release to one click.
 */
static WORD
gem_resident_form_input_sample(GEM_RESIDENT_PD *pd, WORD channel,
	const GEM_VDI_RESIDENT_INPUT *sample)
{
	GEM_FORM_INPUT input;
	GEM_RESIDENT_PENDING *pending;
	UWORD owner_segment;
	WORD consumed;

	if (!pd || !sample
	    || !gem_form_resident_waiting((UWORD) channel,
		pd->generation_lo, pd->generation_hi))
		return FALSE;
	input.owner = (UWORD) channel;
	input.generation_lo = pd->generation_lo;
	input.generation_hi = pd->generation_hi;
	input.mouse_x = sample->mouse_x;
	input.mouse_y = sample->mouse_y;
	input.mouse_buttons = sample->mouse_buttons;
	input.key_code = sample->key_code;
	input.key_state = sample->key_state;
	input.clicks = 1U;
	input.key_ready = sample->key_ready;
	owner_segment = gem_resident_data_segment();
	consumed = gem_form_resident_input(&input, &gem_resident_form_effects);
	__asm__ volatile ("movw %0,%%ds" : : "r" (owner_segment) : "memory");
	if (consumed && gem_resident_form_apply_effects(pd, channel,
		&gem_resident_form_effects)) {
		gem_resident_form_progress();
		return TRUE;
	}

	/* A malformed retained state must release its update lock and client pin. */
	(void) gem_resident_form_detach_owner(pd, channel);
	pending = gem_resident_pending_for_channel(channel);
	if (pending && pending->state == GEM_PENDING_WAITING
	    && pending->operation == GEM_PENDING_FORM)
		gem_resident_complete((UBYTE) channel, -1);
	return TRUE;
}

/*
 * Apply the original fixed sixteen-byte GEM message read requested by an
 * event result.  The client segment is still pinned by its delivered INT EF
 * record.  Validate the exact offset:segment pair before changing the sole
 * resident message queue, so an invalid pointer cannot consume a message.
 */
static WORD
gem_resident_event_message(const struct gemtrap_request *request,
	GEM_RESIDENT_PD *pd, const GEM_EVENT_EFFECTS *effects)
{
	if (!effects->read_message)
		return TRUE;
	if (effects->message_address.hi != request->ds
	    || !gem_resident_memory_pointer(request,
		effects->message_address, 16U)
	    || pd->queue_index < 16U)
		return FALSE;
	gem_resident_dequeue(pd, request->ds, effects->message_address.lo, 16U);
	gem_resident_queue_progress(pd);
	return TRUE;
}

/* Retain one delivered event call as its original process-owned event block. */
static WORD
gem_resident_event_defer(const struct gemtrap_request *request,
	WORD channel, UWORD output_offset)
{
	GEM_RESIDENT_PENDING *pending;

	pending = gem_resident_pending_for_channel(channel);
	if (!pending || pending->state != GEM_PENDING_FREE)
		return -1;
	pending->request = *request;
	pending->buffer_offset = 0;
	pending->output_offset = output_offset;
	pending->length = 0;
	pending->result = 0;
	pending->target = (UBYTE) channel;
	pending->next = GEM_RESIDENT_INDEX_NONE;
	pending->operation = GEM_PENDING_EVENT;
	pending->state = GEM_PENDING_WAITING;
	return GEM_AES_RESIDENT_DEFERRED;
}

/* Direct resident adaptation of GEMSUPER.C selectors EVNT_* 20 through 26. */
static WORD
gem_resident_event(const struct gemtrap_request *request, UWORD output_offset)
{
	GEM_EVENT_CALL call;
	GEM_EVENT_EFFECTS effects;
	GEM_RESIDENT_PD *pd;
	WORD channel;
	WORD handled;
	WORD result;

	pd = gem_resident_pd_for_request(request, &channel);
	if (!pd)
		return -1;

	/*
	 * EVNT_MESAG and the MU_MESAG form of EVNT_MULTI may copy exactly one
	 * original eight-word message.  Check the direct caller pointer before the
	 * event manager can arm a wait or consume a ready key/button condition.
	 */
	if ((control[0] == GEM_EVENT_EVNT_MESAG
	     || (control[0] == GEM_EVENT_EVNT_MULTI
		 && (int_in[0] & GEM_EVENT_MU_MESAG)))
	    && !gem_resident_memory_pointer(request, addr_in[0], 16U))
		return -1;

	call.owner = (UWORD) channel;
	call.generation_lo = pd->generation_lo;
	call.generation_hi = pd->generation_hi;
	call.control = control;
	call.int_in = int_in;
	call.int_out = int_out;
	call.addr_in = addr_in;
	call.message_ready = pd->queue_index >= 16U;
	result = gem_event_resident_dispatch(&call, &effects, &handled);
	if (!handled)
		return -1;
	if (result == GEM_EVENT_RESIDENT_DEFERRED)
		return gem_resident_event_defer(request, channel, output_offset);
	if (!gem_resident_event_message(request, pd, &effects))
		return -1;
	return result;
}

/*
 * Copy one delayed EVNT_* result to the original int_out array and move its
 * retained broker record to the normal ready list.  output_count is at most
 * seven, so doubling by addition is exact and avoids a multiply helper.
 */
static VOID
gem_resident_event_complete(UBYTE index,
	const GEM_EVENT_COMPLETION *completion)
{
	GEM_RESIDENT_PENDING *pending;
	UWORD output_bytes;

	pending = gem_resident_pending_for_channel((WORD) index);
	if (!pending || completion->output_count > GEM_EVENT_OUTPUT_WORDS) {
		if (pending)
			gem_resident_complete(index, -1);
		return;
	}
	output_bytes = (UWORD) (completion->output_count
		+ completion->output_count);
	if (output_bytes)
		gem_resident_memory_to(completion->int_out, pending->request.ds,
			pending->output_offset, output_bytes);
	pending->result = (WORD) completion->int_out[0];
	pending->state = GEM_PENDING_READY;
	gem_resident_ready_append(index);
}

/* Recheck each retained event once; twelve fixed channels bound this scan. */
static VOID
gem_resident_event_progress(VOID)
{
	GEM_EVENT_COMPLETION completion;
	GEM_RESIDENT_PENDING *pending;
	GEM_RESIDENT_PD *pd;
	UBYTE index;

	pending = gem_resident_pending;
	index = 0;
	while (index < GEM_PROC_CHANNELS) {
		if (pending->state == GEM_PENDING_WAITING
		    && pending->operation == GEM_PENDING_EVENT) {
			pd = gem_resident_pd_for_channel((WORD) index);
			if (pd && pd->state == GEM_PD_ATTACHED
			    && gem_event_resident_service((UWORD) index,
				pd->generation_lo, pd->generation_hi,
				pd->queue_index >= 16U, &completion)) {
				if (!gem_resident_event_message(&pending->request, pd,
					&completion.effects))
					gem_resident_complete(index, -1);
				else
					gem_resident_event_complete(index,
						&completion);
			}
		}
		pending++;
		index++;
	}
}

/*
 * Advance original GEM timers and feed one nonblocking native PC input
 * sample to the logical foreground ELKS process.  The elapsed value is
 * scale-one milliseconds supplied by the resident owner's low-rate timer;
 * zero performs only input/message progress after an ordinary AES request.
 */
VOID
gem_aes_resident_poll(UWORD elapsed_milliseconds)
{
	GEM_EVENT_INPUT event_input;
	GEM_GRAF_INPUT graf_input;
	GEM_MENU_PULL_INPUT menu_input;
	GEM_RESIDENT_PD *pd;
	GEM_VDI_RESIDENT_INPUT vdi_input;
	GEM_WINDOW_INPUT window_input;
	UWORD owner_segment;
	WORD channel;
	WORD consumed;
	WORD graf_waiting;

	gem_resident_initialize();
	if (elapsed_milliseconds)
		gem_event_resident_tick(elapsed_milliseconds);
	/* One linked client: the Desktop is always the logical foreground. */
	channel = GEM_PROC_DESKTOP;
	pd = gem_resident_pd_for_channel(channel);
	if (pd && pd->state == GEM_PD_ATTACHED
	    && gem_vdi_resident_poll_input(&vdi_input)) {
		/*
		 * A periodic input tick normally finds no complete serial packet.
		 * Timers must still reach retained EVNT_* calls, but repeating the
		 * form, menu, GRAF, window, and event-input quick checks would only
		 * rediscover the same physical state.  The activity flag consumes the
		 * VDI sample's former padding byte, so this branch reduces idle work
		 * without increasing the 8086 stack frame.  A zero-elapsed poll is
		 * never skipped: it follows a new AES request and must let a newly
		 * armed wait inspect the current mouse state exactly once.
		 */
		if (elapsed_milliseconds && !vdi_input.changed) {
			gem_resident_event_progress();
			return;
		}
		if (gem_resident_form_input_sample(pd, channel, &vdi_input)) {
			gem_resident_graf_progress();
			gem_resident_event_progress();
			return;
		}
		menu_input.mouse_x = vdi_input.mouse_x;
		menu_input.mouse_y = vdi_input.mouse_y;
		menu_input.mouse_buttons = vdi_input.mouse_buttons;
		menu_input.key_code = vdi_input.key_code;
		menu_input.key_ready = vdi_input.key_ready;
		consumed = gem_menu_pull_resident_input(&menu_input,
			&gem_resident_menu_effects);
		if (consumed) {
			/*
			 * GEMCTRL owns this sample until menu tracking completes.
			 * Applying effects before the next event poll preserves the
			 * original select/restore/message order and prevents a duplicate
			 * button or key event from reaching the Desktop.
			 */
			(void) gem_resident_menu_apply_effects(
				&gem_resident_menu_effects);
			gem_resident_graf_progress();
			gem_resident_event_progress();
			return;
		}
		graf_waiting = gem_graf_resident_waiting((UWORD) channel,
			pd->generation_lo, pd->generation_hi);
		graf_input.owner = (UWORD) channel;
		graf_input.generation_lo = pd->generation_lo;
		graf_input.generation_hi = pd->generation_hi;
		graf_input.mouse_x = vdi_input.mouse_x;
		graf_input.mouse_y = vdi_input.mouse_y;
		graf_input.mouse_buttons = vdi_input.mouse_buttons;
		graf_input.key_state = vdi_input.key_state;
		owner_segment = gem_resident_data_segment();
		(void) gem_graf_resident_input(&graf_input);
		__asm__ volatile ("movw %0,%%ds" : : "r" (owner_segment)
			: "memory");
		gem_resident_graf_progress();
		if (!graf_waiting) {
			window_input.mouse_x = vdi_input.mouse_x;
			window_input.mouse_y = vdi_input.mouse_y;
			window_input.mouse_buttons = vdi_input.mouse_buttons;
			consumed = gem_window_resident_input(&gem_resident_windows,
				&window_input, &gem_resident_window_effects);
			if (consumed) {
				(void) gem_resident_window_apply_effects(
					&gem_resident_window_effects);
				gem_resident_graf_progress();
				gem_resident_event_progress();
				return;
			}
			event_input.owner = (UWORD) channel;
			event_input.generation_lo = pd->generation_lo;
			event_input.generation_hi = pd->generation_hi;
			event_input.mouse_x = vdi_input.mouse_x;
			event_input.mouse_y = vdi_input.mouse_y;
			event_input.mouse_buttons = vdi_input.mouse_buttons;
			event_input.key_state = vdi_input.key_state;
			event_input.key_code = vdi_input.key_code;
			event_input.key_ready = vdi_input.key_ready;
			(void) gem_event_resident_input(&event_input);
		}
	}
	gem_resident_graf_progress();
	gem_resident_event_progress();
}

/*
 * Direct first slice of original GEMSUPER.C crysbind1().  APPL_YIELD no
 * longer invokes GEM's DOS-context dsptch(): returning the broker reply makes
 * the real ELKS task runnable and the kernel scheduler owns the next choice.
 */
static WORD
crysbind1(const struct gemtrap_request *request, WORD opcode,
	  UWORD output_offset)
{
	GEM_RESIDENT_PD *pd;
	WORD channel;

	switch (opcode) {
	case APPL_INIT:
		return gem_resident_appl_init(request);
	case APPL_READ:
		return gem_resident_appl_rdwr(request, GEM_PENDING_READ,
					       output_offset);
	case APPL_WRITE:
		return gem_resident_appl_rdwr(request, GEM_PENDING_WRITE,
					       output_offset);
	case APPL_FIND:
		return gem_resident_pd_for_request(request, NULL)
		       ? gem_resident_appl_find(request) : -1;
	case GEM_STARTUP_APPL_BVSET:
	case GEM_STARTUP_APPL_BVEXT:
		return gem_resident_startup(request, output_offset);
	case APPL_YIELD:
		return gem_resident_pd_for_request(request, NULL) ? TRUE : -1;
	case APPL_EXIT:
		pd = gem_resident_pd_for_request(request, &channel);
		if (!pd)
			return -1;
		if (!gem_resident_drop_channel(channel))
			return -1;
		if (!gem_resident_detach(request, pd, channel))
			return -1;
		return TRUE;
	case GEM_EVENT_EVNT_KEYBD:
	case GEM_EVENT_EVNT_BUTTON:
	case GEM_EVENT_EVNT_MOUSE:
	case GEM_EVENT_EVNT_MESAG:
	case GEM_EVENT_EVNT_TIMER:
	case GEM_EVENT_EVNT_MULTI:
	case GEM_EVENT_EVNT_DCLICK:
		return gem_resident_event(request, output_offset);
	case GEM_WINDOW_WIND_CREATE:
	case GEM_WINDOW_WIND_OPEN:
	case GEM_WINDOW_WIND_CLOSE:
	case GEM_WINDOW_WIND_DELETE:
	case GEM_WINDOW_WIND_GET:
	case GEM_WINDOW_WIND_SET:
	case GEM_WINDOW_WIND_FIND:
		return gem_resident_window(request);
	case MENU_BAR:
		return gem_resident_menu_bar(request);
	case GEM_MENU_PULL_ICHECK:
	case GEM_MENU_PULL_IENABLE:
	case GEM_MENU_PULL_TNORMAL:
	case GEM_MENU_PULL_TEXT:
	case GEM_MENU_PULL_REGISTER:
	case GEM_MENU_PULL_UNREGISTER:
	case GEM_MENU_PULL_CLICK:
		return gem_resident_menu_pull(request);
	case GEM_OBJECT_OBJC_ADD:
	case GEM_OBJECT_OBJC_DRAW:
	case GEM_OBJECT_OBJC_FIND:
	case GEM_OBJECT_OBJC_OFFSET:
	case GEM_OBJECT_OBJC_ORDER:
	case GEM_OBJECT_OBJC_CHANGE:
		return gem_resident_object(request);
	case GEM_FORM_OBJC_EDIT:
	case GEM_FORM_DO:
	case GEM_FORM_DIAL:
	case GEM_FORM_ALERT:
	case GEM_FORM_ERROR:
	case GEM_FORM_CENTER:
	case GEM_FORM_KEYBD:
		return gem_resident_form(request, output_offset);
	case GEM_GRAF_RUBBOX:
	case GEM_GRAF_DRAGBOX:
	case GEM_GRAF_MBOX:
	case GEM_GRAF_GROWBOX:
	case GEM_GRAF_SHRINKBOX:
	case GEM_GRAF_WATCHBOX:
	case GEM_GRAF_SLIDEBOX:
	case GEM_GRAF_MKSTATE:
		return gem_resident_graf(request, output_offset);
	case GEM_STARTUP_GRAF_HANDLE:
	case GEM_STARTUP_GRAF_MOUSE:
	case GEM_STARTUP_WIND_UPDATE:
	case GEM_STARTUP_WIND_CALC:
		return gem_resident_startup(request, output_offset);
	case GEM_SHELL_READ:
	case GEM_SHELL_WRITE:
	case GEM_SHELL_GET:
	case GEM_SHELL_PUT:
	case GEM_SHELL_FIND:
	case GEM_SHELL_ENVRN:
	case GEM_SHELL_RDEF:
	case GEM_SHELL_WDEF:
		return gem_resident_shell(request);
	case RSRC_LOAD:
	case RSRC_FREE:
	case RSRC_GADDR:
	case RSRC_SADDR:
	case RSRC_OBFIX:
		return gem_resident_resource(request, opcode);
	default:
		return -1;
	}
}

/* Direct first process-manager slice of original GEMSUPER.C crysbind(). */
static WORD
crysbind(const struct gemtrap_request *request, WORD opcode,
	 UWORD output_offset)
{
	BYTE *tail;
	GEM_RESIDENT_PD *pd;
	WORD result;

	if (opcode < PROC_CREATE
	    || (opcode >= GEM_GRAF_RUBBOX && opcode <= GEM_GRAF_MKSTATE)
	    || opcode == GEM_STARTUP_GRAF_HANDLE
	    || opcode == GEM_STARTUP_GRAF_MOUSE
	    || opcode == GEM_STARTUP_WIND_UPDATE
	    || opcode == GEM_STARTUP_WIND_CALC
	    || (opcode >= GEM_SHELL_READ && opcode <= GEM_SHELL_WDEF)
	    || (opcode >= GEM_WINDOW_WIND_CREATE
		&& opcode <= GEM_WINDOW_WIND_FIND)
	    || (opcode >= RSRC_LOAD && opcode <= RSRC_OBFIX))
		return crysbind1(request, opcode, output_offset);
	if (!gem_resident_pd_for_request(request, NULL))
		return -1;

	/*
	 * GEM/XM opcodes 60 through 67 carved and scheduled DOS process
	 * arenas.  ELKS owns processes and memory outright and program launch
	 * goes through the original single-tasking SHEL_WRITE record, so the
	 * whole extension is intentionally unsupported here.
	 */
	(void) tail;
	(void) pd;
	(void) result;
	(void) opcode;
	return -1;
}

/*
 * Direct port of GEMSUPER.C xif(): copy fixed local arrays, dispatch, and copy
 * only declared outputs.  The 15-word global array is additionally copied
 * because it resides in a different ELKS segment instead of a shared DOS
 * address space.
 */
static WORD
xif(struct gemtrap_request *request)
{
	GEM_BINDINGS_AESPB parameter_block;
	UWORD input_bytes;
	UWORD output_bytes;
	UWORD address_input_bytes;
	UWORD address_output_bytes;
	UWORD index;
	UWORD owner_segment;
	WORD result;

	if (!gem_resident_memory_range(request->bx,
				(UWORD) sizeof(parameter_block),
				request->data_limit))
		return -1;
	gem_resident_memory_from(request->ds, request->bx,
			       &parameter_block, sizeof(parameter_block));

	if (!gem_resident_memory_pointer(request, parameter_block.control,
					10U)
	    || !gem_resident_memory_pointer(request, parameter_block.global,
					30U))
		return -1;
	gem_resident_memory_from(request->ds, parameter_block.control.lo,
			       control, 10U);

	/*
	 * Original XIF copies one far result for RSRC_GADDR independently of the
	 * three-count control table.  PROC_INFO continues to use control[4].
	 */
	if (control[0] == RSRC_GADDR)
		address_output_bytes = 4U;
	else if (!gem_resident_memory_slot_bytes(control[4], AO_SIZE,
					&address_output_bytes))
		return -1;
	if (!gem_resident_memory_word_bytes(control[1], I_SIZE, &input_bytes)
	    || !gem_resident_memory_word_bytes(control[2], O_SIZE, &output_bytes)
	    || !gem_resident_memory_slot_bytes(control[3], AI_SIZE,
					&address_input_bytes)
	    || !gem_resident_memory_pointer(request, parameter_block.intin,
					input_bytes)
	    || !gem_resident_memory_pointer(request, parameter_block.intout,
					output_bytes)
	    || !gem_resident_memory_pointer(request, parameter_block.addrin,
					address_input_bytes)
	    || !gem_resident_memory_pointer(request, parameter_block.addrout,
					address_output_bytes))
		return -1;
	if (!gem_resident_control_valid(control[0]))
		return -1;

	gem_resident_memory_from(request->ds, parameter_block.global.lo,
			       aes_global, 30U);
	index = 0;
	while (index < I_SIZE)
		int_in[index++] = 0;
	index = 0;
	while (index < AI_SIZE) {
		addr_in[index].lo = 0;
		addr_in[index].hi = 0;
		index++;
	}
	if (input_bytes)
		gem_resident_memory_from(request->ds, parameter_block.intin.lo,
				       int_in, input_bytes);
	if (address_input_bytes)
		gem_resident_memory_from(request->ds, parameter_block.addrin.lo,
				       addr_in, address_input_bytes);

	index = 0;
	while (index < O_SIZE)
		int_out[index++] = 0;
	index = 0;
	while (index < AO_SIZE) {
		addr_out[index].lo = 0;
		addr_out[index].hi = 0;
		index++;
	}

	__asm__ volatile ("movw %%ds,%0" : "=r" (owner_segment));
	result = crysbind(request, (WORD) control[0],
			 parameter_block.intout.lo);
	__asm__ volatile ("movw %0,%%ds" : : "r" (owner_segment) : "memory");
	int_out[0] = (UWORD) result;

	gem_resident_memory_to(aes_global, request->ds,
			     parameter_block.global.lo, 30U);
	if (output_bytes && result != GEM_AES_RESIDENT_DEFERRED)
		gem_resident_memory_to(int_out, request->ds,
				     parameter_block.intout.lo, output_bytes);
	if ((control[0] == PROC_INFO || control[0] == RSRC_GADDR)
	    && address_output_bytes)
		gem_resident_memory_to(addr_out, request->ds,
				     parameter_block.addrout.lo,
				     address_output_bytes);
	return result;
}

WORD
gem_aes_resident_request(struct gemtrap_request *request)
{
	gem_resident_initialize();
	if (!request)
		return -1;
	if (request->cx != GEM_AES_SELECTOR
	    && request->cx != GEM_AES_ALT_SELECTOR)
		return -1;
	if (request->es != request->ds)
		return -1;
	return xif(request);
}

WORD
gem_aes_resident_application(const struct gemtrap_request *request)
{
	WORD channel;

	gem_resident_initialize();
	if (!request || !gem_resident_pd_for_request(request, &channel))
		return -1;
	return channel;
}

WORD
gem_aes_resident_ready(struct gemtrap_request *request)
{
	GEM_RESIDENT_PENDING *pending;
	UBYTE index;

	gem_resident_initialize();
	if (!request || gem_resident_ready_head == GEM_RESIDENT_INDEX_NONE)
		return FALSE;
	index = gem_resident_ready_head;
	pending = gem_resident_pending_for_channel((WORD) index);
	gem_resident_ready_head = pending->next;
	if (gem_resident_ready_head == GEM_RESIDENT_INDEX_NONE)
		gem_resident_ready_tail = GEM_RESIDENT_INDEX_NONE;
	*request = pending->request;
	request->ax = (UWORD) pending->result;
	gem_resident_clear_pending(pending);
	return TRUE;
}


/*
 * TRUE while any PD is still attached.  The server uses this after the
 * Desktop pipe closes to distinguish an orderly APPL_EXIT from a client
 * which died mid-session and left retained state behind.
 */
WORD
gem_aes_resident_active(VOID)
{
	GEM_RESIDENT_PD *pd;
	WORD count;

	gem_resident_initialize();
	pd = gem_resident_pds;
	count = GEM_PROC_CHANNELS;
	while (count--) {
		if (pd->state != GEM_PD_FREE)
			return TRUE;
		pd++;
	}
	return FALSE;
}
