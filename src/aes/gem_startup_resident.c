/*
 * gem_startup_resident.c - original GEM startup calls for resident ELKS AES.
 *
 * The operation bodies below are bounded extractions of these GPL-released
 * Digital Research sources identified in docs/ORIGINAL_SOURCE_PROVENANCE.md:
 *
 *   GEMSUPER.C  APPL_BVSET, APPL_BVEXT, GRAF_HANDLE, GRAF_MOUSE,
 *               WIND_GET, WIND_UPDATE, WIND_CALC, and SHEL_GET dispatch;
 *   GEMGRAF.C   gsx_start() screen, character, and box geometry;
 *   GEMGSXIF.C  gsx_moff() and gsx_mon() cursor-hide nesting;
 *   GEMWMLIB.C  wm_get(), wm_update(), and wm_calc();
 *   GEMFLAG.C   tak_flag() and unsync() recursive update ownership;
 *   GEMSHLIB.C  sh_get().
 *
 * ELKS owns processes, scheduling, address spaces, and the physical display.
 * Consequently this file retains only fixed GEM state and returns explicit
 * mouse/client-memory effects to the outer resident owner.  It neither blocks
 * inside a manager nor dereferences an unvalidated client segment.
 */

#include "gem_startup_resident.h"

#define GEM_STARTUP_PD_FREE             0U
#define GEM_STARTUP_PD_ACTIVE           1U
#define GEM_STARTUP_OWNER_NONE          0xffU

#define GEM_STARTUP_APPL_BVEXT_QUERY    0U
#define GEM_STARTUP_APPL_BVEXT_SET      1U

#define GEM_STARTUP_ROOT_WINDOW         0
#define GEM_STARTUP_WF_WXYWH            4
#define GEM_STARTUP_END_UPDATE          0
#define GEM_STARTUP_BEG_UPDATE          1
#define GEM_STARTUP_WC_BORDER           0
#define GEM_STARTUP_WC_WORK             1

#define GEM_STARTUP_ARROW               0
#define GEM_STARTUP_LAST_SYSTEM_MOUSE   7
#define GEM_STARTUP_USER_MOUSE          255
#define GEM_STARTUP_MOUSE_OFF           256
#define GEM_STARTUP_MOUSE_ON            257

/* Window-kind bits copied from original GEMLIB.H/CRYSBIND.H. */
#define GEM_STARTUP_NAME                0x0001U
#define GEM_STARTUP_CLOSER              0x0002U
#define GEM_STARTUP_FULLER              0x0004U
#define GEM_STARTUP_INFO                0x0010U
#define GEM_STARTUP_SIZER               0x0020U
#define GEM_STARTUP_UPARROW             0x0040U
#define GEM_STARTUP_DNARROW             0x0080U
#define GEM_STARTUP_VSLIDE              0x0100U
#define GEM_STARTUP_LFARROW             0x0200U
#define GEM_STARTUP_RTARROW             0x0400U
#define GEM_STARTUP_HSLIDE              0x0800U

/* One original GEM PD contributes only generation and update-lock state. */
typedef struct gem_startup_pd {
	UWORD generation_lo;
	UWORD generation_hi;
	UWORD update_depth;
	UBYTE state;
} GEM_STARTUP_PD;

static GEM_STARTUP_PD gem_startup_pds[GEM_STARTUP_PD_COUNT];
static GEM_STARTUP_SCREEN gem_startup_screen;
static GEM_BINDINGS_POINTER_SLOT gem_startup_bvdisk;
static GEM_BINDINGS_POINTER_SLOT gem_startup_bvhard;
static UWORD gem_startup_mouse_hide_depth;
static WORD gem_startup_mouse_number;
static UBYTE gem_startup_update_owner;
static UBYTE gem_startup_screen_ready;

/* Locate a PD with pointer increments, avoiding a structure-size multiply. */
static GEM_STARTUP_PD *
gem_startup_pd_at(UWORD owner)
{
	GEM_STARTUP_PD *pd;
	UWORD index;

	if (owner >= GEM_STARTUP_PD_COUNT)
		return (GEM_STARTUP_PD *) 0;
	pd = gem_startup_pds;
	index = owner;
	while (index--)
		pd++;
	return pd;
}

static VOID
gem_startup_clear_pd(GEM_STARTUP_PD *pd)
{
	pd->generation_lo = 0;
	pd->generation_hi = 0;
	pd->update_depth = 0;
	pd->state = GEM_STARTUP_PD_FREE;
}

/*
 * A new generation in the same channel proves that the old ELKS client is
 * gone.  Release only that channel's abandoned update lock.  No waiter is
 * awakened here; the later event-manager closure owns waiter completion.
 */
static GEM_STARTUP_PD *
gem_startup_bind_pd(UWORD owner, UWORD generation_lo, UWORD generation_hi)
{
	GEM_STARTUP_PD *pd;

	pd = gem_startup_pd_at(owner);
	if (!pd)
		return (GEM_STARTUP_PD *) 0;
	if (pd->state == GEM_STARTUP_PD_ACTIVE
	    && pd->generation_lo == generation_lo
	    && pd->generation_hi == generation_hi)
		return pd;

	if (gem_startup_update_owner == (UBYTE) owner)
		gem_startup_update_owner = GEM_STARTUP_OWNER_NONE;
	pd->generation_lo = generation_lo;
	pd->generation_hi = generation_hi;
	pd->update_depth = 0;
	pd->state = GEM_STARTUP_PD_ACTIVE;
	return pd;
}

static VOID
gem_startup_clear_effects(GEM_STARTUP_EFFECTS *effects)
{
	UWORD index;

	effects->fill_count = 0;
	effects->mouse_action = GEM_STARTUP_MOUSE_NONE;
	effects->mouse_number = GEM_STARTUP_ARROW;
	effects->mouse_form_address.lo = 0;
	effects->mouse_form_address.hi = 0;
	index = 0;
	while (index < GEM_STARTUP_MAX_FILLS) {
		effects->fills[index].address.lo = 0;
		effects->fills[index].address.hi = 0;
		effects->fills[index].count = 0;
		effects->fills[index].value = 0;
		index++;
	}
}

/* Verify the original XIF array counts before reading any indexed field. */
static WORD
gem_startup_call_shape(const GEM_STARTUP_CALL *call, UWORD int_in_count,
	UWORD int_out_count, UWORD addr_in_count)
{
	if (!call->control || call->control[1] < int_in_count
	    || call->control[2] < int_out_count
	    || call->control[3] < addr_in_count)
		return FALSE;
	if (int_in_count && !call->int_in)
		return FALSE;
	if (int_out_count && !call->int_out)
		return FALSE;
	if (addr_in_count && !call->addr_in)
		return FALSE;
	return TRUE;
}

static WORD
gem_startup_finish(const GEM_STARTUP_CALL *call, WORD result, WORD *handled)
{
	call->int_out[0] = (UWORD) result;
	*handled = TRUE;
	return result;
}

/* A malformed recognized request is a real AES failure, not a fallthrough. */
static WORD
gem_startup_malformed(const GEM_STARTUP_CALL *call, WORD *handled)
{
	*handled = TRUE;
	if (call && call->control && call->int_out && call->control[2])
		call->int_out[0] = FALSE;
	return FALSE;
}

/*
 * These helpers force defined unsigned-word wrap before returning the signed
 * GEM bit pattern.  The scale is one pixel per unit, no rounding occurs, and
 * overflow wraps modulo 65536 exactly like the original 8086 manager.
 */
static WORD
gem_startup_word_add(WORD left, WORD right)
{
	return (WORD) ((UWORD) left + (UWORD) right);
}

static WORD
gem_startup_word_sub(WORD left, WORD right)
{
	return (WORD) ((UWORD) left - (UWORD) right);
}

static WORD
gem_startup_word_negate(WORD value)
{
	return (WORD) (0U - (UWORD) value);
}

VOID
gem_startup_resident_reset(VOID)
{
	GEM_STARTUP_PD *pd;
	UWORD count;

	pd = gem_startup_pds;
	count = GEM_STARTUP_PD_COUNT;
	while (count--)
		gem_startup_clear_pd(pd++);

	gem_startup_screen.vdi_handle = 0;
	gem_startup_screen.character_width = 0;
	gem_startup_screen.character_height = 0;
	gem_startup_screen.box_width = 0;
	gem_startup_screen.box_height = 0;
	gem_startup_screen.screen_width = 0;
	gem_startup_screen.screen_height = 0;
	gem_startup_screen.frame_3d = 0;
	gem_startup_bvdisk.lo = 0;
	gem_startup_bvdisk.hi = 0;
	gem_startup_bvhard.lo = 0;
	gem_startup_bvhard.hi = 0;
	gem_startup_mouse_hide_depth = 0;
	gem_startup_mouse_number = GEM_STARTUP_ARROW;
	gem_startup_update_owner = GEM_STARTUP_OWNER_NONE;
	gem_startup_screen_ready = FALSE;
}

WORD
gem_startup_resident_configure(const GEM_STARTUP_SCREEN *screen)
{
	if (!screen || screen->vdi_handle <= 0
	    || screen->character_width <= 0
	    || screen->character_height <= 0
	    || screen->box_width <= 0 || screen->box_height <= 0
	    || screen->screen_width <= 0
	    || screen->screen_height <= screen->box_height
	    || screen->frame_3d > 1U)
		return FALSE;

	gem_startup_screen.vdi_handle = screen->vdi_handle;
	gem_startup_screen.character_width = screen->character_width;
	gem_startup_screen.character_height = screen->character_height;
	gem_startup_screen.box_width = screen->box_width;
	gem_startup_screen.box_height = screen->box_height;
	gem_startup_screen.screen_width = screen->screen_width;
	gem_startup_screen.screen_height = screen->screen_height;
	gem_startup_screen.frame_3d = screen->frame_3d;
	gem_startup_screen_ready = TRUE;
	return TRUE;
}

VOID
gem_startup_resident_detach(UWORD owner, UWORD generation_lo,
	UWORD generation_hi)
{
	GEM_STARTUP_PD *pd;

	pd = gem_startup_pd_at(owner);
	if (!pd || pd->state != GEM_STARTUP_PD_ACTIVE
	    || pd->generation_lo != generation_lo
	    || pd->generation_hi != generation_hi)
		return;
	if (gem_startup_update_owner == (UBYTE) owner)
		gem_startup_update_owner = GEM_STARTUP_OWNER_NONE;
	gem_startup_clear_pd(pd);
}

/* Direct APPL_BVSET/APPL_BVEXT cases from original GEMSUPER.C. */
static WORD
gem_startup_appl_vectors(const GEM_STARTUP_CALL *call,
	GEM_STARTUP_EFFECTS *effects, WORD *handled)
{
	GEM_STARTUP_PD *pd;
	UWORD opcode;
	UWORD mode;

	(void) effects;
	opcode = call->control[0];
	if (opcode == GEM_STARTUP_APPL_BVSET) {
		if (!gem_startup_call_shape(call, 2U, 1U, 0U))
			return gem_startup_malformed(call, handled);
		pd = gem_startup_bind_pd(call->owner, call->generation_lo,
			call->generation_hi);
		if (!pd)
			return gem_startup_malformed(call, handled);
		/*
		 * Original APPL_BVSET shifts each 16-bit vector into the high
		 * half.  Copying it to hi and clearing lo is the same operation
		 * without a 32-bit temporary or shift helper.
		 */
		gem_startup_bvdisk.lo = 0;
		gem_startup_bvdisk.hi = call->int_in[0];
		gem_startup_bvhard.lo = 0;
		gem_startup_bvhard.hi = call->int_in[1];
		return gem_startup_finish(call, TRUE, handled);
	}

	if (!gem_startup_call_shape(call, 1U, 5U, 2U))
		return gem_startup_malformed(call, handled);
	mode = call->int_in[0];
	if (mode != GEM_STARTUP_APPL_BVEXT_QUERY
	    && mode != GEM_STARTUP_APPL_BVEXT_SET)
		return FALSE;
	pd = gem_startup_bind_pd(call->owner, call->generation_lo,
		call->generation_hi);
	if (!pd)
		return gem_startup_malformed(call, handled);

	if (mode == GEM_STARTUP_APPL_BVEXT_SET) {
		/* addr_in contains values here, exactly as PPDG089.C supplies. */
		gem_startup_bvdisk = call->addr_in[0];
		gem_startup_bvhard = call->addr_in[1];
	} else {
		call->int_out[1] = gem_startup_bvdisk.lo;
		call->int_out[2] = gem_startup_bvdisk.hi;
		call->int_out[3] = gem_startup_bvhard.lo;
		call->int_out[4] = gem_startup_bvhard.hi;
	}
	return gem_startup_finish(call, TRUE, handled);
}

/* Direct GRAF_HANDLE case from original GEMSUPER.C. */
static WORD
gem_startup_graf_handle(const GEM_STARTUP_CALL *call, WORD *handled)
{
	GEM_STARTUP_PD *pd;

	if (!gem_startup_call_shape(call, 0U, 5U, 0U))
		return gem_startup_malformed(call, handled);
	if (!gem_startup_screen_ready)
		return FALSE;
	pd = gem_startup_bind_pd(call->owner, call->generation_lo,
		call->generation_hi);
	if (!pd)
		return gem_startup_malformed(call, handled);
	call->int_out[1] = (UWORD) gem_startup_screen.character_width;
	call->int_out[2] = (UWORD) gem_startup_screen.character_height;
	call->int_out[3] = (UWORD) gem_startup_screen.box_width;
	call->int_out[4] = (UWORD) gem_startup_screen.box_height;
	return gem_startup_finish(call, gem_startup_screen.vdi_handle, handled);
}

/* Direct GRAF_MOUSE case plus GEMGSXIF.C cursor-hide transition rules. */
static WORD
gem_startup_graf_mouse(const GEM_STARTUP_CALL *call,
	GEM_STARTUP_EFFECTS *effects, WORD *handled)
{
	GEM_STARTUP_PD *pd;
	WORD number;

	if (!gem_startup_call_shape(call, 1U, 1U, 1U))
		return gem_startup_malformed(call, handled);
	number = (WORD) call->int_in[0];
	if (!((number >= GEM_STARTUP_ARROW
	       && number <= GEM_STARTUP_LAST_SYSTEM_MOUSE)
	      || number == GEM_STARTUP_USER_MOUSE
	      || number == GEM_STARTUP_MOUSE_OFF
	      || number == GEM_STARTUP_MOUSE_ON))
		return FALSE;
	if (number == GEM_STARTUP_USER_MOUSE
	    && !call->addr_in[0].lo && !call->addr_in[0].hi)
		return gem_startup_malformed(call, handled);
	pd = gem_startup_bind_pd(call->owner, call->generation_lo,
		call->generation_hi);
	if (!pd)
		return gem_startup_malformed(call, handled);

	if (number == GEM_STARTUP_MOUSE_OFF) {
		if (gem_startup_mouse_hide_depth == (UWORD) -1)
			return gem_startup_finish(call, FALSE, handled);
		if (!gem_startup_mouse_hide_depth)
			effects->mouse_action = GEM_STARTUP_MOUSE_HIDE;
		gem_startup_mouse_hide_depth++;
	} else if (number == GEM_STARTUP_MOUSE_ON) {
		if (!gem_startup_mouse_hide_depth)
			return gem_startup_finish(call, FALSE, handled);
		gem_startup_mouse_hide_depth--;
		if (!gem_startup_mouse_hide_depth)
			effects->mouse_action = GEM_STARTUP_MOUSE_SHOW;
	} else {
		gem_startup_mouse_number = number;
		effects->mouse_action = GEM_STARTUP_MOUSE_FORM;
		effects->mouse_number = number;
		if (number == GEM_STARTUP_USER_MOUSE)
			effects->mouse_form_address = call->addr_in[0];
	}
	return gem_startup_finish(call, TRUE, handled);
}

/* Bounded root-window portion of original GEMWMLIB.C wm_get(). */
static WORD
gem_startup_wind_get(const GEM_STARTUP_CALL *call, WORD *handled)
{
	GEM_STARTUP_PD *pd;

	if (!gem_startup_call_shape(call, 2U, 5U, 0U))
		return gem_startup_malformed(call, handled);
	if ((WORD) call->int_in[0] != GEM_STARTUP_ROOT_WINDOW
	    || (WORD) call->int_in[1] != GEM_STARTUP_WF_WXYWH
	    || !gem_startup_screen_ready)
		return FALSE;
	pd = gem_startup_bind_pd(call->owner, call->generation_lo,
		call->generation_hi);
	if (!pd)
		return gem_startup_malformed(call, handled);

	/* GEMGRAF.C gsx_start() defines gl_rfull below the one menu box. */
	call->int_out[1] = 0;
	call->int_out[2] = (UWORD) gem_startup_screen.box_height;
	call->int_out[3] = (UWORD) gem_startup_screen.screen_width;
	call->int_out[4] = (UWORD) (gem_startup_screen.screen_height
		- gem_startup_screen.box_height);
	return gem_startup_finish(call, TRUE, handled);
}

/* Bounded BEG_UPDATE/END_UPDATE part of GEMWMLIB.C wm_update(). */
static WORD
gem_startup_wind_update(const GEM_STARTUP_CALL *call, WORD *handled)
{
	GEM_STARTUP_PD *pd;
	WORD operation;

	if (!gem_startup_call_shape(call, 1U, 1U, 0U))
		return gem_startup_malformed(call, handled);
	operation = (WORD) call->int_in[0];
	if (operation != GEM_STARTUP_BEG_UPDATE
	    && operation != GEM_STARTUP_END_UPDATE)
		return FALSE;
	pd = gem_startup_bind_pd(call->owner, call->generation_lo,
		call->generation_hi);
	if (!pd)
		return gem_startup_malformed(call, handled);

	if (operation == GEM_STARTUP_BEG_UPDATE) {
		if (gem_startup_update_owner != GEM_STARTUP_OWNER_NONE
		    && gem_startup_update_owner != (UBYTE) call->owner) {
			/* The outer resident retains this call on its mutex FIFO. */
			return FALSE;
		}
		if (pd->update_depth == (UWORD) -1)
			return gem_startup_finish(call, FALSE, handled);
		gem_startup_update_owner = (UBYTE) call->owner;
		pd->update_depth++;
	} else {
		if (gem_startup_update_owner != (UBYTE) call->owner
		    || !pd->update_depth)
			return gem_startup_finish(call, FALSE, handled);
		pd->update_depth--;
		if (!pd->update_depth)
			gem_startup_update_owner = GEM_STARTUP_OWNER_NONE;
	}
	return gem_startup_finish(call, TRUE, handled);
}

/* Direct arithmetic structure of original GEMWMLIB.C wm_calc(). */
static WORD
gem_startup_wind_calc(const GEM_STARTUP_CALL *call, WORD *handled)
{
	GEM_STARTUP_PD *pd;
	UWORD kind;
	WORD wtype;
	WORD top;
	WORD bottom;
	WORD left;
	WORD right;
	WORD extra;
	WORD edge;
	WORD x;
	WORD y;
	WORD width;
	WORD height;

	if (!gem_startup_call_shape(call, 6U, 5U, 0U))
		return gem_startup_malformed(call, handled);
	wtype = (WORD) call->int_in[0];
	if ((wtype != GEM_STARTUP_WC_BORDER
	     && wtype != GEM_STARTUP_WC_WORK)
	    || !gem_startup_screen_ready)
		return FALSE;
	pd = gem_startup_bind_pd(call->owner, call->generation_lo,
		call->generation_hi);
	if (!pd)
		return gem_startup_malformed(call, handled);

	kind = call->int_in[1];
	edge = gem_startup_screen.frame_3d ? 4 : 1;
	top = edge;
	bottom = edge;
	left = edge;
	right = edge;

	if (kind & (GEM_STARTUP_NAME | GEM_STARTUP_CLOSER
	    | GEM_STARTUP_FULLER)) {
		extra = gem_startup_word_add(gem_startup_screen.box_height,
			gem_startup_screen.frame_3d ? 4 : -1);
		top = gem_startup_word_add(top, extra);
	}
	if (kind & GEM_STARTUP_INFO)
		top = gem_startup_word_add(top,
			gem_startup_screen.box_height - 1);
	if (kind & (GEM_STARTUP_UPARROW | GEM_STARTUP_DNARROW
	    | GEM_STARTUP_VSLIDE | GEM_STARTUP_SIZER)) {
		extra = gem_startup_word_add(gem_startup_screen.box_width,
			gem_startup_screen.frame_3d ? 4 : -1);
		right = gem_startup_word_add(right, extra);
	}
	if (kind & (GEM_STARTUP_LFARROW | GEM_STARTUP_RTARROW
	    | GEM_STARTUP_HSLIDE | GEM_STARTUP_SIZER)) {
		extra = gem_startup_word_add(gem_startup_screen.box_height,
			gem_startup_screen.frame_3d ? 4 : -1);
		bottom = gem_startup_word_add(bottom, extra);
	}
	if (wtype == GEM_STARTUP_WC_BORDER) {
		left = gem_startup_word_negate(left);
		top = gem_startup_word_negate(top);
		right = gem_startup_word_negate(right);
		bottom = gem_startup_word_negate(bottom);
	}

	x = (WORD) call->int_in[2];
	y = (WORD) call->int_in[3];
	width = (WORD) call->int_in[4];
	height = (WORD) call->int_in[5];
	call->int_out[1] = (UWORD) gem_startup_word_add(x, left);
	call->int_out[2] = (UWORD) gem_startup_word_add(y, top);
	call->int_out[3] = (UWORD) gem_startup_word_sub(
		gem_startup_word_sub(width, left), right);
	call->int_out[4] = (UWORD) gem_startup_word_sub(
		gem_startup_word_sub(height, top), bottom);
	return gem_startup_finish(call, TRUE, handled);
}

/*
 * Initial saved-context form of GEMSHLIB.C sh_get().  Before any SHEL_PUT,
 * the Desktop marker is '#'.  Two fill effects produce '#' followed by zeros
 * without a resident 2048-byte staging buffer or a per-byte trap wrapper.
 */
static WORD
gem_startup_shel_get(const GEM_STARTUP_CALL *call,
	GEM_STARTUP_EFFECTS *effects, WORD *handled)
{
	GEM_STARTUP_PD *pd;
	UWORD length;

	if (!gem_startup_call_shape(call, 1U, 1U, 1U))
		return gem_startup_malformed(call, handled);
	length = call->int_in[0];
	if (length > GEM_STARTUP_SHELL_BYTES)
		return FALSE;
	if (length && !call->addr_in[0].lo && !call->addr_in[0].hi)
		return gem_startup_malformed(call, handled);
	pd = gem_startup_bind_pd(call->owner, call->generation_lo,
		call->generation_hi);
	if (!pd)
		return gem_startup_malformed(call, handled);

	if (length) {
		effects->fills[0].address = call->addr_in[0];
		effects->fills[0].count = length;
		effects->fills[0].value = 0;
		effects->fills[1].address = call->addr_in[0];
		effects->fills[1].count = 1;
		effects->fills[1].value = (UBYTE) '#';
		effects->fill_count = 2;
	}
	return gem_startup_finish(call, TRUE, handled);
}

WORD
gem_startup_resident_dispatch(const GEM_STARTUP_CALL *call,
	GEM_STARTUP_EFFECTS *effects, WORD *handled)
{
	UWORD opcode;

	if (!handled)
		return FALSE;
	*handled = FALSE;
	if (!call || !call->control || !effects)
		return FALSE;
	gem_startup_clear_effects(effects);
	opcode = call->control[0];

	switch (opcode) {
	case GEM_STARTUP_APPL_BVSET:
	case GEM_STARTUP_APPL_BVEXT:
		return gem_startup_appl_vectors(call, effects, handled);
	case GEM_STARTUP_GRAF_HANDLE:
		return gem_startup_graf_handle(call, handled);
	case GEM_STARTUP_GRAF_MOUSE:
		return gem_startup_graf_mouse(call, effects, handled);
	case GEM_STARTUP_WIND_GET:
		return gem_startup_wind_get(call, handled);
	case GEM_STARTUP_WIND_UPDATE:
		return gem_startup_wind_update(call, handled);
	case GEM_STARTUP_WIND_CALC:
		return gem_startup_wind_calc(call, handled);
	case GEM_STARTUP_SHEL_GET:
		return gem_startup_shel_get(call, effects, handled);
	default:
		/* Never claim success for a selector outside this bounded unit. */
		return FALSE;
	}
}
