/*
 * gem_aes_resident.c - resident FreeGEM AES dispatcher for ELKS
 *
 * Dispatcher shape and local XIF arrays from FreeGEM AES GEMSUPER.C
 * (Digital Research, 1984-1987; GPL release by Caldera Thin Clients,
 * Inc., 1999).  Selectors the ported managers don't cover return -1.
 *
 * one of four files making up the AES core:
 *
 *   gem_aes_resident.c  this file - takes each AES trap, attaches and
 *                       detaches clients, and draws each manager's
 *                       screen effects
 *   gem_aes_tick.c      the per-tick loop: who owns each input sample,
 *                       and finishing parked calls
 *   gem_pending.c       the parked-request state machine and the
 *                       message queues
 *   gem_menu_seam.c     the menu half
 *
 * a call that cant finish in the trap gets parked, see gem_pending.h,
 * the *_defer helpers below are the parking spots. shared stuff lives
 * in gem_aes_internal.h and nowhere else
 */

#include <errno.h>

#include "gem_aes_internal.h"
#include "gem_bindings_elks.h"
#include "gem_event_resident.h"
#include "gem_form_resident.h"
#include "gem_graf_resident.h"
#include "gem_menu_object_resident.h"
#include "gem_menu_pull_resident.h"
#include "gem_object_resident.h"
#include "gem_proc.h"
#include "gem_resident_memory.h"
#include "gem_rsc.h"
#include "gem_scrap_resident.h"
#include "gem_system_resource.h"
#include "gem_tape_resident.h"
#include "gem_resource_resident.h"
#include "gem_shell_resident.h"
#include "gem_startup_resident.h"
#include "gem_vdi_resident.h"
#include "gem_window_resident.h"

static GEM_RESIDENT_PD gem_resident_pds[GEM_PROC_CHANNELS];
static UBYTE gem_resident_initialized;
GEM_MENU_OBJECT_RESIDENT gem_resident_menu;
GEM_MENU_PULL_EFFECTS gem_resident_menu_effects;
GEM_MENU_PULL_DESK_OBJECT gem_resident_menu_desk_snapshot;
OBJECT gem_resident_menu_desk_object;
GEM_FORM_EFFECTS gem_resident_form_effects;
GEM_WINDOW_RESIDENT gem_resident_windows;
GEM_WINDOW_EFFECTS gem_resident_window_effects;

/* private call record for GRAF trackers grabbing the WIND_UPDATE lock */
static GEM_STARTUP_CALL gem_resident_graf_update_call;
static GEM_STARTUP_EFFECTS gem_resident_graf_update_effects;
static UWORD gem_resident_graf_update_control[C_SIZE];
static UWORD gem_resident_graf_update_input[1];
static UWORD gem_resident_graf_update_output[1];

/* XIF isnt reentrant, broker sends one request at a time and the
 * kernel wont let an owner trap into itself */
UWORD control[C_SIZE];
UWORD aes_global[G_SIZE_RESIDENT];
UWORD int_in[I_SIZE];
UWORD int_out[O_SIZE];
GEM_BINDINGS_POINTER_SLOT addr_in[AI_SIZE];
static GEM_BINDINGS_POINTER_SLOT addr_out[AO_SIZE];

/* private XIF-shaped scratch for the W_ACTIVE frame tree, so drawing a
 * frame cant clobber the app arrays still waiting to copy back */
static UWORD gem_resident_frame_control[C_SIZE];
static UWORD gem_resident_frame_input[6];
static UWORD gem_resident_frame_output[1];
static GEM_BINDINGS_POINTER_SLOT gem_resident_frame_address[1];

static BYTE gem_resident_command[GEM_RESIDENT_COMMAND_BYTES];

/* near-data segment holding W_ACTIVE and its shared TEDINFO */
UWORD
gem_resident_data_segment(VOID)
{
	UWORD segment;

	__asm__ volatile ("movw %%ds,%0":"=r" (segment));
	return segment;
}

/* make sure the control record is long enough for this opcode so a
 * short one cant read stale scratch */
static WORD
gem_resident_control_valid(UWORD opcode)
{
	switch (opcode) {
	case APPL_INIT:
		return control[2] >= 1;
	case GEM_STARTUP_APPL_BVSET:
		return control[1] >= 2 && control[2] >= 1;
	case GEM_STARTUP_APPL_BVEXT:
		return control[1] >= 1 && control[2] >= 5 && control[3] >= 2;
	case APPL_READ:
	case APPL_WRITE:
		return control[1] >= 2 && control[2] >= 1 && control[3] >= 1;
	case APPL_FIND:
		return control[2] >= 1 && control[3] >= 1;
	case APPL_TPLAY:
		return control[1] >= 3 && control[3] >= 1;
	case APPL_TRECORD:
		return control[1] >= 2 && control[2] >= 1 && control[3] >= 1;
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
		return control[1] >= 16 && control[2] >= 7 && control[3] >= 1;
	case GEM_EVENT_EVNT_DCLICK:
		return control[1] >= 2 && control[2] >= 1;
	case MENU_BAR:
		return control[1] >= 1 && control[2] >= 1 && control[3] >= 1;
	case GEM_MENU_PULL_ICHECK:
	case GEM_MENU_PULL_IENABLE:
	case GEM_MENU_PULL_TNORMAL:
		return control[1] >= 2 && control[2] >= 1 && control[3] >= 1;
	case GEM_MENU_PULL_TEXT:
		return control[1] >= 1 && control[2] >= 1 && control[3] >= 2;
	case GEM_MENU_PULL_REGISTER:
		return control[1] >= 1 && control[2] >= 1 && control[3] >= 1;
	case GEM_MENU_PULL_UNREGISTER:
		return control[1] >= 1 && control[2] >= 1;
	case GEM_MENU_PULL_CLICK:
		return control[1] >= 2 && control[2] >= 1;
	case GEM_OBJECT_OBJC_ADD:
	case GEM_OBJECT_OBJC_ORDER:
		return control[1] >= 2 && control[2] >= 1 && control[3] >= 1;
	case GEM_OBJECT_OBJC_DELETE:
		return control[1] >= 1 && control[2] >= 1 && control[3] >= 1;
	case GEM_OBJECT_OBJC_DRAW:
		return control[1] >= 6 && control[2] >= 1 && control[3] >= 1;
	case GEM_OBJECT_OBJC_FIND:
		return control[1] >= 4 && control[2] >= 1 && control[3] >= 1;
	case GEM_OBJECT_OBJC_OFFSET:
		return control[1] >= 1 && control[2] >= 3 && control[3] >= 1;
	case GEM_OBJECT_OBJC_CHANGE:
		return control[1] >= 8 && control[2] >= 1 && control[3] >= 1;
	case GEM_FORM_OBJC_EDIT:
		return control[1] >= 4 && control[2] >= 2 && control[3] >= 1;
	case GEM_FORM_DO:
		return control[1] >= 1 && control[2] >= 1 && control[3] >= 1;
	case GEM_FORM_DIAL:
		return control[1] >= 9 && control[2] >= 1;
	case GEM_FORM_ALERT:
		return control[1] >= 1 && control[2] >= 1 && control[3] >= 1;
	case GEM_FORM_ERROR:
		return control[1] >= 1 && control[2] >= 1;
	case GEM_FORM_CENTER:
		return control[2] >= 5 && control[3] >= 1;
	case GEM_FORM_KEYBD:
		return control[1] >= 3 && control[2] >= 3 && control[3] >= 1;
	case GEM_FORM_BUTTON:
		return control[1] >= 2 && control[2] >= 2 && control[3] >= 1;
	case GEM_FORM_FSEL_INPUT:
		return control[2] >= 2 && control[3] >= 2;
	case GEM_FORM_FSEL_EXINPUT:
		return control[2] >= 2 && control[3] >= 3;
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
		return control[1] >= 4 && control[2] >= 1 && control[3] >= 1;
	case GEM_GRAF_SLIDEBOX:
		return control[1] >= 3 && control[2] >= 1 && control[3] >= 1;
	case GEM_STARTUP_GRAF_HANDLE:
		return control[2] >= 5;
	case GEM_STARTUP_GRAF_MOUSE:
		return control[1] >= 1 && control[2] >= 1 && control[3] >= 1;
	case GEM_GRAF_MKSTATE:
		return control[2] >= 5;
	case GEM_XGRF_STEPCALC:
		return control[1] >= 6 && control[2] >= 6;
	case GEM_XGRF_2BOX:
		return control[1] >= 9 && control[2] >= 1;
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
	case GEM_SCRAP_READ:
	case GEM_SCRAP_WRITE:
		return control[2] >= 1 && control[3] >= 1;
	case GEM_SCRAP_CLEAR:
		return control[2] >= 1;
	case RSRC_LOAD:
		return control[2] >= 1 && control[3] >= 1;
	case RSRC_FREE:
		return control[2] >= 1;
	case RSRC_GADDR:
		return control[1] >= 2 && control[2] >= 1;
	case RSRC_SADDR:
		return control[1] >= 2 && control[2] >= 1 && control[3] >= 1;
	case RSRC_OBFIX:
		return control[1] >= 1 && control[2] >= 1 && control[3] >= 1;
	case GEM_SHELL_READ:
		return control[2] >= 1 && control[3] >= 2;
	case GEM_SHELL_WRITE:
		return control[1] >= 3 && control[2] >= 1 && control[3] >= 2;
	case GEM_SHELL_GET:
	case GEM_SHELL_PUT:
		return control[1] >= 1 && control[2] >= 1 && control[3] >= 1;
	case GEM_SHELL_FIND:
		return control[2] >= 1 && control[3] >= 1;
	case GEM_SHELL_ENVRN:
	case GEM_SHELL_RDEF:
	case GEM_SHELL_WDEF:
		return control[2] >= 1 && control[3] >= 2;
	case PROC_CREATE:
		return control[1] >= 2 && control[2] >= 2 && control[3] >= 2;
	case PROC_RUN:
		return control[1] >= 3 && control[2] >= 1 && control[3] >= 2;
	case PROC_DELETE:
		return control[1] >= 1 && control[2] >= 1;
	case PROC_INFO:
		return control[1] >= 1 && control[2] >= 3 && control[4] >= 5;
	case PROC_MALLOC:
	case PROC_MFREE:
		return TRUE;
	case PROC_SWITCH:
	case PROC_SETBLOCK:
		return control[1] >= 1 && control[2] >= 1;
	default:
		/* unsupported opcodes return -1 later */
		return TRUE;
	}
}

GEM_RESIDENT_PD *
gem_resident_pd_for_channel(WORD channel)
{
	GEM_RESIDENT_PD *pd;
	UWORD count;

	if (channel < 0 || channel >= GEM_PROC_CHANNELS)
		return NULL;
	pd = gem_resident_pds;
	count = (UWORD) channel;
	while (count--) {
		pd++;
	}
	return pd;
}

GEM_RESIDENT_PD *
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
	gem_pending_list_init(&pd->read_waiters);
	gem_pending_list_init(&pd->write_waiters);
	gem_resource_resident_init(&pd->resource);
}

/* C startup zeroes everything but the null event link needs to be ff
 * since channel zero is a real PD, so set every link by hand first time */
VOID
gem_resident_initialize(VOID)
{
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
	count = GEM_PROC_CHANNELS;
	while (count--)
		gem_resident_clear_pd(pd++);
	gem_pending_reset();
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
		/* box height is char height plus three, square pixels make box width match */
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

/* pull the 8-char program name out of the path */
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

/* park one waiter for the WIND_UPDATE screen lock */
static WORD GEM_RESIDENT_COLD
gem_resident_update_defer(const struct gemtrap_request *request, WORD channel,
	UWORD output_offset)
{
	GEM_RESIDENT_PD *pd;

	pd = gem_resident_pd_for_channel(channel);
	if (!pd || pd->state != GEM_PD_ATTACHED)
		return -1;
	/* UPDATE needs no buffer or length so the attachment generation
	 * rides in those fields, a stale waiter cant grab the lock for a
	 * reused channel */
	if (!gem_pending_park(request, channel, GEM_PENDING_UPDATE, channel,
			pd->generation_lo, pd->generation_hi, output_offset))
		return -1;
	gem_pending_list_append(&gem_pending_update_waiters, (UBYTE) channel);
	return GEM_AES_RESIDENT_DEFERRED;
}

/* record waits till the buffer fills and play waits till the tape runs
 * out. blocking in the trap would stall every other client, so park the
 * request and finish it when its tape ends */
static WORD
gem_resident_begin_tape(const struct gemtrap_request *request, WORD channel,
	UWORD output_offset, WORD playing)
{
	GEM_RESIDENT_PD *pd;
	WORD armed;

	pd = gem_resident_pd_for_channel(channel);
	if (!pd || pd->state != GEM_PD_ATTACHED)
		return -1;
	if (addr_in[0].hi != request->ds)
		return -1;
	if (playing)
		armed = gem_tape_resident_play((UWORD) channel, request->ds,
			addr_in[0].lo, int_in[0], (WORD) int_in[1]);
	else
		armed = gem_tape_resident_record((UWORD) channel, request->ds,
			addr_in[0].lo, int_in[0]);
	if (!armed)
		return -1;
	if (!gem_pending_park(request, channel, GEM_PENDING_TAPE, channel,
			addr_in[0].lo, int_in[0], output_offset)) {
		gem_tape_resident_detach((UWORD) channel);
		return -1;
	}
	return GEM_AES_RESIDENT_DEFERRED;
}

/* message queues: a finished write wakes the oldest read, a finished
 * read frees room for the oldest write */

/* copy one eight-word AES message into the 128-byte PD queue */

/* resident_segment is zero for application/RSC trees, and the owner DS
 * for trusted AES trees like W_ACTIVE and M_DESK */
WORD
gem_resident_draw_object_tree(GEM_RESIDENT_PD *pd,
	GEM_BINDINGS_POINTER_SLOT tree, WORD object, UWORD depth,
	const GRECT *clip, UWORD resident_segment, WORD system_tree)
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
	call.resource = system_tree ? gem_system_resource() : &pd->resource;
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

/* every caller passes screen-clipped rectangles so x + width stays in a
 * signed sixteen-bit word */
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

/* redraw only the Desktop-owned pieces of the damaged area, one exposed
 * rectangle at a time, clearing each to white first, so covered client
 * work isnt wiped */
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
					(WORD)
					gem_resident_windows.desktop_root, 8U,
					&clipped, 0, FALSE))
				return FALSE;
		}
		owned = owned->next;
	}
	return TRUE;
}

/* draw every open window frame bottom to top, clipped to each window's
 * exposed rectangles. one big clip isnt enough, a partly covered lower
 * frame would paint through a higher window's work area */
static WORD
gem_resident_window_draw_frames_rectangle(const GRECT *rectangle)
{
	GEM_RESIDENT_PD *pd;
	const GEM_WINDOW_ORECT *owned;
	GEM_WINDOW_SLOT *slot;
	OBJECT *objects;
	GEM_BINDINGS_POINTER_SLOT tree;
	GRECT clipped;
	GRECT grown;
	UWORD count;
	UWORD resident_segment;
	WORD handle;

	if (!rectangle || rectangle->g_w <= 0 || rectangle->g_h <= 0)
		return FALSE;
	resident_segment = gem_resident_data_segment();

	/* a clipped root draw can drop the one-pixel closer/name edges when
	 * only the rightmost two pixels were covered, so finish those child
	 * outlines by hand after each object draw */
	handle = gem_window_resident_first(&gem_resident_windows);
	while (handle >= 0) {
		slot = gem_resident_windows.windows;
		count = (UWORD) handle;
		while (count--)
			slot++;
		/* no exposed rectangle means fully covered, dont paint it */
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
		objects =
			gem_window_resident_build_active(&gem_resident_windows,
			handle);
		if (!objects)
			return FALSE;
		tree.lo = (UWORD) objects;
		tree.hi = resident_segment;
		owned = slot->first_rect;
		while (owned) {
			if (gem_resident_screen_intersection(rectangle,
					&owned->rectangle, &clipped)) {
				/* the window's drop shadow paints black strips just
				 * outside its outer edge, so give the clip two extra
				 * pixels right and below, never past the dirty area */
				grown = clipped;
				grown.g_w += 2;
				grown.g_h += 2;
				if (gem_resident_screen_intersection(rectangle,
						&grown, &grown))
					clipped = grown;
				if (!gem_resident_draw_object_tree(pd, tree,
						ROOT, 8U, &clipped,
						resident_segment, FALSE))
					return FALSE;

				/*
				 * finish the closer/name double edge while the
				 * clip is still active */
				if (slot->kind & GEM_WINDOW_CLOSER) {
					gem_vdi_set_mode(GEM_VDI_REPLACE);
					gem_vdi_set_foreground
						(gem_vdi_resident_screen(),
						GEM_RESIDENT_NATIVE_BLACK);
					gem_vdi_fill_rect
						(gem_vdi_resident_screen(),
						(WORD) ((WORD)
							objects[ROOT].ob_x +
							gem_resident_windows.box_width
							- 1),
						(WORD) ((WORD)
							objects[ROOT].ob_y + 1),
						(slot->kind & GEM_WINDOW_NAME)
						? 2 : 1,
						(WORD)
						(gem_resident_windows.box_height
							- 2));
				}
			}
			owned = owned->next;
		}
		handle = gem_window_resident_next(&gem_resident_windows,
			handle);
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

/* check queue space for every target PD before copying the first
 * message, so a full queue cant leave half an effect behind */
WORD
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
	/* nothing touches the framebuffer unless every client can take its
	 * promised WM_REDRAW. title, information and slider changes send no
	 * WM_REDRAW, so they keep client pixels and draw only frames */
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

/* ordinary window effects reserve no extra queue space */
WORD
gem_resident_window_apply_effects(const GEM_WINDOW_EFFECTS *effects)
{
	return gem_resident_window_apply_effects_reserved(effects, NIL);
}

static VOID
gem_resident_window_detach_owner(GEM_RESIDENT_PD *pd, WORD channel)
{
	gem_window_resident_detach(&gem_resident_windows, channel,
		pd->generation_lo, pd->generation_hi,
		&gem_resident_window_effects);
	/* a dying owner gets detached even if another PD queue is full */
	(void) gem_resident_window_apply_effects(&gem_resident_window_effects);
}

/* take or release one level of the recursive update lock through the
 * WIND_UPDATE manager, which owns that state */
WORD
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
	gem_resident_graf_update_call.control =
		gem_resident_graf_update_control;
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

/* apply a begin/end update pair in begin-then-end order */
WORD
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

/* a caller tree is only accepted through the pinned client/RSC ranges,
 * the fixed alert tree only in this resident data segment. the final
 * END_UPDATE runs even when a repaint fails, so a dying form cant strand
 * the screen lock */
WORD
gem_resident_form_apply_effects(GEM_RESIDENT_PD *pd, WORD channel,
	const GEM_FORM_EFFECTS *effects)
{
	GRECT rectangle;
	UWORD resident_segment;
	WORD system_tree;
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
		system_tree = FALSE;
		if (effects->tree_kind == GEM_FORM_TREE_ALERT) {
			/* the alert tree is GEM.RSC's but its message and
			 * button strings point at resident text */
			resident_segment = gem_resident_data_segment();
			system_tree = TRUE;
			if (effects->resident_segment != resident_segment)
				result = FALSE;
		} else if (effects->tree_kind == GEM_FORM_TREE_SYSTEM) {
			system_tree = TRUE;
			if (effects->resident_segment)
				result = FALSE;
		} else if (effects->tree_kind != GEM_FORM_TREE_CALLER
			|| effects->resident_segment) {
			result = FALSE;
		}
		if (result && !gem_resident_draw_object_tree(pd, effects->tree,
				ROOT, 8U, &rectangle, resident_segment,
				system_tree))
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

/* wind down a held FORM_DO/alert before its RSC goes away */
WORD
gem_resident_form_detach_owner(GEM_RESIDENT_PD *pd, WORD channel)
{
	UWORD owner_segment;

	if (!pd)
		return FALSE;
	owner_segment = gem_resident_data_segment();
	gem_form_resident_detach((UWORD) channel, pd->generation_lo,
		pd->generation_hi, &gem_resident_form_effects);
	__asm__ volatile ("movw %0,%%ds"::"r" (owner_segment):"memory");
	return gem_resident_form_apply_effects(pd, channel,
		&gem_resident_form_effects);
}

/* erase a dying generation's XOR outline before its pinned RSC vanishes */
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

	/* restore form/tracker/menu/window pixels while every RSC is still here */
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
	/* only eight workstation records exist, a dead client's goes back */
	gem_vdi_resident_release((WORD) channel);
	gem_resident_update_progress();
	/* the far RSC segment belongs to this exact attachment generation */
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
		|| !gem_resident_memory_range(pointer.lo, 2,
			request->data_limit))
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
	GEM_FAR_ADDRESS desktop_tree;
	GEM_BINDINGS_POINTER_SLOT desktop_spec;
	GEM_RESIDENT_PD *pd;
	GEM_VDI_SCREEN *screen;
	WORD channel;
	UWORD index;

	pd = gem_resident_pd_for_request(request, &channel);
	if (!pd) {
		/* the direct-linked build has one client, the Desktop, so
		 * APPL_INIT always binds channel zero */
		channel = GEM_PROC_DESKTOP;
		if (!gem_resident_attach(request, channel))
			return -1;
	}
	if (channel == GEM_PROC_DESKTOP)
		gem_resident_nameit(gem_resident_pd_for_channel(channel),
			"DESKTOP");

	/* fill in the AES global array for this client */
	aes_global[0] = 0x0110U;
	aes_global[1] = GEM_PROC_CHANNELS - 1;
	aes_global[2] = (UWORD) channel;
	index = 3;
	while (index < G_SIZE_RESIDENT)
		aes_global[index++] = 0;

	/* words three and four hold the desktop background pattern, read it
	 * from GEM.RSC's own DESKTOP tree not from here. a two-colour adapter
	 * drops the interior colour, the low nibble */
	screen = gem_vdi_resident_screen();
	aes_global[3] = 0;
	aes_global[4] = 0;
	if (gem_system_resource_gaddr(R_TREE, GEM_SYSTEM_TREE_DESKTOP,
			&desktop_tree) && desktop_tree.hi) {
		gem_resident_memory_from(desktop_tree.hi,
			(UWORD) (desktop_tree.lo + GEM_RSC_OB_SPEC),
			&desktop_spec, sizeof(desktop_spec));
		aes_global[3] = desktop_spec.lo;
		aes_global[4] = desktop_spec.hi;
		if (screen && screen->colors <= 2U)
			aes_global[3] &= 0xfff0U;
	}

	if (!gem_resident_xbuf_init(request, addr_in[0])) {
		pd = gem_resident_pd_for_channel(channel);
		if (pd)
			(void) gem_resident_detach(request, pd, channel);
		return -1;
	}
	return channel;
}

/* find an attached client by its 8-char name */
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

/* park an APPL_READ/APPL_WRITE that cant finish yet on the target PD's
 * reader or writer FIFO, the queue pump finishes it later */
static WORD
gem_resident_appl_defer(const struct gemtrap_request *request,
	GEM_RESIDENT_PD *target_pd, WORD caller_channel,
	WORD target_channel, UBYTE operation, UWORD length, UWORD output_offset)
{
	if (!gem_pending_park(request, caller_channel, operation,
			target_channel, addr_in[0].lo, length, output_offset))
		return -1;
	gem_pending_list_append(gem_pending_waiters(target_pd, operation),
		(UBYTE) caller_channel);
	return GEM_AES_RESIDENT_DEFERRED;
}

/* APPL_READ/APPL_WRITE, one message queue per target PD. an operation
 * that cant finish yet is parked and finished later */
static WORD
gem_resident_appl_rdwr(const struct gemtrap_request *request,
	UBYTE operation, UWORD output_offset)
{
	GEM_PENDING *caller_pending;
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

	caller_pending = gem_pending_at(caller_channel);
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
		target_channel, operation, length, output_offset);
}

/* take one dying PD out of everything. cancelling its parked trap
 * releases the segment pin, writers waiting on its queue get a failure */
static WORD
gem_resident_drop_channel(WORD channel)
{
	GEM_PENDING *pending;
	GEM_RESIDENT_PD *pd;
	GEM_RESIDENT_PD *target_pd;
	WORD index;
	WORD result;

	result = TRUE;
	gem_tape_resident_detach((UWORD) channel);
	pd = gem_resident_pd_for_channel(channel);
	/* a held FORM or GRAF request may own visible pixels and one
	 * WIND_UPDATE depth, restore both before GEMCTL_CANCEL unpins the
	 * request DS */
	if (pd && !gem_resident_form_detach_owner(pd, channel))
		result = FALSE;
	if (pd && !gem_resident_graf_detach_owner(pd, channel))
		result = FALSE;

	/* sweep every parked request the death touches: the channel's own is
	 * cancelled outright, any other client parked on this channel's
	 * queues gets a failure. a waiter is unlinked from whichever FIFO it
	 * sits on first */
	index = 0;
	while (index < GEM_PROC_CHANNELS) {
		pending = gem_pending_at(index);
		if (pending->state == GEM_PENDING_WAITING
			&& (index == channel
				|| pending->target == (UBYTE) channel)) {
			if (pending->operation == GEM_PENDING_UPDATE) {
				gem_pending_list_remove
					(&gem_pending_update_waiters,
					(UBYTE) index);
			} else if (pending->operation == GEM_PENDING_READ
				|| pending->operation == GEM_PENDING_WRITE) {
				target_pd = gem_resident_pd_for_channel((WORD)
					pending->target);
				if (target_pd)
					gem_pending_list_remove
						(gem_pending_waiters(target_pd,
							pending->operation),
						(UBYTE) index);
			}
			if (index == channel) {
				if (!gem_pending_cancel(pending))
					result = FALSE;
			} else {
				gem_pending_complete((UBYTE) index, -1);
			}
		} else if (pending->state == GEM_PENDING_READY
			&& index == channel) {
			gem_pending_ready_remove((UBYTE) index);
			if (!gem_pending_cancel(pending))
				result = FALSE;
		}
		index++;
	}
	if (pd) {
		gem_event_resident_detach((UWORD) channel,
			pd->generation_lo, pd->generation_hi);
		pd->queue_index = 0;
		gem_pending_list_init(&pd->read_waiters);
		gem_pending_list_init(&pd->write_waiters);
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

/* fill the five resource words in the AES global array from this PD's
 * loaded resource */
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

/* a replacement address can only name the caller's pinned data segment
 * or its resident resource segment. null and GEM's ffff:ffff sentinel
 * pass through unchanged */
static WORD
gem_resident_resource_address_valid(const struct gemtrap_request *request,
	const GEM_RESIDENT_PD *pd, GEM_FAR_ADDRESS address)
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

/* the scrap-directory calls. the directory is one AES-wide string so no
 * PD is involved, only the path buffer crosses the trap and its checked
 * against the pinned client DS like every other far slot */
static WORD
gem_resident_scrap(const struct gemtrap_request *request, WORD opcode)
{
	BYTE path[GEM_SCRAP_PATH_MAX + 1U];
	UWORD count;

	switch (opcode) {
	case GEM_SCRAP_READ:
		if (!gem_resident_memory_pointer(request, addr_in[0],
				GEM_SCRAP_PATH_MAX + 1U))
			return FALSE;
		int_out[0] = (UWORD) gem_scrap_resident_read(path,
			sizeof(path));
		if (!int_out[0])
			return FALSE;
		count = 0;
		while (path[count])
			count++;
		gem_resident_memory_to(path, request->ds, addr_in[0].lo,
			(UWORD) (count + 1U));
		return (WORD) int_out[0];
	case GEM_SCRAP_WRITE:
		if (!gem_resident_copy_command(request, addr_in[0]))
			return FALSE;
		return gem_scrap_resident_write(gem_resident_command);
	case GEM_SCRAP_CLEAR:
		return gem_scrap_resident_clear();
	default:
		return -1;
	}
}

/* the resource calls (load, free, get/set address, fix), per PD */
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
	__asm__ volatile ("movw %%ds,%0":"=r" (owner_segment));
	result = FALSE;
	switch (opcode) {
	case RSRC_LOAD:
		if (!gem_resident_copy_command(request, addr_in[0])
			|| !gem_vdi_resident_get_metrics(&metrics.screen_width,
				&metrics.screen_height,
				&metrics.character_width,
				&metrics.character_height))
			break;
		/* draw the root object outlined, the GEM default */
		metrics.options = GEM_RESOURCE_OPTION_OUTLINED_ROOT;
		result = gem_resource_resident_load(&pd->resource,
			gem_resident_command, &metrics);
		break;
	case RSRC_FREE:
		/* an active menu must not keep pointing at a freed RSC tree */
		if (gem_resident_menu.visible
			== GEM_MENU_OBJECT_RESIDENT_VISIBLE
			&& gem_resident_menu.owner == channel
			&& gem_resident_menu.generation_lo == pd->generation_lo
			&& gem_resident_menu.generation_hi ==
			pd->generation_hi) {
			if (!gem_menu_pull_resident_deactivate((UWORD) channel,
					pd->generation_lo, pd->generation_hi,
					&gem_resident_menu_effects)
				||
				!gem_resident_menu_apply_effects
				(&gem_resident_menu_effects))
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
		if (!gem_resident_resource_address_valid(request, pd,
				addr_in[0]))
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

	/* resource records live in their own paragraph segment, put back the
	 * DS captured before dispatch so a resource DS cant leak into XIF's
	 * client-global copy */
	__asm__ volatile ("movw %0,%%ds"::"r" (owner_segment):"memory");
	if (!gem_resident_resource_global(pd))
		return -1;
	return result;
}

/* the OBJC_* object calls */
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

/* park one modal form, the tick finishes it when the form settles */
static WORD
gem_resident_form_defer(const struct gemtrap_request *request,
	WORD channel, UWORD output_offset)
{
	if (!gem_pending_park(request, channel, GEM_PENDING_FORM, channel,
			0, 0, output_offset))
		return -1;
	return GEM_AES_RESIDENT_DEFERRED;
}

/* the OBJC_EDIT and FORM calls. interactive ones hold on to the
 * delivered request */
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
	__asm__ volatile ("movw %0,%%ds"::"r" (owner_segment):"memory");
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

/* check every client range before applying the startup manager's
 * effects, so SHEL_GET and USER_DEF mouse changes stay all-or-nothing */
static WORD
gem_resident_startup(const struct gemtrap_request *request, UWORD output_offset)
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

	/* USER_DEF and SHEL_GET are the only startup calls that touch the
	 * client segment, check their inputs before the manager changes
	 * cursor nesting or per-PD state */
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
		/* someone else holds the update lock, thats not a failure,
		 * hold the request till the lock is released */
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

	/* recheck every requested fill before the first client write */
	index = 0;
	while (index < effects.fill_count) {
		fill = &effects.fills[index++];
		if (fill->address.hi != request->ds
			|| !gem_resident_memory_range(fill->address.lo,
				fill->count, request->data_limit))
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

/* the SHEL_* calls, with path lookup and program launch done by ELKS
 * system calls */
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

/* the WIND_* window calls */
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
	/* multi-application GEM re-routed raw input when the top window
	 * changed owner, one client means nothing to switch */
	return result;
}

/* park one held GRAF interaction for the tick to finish */
static WORD
gem_resident_graf_defer(const struct gemtrap_request *request,
	WORD channel, UWORD output_offset)
{
	if (!gem_pending_park(request, channel, GEM_PENDING_GRAF, channel,
			0, 0, output_offset))
		return -1;
	return GEM_AES_RESIDENT_DEFERRED;
}

/* the GRAF tracker calls (rubber box, drag box, etc) */
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
	__asm__ volatile ("movw %0,%%ds"::"r" (owner_segment):"memory");
	if (!handled)
		return -1;
	if (!gem_resident_graf_apply_effects(pd, channel, &effects)) {
		/* dispatch may have drawn one XOR outline already, erase it now */
		gem_graf_resident_detach((UWORD) channel, pd->generation_lo,
			pd->generation_hi, &cleanup_effects);
		__asm__ volatile ("movw %0,%%ds"::"r" (owner_segment)
			:"memory");
		return -1;
	}
	if (result != GEM_GRAF_RESIDENT_DEFERRED)
		return result;

	result = gem_resident_graf_defer(request, channel, output_offset);
	if (result == GEM_AES_RESIDENT_DEFERRED)
		return result;

	/* a channel never has a second trap, unwind pixels and lock if it does */
	gem_graf_resident_detach((UWORD) channel, pd->generation_lo,
		pd->generation_hi, &cleanup_effects);
	__asm__ volatile ("movw %0,%%ds"::"r" (owner_segment):"memory");
	(void) gem_resident_graf_apply_effects(pd, channel, &cleanup_effects);
	return -1;
}

/* park one delivered event call for the tick to finish */
static WORD
gem_resident_event_defer(const struct gemtrap_request *request,
	WORD channel, UWORD output_offset)
{
	if (!gem_pending_park(request, channel, GEM_PENDING_EVENT, channel,
			0, 0, output_offset))
		return -1;
	return GEM_AES_RESIDENT_DEFERRED;
}

/* the EVNT_* event calls */
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

	/* EVNT_MESAG and MU_MESAG copy one eight-word message, check the
	 * pointer before the manager can arm a wait or eat a key */
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
		return gem_resident_event_defer(request, channel,
			output_offset);
	if (!gem_resident_event_message(request, pd, &effects))
		return -1;
	return result;
}

/* dispatch to the right manager by opcode. APPL_YIELD just returns, the
 * kernel scheduler picks what runs next */
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
	case APPL_TPLAY:
	case APPL_TRECORD:
		if (!gem_resident_pd_for_request(request, &channel))
			return -1;
		return gem_resident_begin_tape(request, channel,
			output_offset, opcode == APPL_TPLAY);
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
	case GEM_OBJECT_OBJC_DELETE:
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
	case GEM_FORM_BUTTON:
	case GEM_FORM_FSEL_INPUT:
	case GEM_FORM_FSEL_EXINPUT:
		return gem_resident_form(request, output_offset);
	case GEM_GRAF_RUBBOX:
	case GEM_GRAF_DRAGBOX:
	case GEM_GRAF_MBOX:
	case GEM_GRAF_GROWBOX:
	case GEM_GRAF_SHRINKBOX:
	case GEM_GRAF_WATCHBOX:
	case GEM_GRAF_SLIDEBOX:
	case GEM_GRAF_MKSTATE:
	case GEM_XGRF_STEPCALC:
	case GEM_XGRF_2BOX:
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
	case GEM_SCRAP_READ:
	case GEM_SCRAP_WRITE:
	case GEM_SCRAP_CLEAR:
		return gem_resident_scrap(request, opcode);
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

/* the process-manager opcodes, everything else goes to crysbind1 */
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

	/* these opcodes managed DOS memory arenas. ELKS owns processes and
	 * memory, so theyre unsupported */
	(void) tail;
	(void) pd;
	(void) result;
	(void) opcode;
	return -1;
}

/* copy the caller's parameter arrays in, dispatch, then copy back only
 * the declared outputs. the global array gets copied too since it lives
 * in a different ELKS segment */
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
			(UWORD) sizeof(parameter_block), request->data_limit))
		return -1;
	gem_resident_memory_from(request->ds, request->bx,
		&parameter_block, sizeof(parameter_block));

	if (!gem_resident_memory_pointer(request, parameter_block.control, 10U)
		|| !gem_resident_memory_pointer(request, parameter_block.global,
			30U))
		 return -1;
	gem_resident_memory_from(request->ds, parameter_block.control.lo,
		control, 10U);

	/* RSRC_GADDR always copies one far result no matter what the control
	 * record says, PROC_INFO still uses control[4] */
	if (control[0] == RSRC_GADDR)
		address_output_bytes = 4U;
	else if (!gem_resident_memory_slot_bytes(control[4], AO_SIZE,
			&address_output_bytes))
		return -1;
	if (!gem_resident_memory_word_bytes(control[1], I_SIZE, &input_bytes)
		|| !gem_resident_memory_word_bytes(control[2], O_SIZE,
			&output_bytes)
		|| !gem_resident_memory_slot_bytes(control[3], AI_SIZE,
			&address_input_bytes)
		|| !gem_resident_memory_pointer(request, parameter_block.intin,
			input_bytes)
		|| !gem_resident_memory_pointer(request, parameter_block.intout,
			output_bytes)
		|| !gem_resident_memory_pointer(request, parameter_block.addrin,
			address_input_bytes)
		|| !gem_resident_memory_pointer(request,
			parameter_block.addrout, address_output_bytes))
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

	__asm__ volatile ("movw %%ds,%0":"=r" (owner_segment));
	result = crysbind(request, (WORD) control[0],
		parameter_block.intout.lo);
	__asm__ volatile ("movw %0,%%ds"::"r" (owner_segment):"memory");
	int_out[0] = (UWORD) result;

	gem_resident_memory_to(aes_global, request->ds,
		parameter_block.global.lo, 30U);
	if (output_bytes && result != GEM_AES_RESIDENT_DEFERRED)
		gem_resident_memory_to(int_out, request->ds,
			parameter_block.intout.lo, output_bytes);
	if ((control[0] == PROC_INFO || control[0] == RSRC_GADDR)
		&& address_output_bytes)
		gem_resident_memory_to(addr_out, request->ds,
			parameter_block.addrout.lo, address_output_bytes);
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
	gem_resident_initialize();
	if (!request)
		return FALSE;
	return gem_pending_take_ready(request);
}

/* TRUE while any PD is attached. the server uses this after the Desktop
 * pipe closes to tell an orderly APPL_EXIT from a client that died
 * mid-session */
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
