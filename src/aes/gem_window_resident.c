/*
 * gem_window_resident.c - original GEM window manager for resident ELKS AES.
 *
 * The operation flow is a direct bounded port of these GPL-released Digital
 * Research sources identified in docs/ORIGINAL_SOURCE_PROVENANCE.md:
 *
 *   GEMSUPER.C  WIND_CREATE through WIND_FIND dispatch;
 *   GEMWMLIB.C  w_nilit(), w_obadd(), w_setup(), w_getsize(), w_setsize(),
 *               w_bldactive(), w_redraw(), draw_change(), wm_create(),
 *               wm_open(), wm_close(), wm_delete(), wm_get(), wm_set(),
 *               wm_find(), and the two-dimensional part of wm_calc();
 *   GEMWRECT.C  or_start(), get_orect(), mkpiece(), brkrct(), and newrect();
 *   GEMCTRL.C   ctlmgr(), closer/fuller watch, name drag, sizer rubbering,
 *               arrow/page selection, and scale-1000 elevator tracking.
 *
 * ELKS adaptations are limited to replacing PD pointers with channel and
 * generation words, replacing ORECT pointers with exact word indexes, and
 * returning draw/message effects to the resident owner.  W_TREE, W_ACTIVE,
 * TEDINFO, AES arrays, and eight-word messages retain their original forms.
 * There is no heap, recursion, scale conversion, multiplication, division,
 * floating-point operation, or application-memory copy in this module.
 */

#include "gem_window_resident.h"

#if !defined(ELKS) || !ELKS
#error gem_window_resident.c requires the ELKS IA-16 object ABI
#endif

#define GEM_WINDOW_ROOT                 0
#define GEM_WINDOW_NIL                  (-1)
#define GEM_WINDOW_OPEN_MIN             1
#define GEM_WINDOW_OPEN_LIMIT           12

#define GEM_WINDOW_G_BOX                20U
#define GEM_WINDOW_G_BOXTEXT            22U
#define GEM_WINDOW_G_IBOX               25U
#define GEM_WINDOW_G_BOXCHAR            27U

#define GEM_WINDOW_NORMAL               0x0000U
#define GEM_WINDOW_DISABLED             0x0008U
#define GEM_WINDOW_LASTOB               0x0020U
#define GEM_WINDOW_FLAG3D               0x1000U
#define GEM_WINDOW_USECOLORCAT           0x2000U
#define GEM_WINDOW_TE_LEFT              0
#define GEM_WINDOW_TE_CENTER            2
#define GEM_WINDOW_IBM_FONT             3

#define GEM_WINDOW_WORD_MAX             32767
#define GEM_WINDOW_WORD_MIN             (-32767 - 1)

/* Original two-dimensional gl_waspec words for the bounded W_ACTIVE tree. */
#define GEM_WINDOW_BOX_SPEC_LO           0x1101U
#define GEM_WINDOW_BOX_SPEC_HI           0x0001U
#define GEM_WINDOW_DATA_SPEC_HI          0x0000U
#define GEM_WINDOW_GADGET_SPEC_LO        0x110bU
#define GEM_WINDOW_CLOSER_SPEC_HI        0x1201U
#define GEM_WINDOW_FULLER_SPEC_HI        0x0701U
#define GEM_WINDOW_SIZER_SPEC_HI         0x0601U
#define GEM_WINDOW_SIZER_BLANK_SPEC_LO   0x1100U
#define GEM_WINDOW_SIZER_BLANK_SPEC_HI   0x0001U
#define GEM_WINDOW_UPARROW_SPEC_HI       0x0c01U
#define GEM_WINDOW_DNARROW_SPEC_HI       0x0d01U
#define GEM_WINDOW_LFARROW_SPEC_HI       0x0f01U
#define GEM_WINDOW_RTARROW_SPEC_HI       0x0e01U
/*
 * Original two-dimensional W_VBAR/W_HBAR used hollow 1101h surfaces because
 * GEM restored a saved screen beneath resized chrome.  This ELKS owner keeps
 * no large backing bitmap.  Bars are AES-owned chrome, so give only those two
 * roots the equivalent solid logical-white 1170h surface; otherwise a smaller
 * SIZER-only window exposes its former client pixels through the new bars.
 * W_BOX, W_DATA, and W_WORK remain their original hollow objects.
 */
#define GEM_WINDOW_BAR_SPEC_LO           0x1170U
#define GEM_WINDOW_BAR_SPEC_HI           0x0001U
#define GEM_WINDOW_SLIDE_SPEC_LO         0x1109U
#define GEM_WINDOW_SLIDE_SPEC_HI         0x0001U
#define GEM_WINDOW_ELEV_SPEC_LO          0x110bU
#define GEM_WINDOW_ELEV_SPEC_HI          0x0001U

/*
 * GEMGRAF.C resolves CC_NAME, CC_INFO, and CC_BUTTON through cc_s[] before
 * painting a monochrome W_ACTIVE object.  The resident ELKS renderer has no
 * mutable colour-category table, so retain the exact resolved packed colour:
 * black border/text, IP_SOLID pattern seven, logical-white interior zero.
 * This makes repeated WF_NAME/WF_INFO and gadget draws opaque, just as
 * original grcc_rect() was, without adding a lookup or any wider arithmetic.
 */
#define GEM_WINDOW_MONO_OPAQUE_COLOR     0x1170U

/* Nonblocking states corresponding to GEMCTRL.C's internal held-button waits. */
#define GEM_WINDOW_CONTROL_IDLE          0U
#define GEM_WINDOW_CONTROL_IGNORE        1U
#define GEM_WINDOW_CONTROL_RELEASE       2U
#define GEM_WINDOW_CONTROL_WATCH         3U
#define GEM_WINDOW_CONTROL_MOVE          4U
#define GEM_WINDOW_CONTROL_SIZE          5U
#define GEM_WINDOW_CONTROL_HSLIDE        6U
#define GEM_WINDOW_CONTROL_VSLIDE        7U

static VOID gem_window_active_types(GEM_WINDOW_RESIDENT *manager);

/* Original unnamed windows render an empty title, never a null far string. */
static UBYTE gem_window_empty_text;

/* Capture the resident data segment for W_ACTIVE's shared TEDINFO records. */
static UWORD
gem_window_data_segment(VOID)
{
	UWORD segment;

	__asm__ volatile ("movw %%ds,%0" : "=r" (segment));
	return segment;
}

/* Pointer increments avoid a structure-size multiplication on an 8088. */
static GEM_WINDOW_SLOT *
gem_window_slot_at(GEM_WINDOW_RESIDENT *manager, UWORD handle)
{
	GEM_WINDOW_SLOT *slot;

	slot = manager->windows;
	while (handle--)
		slot++;
	return slot;
}

static OBJECT *
gem_window_tree_at(GEM_WINDOW_RESIDENT *manager, UWORD handle)
{
	OBJECT *object;

	object = manager->tree;
	while (handle--)
		object++;
	return object;
}

static const OBJECT *
gem_window_const_tree_at(const GEM_WINDOW_RESIDENT *manager, UWORD handle)
{
	const OBJECT *object;

	object = manager->tree;
	while (handle--)
		object++;
	return object;
}

static GEM_WINDOW_MESSAGE *
gem_window_message_at(GEM_WINDOW_EFFECTS *effects, UWORD index)
{
	GEM_WINDOW_MESSAGE *message;

	message = effects->messages;
	while (index--)
		message++;
	return message;
}

static VOID
gem_window_rect_zero(GRECT *rectangle)
{
	rectangle->g_x = 0;
	rectangle->g_y = 0;
	rectangle->g_w = 0;
	rectangle->g_h = 0;
}

static VOID
gem_window_rect_copy(const GRECT *source, GRECT *destination)
{
	destination->g_x = source->g_x;
	destination->g_y = source->g_y;
	destination->g_w = source->g_w;
	destination->g_h = source->g_h;
}

/* Checked signed pixel addition; success returns an exact unscaled value. */
static WORD
gem_window_add(WORD left, WORD right, WORD *result)
{
	if (right > 0 && left > GEM_WINDOW_WORD_MAX - right)
		return FALSE;
	if (right < 0 && left < GEM_WINDOW_WORD_MIN - right)
		return FALSE;
	*result = left + right;
	return TRUE;
}

static WORD
gem_window_rect_edges(const GRECT *rectangle, WORD *right, WORD *bottom)
{
	if (!rectangle || rectangle->g_w <= 0 || rectangle->g_h <= 0)
		return FALSE;
	return gem_window_add(rectangle->g_x, rectangle->g_w, right)
		&& gem_window_add(rectangle->g_y, rectangle->g_h, bottom);
}

/* Original rc_intersect() semantics, with checked edge arithmetic. */
static WORD
gem_window_rect_intersect(const GRECT *clip, GRECT *rectangle)
{
	WORD clip_right;
	WORD clip_bottom;
	WORD rectangle_right;
	WORD rectangle_bottom;
	WORD left;
	WORD top;
	WORD right;
	WORD bottom;

	if (!gem_window_rect_edges(clip, &clip_right, &clip_bottom)
	    || !gem_window_rect_edges(rectangle, &rectangle_right,
					&rectangle_bottom)) {
		gem_window_rect_zero(rectangle);
		return FALSE;
	}
	left = clip->g_x > rectangle->g_x ? clip->g_x : rectangle->g_x;
	top = clip->g_y > rectangle->g_y ? clip->g_y : rectangle->g_y;
	right = clip_right < rectangle_right ? clip_right : rectangle_right;
	bottom = clip_bottom < rectangle_bottom ? clip_bottom : rectangle_bottom;
	if (right <= left || bottom <= top) {
		gem_window_rect_zero(rectangle);
		return FALSE;
	}
	rectangle->g_x = left;
	rectangle->g_y = top;
	rectangle->g_w = right - left;
	rectangle->g_h = bottom - top;
	return TRUE;
}

/* Original rc_union() for two valid screen-bounded rectangles. */
static WORD
gem_window_rect_union(const GRECT *source, GRECT *destination)
{
	WORD source_right;
	WORD source_bottom;
	WORD destination_right;
	WORD destination_bottom;
	WORD left;
	WORD top;
	WORD right;
	WORD bottom;

	if (source->g_w <= 0 || source->g_h <= 0)
		return destination->g_w > 0 && destination->g_h > 0;
	if (destination->g_w <= 0 || destination->g_h <= 0) {
		gem_window_rect_copy(source, destination);
		return TRUE;
	}
	if (!gem_window_rect_edges(source, &source_right, &source_bottom)
	    || !gem_window_rect_edges(destination, &destination_right,
					&destination_bottom))
		return FALSE;
	left = source->g_x < destination->g_x
		? source->g_x : destination->g_x;
	top = source->g_y < destination->g_y
		? source->g_y : destination->g_y;
	right = source_right > destination_right
		? source_right : destination_right;
	bottom = source_bottom > destination_bottom
		? source_bottom : destination_bottom;
	destination->g_x = left;
	destination->g_y = top;
	destination->g_w = right - left;
	destination->g_h = bottom - top;
	return TRUE;
}

static WORD
gem_window_rect_contains(const GRECT *rectangle, WORD x, WORD y)
{
	WORD right;
	WORD bottom;

	if (!gem_window_rect_edges(rectangle, &right, &bottom))
		return FALSE;
	return x >= rectangle->g_x && x < right
		&& y >= rectangle->g_y && y < bottom;
}

static VOID
gem_window_object_rect(const OBJECT *object, GRECT *rectangle)
{
	rectangle->g_x = (WORD) object->ob_x;
	rectangle->g_y = (WORD) object->ob_y;
	rectangle->g_w = (WORD) object->ob_width;
	rectangle->g_h = (WORD) object->ob_height;
}

static VOID
gem_window_set_object_rect(OBJECT *object, const GRECT *rectangle)
{
	object->ob_x = (UWORD) rectangle->g_x;
	object->ob_y = (UWORD) rectangle->g_y;
	object->ob_width = (UWORD) rectangle->g_w;
	object->ob_height = (UWORD) rectangle->g_h;
}

/* Original two-dimensional wm_calc(WC_WORK), without a scale operation. */
static WORD
gem_window_work_rect(const GEM_WINDOW_RESIDENT *manager, UWORD kind,
	const GRECT *outer, GRECT *work)
{
	WORD top_border;
	WORD bottom_border;
	WORD left_border;
	WORD right_border;
	WORD horizontal;
	WORD vertical;
	WORD title;
	WORD info;

	if (!manager || !outer || !work)
		return FALSE;
	top_border = 1;
	bottom_border = 1;
	left_border = 1;
	right_border = 1;
	title = (kind & (GEM_WINDOW_NAME | GEM_WINDOW_CLOSER
		| GEM_WINDOW_FULLER)) != 0;
	info = (kind & GEM_WINDOW_INFO) != 0;
	vertical = (kind & (GEM_WINDOW_UPARROW | GEM_WINDOW_DNARROW
		| GEM_WINDOW_VSLIDE | GEM_WINDOW_SIZER)) != 0;
	horizontal = (kind & (GEM_WINDOW_LFARROW | GEM_WINDOW_RTARROW
		| GEM_WINDOW_HSLIDE | GEM_WINDOW_SIZER)) != 0;
	if (title)
		top_border += manager->box_height - 1;
	if (info)
		top_border += manager->box_height - 1;
	if (vertical)
		right_border += manager->box_width - 1;
	if (horizontal)
		bottom_border += manager->box_height - 1;
	if (outer->g_w <= left_border + right_border
	    || outer->g_h <= top_border + bottom_border)
		return FALSE;
	if (!gem_window_add(outer->g_x, left_border, &work->g_x)
	    || !gem_window_add(outer->g_y, top_border, &work->g_y))
		return FALSE;
	work->g_w = outer->g_w - left_border - right_border;
	work->g_h = outer->g_h - top_border - bottom_border;
	return TRUE;
}

static WORD
gem_window_public_rect(const GEM_WINDOW_RESIDENT *manager, UWORD kind,
	const GRECT *rectangle, GRECT *work)
{
	WORD right;
	WORD bottom;

	if (!manager || !manager->ready || !rectangle
	    || rectangle->g_x < -1 || rectangle->g_y < 0
	    || !gem_window_rect_edges(rectangle, &right, &bottom)
	    || right <= 0 || bottom <= manager->box_height
	    || right > manager->screen_width
	    || bottom > manager->screen_height)
		return FALSE;
	return gem_window_work_rect(manager, kind, rectangle, work);
}

static VOID
gem_window_clear_object(OBJECT *object)
{
	object->ob_next = GEM_WINDOW_NIL;
	object->ob_head = GEM_WINDOW_NIL;
	object->ob_tail = GEM_WINDOW_NIL;
	object->ob_type = GEM_WINDOW_G_IBOX;
	object->ob_flags = 0;
	object->ob_state = GEM_WINDOW_NORMAL;
	object->ob_spec.lo = 0;
	object->ob_spec.hi = 0;
	object->ob_x = 0;
	object->ob_y = 0;
	object->ob_width = 0;
	object->ob_height = 0;
}

static VOID
gem_window_clear_slot(GEM_WINDOW_SLOT *slot)
{
	slot->flags = 0;
	slot->kind = 0;
	slot->owner = GEM_WINDOW_NIL;
	slot->generation_lo = 0;
	slot->generation_hi = 0;
	gem_window_rect_zero(&slot->full);
	gem_window_rect_zero(&slot->work);
	gem_window_rect_zero(&slot->previous);
	slot->hslide = 0;
	slot->vslide = 0;
	slot->hslsiz = -1;
	slot->vslsiz = -1;
	slot->name.lo = 0;
	slot->name.hi = 0;
	slot->info.lo = 0;
	slot->info.hi = 0;
	slot->first_rect = (GEM_WINDOW_ORECT *) 0;
	slot->next_rect = (GEM_WINDOW_ORECT *) 0;
}

static VOID
gem_window_control_clear(GEM_WINDOW_RESIDENT *manager)
{
	manager->control_state = GEM_WINDOW_CONTROL_IDLE;
	manager->control_handle = GEM_WINDOW_NIL;
	manager->control_gadget = GEM_WINDOW_NIL;
	manager->control_start_x = 0;
	manager->control_start_y = 0;
	gem_window_rect_zero(&manager->control_start);
	gem_window_rect_zero(&manager->control_track);
}

/*
 * Return floor(value * multiplier / divisor) using only one-word arithmetic.
 * Controller callers keep value <= divisor and multiplier <= 1000.  The ten
 * fixed iterations consume multiplier bits 9..0.  At each step remainder is
 * below divisor, so twice the remainder plus value is below three divisors;
 * two subtractions are sufficient and every temporary fits in one UWORD.
 * A zero divisor returns zero.  The only saturation is the public scale-1000
 * endpoint when value reaches divisor.
 */
static UWORD
gem_window_scale(UWORD value, UWORD multiplier, UWORD divisor)
{
	UWORD quotient;
	UWORD remainder;
	UWORD temporary;
	UWORD mask;

	if (!divisor)
		return 0;
	if (value > divisor)
		value = divisor;
	if (multiplier > 1000U)
		multiplier = 1000U;
	quotient = 0;
	remainder = 0;
	mask = 0x0200U;
	while (mask) {
		quotient = (UWORD) (quotient + quotient);
		temporary = (UWORD) (remainder + remainder);
		if (multiplier & mask)
			temporary = (UWORD) (temporary + value);
		if (temporary >= divisor) {
			temporary = (UWORD) (temporary - divisor);
			quotient++;
		}
		if (temporary >= divisor) {
			temporary = (UWORD) (temporary - divisor);
			quotient++;
		}
		remainder = temporary;
		mask >>= 1;
	}
	return quotient;
}

/* Original w_obadd(): append a child at the tail of a sibling list. */
static VOID
gem_window_obadd(OBJECT *objects, WORD parent, WORD child)
{
	OBJECT *parent_object;
	OBJECT *child_object;
	OBJECT *last_object;
	WORD last;

	if (parent == GEM_WINDOW_NIL || child == GEM_WINDOW_NIL)
		return;
	parent_object = objects + parent;
	child_object = objects + child;
	child_object->ob_next = parent;
	last = parent_object->ob_tail;
	if (last == GEM_WINDOW_NIL)
		parent_object->ob_head = child;
	else {
		last_object = objects + last;
		last_object->ob_next = child;
	}
	parent_object->ob_tail = child;
}

static VOID
gem_window_tree_add(GEM_WINDOW_RESIDENT *manager, WORD handle)
{
	OBJECT *root;
	OBJECT *window;
	OBJECT *tail;
	WORD last;

	root = gem_window_tree_at(manager, GEM_WINDOW_ROOT);
	window = gem_window_tree_at(manager, (UWORD) handle);
	window->ob_next = GEM_WINDOW_ROOT;
	last = root->ob_tail;
	if (last == GEM_WINDOW_NIL)
		root->ob_head = handle;
	else {
		tail = gem_window_tree_at(manager, (UWORD) last);
		tail->ob_next = handle;
	}
	root->ob_tail = handle;
	manager->top = handle;
}

/* Original ob_delete() for the shallow W_TREE sibling list. */
static WORD
gem_window_tree_delete(GEM_WINDOW_RESIDENT *manager, WORD handle)
{
	OBJECT *root;
	OBJECT *object;
	OBJECT *previous;
	WORD current;
	WORD prior;
	WORD next;

	root = gem_window_tree_at(manager, GEM_WINDOW_ROOT);
	current = root->ob_head;
	prior = GEM_WINDOW_NIL;
	while (current != GEM_WINDOW_NIL && current != GEM_WINDOW_ROOT) {
		object = gem_window_tree_at(manager, (UWORD) current);
		next = object->ob_next;
		if (current == handle) {
			if (prior == GEM_WINDOW_NIL)
				root->ob_head = next == GEM_WINDOW_ROOT
					? GEM_WINDOW_NIL : next;
			else {
				previous = gem_window_tree_at(manager,
					(UWORD) prior);
				previous->ob_next = next;
			}
			if (root->ob_tail == handle)
				root->ob_tail = prior;
			object->ob_next = GEM_WINDOW_NIL;
			manager->top = root->ob_tail == GEM_WINDOW_NIL
				? GEM_WINDOW_ROOT : root->ob_tail;
			return TRUE;
		}
		prior = current;
		current = next;
	}
	return FALSE;
}

static WORD
gem_window_tree_top(GEM_WINDOW_RESIDENT *manager, WORD handle)
{
	if (manager->top == handle)
		return TRUE;
	if (!gem_window_tree_delete(manager, handle))
		return FALSE;
	gem_window_tree_add(manager, handle);
	return TRUE;
}

/*
 * Preserve the shallow W_TREE ordering across a bounded geometry preflight.
 * Only ob_next and the root head/tail change when a window is topped.  Twelve
 * one-word links cost 24 stack bytes on IA-16 and avoid copying OBJECT records
 * or allocating a rollback structure in resident data.
 */
static VOID
gem_window_tree_save_order(const GEM_WINDOW_RESIDENT *manager, WORD *next,
	WORD *head, WORD *tail, WORD *top)
{
	const OBJECT *object;
	UWORD count;

	object = manager->tree;
	count = GEM_WINDOW_COUNT;
	while (count--) {
		*next++ = object->ob_next;
		object++;
	}
	object = manager->tree + GEM_WINDOW_ROOT;
	*head = object->ob_head;
	*tail = object->ob_tail;
	*top = manager->top;
}

static VOID
gem_window_tree_restore_order(GEM_WINDOW_RESIDENT *manager, const WORD *next,
	WORD head, WORD tail, WORD top)
{
	OBJECT *object;
	UWORD count;

	object = manager->tree;
	count = GEM_WINDOW_COUNT;
	while (count--) {
		object->ob_next = *next++;
		object++;
	}
	object = manager->tree + GEM_WINDOW_ROOT;
	object->ob_head = head;
	object->ob_tail = tail;
	manager->top = top;
}

static GEM_WINDOW_ORECT *
gem_window_rect_alloc(GEM_WINDOW_RESIDENT *manager)
{
	GEM_WINDOW_ORECT *rectangle;

	rectangle = manager->free_rect;
	if (!rectangle)
		return (GEM_WINDOW_ORECT *) 0;
	manager->free_rect = rectangle->next;
	rectangle->next = (GEM_WINDOW_ORECT *) 0;
	gem_window_rect_zero(&rectangle->rectangle);
	return rectangle;
}

static VOID
gem_window_rect_free(GEM_WINDOW_RESIDENT *manager,
	GEM_WINDOW_ORECT *rectangle)
{
	if (!rectangle)
		return;
	rectangle->next = manager->free_rect;
	manager->free_rect = rectangle;
}

static VOID
gem_window_release_list(GEM_WINDOW_RESIDENT *manager,
	GEM_WINDOW_SLOT *slot)
{
	GEM_WINDOW_ORECT *rectangle;
	GEM_WINDOW_ORECT *next;

	rectangle = slot->first_rect;
	while (rectangle) {
		next = rectangle->next;
		gem_window_rect_free(manager, rectangle);
		rectangle = next;
	}
	slot->first_rect = (GEM_WINDOW_ORECT *) 0;
	slot->next_rect = (GEM_WINDOW_ORECT *) 0;
}

static VOID
gem_window_release_lists(GEM_WINDOW_RESIDENT *manager)
{
	GEM_WINDOW_SLOT *slot;
	UWORD count;

	slot = manager->windows;
	count = GEM_WINDOW_COUNT;
	while (count--) {
		gem_window_release_list(manager, slot);
		slot++;
	}
}

static WORD
gem_window_orect_append(GEM_WINDOW_RESIDENT *manager,
	GEM_WINDOW_ORECT **head, GEM_WINDOW_ORECT **tail,
	const GRECT *piece)
{
	GEM_WINDOW_ORECT *rectangle;

	if (piece->g_w <= 0 || piece->g_h <= 0)
		return TRUE;
	rectangle = gem_window_rect_alloc(manager);
	if (!rectangle)
		return FALSE;
	gem_window_rect_copy(piece, &rectangle->rectangle);
	if (!*head)
		*head = rectangle;
	else
		(*tail)->next = rectangle;
	*tail = rectangle;
	return TRUE;
}

static VOID
gem_window_release_temporary(GEM_WINDOW_RESIDENT *manager,
	GEM_WINDOW_ORECT *head)
{
	GEM_WINDOW_ORECT *next;

	while (head) {
		next = head->next;
		gem_window_rect_free(manager, head);
		head = next;
	}
}

/*
 * Direct GEMWRECT.C mkpiece()/brkrct() split.  Top and bottom keep the old
 * width; left and right retain only the vertical overlap with the occluder.
 */
static WORD
gem_window_break_one(GEM_WINDOW_RESIDENT *manager,
	GEM_WINDOW_ORECT **link,
	const GRECT *occluder)
{
	GEM_WINDOW_ORECT *old_rectangle;
	GEM_WINDOW_ORECT *successor;
	GEM_WINDOW_ORECT *head;
	GEM_WINDOW_ORECT *tail;
	GRECT old;
	GRECT piece;
	WORD old_right;
	WORD old_bottom;
	WORD new_right;
	WORD new_bottom;
	WORD overlap_top;
	WORD overlap_bottom;

	old_rectangle = *link;
	gem_window_rect_copy(&old_rectangle->rectangle, &old);
	if (!gem_window_rect_edges(&old, &old_right, &old_bottom)
	    || !gem_window_rect_edges(occluder, &new_right, &new_bottom))
		return TRUE;
	if (occluder->g_x >= old_right || new_right <= old.g_x
	    || occluder->g_y >= old_bottom || new_bottom <= old.g_y)
		return TRUE;

	head = (GEM_WINDOW_ORECT *) 0;
	tail = (GEM_WINDOW_ORECT *) 0;
	overlap_top = old.g_y > occluder->g_y ? old.g_y : occluder->g_y;
	overlap_bottom = old_bottom < new_bottom ? old_bottom : new_bottom;

	/* TOP */
	piece.g_x = old.g_x;
	piece.g_y = old.g_y;
	piece.g_w = old.g_w;
	piece.g_h = occluder->g_y - old.g_y;
	if (!gem_window_orect_append(manager, &head, &tail, &piece))
		goto no_room;

	/* LEFT */
	piece.g_x = old.g_x;
	piece.g_y = overlap_top;
	piece.g_w = occluder->g_x - old.g_x;
	piece.g_h = overlap_bottom - overlap_top;
	if (!gem_window_orect_append(manager, &head, &tail, &piece))
		goto no_room;

	/* RIGHT */
	piece.g_x = new_right;
	piece.g_y = overlap_top;
	piece.g_w = old_right - new_right;
	piece.g_h = overlap_bottom - overlap_top;
	if (!gem_window_orect_append(manager, &head, &tail, &piece))
		goto no_room;

	/* BOTTOM */
	piece.g_x = old.g_x;
	piece.g_y = new_bottom;
	piece.g_w = old.g_w;
	piece.g_h = old_bottom - new_bottom;
	if (!gem_window_orect_append(manager, &head, &tail, &piece))
		goto no_room;

	successor = old_rectangle->next;
	if (!head)
		*link = successor;
	else {
		*link = head;
		tail->next = successor;
	}
	gem_window_rect_free(manager, old_rectangle);
	return TRUE;

no_room:
	gem_window_release_temporary(manager, head);
	manager->rect_overflow = TRUE;
	return FALSE;
}

/* Apply one occluder to every current piece without revisiting new pieces. */
static WORD
gem_window_break_list(GEM_WINDOW_RESIDENT *manager,
	GEM_WINDOW_SLOT *slot, const GRECT *occluder)
{
	GEM_WINDOW_ORECT *rectangle;
	GEM_WINDOW_ORECT *tail;
	GEM_WINDOW_ORECT **link;
	GEM_WINDOW_ORECT *successor;
	GEM_WINDOW_ORECT *scan;

	link = &slot->first_rect;
	while (*link) {
		rectangle = *link;
		successor = rectangle->next;
		if (!gem_window_break_one(manager, link, occluder)) {
			return FALSE;
		}
		if (*link == rectangle) {
			link = &rectangle->next;
			continue;
		}

		/* Find the original successor after the newly inserted pieces. */
		scan = *link;
		if (scan == successor)
			continue;
		while (scan) {
			tail = scan;
			if (tail->next == successor) {
				link = &tail->next;
				break;
			}
			scan = tail->next;
		}
		if (!scan)
			return FALSE;
	}
	return TRUE;
}

static WORD
gem_window_build_one_list(GEM_WINDOW_RESIDENT *manager, WORD handle)
{
	GEM_WINDOW_SLOT *slot;
	GEM_WINDOW_ORECT *rectangle;
	OBJECT *object;
	GRECT base;
	GRECT occluder;
	WORD above;

	slot = gem_window_slot_at(manager, (UWORD) handle);
	if (handle == GEM_WINDOW_ROOT)
		gem_window_rect_copy(&slot->work, &base);
	else {
		object = gem_window_tree_at(manager, (UWORD) handle);
		gem_window_object_rect(object, &base);
	}
	if (base.g_w <= 0 || base.g_h <= 0)
		return TRUE;
	rectangle = gem_window_rect_alloc(manager);
	if (!rectangle) {
		manager->rect_overflow = TRUE;
		return FALSE;
	}
	gem_window_rect_copy(&base, &rectangle->rectangle);
	slot->first_rect = rectangle;

	if (handle == GEM_WINDOW_ROOT) {
		object = gem_window_tree_at(manager, GEM_WINDOW_ROOT);
		above = object->ob_head;
	} else {
		object = gem_window_tree_at(manager, (UWORD) handle);
		above = object->ob_next;
	}
	while (above != GEM_WINDOW_NIL && above != GEM_WINDOW_ROOT) {
		object = gem_window_tree_at(manager, (UWORD) above);
		gem_window_object_rect(object, &occluder);
		if (!gem_window_break_list(manager, slot, &occluder))
			return FALSE;
		above = object->ob_next;
	}
	if (slot->first_rect) {
		rectangle = slot->first_rect;
		if (rectangle->next)
			slot->flags |= GEM_WINDOW_VF_BROKEN;
		else
			slot->flags &= (UWORD) ~GEM_WINDOW_VF_BROKEN;
	}
	return TRUE;
}

/* Original newrect() over the shallow W_TREE, rebuilt only after mutation. */
static WORD
gem_window_rebuild_rectangles(GEM_WINDOW_RESIDENT *manager)
{
	OBJECT *root;
	OBJECT *object;
	WORD handle;

	gem_window_release_lists(manager);
	manager->rect_overflow = FALSE;
	if (!gem_window_build_one_list(manager, GEM_WINDOW_ROOT))
		return FALSE;
	root = gem_window_tree_at(manager, GEM_WINDOW_ROOT);
	handle = root->ob_head;
	while (handle != GEM_WINDOW_NIL && handle != GEM_WINDOW_ROOT) {
		object = gem_window_tree_at(manager, (UWORD) handle);
		if (!gem_window_build_one_list(manager, handle))
			return FALSE;
		handle = object->ob_next;
	}
	return TRUE;
}

static WORD
gem_window_visible_union(const GEM_WINDOW_SLOT *slot, GRECT *rectangle)
{
	const GEM_WINDOW_ORECT *owned;

	gem_window_rect_zero(rectangle);
	owned = slot->first_rect;
	while (owned) {
		if (!gem_window_rect_union(&owned->rectangle, rectangle))
			return FALSE;
		owned = owned->next;
	}
	return rectangle->g_w > 0 && rectangle->g_h > 0;
}

VOID
gem_window_resident_effects_init(GEM_WINDOW_EFFECTS *effects)
{
	if (!effects)
		return;
	effects->dirty_valid = FALSE;
	effects->redraw_background = FALSE;
	gem_window_rect_zero(&effects->dirty);
	effects->message_count = 0;
}

static VOID
gem_window_dirty(GEM_WINDOW_EFFECTS *effects, const GRECT *rectangle)
{
	if (!effects || !rectangle || rectangle->g_w <= 0
	    || rectangle->g_h <= 0)
		return;
	if (!effects->dirty_valid) {
		gem_window_rect_copy(rectangle, &effects->dirty);
		effects->dirty_valid = TRUE;
	} else
		gem_window_rect_union(rectangle, &effects->dirty);
}

/* Geometry or z-order damage must expose Desktop before frames are rebuilt. */
static VOID
gem_window_background_dirty(GEM_WINDOW_EFFECTS *effects,
	const GRECT *rectangle)
{
	if (!effects || !rectangle || rectangle->g_w <= 0
	    || rectangle->g_h <= 0)
		return;
	effects->redraw_background = TRUE;
	gem_window_dirty(effects, rectangle);
}

static WORD
gem_window_append_message(GEM_WINDOW_RESIDENT *manager,
	GEM_WINDOW_EFFECTS *effects, const GEM_WINDOW_SLOT *slot,
	UWORD type, WORD handle, const GRECT *rectangle)
{
	GEM_WINDOW_MESSAGE *message;

	if (!effects || !slot || effects->message_count
	    >= GEM_WINDOW_MESSAGE_COUNT)
		return FALSE;
	message = gem_window_message_at(effects, effects->message_count);
	effects->message_count++;
	message->owner = slot->owner;
	message->generation_lo = slot->generation_lo;
	message->generation_hi = slot->generation_hi;
	message->words[0] = type;
	message->words[1] = (UWORD) manager->system_owner;
	message->words[2] = 0;
	message->words[3] = (UWORD) handle;
	message->words[4] = (UWORD) rectangle->g_x;
	message->words[5] = (UWORD) rectangle->g_y;
	message->words[6] = (UWORD) rectangle->g_w;
	message->words[7] = (UWORD) rectangle->g_h;
	return TRUE;
}

/* Direct w_redraw(): work intersection, visible union, then WM_REDRAW. */
static VOID
gem_window_redraw(GEM_WINDOW_RESIDENT *manager,
	GEM_WINDOW_EFFECTS *effects)
{
	GEM_WINDOW_SLOT *slot;
	OBJECT *root;
	OBJECT *object;
	GRECT clip;
	GRECT visible;
	WORD handle;

	if (!effects || !effects->dirty_valid)
		return;
	root = gem_window_tree_at(manager, GEM_WINDOW_ROOT);
	handle = root->ob_head;
	while (handle != GEM_WINDOW_NIL && handle != GEM_WINDOW_ROOT) {
		slot = gem_window_slot_at(manager, (UWORD) handle);
		object = gem_window_tree_at(manager, (UWORD) handle);
		gem_window_rect_copy(&effects->dirty, &clip);
		if (gem_window_rect_intersect(&slot->work, &clip)
		    && gem_window_visible_union(slot, &visible)
		    && gem_window_rect_intersect(&visible, &clip))
			gem_window_append_message(manager, effects, slot,
				GEM_WINDOW_WM_REDRAW, handle, &clip);
		handle = object->ob_next;
	}
}

/*
 * Public bounded damage seam for transient menu and form surfaces.  Original
 * GEM restored SAVE_AREA through w_redraw(), which repainted the Desktop and
 * sent WM_REDRAW to every visible client work area.  The ELKS resident keeps
 * no large backing bitmap, so it creates that same effect graph directly.
 *
 * Coordinates are signed scale-one pixels.  Intersecting with root work both
 * protects the menu bar and rejects wrapped or off-screen rectangles.  The
 * effect is initialized here rather than by every caller; with eleven client
 * handles, one message per handle always fits the fixed twelve-record array.
 */
WORD
gem_window_resident_damage(GEM_WINDOW_RESIDENT *manager,
	const GRECT *rectangle, GEM_WINDOW_EFFECTS *effects)
{
	GEM_WINDOW_SLOT *root;
	GRECT clipped;

	if (!manager || !manager->ready || !rectangle || !effects)
		return FALSE;
	gem_window_resident_effects_init(effects);
	root = gem_window_slot_at(manager, GEM_WINDOW_ROOT);
	gem_window_rect_copy(rectangle, &clipped);
	if (!gem_window_rect_intersect(&root->work, &clipped))
		return FALSE;
	gem_window_background_dirty(effects, &clipped);
	gem_window_redraw(manager, effects);
	return TRUE;
}

static WORD
gem_window_owner(const GEM_WINDOW_SLOT *slot, WORD owner,
	UWORD generation_lo, UWORD generation_hi)
{
	return slot->owner == owner && slot->generation_lo == generation_lo
		&& slot->generation_hi == generation_hi;
}

static GEM_WINDOW_SLOT *
gem_window_owned_slot(GEM_WINDOW_RESIDENT *manager, WORD handle,
	WORD owner, UWORD generation_lo, UWORD generation_hi)
{
	GEM_WINDOW_SLOT *slot;

	if (!manager || handle < GEM_WINDOW_OPEN_MIN
	    || handle >= GEM_WINDOW_OPEN_LIMIT)
		return (GEM_WINDOW_SLOT *) 0;
	slot = gem_window_slot_at(manager, (UWORD) handle);
	if (!(slot->flags & GEM_WINDOW_VF_INUSE)
	    || !gem_window_owner(slot, owner, generation_lo, generation_hi))
		return (GEM_WINDOW_SLOT *) 0;
	return slot;
}

static WORD
gem_window_create(GEM_WINDOW_RESIDENT *manager, WORD owner,
	UWORD generation_lo, UWORD generation_hi, UWORD kind,
	const GRECT *full)
{
	GEM_WINDOW_SLOT *slot;
	OBJECT *object;
	GRECT work;
	WORD handle;

	if (!manager || !manager->ready || !full)
		return GEM_WINDOW_NIL;
	/*
	 * Original GEMWMLIB.C wm_create() stores the caller's WS_FULL rectangle
	 * verbatim and initializes WS_CURR/WS_PREV to gl_rzero.  In particular,
	 * Desk accessories commonly allocate with a zero full rectangle and pass
	 * their real scale-one pixel geometry to wm_open() immediately afterward.
	 * Keep the bounded screen check for a nonzero full rectangle, but preserve
	 * that exact original zero-rectangle allocation contract.  No coordinate
	 * arithmetic is performed for the zero case, so there is no overflow or
	 * saturation boundary.
	 */
	if ((full->g_x || full->g_y || full->g_w || full->g_h)
	    && !gem_window_public_rect(manager, kind, full, &work))
		return GEM_WINDOW_NIL;
	handle = GEM_WINDOW_OPEN_MIN;
	slot = gem_window_slot_at(manager, GEM_WINDOW_OPEN_MIN);
	while (handle < GEM_WINDOW_OPEN_LIMIT) {
		if (!(slot->flags & GEM_WINDOW_VF_INUSE))
			break;
		slot++;
		handle++;
	}
	if (handle >= GEM_WINDOW_OPEN_LIMIT)
		return GEM_WINDOW_NIL;
	gem_window_clear_slot(slot);
	slot->flags = GEM_WINDOW_VF_INUSE;
	slot->kind = kind;
	slot->owner = owner;
	slot->generation_lo = generation_lo;
	slot->generation_hi = generation_hi;
	gem_window_rect_copy(full, &slot->full);
	object = gem_window_tree_at(manager, (UWORD) handle);
	gem_window_clear_object(object);
	return handle;
}

static WORD
gem_window_open(GEM_WINDOW_RESIDENT *manager, GEM_WINDOW_SLOT *slot,
	WORD handle, const GRECT *rectangle, GEM_WINDOW_EFFECTS *effects)
{
	OBJECT *object;
	GRECT old_outer;
	GRECT old_previous;
	GRECT old_work;
	GRECT work;
	UWORD old_flags;
	WORD old_top;

	if (!slot || (slot->flags & GEM_WINDOW_VF_INTREE)
	    || !gem_window_public_rect(manager, slot->kind, rectangle, &work))
		return FALSE;
	object = gem_window_tree_at(manager, (UWORD) handle);
	gem_window_object_rect(object, &old_outer);
	gem_window_rect_copy(&slot->previous, &old_previous);
	gem_window_rect_copy(&slot->work, &old_work);
	old_flags = slot->flags;
	old_top = manager->top;
	gem_window_set_object_rect(object, rectangle);
	gem_window_rect_copy(&work, &slot->work);
	gem_window_rect_copy(rectangle, &slot->previous);
	slot->flags |= GEM_WINDOW_VF_INTREE;
	gem_window_tree_add(manager, handle);
	if (!gem_window_rebuild_rectangles(manager)) {
		(void) gem_window_tree_delete(manager, handle);
		gem_window_set_object_rect(object, &old_outer);
		gem_window_rect_copy(&old_previous, &slot->previous);
		gem_window_rect_copy(&old_work, &slot->work);
		slot->flags = old_flags;
		manager->top = old_top;
		(void) gem_window_rebuild_rectangles(manager);
		return FALSE;
	}
	gem_window_background_dirty(effects, rectangle);
	gem_window_redraw(manager, effects);
	return TRUE;
}

static WORD
gem_window_close(GEM_WINDOW_RESIDENT *manager, GEM_WINDOW_SLOT *slot,
	WORD handle, GEM_WINDOW_EFFECTS *effects)
{
	OBJECT *object;
	GRECT old;

	if (!slot || !(slot->flags & GEM_WINDOW_VF_INTREE))
		return FALSE;
	object = gem_window_tree_at(manager, (UWORD) handle);
	gem_window_object_rect(object, &old);
	if (!gem_window_tree_delete(manager, handle))
		return FALSE;
	gem_window_rect_copy(&old, &slot->previous);
	gem_window_rect_zero(&slot->work);
	object->ob_x = 0;
	object->ob_y = 0;
	object->ob_width = 0;
	object->ob_height = 0;
	slot->flags &= (UWORD) ~(GEM_WINDOW_VF_INTREE | GEM_WINDOW_VF_BROKEN);
	if (manager->control_handle == handle)
		gem_window_control_clear(manager);
	(void) gem_window_rebuild_rectangles(manager);
	gem_window_background_dirty(effects, &old);
	gem_window_redraw(manager, effects);
	return TRUE;
}

static WORD
gem_window_delete(GEM_WINDOW_RESIDENT *manager, GEM_WINDOW_SLOT *slot,
	WORD handle)
{
	OBJECT *object;

	if (!slot || (slot->flags & GEM_WINDOW_VF_INTREE))
		return FALSE;
	gem_window_release_list(manager, slot);
	gem_window_clear_slot(slot);
	object = gem_window_tree_at(manager, (UWORD) handle);
	gem_window_clear_object(object);
	return TRUE;
}

static WORD
gem_window_next_owned(GEM_WINDOW_SLOT *slot, GRECT *result)
{
	GEM_WINDOW_ORECT *owned;
	GRECT candidate;

	owned = slot->next_rect;
	while (owned) {
		slot->next_rect = owned->next;
		gem_window_rect_copy(&owned->rectangle, &candidate);
		if (gem_window_rect_intersect(&slot->work, &candidate)) {
			gem_window_rect_copy(&candidate, result);
			return TRUE;
		}
		owned = slot->next_rect;
	}
	gem_window_rect_zero(result);
	return TRUE;
}

static WORD
gem_window_get(GEM_WINDOW_RESIDENT *manager, WORD handle, UWORD field,
	WORD owner, UWORD generation_lo, UWORD generation_hi, GRECT *result)
{
	GEM_WINDOW_SLOT *slot;
	OBJECT *object;

	gem_window_rect_zero(result);
	if (handle == GEM_WINDOW_ROOT)
		slot = gem_window_slot_at(manager, GEM_WINDOW_ROOT);
	else {
		slot = gem_window_owned_slot(manager, handle, owner,
			generation_lo, generation_hi);
		if (!slot)
			return FALSE;
	}
	object = gem_window_tree_at(manager, (UWORD) handle);
	switch (field) {
	case GEM_WINDOW_WF_KIND:
		result->g_x = (WORD) slot->kind;
		return TRUE;
	case GEM_WINDOW_WF_WXYWH:
		gem_window_rect_copy(&slot->work, result);
		return TRUE;
	case GEM_WINDOW_WF_CXYWH:
		gem_window_object_rect(object, result);
		return TRUE;
	case GEM_WINDOW_WF_PXYWH:
		gem_window_rect_copy(&slot->previous, result);
		return TRUE;
	case GEM_WINDOW_WF_FXYWH:
		gem_window_rect_copy(&slot->full, result);
		return TRUE;
	case GEM_WINDOW_WF_HSLIDE:
		result->g_x = slot->hslide;
		return TRUE;
	case GEM_WINDOW_WF_VSLIDE:
		result->g_x = slot->vslide;
		return TRUE;
	case GEM_WINDOW_WF_HSLSIZ:
		result->g_x = slot->hslsiz;
		return TRUE;
	case GEM_WINDOW_WF_VSLSIZ:
		result->g_x = slot->vslsiz;
		return TRUE;
	case GEM_WINDOW_WF_TOP:
		result->g_x = manager->top;
		return TRUE;
	case GEM_WINDOW_WF_FIRSTXYWH:
		slot->next_rect = slot->first_rect;
		return gem_window_next_owned(slot, result);
	case GEM_WINDOW_WF_NEXTXYWH:
		return gem_window_next_owned(slot, result);
	case GEM_WINDOW_WF_TATTRB:
		result->g_x = (WORD) (slot->flags >> 3);
		return TRUE;
	default:
		return FALSE;
	}
}

static WORD
gem_window_set_current(GEM_WINDOW_RESIDENT *manager,
	GEM_WINDOW_SLOT *slot, WORD handle, const GRECT *rectangle,
	WORD top, GEM_WINDOW_EFFECTS *effects)
{
	OBJECT *object;
	GRECT old;
	GRECT old_previous;
	GRECT old_work;
	GRECT work;
	WORD old_head;
	WORD old_tail;
	WORD old_top;
	WORD old_next[GEM_WINDOW_COUNT];

	if (!(slot->flags & GEM_WINDOW_VF_INTREE)
	    || !gem_window_public_rect(manager, slot->kind, rectangle, &work))
		return FALSE;
	object = gem_window_tree_at(manager, (UWORD) handle);
	gem_window_object_rect(object, &old);
	gem_window_rect_copy(&slot->previous, &old_previous);
	gem_window_rect_copy(&slot->work, &old_work);
	if (top)
		gem_window_tree_save_order(manager, old_next, &old_head,
			&old_tail, &old_top);
	gem_window_rect_copy(&old, &slot->previous);
	gem_window_set_object_rect(object, rectangle);
	gem_window_rect_copy(&work, &slot->work);
	if (top && !gem_window_tree_top(manager, handle)) {
		gem_window_set_object_rect(object, &old);
		gem_window_rect_copy(&old_previous, &slot->previous);
		gem_window_rect_copy(&old_work, &slot->work);
		return FALSE;
	}
	if (!gem_window_rebuild_rectangles(manager)) {
		gem_window_set_object_rect(object, &old);
		gem_window_rect_copy(&old_previous, &slot->previous);
		gem_window_rect_copy(&old_work, &slot->work);
		if (top)
			gem_window_tree_restore_order(manager, old_next, old_head,
				old_tail, old_top);
		(void) gem_window_rebuild_rectangles(manager);
		return FALSE;
	}
	gem_window_background_dirty(effects, &old);
	gem_window_background_dirty(effects, rectangle);
	gem_window_redraw(manager, effects);
	return TRUE;
}

static WORD
gem_window_set_top(GEM_WINDOW_RESIDENT *manager, GEM_WINDOW_SLOT *slot,
	WORD handle, GEM_WINDOW_EFFECTS *effects)
{
	OBJECT *object;
	GRECT rectangle;
	WORD old_head;
	WORD old_tail;
	WORD old_top;
	WORD old_next[GEM_WINDOW_COUNT];

	if (!(slot->flags & GEM_WINDOW_VF_INTREE))
		return FALSE;
	object = gem_window_tree_at(manager, (UWORD) handle);
	gem_window_object_rect(object, &rectangle);
	gem_window_tree_save_order(manager, old_next, &old_head, &old_tail,
		&old_top);
	if (!gem_window_tree_top(manager, handle))
		return FALSE;
	if (!gem_window_rebuild_rectangles(manager)) {
		gem_window_tree_restore_order(manager, old_next, old_head, old_tail,
			old_top);
		(void) gem_window_rebuild_rectangles(manager);
		return FALSE;
	}
	gem_window_background_dirty(effects, &rectangle);
	gem_window_redraw(manager, effects);
	return TRUE;
}

static WORD
gem_window_set(GEM_WINDOW_RESIDENT *manager, WORD handle, UWORD field,
	const UWORD *input, WORD owner, UWORD generation_lo,
	UWORD generation_hi, GEM_WINDOW_EFFECTS *effects)
{
	GEM_WINDOW_SLOT *slot;
	OBJECT *object;
	GRECT rectangle;
	WORD value;

	if (handle == GEM_WINDOW_ROOT) {
		slot = gem_window_slot_at(manager, GEM_WINDOW_ROOT);
		if (field != GEM_WINDOW_WF_NEWDESK)
			return FALSE;
		manager->desktop.lo = input[2];
		manager->desktop.hi = input[3];
		manager->desktop_root = input[4];
		slot->owner = owner;
		slot->generation_lo = generation_lo;
		slot->generation_hi = generation_hi;
		gem_window_background_dirty(effects, &slot->work);
		return TRUE;
	}
	slot = gem_window_owned_slot(manager, handle, owner,
		generation_lo, generation_hi);
	if (!slot)
		return FALSE;
	object = gem_window_tree_at(manager, (UWORD) handle);
	switch (field) {
	case GEM_WINDOW_WF_NAME:
		slot->name.lo = input[2];
		slot->name.hi = input[3];
		gem_window_object_rect(object, &rectangle);
		gem_window_dirty(effects, &rectangle);
		return TRUE;
	case GEM_WINDOW_WF_INFO:
		slot->info.lo = input[2];
		slot->info.hi = input[3];
		gem_window_object_rect(object, &rectangle);
		gem_window_dirty(effects, &rectangle);
		return TRUE;
	case GEM_WINDOW_WF_CXYWH:
	case GEM_WINDOW_WF_SIZTOP:
		rectangle.g_x = (WORD) input[2];
		rectangle.g_y = (WORD) input[3];
		rectangle.g_w = (WORD) input[4];
		rectangle.g_h = (WORD) input[5];
		return gem_window_set_current(manager, slot, handle, &rectangle,
			field == GEM_WINDOW_WF_SIZTOP, effects);
	case GEM_WINDOW_WF_TOP:
		return gem_window_set_top(manager, slot, handle, effects);
	case GEM_WINDOW_WF_HSLIDE:
	case GEM_WINDOW_WF_VSLIDE:
	case GEM_WINDOW_WF_HSLSIZ:
	case GEM_WINDOW_WF_VSLSIZ:
		value = (WORD) input[2];
		if (value < -1)
			value = -1;
		if (value > 1000)
			value = 1000;
		if (field == GEM_WINDOW_WF_HSLIDE)
			slot->hslide = value;
		else if (field == GEM_WINDOW_WF_VSLIDE)
			slot->vslide = value;
		else if (field == GEM_WINDOW_WF_HSLSIZ)
			slot->hslsiz = value;
		else
			slot->vslsiz = value;
		if (slot->flags & GEM_WINDOW_VF_INTREE) {
			gem_window_object_rect(object, &rectangle);
			gem_window_dirty(effects, &rectangle);
		}
		return TRUE;
	default:
		return FALSE;
	}
}

static WORD
gem_window_find(const GEM_WINDOW_RESIDENT *manager, WORD x, WORD y)
{
	const OBJECT *root;
	const OBJECT *object;
	GRECT rectangle;
	WORD handle;
	WORD found;

	if (!manager || !manager->ready || x < 0 || y < 0
	    || x >= manager->screen_width || y >= manager->screen_height)
		return GEM_WINDOW_NIL;
	found = GEM_WINDOW_ROOT;
	root = gem_window_const_tree_at(manager, GEM_WINDOW_ROOT);
	handle = root->ob_head;
	while (handle != GEM_WINDOW_NIL && handle != GEM_WINDOW_ROOT) {
		object = gem_window_const_tree_at(manager, (UWORD) handle);
		gem_window_object_rect(object, &rectangle);
		if (gem_window_rect_contains(&rectangle, x, y))
			found = handle;
		handle = object->ob_next;
	}
	return found;
}

VOID
gem_window_resident_init(GEM_WINDOW_RESIDENT *manager)
{
	GEM_WINDOW_SLOT *slot;
	GEM_WINDOW_ORECT *rectangle;
	OBJECT *object;
	UWORD count;
	UWORD index;

	if (!manager)
		return;
	slot = manager->windows;
	count = GEM_WINDOW_COUNT;
	while (count--) {
		gem_window_clear_slot(slot);
		slot++;
	}
	object = manager->tree;
	count = GEM_WINDOW_COUNT;
	while (count--) {
		gem_window_clear_object(object);
		object++;
	}
	object = manager->active;
	count = GEM_WINDOW_ACTIVE_COUNT;
	while (count--) {
		gem_window_clear_object(object);
		object++;
	}
	rectangle = manager->rectangles;
	index = GEM_WINDOW_RECT_COUNT;
	while (index > 1U) {
		rectangle->next = rectangle + 1;
		gem_window_rect_zero(&rectangle->rectangle);
		rectangle++;
		index--;
	}
	rectangle->next = (GEM_WINDOW_ORECT *) 0;
	gem_window_rect_zero(&rectangle->rectangle);
	manager->active_name.te_ptext.lo = 0;
	manager->active_name.te_ptext.hi = 0;
	manager->active_name.te_ptmplt.lo = 0;
	manager->active_name.te_ptmplt.hi = 0;
	manager->active_name.te_pvalid.lo = 0;
	manager->active_name.te_pvalid.hi = 0;
	manager->active_name.te_font = GEM_WINDOW_IBM_FONT;
	manager->active_name.te_junk1 = 0;
	manager->active_name.te_just = GEM_WINDOW_TE_CENTER;
	manager->active_name.te_color = GEM_WINDOW_MONO_OPAQUE_COLOR;
	manager->active_name.te_junk2 = 0;
	manager->active_name.te_thickness = 1;
	manager->active_name.te_txtlen = GEM_WINDOW_TITLE_BYTES;
	manager->active_name.te_tmplen = GEM_WINDOW_TITLE_BYTES;
	manager->active_info.te_ptext.lo = 0;
	manager->active_info.te_ptext.hi = 0;
	manager->active_info.te_ptmplt.lo = 0;
	manager->active_info.te_ptmplt.hi = 0;
	manager->active_info.te_pvalid.lo = 0;
	manager->active_info.te_pvalid.hi = 0;
	manager->active_info.te_font = GEM_WINDOW_IBM_FONT;
	manager->active_info.te_junk1 = 0;
	manager->active_info.te_just = GEM_WINDOW_TE_LEFT;
	manager->active_info.te_color = GEM_WINDOW_MONO_OPAQUE_COLOR;
	manager->active_info.te_junk2 = 0;
	manager->active_info.te_thickness = 1;
	manager->active_info.te_txtlen = GEM_WINDOW_TITLE_BYTES;
	manager->active_info.te_tmplen = GEM_WINDOW_TITLE_BYTES;
	manager->desktop.lo = 0;
	manager->desktop.hi = 0;
	manager->desktop_root = 0;
	manager->free_rect = manager->rectangles;
	manager->top = GEM_WINDOW_ROOT;
	manager->system_owner = 0;
	manager->screen_width = 0;
	manager->screen_height = 0;
	manager->box_width = 0;
	manager->box_height = 0;
	gem_window_control_clear(manager);
	manager->ready = FALSE;
	manager->rect_overflow = FALSE;
}

WORD
gem_window_resident_configure(GEM_WINDOW_RESIDENT *manager,
	const GEM_WINDOW_SCREEN *screen)
{
	GEM_WINDOW_SLOT *root_slot;
	OBJECT *root;
	GRECT screen_rectangle;
	GRECT work_rectangle;

	if (!manager || !screen || screen->screen_width <= 0
	    || screen->screen_height <= 0 || screen->box_width <= 0
	    || screen->box_height <= 0
	    || screen->screen_width > GEM_WINDOW_WORD_MAX
	    || screen->screen_height > GEM_WINDOW_WORD_MAX
	    || screen->box_width >= screen->screen_width
	    || screen->box_height >= screen->screen_height)
		return FALSE;
	gem_window_resident_init(manager);
	manager->system_owner = screen->system_owner;
	manager->screen_width = screen->screen_width;
	manager->screen_height = screen->screen_height;
	manager->box_width = screen->box_width;
	manager->box_height = screen->box_height;
	manager->ready = TRUE;
	gem_window_active_types(manager);

	screen_rectangle.g_x = 0;
	screen_rectangle.g_y = 0;
	screen_rectangle.g_w = screen->screen_width;
	screen_rectangle.g_h = screen->screen_height;
	work_rectangle.g_x = 0;
	work_rectangle.g_y = screen->box_height;
	work_rectangle.g_w = screen->screen_width;
	work_rectangle.g_h = screen->screen_height - screen->box_height;
	root_slot = gem_window_slot_at(manager, GEM_WINDOW_ROOT);
	root_slot->flags = GEM_WINDOW_VF_INUSE | GEM_WINDOW_VF_INTREE;
	root_slot->owner = screen->system_owner;
	gem_window_rect_copy(&work_rectangle, &root_slot->full);
	gem_window_rect_copy(&work_rectangle, &root_slot->work);
	gem_window_rect_copy(&screen_rectangle, &root_slot->previous);
	root = gem_window_tree_at(manager, GEM_WINDOW_ROOT);
	root->ob_type = GEM_WINDOW_G_BOX;
	root->ob_spec.lo = GEM_WINDOW_BOX_SPEC_LO;
	root->ob_spec.hi = GEM_WINDOW_BOX_SPEC_HI;
	gem_window_set_object_rect(root, &screen_rectangle);
	if (!gem_window_rebuild_rectangles(manager)) {
		manager->ready = FALSE;
		return FALSE;
	}
	return TRUE;
}

static WORD
gem_window_call_shape(const GEM_WINDOW_CALL *call, UWORD input_count,
	UWORD output_count)
{
	if (!call || !call->control || call->control[1] < input_count
	    || call->control[2] < output_count)
		return FALSE;
	if (input_count && !call->int_in)
		return FALSE;
	if (output_count && !call->int_out)
		return FALSE;
	return TRUE;
}

static WORD
gem_window_finish(const GEM_WINDOW_CALL *call, WORD result, WORD *handled)
{
	call->int_out[0] = (UWORD) result;
	*handled = TRUE;
	return result;
}

static WORD
gem_window_malformed(const GEM_WINDOW_CALL *call, WORD *handled)
{
	*handled = TRUE;
	if (call && call->control && call->int_out && call->control[2])
		call->int_out[0] = FALSE;
	return FALSE;
}

WORD
gem_window_resident_dispatch(GEM_WINDOW_RESIDENT *manager,
	const GEM_WINDOW_CALL *call, GEM_WINDOW_EFFECTS *effects,
	WORD *handled)
{
	GEM_WINDOW_SLOT *slot;
	GRECT rectangle;
	WORD handle;
	WORD result;
	UWORD opcode;

	if (!handled)
		return FALSE;
	*handled = FALSE;
	if (!call || !call->control)
		return FALSE;
	opcode = call->control[0];
	if (opcode < GEM_WINDOW_WIND_CREATE || opcode > GEM_WINDOW_WIND_FIND)
		return FALSE;
	if (!manager || !effects)
		return gem_window_malformed(call, handled);
	gem_window_resident_effects_init(effects);
	switch (opcode) {
	case GEM_WINDOW_WIND_CREATE:
		if (!gem_window_call_shape(call, 5U, 1U))
			return gem_window_malformed(call, handled);
		rectangle.g_x = (WORD) call->int_in[1];
		rectangle.g_y = (WORD) call->int_in[2];
		rectangle.g_w = (WORD) call->int_in[3];
		rectangle.g_h = (WORD) call->int_in[4];
		result = gem_window_create(manager, call->owner,
			call->generation_lo, call->generation_hi,
			call->int_in[0], &rectangle);
		return gem_window_finish(call, result, handled);
	case GEM_WINDOW_WIND_OPEN:
		if (!gem_window_call_shape(call, 5U, 1U))
			return gem_window_malformed(call, handled);
		handle = (WORD) call->int_in[0];
		slot = gem_window_owned_slot(manager, handle, call->owner,
			call->generation_lo, call->generation_hi);
		rectangle.g_x = (WORD) call->int_in[1];
		rectangle.g_y = (WORD) call->int_in[2];
		rectangle.g_w = (WORD) call->int_in[3];
		rectangle.g_h = (WORD) call->int_in[4];
		result = gem_window_open(manager, slot, handle, &rectangle,
			effects);
		return gem_window_finish(call, result, handled);
	case GEM_WINDOW_WIND_CLOSE:
		if (!gem_window_call_shape(call, 1U, 1U))
			return gem_window_malformed(call, handled);
		handle = (WORD) call->int_in[0];
		slot = gem_window_owned_slot(manager, handle, call->owner,
			call->generation_lo, call->generation_hi);
		result = gem_window_close(manager, slot, handle, effects);
		return gem_window_finish(call, result, handled);
	case GEM_WINDOW_WIND_DELETE:
		if (!gem_window_call_shape(call, 1U, 1U))
			return gem_window_malformed(call, handled);
		handle = (WORD) call->int_in[0];
		slot = gem_window_owned_slot(manager, handle, call->owner,
			call->generation_lo, call->generation_hi);
		result = gem_window_delete(manager, slot, handle);
		return gem_window_finish(call, result, handled);
	case GEM_WINDOW_WIND_GET:
		if (!gem_window_call_shape(call, 2U, 5U))
			return gem_window_malformed(call, handled);
		result = gem_window_get(manager, (WORD) call->int_in[0],
			call->int_in[1], call->owner, call->generation_lo,
			call->generation_hi, &rectangle);
		call->int_out[1] = (UWORD) rectangle.g_x;
		call->int_out[2] = (UWORD) rectangle.g_y;
		call->int_out[3] = (UWORD) rectangle.g_w;
		call->int_out[4] = (UWORD) rectangle.g_h;
		return gem_window_finish(call, result, handled);
	case GEM_WINDOW_WIND_SET:
		if (!gem_window_call_shape(call, 6U, 1U))
			return gem_window_malformed(call, handled);
		result = gem_window_set(manager, (WORD) call->int_in[0],
			call->int_in[1], call->int_in, call->owner,
			call->generation_lo, call->generation_hi, effects);
		return gem_window_finish(call, result, handled);
	case GEM_WINDOW_WIND_FIND:
		if (!gem_window_call_shape(call, 2U, 1U))
			return gem_window_malformed(call, handled);
		result = gem_window_find(manager, (WORD) call->int_in[0],
			(WORD) call->int_in[1]);
		return gem_window_finish(call, result, handled);
	default:
		return FALSE;
	}
}

VOID
gem_window_resident_detach(GEM_WINDOW_RESIDENT *manager, WORD owner,
	UWORD generation_lo, UWORD generation_hi,
	GEM_WINDOW_EFFECTS *effects)
{
	GEM_WINDOW_SLOT *slot;
	OBJECT *object;
	GRECT rectangle;
	WORD handle;

	if (!manager || !effects)
		return;
	gem_window_resident_effects_init(effects);
	handle = GEM_WINDOW_OPEN_MIN;
	slot = gem_window_slot_at(manager, GEM_WINDOW_OPEN_MIN);
	while (handle < GEM_WINDOW_OPEN_LIMIT) {
		if ((slot->flags & GEM_WINDOW_VF_INUSE)
		    && gem_window_owner(slot, owner, generation_lo,
					generation_hi)) {
			object = gem_window_tree_at(manager, (UWORD) handle);
			if (slot->flags & GEM_WINDOW_VF_INTREE) {
				gem_window_object_rect(object, &rectangle);
				gem_window_background_dirty(effects, &rectangle);
				gem_window_tree_delete(manager, handle);
			}
			gem_window_release_list(manager, slot);
			gem_window_clear_slot(slot);
			gem_window_clear_object(object);
			if (manager->control_handle == handle)
				gem_window_control_clear(manager);
		}
		slot++;
		handle++;
	}
	slot = gem_window_slot_at(manager, GEM_WINDOW_ROOT);
	if (gem_window_owner(slot, owner, generation_lo, generation_hi)) {
		manager->desktop.lo = 0;
		manager->desktop.hi = 0;
		manager->desktop_root = 0;
		slot->owner = manager->system_owner;
		slot->generation_lo = 0;
		slot->generation_hi = 0;
		gem_window_background_dirty(effects, &slot->work);
	}
	(void) gem_window_rebuild_rectangles(manager);
	gem_window_redraw(manager, effects);
}

WORD
gem_window_resident_first(const GEM_WINDOW_RESIDENT *manager)
{
	const OBJECT *root;

	if (!manager || !manager->ready)
		return GEM_WINDOW_NIL;
	root = gem_window_const_tree_at(manager, GEM_WINDOW_ROOT);
	return root->ob_head;
}

WORD
gem_window_resident_next(const GEM_WINDOW_RESIDENT *manager, WORD handle)
{
	const OBJECT *object;

	if (!manager || handle < GEM_WINDOW_OPEN_MIN
	    || handle >= GEM_WINDOW_OPEN_LIMIT)
		return GEM_WINDOW_NIL;
	object = gem_window_const_tree_at(manager, (UWORD) handle);
	return object->ob_next == GEM_WINDOW_ROOT
		? GEM_WINDOW_NIL : object->ob_next;
}

WORD
gem_window_resident_top_owner(const GEM_WINDOW_RESIDENT *manager)
{
	const GEM_WINDOW_SLOT *slot;

	if (!manager || !manager->ready || manager->top < GEM_WINDOW_ROOT
	    || manager->top >= GEM_WINDOW_COUNT)
		return GEM_WINDOW_NIL;
	slot = manager->windows + manager->top;
	if (manager->top != GEM_WINDOW_ROOT
	    && !(slot->flags & GEM_WINDOW_VF_INTREE))
		return GEM_WINDOW_NIL;
	return slot->owner;
}

static VOID
gem_window_active_add(OBJECT *objects, WORD parent, WORD child,
	WORD x, WORD y, WORD width, WORD height)
{
	OBJECT *object;
	GRECT rectangle;

	object = objects + child;
	object->ob_head = GEM_WINDOW_NIL;
	object->ob_tail = GEM_WINDOW_NIL;
	rectangle.g_x = x;
	rectangle.g_y = y;
	rectangle.g_w = width;
	rectangle.g_h = height;
	gem_window_set_object_rect(object, &rectangle);
	gem_window_obadd(objects, parent, child);
}

static VOID
gem_window_active_types(GEM_WINDOW_RESIDENT *manager)
{
	OBJECT *objects;
	OBJECT *object;
	UWORD data_segment;

	objects = manager->active;
	object = objects + GEM_WINDOW_W_BOX;
	/*
	 * Keep the original GEMWMLIB.C gl_watype[W_BOX] value.  W_BOX is a
	 * structural IBOX: its children paint the title and controls, while the
	 * application retains every pixel in W_WORK until its WM_REDRAW handler
	 * runs.  Making this root an opaque G_BOX clears client work whenever AES
	 * rebuilds chrome after dismissing a menu or form.
	 */
	object->ob_type = GEM_WINDOW_G_IBOX;
	object->ob_spec.lo = GEM_WINDOW_BOX_SPEC_LO;
	object->ob_spec.hi = GEM_WINDOW_BOX_SPEC_HI;
	object = objects + GEM_WINDOW_W_TITLE;
	object->ob_type = GEM_WINDOW_G_BOX;
	object->ob_spec.lo = GEM_WINDOW_BOX_SPEC_LO;
	object->ob_spec.hi = GEM_WINDOW_BOX_SPEC_HI;
	object = objects + GEM_WINDOW_W_CLOSER;
	object->ob_type = GEM_WINDOW_G_BOXCHAR;
	object->ob_flags = GEM_WINDOW_FLAG3D | GEM_WINDOW_USECOLORCAT;
	object->ob_spec.lo = GEM_WINDOW_MONO_OPAQUE_COLOR;
	object->ob_spec.hi = GEM_WINDOW_CLOSER_SPEC_HI;
	object = objects + GEM_WINDOW_W_NAME;
	object->ob_type = GEM_WINDOW_G_BOXTEXT;
	object->ob_flags = GEM_WINDOW_USECOLORCAT;
	data_segment = gem_window_data_segment();
	object->ob_spec.lo = (UWORD) &manager->active_name;
	object->ob_spec.hi = data_segment;
	object = objects + GEM_WINDOW_W_FULLER;
	object->ob_type = GEM_WINDOW_G_BOXCHAR;
	object->ob_flags = GEM_WINDOW_FLAG3D | GEM_WINDOW_USECOLORCAT;
	object->ob_spec.lo = GEM_WINDOW_MONO_OPAQUE_COLOR;
	object->ob_spec.hi = GEM_WINDOW_FULLER_SPEC_HI;
	object = objects + GEM_WINDOW_W_INFO;
	object->ob_type = GEM_WINDOW_G_BOXTEXT;
	object->ob_flags = GEM_WINDOW_USECOLORCAT;
	object->ob_spec.lo = (UWORD) &manager->active_info;
	object->ob_spec.hi = data_segment;
	object = objects + GEM_WINDOW_W_DATA;
	object->ob_type = GEM_WINDOW_G_IBOX;
	object->ob_spec.lo = GEM_WINDOW_BOX_SPEC_LO;
	object->ob_spec.hi = GEM_WINDOW_DATA_SPEC_HI;
	object = objects + GEM_WINDOW_W_WORK;
	object->ob_type = GEM_WINDOW_G_IBOX;
	object->ob_spec.lo = GEM_WINDOW_BOX_SPEC_LO;
	object->ob_spec.hi = GEM_WINDOW_DATA_SPEC_HI;
	object = objects + GEM_WINDOW_W_SIZER;
	object->ob_type = GEM_WINDOW_G_BOXCHAR;
	object->ob_flags = GEM_WINDOW_FLAG3D | GEM_WINDOW_USECOLORCAT;
	object->ob_spec.lo = GEM_WINDOW_MONO_OPAQUE_COLOR;
	object->ob_spec.hi = GEM_WINDOW_SIZER_SPEC_HI;
	object = objects + GEM_WINDOW_W_VBAR;
	object->ob_type = GEM_WINDOW_G_BOX;
	object->ob_spec.lo = GEM_WINDOW_BAR_SPEC_LO;
	object->ob_spec.hi = GEM_WINDOW_BAR_SPEC_HI;
	object = objects + GEM_WINDOW_W_UPARROW;
	object->ob_type = GEM_WINDOW_G_BOXCHAR;
	object->ob_flags = GEM_WINDOW_FLAG3D | GEM_WINDOW_USECOLORCAT;
	object->ob_spec.lo = GEM_WINDOW_MONO_OPAQUE_COLOR;
	object->ob_spec.hi = GEM_WINDOW_UPARROW_SPEC_HI;
	object = objects + GEM_WINDOW_W_DNARROW;
	object->ob_type = GEM_WINDOW_G_BOXCHAR;
	object->ob_flags = GEM_WINDOW_FLAG3D | GEM_WINDOW_USECOLORCAT;
	object->ob_spec.lo = GEM_WINDOW_MONO_OPAQUE_COLOR;
	object->ob_spec.hi = GEM_WINDOW_DNARROW_SPEC_HI;
	object = objects + GEM_WINDOW_W_VSLIDE;
	object->ob_type = GEM_WINDOW_G_BOX;
	object->ob_flags = GEM_WINDOW_USECOLORCAT;
	object->ob_spec.lo = GEM_WINDOW_MONO_OPAQUE_COLOR;
	object->ob_spec.hi = GEM_WINDOW_SLIDE_SPEC_HI;
	object = objects + GEM_WINDOW_W_VELEV;
	object->ob_type = GEM_WINDOW_G_BOX;
	object->ob_flags = GEM_WINDOW_FLAG3D | GEM_WINDOW_USECOLORCAT;
	object->ob_spec.lo = GEM_WINDOW_MONO_OPAQUE_COLOR;
	object->ob_spec.hi = GEM_WINDOW_ELEV_SPEC_HI;
	object = objects + GEM_WINDOW_W_HBAR;
	object->ob_type = GEM_WINDOW_G_BOX;
	object->ob_spec.lo = GEM_WINDOW_BAR_SPEC_LO;
	object->ob_spec.hi = GEM_WINDOW_BAR_SPEC_HI;
	object = objects + GEM_WINDOW_W_LFARROW;
	object->ob_type = GEM_WINDOW_G_BOXCHAR;
	object->ob_flags = GEM_WINDOW_FLAG3D | GEM_WINDOW_USECOLORCAT;
	object->ob_spec.lo = GEM_WINDOW_MONO_OPAQUE_COLOR;
	object->ob_spec.hi = GEM_WINDOW_LFARROW_SPEC_HI;
	object = objects + GEM_WINDOW_W_RTARROW;
	object->ob_type = GEM_WINDOW_G_BOXCHAR;
	object->ob_flags = GEM_WINDOW_FLAG3D | GEM_WINDOW_USECOLORCAT;
	object->ob_spec.lo = GEM_WINDOW_MONO_OPAQUE_COLOR;
	object->ob_spec.hi = GEM_WINDOW_RTARROW_SPEC_HI;
	object = objects + GEM_WINDOW_W_HSLIDE;
	object->ob_type = GEM_WINDOW_G_BOX;
	object->ob_flags = GEM_WINDOW_USECOLORCAT;
	object->ob_spec.lo = GEM_WINDOW_MONO_OPAQUE_COLOR;
	object->ob_spec.hi = GEM_WINDOW_SLIDE_SPEC_HI;
	object = objects + GEM_WINDOW_W_HELEV;
	object->ob_type = GEM_WINDOW_G_BOX;
	object->ob_flags = GEM_WINDOW_LASTOB | GEM_WINDOW_FLAG3D
		| GEM_WINDOW_USECOLORCAT;
	object->ob_spec.lo = GEM_WINDOW_MONO_OPAQUE_COLOR;
	object->ob_spec.hi = GEM_WINDOW_ELEV_SPEC_HI;
}

/*
 * Direct two-dimensional w_bldbar()/w_barcalc() closure.  Slider values and
 * sizes use GEM's scale of 1000.  Pixel conversion truncates toward zero;
 * the elevator is never smaller than one window box and saturates at the
 * available track length.  gem_window_scale() keeps every intermediate in a
 * word and therefore introduces no compiler multiply or divide helper.
 */
static VOID
gem_window_active_bar(GEM_WINDOW_RESIDENT *manager, OBJECT *objects,
	const GEM_WINDOW_SLOT *slot, WORD active, WORD vertical,
	const GRECT *bar)
{
	GRECT remaining;
	WORD bar_object;
	WORD first_arrow;
	WORD last_arrow;
	WORD slide_object;
	WORD elevator_object;
	UWORD first_kind;
	UWORD last_kind;
	UWORD slide_kind;
	WORD slide_value;
	WORD slide_size;
	WORD minimum_size;
	WORD space;
	WORD travel;
	WORD position;

	if (!bar || bar->g_w <= 0 || bar->g_h <= 0)
		return;
	if (vertical) {
		bar_object = GEM_WINDOW_W_VBAR;
		first_arrow = GEM_WINDOW_W_UPARROW;
		last_arrow = GEM_WINDOW_W_DNARROW;
		slide_object = GEM_WINDOW_W_VSLIDE;
		elevator_object = GEM_WINDOW_W_VELEV;
		first_kind = GEM_WINDOW_UPARROW;
		last_kind = GEM_WINDOW_DNARROW;
		slide_kind = GEM_WINDOW_VSLIDE;
		slide_value = slot->vslide;
		slide_size = slot->vslsiz;
		minimum_size = manager->box_height;
	} else {
		bar_object = GEM_WINDOW_W_HBAR;
		first_arrow = GEM_WINDOW_W_LFARROW;
		last_arrow = GEM_WINDOW_W_RTARROW;
		slide_object = GEM_WINDOW_W_HSLIDE;
		elevator_object = GEM_WINDOW_W_HELEV;
		first_kind = GEM_WINDOW_LFARROW;
		last_kind = GEM_WINDOW_RTARROW;
		slide_kind = GEM_WINDOW_HSLIDE;
		slide_value = slot->hslide;
		slide_size = slot->hslsiz;
		minimum_size = manager->box_width;
	}
	gem_window_active_add(objects, GEM_WINDOW_W_DATA, bar_object,
		bar->g_x, bar->g_y, bar->g_w, bar->g_h);
	if (!active)
		return;

	remaining.g_x = 0;
	remaining.g_y = 0;
	remaining.g_w = bar->g_w;
	remaining.g_h = bar->g_h;
	if (slot->kind & first_kind) {
		gem_window_active_add(objects, bar_object, first_arrow,
			remaining.g_x, remaining.g_y, manager->box_width,
			manager->box_height);
		if (vertical) {
			remaining.g_y += manager->box_height - 1;
			remaining.g_h -= manager->box_height - 1;
		} else {
			remaining.g_x += manager->box_width - 1;
			remaining.g_w -= manager->box_width - 1;
		}
	}
	if (slot->kind & last_kind) {
		if (vertical) {
			remaining.g_h -= manager->box_height - 1;
			if (remaining.g_h > 0)
				gem_window_active_add(objects, bar_object,
					last_arrow, remaining.g_x,
					remaining.g_y + remaining.g_h - 1,
					manager->box_width, manager->box_height);
		} else {
			remaining.g_w -= manager->box_width - 1;
			if (remaining.g_w > 0)
				gem_window_active_add(objects, bar_object,
					last_arrow,
					remaining.g_x + remaining.g_w - 1,
					remaining.g_y, manager->box_width,
					manager->box_height);
		}
	}
	if (!(slot->kind & slide_kind)
	    || remaining.g_w <= 0 || remaining.g_h <= 0)
		return;
	gem_window_active_add(objects, bar_object, slide_object,
		remaining.g_x, remaining.g_y, remaining.g_w, remaining.g_h);
	space = vertical ? remaining.g_h : remaining.g_w;
	if (space <= 0)
		return;
	if (slide_size < 0)
		slide_size = minimum_size;
	else
		slide_size = (WORD) gem_window_scale((UWORD) space,
			(UWORD) slide_size, 1000U);
	if (slide_size < minimum_size)
		slide_size = minimum_size;
	if (slide_size > space)
		slide_size = space;
	if (slide_value < 0)
		slide_value = 0;
	if (slide_value > 1000)
		slide_value = 1000;
	travel = space - slide_size;
	position = (WORD) gem_window_scale((UWORD) travel,
		(UWORD) slide_value, 1000U);
	if (vertical)
		gem_window_active_add(objects, slide_object, elevator_object,
			0, position, manager->box_width, slide_size);
	else
		gem_window_active_add(objects, slide_object, elevator_object,
			position, 0, slide_size, manager->box_height);
}

OBJECT *
gem_window_resident_build_active(GEM_WINDOW_RESIDENT *manager, WORD handle)
{
	GEM_WINDOW_SLOT *slot;
	OBJECT *objects;
	OBJECT *object;
	GRECT outer;
	GRECT area;
	GRECT bar;
	WORD count;
	WORD active;
	WORD title_width;
	WORD vertical;
	WORD horizontal;

	if (!manager || handle < GEM_WINDOW_OPEN_MIN
	    || handle >= GEM_WINDOW_OPEN_LIMIT)
		return (OBJECT *) 0;
	slot = gem_window_slot_at(manager, (UWORD) handle);
	if (!(slot->flags & GEM_WINDOW_VF_INTREE))
		return (OBJECT *) 0;
	objects = manager->active;
	object = objects;
	count = GEM_WINDOW_ACTIVE_COUNT;
	while (count--) {
		object->ob_next = GEM_WINDOW_NIL;
		object->ob_head = GEM_WINDOW_NIL;
		object->ob_tail = GEM_WINDOW_NIL;
		object->ob_state = GEM_WINDOW_NORMAL;
		object->ob_x = 0;
		object->ob_y = 0;
		object->ob_width = 0;
		object->ob_height = 0;
		object++;
	}
	if (slot->name.lo || slot->name.hi) {
		manager->active_name.te_ptext.lo = slot->name.lo;
		manager->active_name.te_ptext.hi = slot->name.hi;
	} else {
		manager->active_name.te_ptext.lo = (UWORD) &gem_window_empty_text;
		manager->active_name.te_ptext.hi = gem_window_data_segment();
	}
	if (slot->info.lo || slot->info.hi) {
		manager->active_info.te_ptext.lo = slot->info.lo;
		manager->active_info.te_ptext.hi = slot->info.hi;
	} else {
		manager->active_info.te_ptext.lo = (UWORD) &gem_window_empty_text;
		manager->active_info.te_ptext.hi = gem_window_data_segment();
	}
	object = gem_window_tree_at(manager, (UWORD) handle);
	gem_window_object_rect(object, &outer);
	gem_window_set_object_rect(objects + GEM_WINDOW_W_BOX, &outer);
	active = manager->top == handle;
	area.g_x = 0;
	area.g_y = 0;
	area.g_w = outer.g_w;
	area.g_h = outer.g_h;

	if (slot->kind & (GEM_WINDOW_NAME | GEM_WINDOW_CLOSER
		| GEM_WINDOW_FULLER)) {
		gem_window_active_add(objects, GEM_WINDOW_W_BOX,
			GEM_WINDOW_W_TITLE, 0, 0, area.g_w,
			manager->box_height);
		title_width = area.g_w;
		if ((slot->kind & GEM_WINDOW_CLOSER) && active) {
			gem_window_active_add(objects, GEM_WINDOW_W_TITLE,
				GEM_WINDOW_W_CLOSER, 0, 0,
				manager->box_width, manager->box_height);
			area.g_x += manager->box_width;
			title_width -= manager->box_width;
		}
		if ((slot->kind & GEM_WINDOW_FULLER) && active) {
			title_width -= manager->box_width;
			gem_window_active_add(objects, GEM_WINDOW_W_TITLE,
				GEM_WINDOW_W_FULLER, area.g_x + title_width, 0,
				manager->box_width, manager->box_height);
		}
		if (slot->kind & GEM_WINDOW_NAME) {
			gem_window_active_add(objects, GEM_WINDOW_W_TITLE,
				GEM_WINDOW_W_NAME, area.g_x, 0, title_width,
				manager->box_height);
			if (!active)
				(objects + GEM_WINDOW_W_NAME)->ob_state
					= GEM_WINDOW_DISABLED;
		}
		area.g_x = 0;
		area.g_y += manager->box_height - 1;
		area.g_h -= manager->box_height - 1;
	}
	if (slot->kind & GEM_WINDOW_INFO) {
		gem_window_active_add(objects, GEM_WINDOW_W_BOX,
			GEM_WINDOW_W_INFO, area.g_x, area.g_y, area.g_w,
			manager->box_height);
		area.g_y += manager->box_height - 1;
		area.g_h -= manager->box_height - 1;
	}
	gem_window_active_add(objects, GEM_WINDOW_W_BOX, GEM_WINDOW_W_DATA,
		area.g_x, area.g_y, area.g_w, area.g_h);
	area.g_x = 1;
	area.g_y = 1;
	area.g_w -= 2;
	area.g_h -= 2;
	vertical = (slot->kind & (GEM_WINDOW_UPARROW | GEM_WINDOW_DNARROW
		| GEM_WINDOW_VSLIDE | GEM_WINDOW_SIZER)) != 0;
	horizontal = (slot->kind & (GEM_WINDOW_LFARROW
		| GEM_WINDOW_RTARROW | GEM_WINDOW_HSLIDE
		| GEM_WINDOW_SIZER)) != 0;
	if (vertical)
		area.g_w -= manager->box_width - 1;
	if (horizontal)
		area.g_h -= manager->box_height - 1;
	gem_window_active_add(objects, GEM_WINDOW_W_DATA, GEM_WINDOW_W_WORK,
		area.g_x, area.g_y, area.g_w, area.g_h);
	if (vertical) {
		bar.g_x = area.g_x + area.g_w;
		bar.g_y = 0;
		bar.g_w = manager->box_width;
		bar.g_h = area.g_h + 2;
		gem_window_active_bar(manager, objects, slot, active, TRUE, &bar);
	}
	if (horizontal) {
		bar.g_x = 0;
		bar.g_y = area.g_y + area.g_h;
		bar.g_w = area.g_w + 2;
		bar.g_h = manager->box_height;
		gem_window_active_bar(manager, objects, slot, active, FALSE, &bar);
	}
	if (vertical && horizontal) {
		gem_window_active_add(objects, GEM_WINDOW_W_DATA,
			GEM_WINDOW_W_SIZER, area.g_x + area.g_w,
			area.g_y + area.g_h, manager->box_width,
			manager->box_height);
		object = objects + GEM_WINDOW_W_SIZER;
		if (active && (slot->kind & GEM_WINDOW_SIZER)) {
			object->ob_flags = GEM_WINDOW_FLAG3D
				| GEM_WINDOW_USECOLORCAT;
			object->ob_spec.lo = GEM_WINDOW_MONO_OPAQUE_COLOR;
			object->ob_spec.hi = GEM_WINDOW_SIZER_SPEC_HI;
		} else {
			/* Original w_bldactive() fills an inactive corner, not a hole. */
			object->ob_flags = GEM_WINDOW_USECOLORCAT;
			object->ob_spec.lo = GEM_WINDOW_SIZER_BLANK_SPEC_LO;
			object->ob_spec.hi = GEM_WINDOW_SIZER_BLANK_SPEC_HI;
		}
	}
	return objects;
}

static WORD
gem_window_active_parent(WORD index)
{
	switch (index) {
	case GEM_WINDOW_W_TITLE:
	case GEM_WINDOW_W_INFO:
	case GEM_WINDOW_W_DATA:
		return GEM_WINDOW_W_BOX;
	case GEM_WINDOW_W_CLOSER:
	case GEM_WINDOW_W_NAME:
	case GEM_WINDOW_W_FULLER:
		return GEM_WINDOW_W_TITLE;
	case GEM_WINDOW_W_WORK:
	case GEM_WINDOW_W_SIZER:
	case GEM_WINDOW_W_VBAR:
	case GEM_WINDOW_W_HBAR:
		return GEM_WINDOW_W_DATA;
	case GEM_WINDOW_W_UPARROW:
	case GEM_WINDOW_W_DNARROW:
	case GEM_WINDOW_W_VSLIDE:
		return GEM_WINDOW_W_VBAR;
	case GEM_WINDOW_W_VELEV:
		return GEM_WINDOW_W_VSLIDE;
	case GEM_WINDOW_W_LFARROW:
	case GEM_WINDOW_W_RTARROW:
	case GEM_WINDOW_W_HSLIDE:
		return GEM_WINDOW_W_HBAR;
	case GEM_WINDOW_W_HELEV:
		return GEM_WINDOW_W_HSLIDE;
	default:
		return GEM_WINDOW_NIL;
	}
}

static WORD
gem_window_active_absolute(OBJECT *objects, WORD index, GRECT *rectangle)
{
	OBJECT *object;
	WORD parent;
	WORD height;
	WORD width;
	WORD x;
	WORD y;

	if (!objects || !rectangle || index < GEM_WINDOW_W_BOX
	    || index >= (WORD) GEM_WINDOW_ACTIVE_COUNT)
		return FALSE;
	object = objects + index;
	if (index != GEM_WINDOW_W_BOX && object->ob_next == GEM_WINDOW_NIL)
		return FALSE;
	x = (WORD) object->ob_x;
	y = (WORD) object->ob_y;
	width = (WORD) object->ob_width;
	height = (WORD) object->ob_height;
	parent = gem_window_active_parent(index);
	while (parent != GEM_WINDOW_NIL) {
		object = objects + parent;
		if (!gem_window_add(x, (WORD) object->ob_x, &x)
		    || !gem_window_add(y, (WORD) object->ob_y, &y))
			return FALSE;
		parent = gem_window_active_parent(parent);
	}
	rectangle->g_x = x;
	rectangle->g_y = y;
	rectangle->g_w = width;
	rectangle->g_h = height;
	return TRUE;
}

WORD
gem_window_resident_gadget(GEM_WINDOW_RESIDENT *manager, WORD x,
	WORD y, WORD *handle)
{
	static const UBYTE hit_order[] = {
		GEM_WINDOW_W_VELEV, GEM_WINDOW_W_HELEV,
		GEM_WINDOW_W_CLOSER, GEM_WINDOW_W_FULLER,
		GEM_WINDOW_W_SIZER,
		GEM_WINDOW_W_UPARROW, GEM_WINDOW_W_DNARROW,
		GEM_WINDOW_W_LFARROW, GEM_WINDOW_W_RTARROW,
		GEM_WINDOW_W_VSLIDE, GEM_WINDOW_W_HSLIDE,
		GEM_WINDOW_W_VBAR, GEM_WINDOW_W_HBAR,
		GEM_WINDOW_W_NAME, GEM_WINDOW_W_WORK,
		GEM_WINDOW_W_INFO, GEM_WINDOW_W_DATA,
		GEM_WINDOW_W_TITLE
	};
	OBJECT *objects;
	GRECT rectangle;
	const UBYTE *candidate;
	UWORD count;
	WORD found;

	if (!handle)
		return GEM_WINDOW_NIL;
	*handle = gem_window_find(manager, x, y);
	if (*handle <= GEM_WINDOW_ROOT)
		return GEM_WINDOW_NIL;
	objects = gem_window_resident_build_active(manager, *handle);
	if (!objects)
		return GEM_WINDOW_NIL;
	candidate = hit_order;
	count = (UWORD) sizeof(hit_order);
	while (count--) {
		found = (WORD) *candidate++;
		if (gem_window_active_absolute(objects, found, &rectangle)
		    && gem_window_rect_contains(&rectangle, x, y))
			return found;
	}
	return GEM_WINDOW_W_BOX;
}

WORD
gem_window_resident_control_message(GEM_WINDOW_RESIDENT *manager,
	WORD handle, UWORD message, const GRECT *rectangle,
	GEM_WINDOW_EFFECTS *effects)
{
	GEM_WINDOW_SLOT *slot;
	OBJECT *object;
	GRECT current;
	GRECT work;

	if (!manager || !effects || handle < GEM_WINDOW_OPEN_MIN
	    || handle >= GEM_WINDOW_OPEN_LIMIT)
		return FALSE;
	slot = gem_window_slot_at(manager, (UWORD) handle);
	if (!(slot->flags & GEM_WINDOW_VF_INTREE))
		return FALSE;
	if (message != GEM_WINDOW_WM_TOPPED
	    && message != GEM_WINDOW_WM_CLOSED
	    && message != GEM_WINDOW_WM_FULLED
	    && message != GEM_WINDOW_WM_ARROWED
	    && message != GEM_WINDOW_WM_HSLID
	    && message != GEM_WINDOW_WM_VSLID
	    && message != GEM_WINDOW_WM_MOVED
	    && message != GEM_WINDOW_WM_SIZED)
		return FALSE;
	object = gem_window_tree_at(manager, (UWORD) handle);
	gem_window_object_rect(object, &current);
	if (message == GEM_WINDOW_WM_MOVED || message == GEM_WINDOW_WM_SIZED) {
		if (!rectangle || !gem_window_public_rect(manager, slot->kind,
						rectangle, &work))
			return FALSE;
		return gem_window_append_message(manager, effects, slot, message,
			handle, rectangle);
	}
	if (message == GEM_WINDOW_WM_ARROWED
	    || message == GEM_WINDOW_WM_HSLID
	    || message == GEM_WINDOW_WM_VSLID) {
		if (!rectangle || rectangle->g_x < 0)
			return FALSE;
		if (message == GEM_WINDOW_WM_ARROWED
		    && rectangle->g_x > (WORD) GEM_WINDOW_WA_RTLINE)
			return FALSE;
		if (message != GEM_WINDOW_WM_ARROWED
		    && rectangle->g_x > 1000)
			return FALSE;
		current.g_x = rectangle->g_x;
		return gem_window_append_message(manager, effects, slot, message,
			handle, &current);
	}
	return gem_window_append_message(manager, effects, slot, message,
		handle, &current);
}

/* Clamp one already-word-sized pixel value without division or conversion. */
static WORD
gem_window_control_clamp(WORD value, WORD minimum, WORD maximum)
{
	if (value < minimum)
		return minimum;
	if (value > maximum)
		return maximum;
	return value;
}

/*
 * Compute the original controller's seven-box minimum without a target MUL.
 * If damaged metrics make seven boxes exceed the available screen dimension,
 * the result saturates to that available maximum instead of overflowing.
 */
static WORD
gem_window_control_minimum(WORD box, WORD maximum)
{
	WORD result;
	UWORD count;

	if (box <= 0 || maximum <= 0)
		return 0;
	result = box;
	count = 1U;
	while (count < 7U) {
		if (result > maximum - box)
			return maximum;
		result += box;
		count++;
	}
	return result;
}

/*
 * Nonblocking ELKS form of GEMCTRL.C hctl_window().  Original GEM waited in
 * nested dispatcher loops while a button was held.  Here the same current
 * window, gadget, start rectangle, and slider track remain in resident near
 * data until a later physical sample reports release.  Thus gemaes returns to
 * the ELKS kernel after every sample and no application-side wrapper is used.
 */
WORD
gem_window_resident_input(GEM_WINDOW_RESIDENT *manager,
	const GEM_WINDOW_INPUT *input, GEM_WINDOW_EFFECTS *effects)
{
	GEM_WINDOW_SLOT *slot;
	OBJECT *object;
	OBJECT *objects;
	GRECT elevator;
	GRECT rectangle;
	GRECT parameter;
	UWORD button_down;
	UWORD message;
	UWORD state;
	WORD action;
	WORD delta_x;
	WORD delta_y;
	WORD gadget;
	WORD handle;
	WORD maximum;
	WORD minimum;
	WORD position;
	WORD release_gadget;
	WORD release_handle;
	WORD span;

	if (!effects)
		return FALSE;
	gem_window_resident_effects_init(effects);
	if (!manager || !manager->ready || !input)
		return FALSE;
	button_down = input->mouse_buttons & 1U;

	/* A press in client work or on the desktop stays generic through release. */
	if (manager->control_state == GEM_WINDOW_CONTROL_IGNORE) {
		if (button_down)
			return FALSE;
		gem_window_control_clear(manager);
		return FALSE;
	}

	if (manager->control_state == GEM_WINDOW_CONTROL_IDLE) {
		if (!button_down)
			return FALSE;
		handle = GEM_WINDOW_NIL;
		gadget = gem_window_resident_gadget(manager, input->mouse_x,
			input->mouse_y, &handle);
		if (handle <= GEM_WINDOW_ROOT || gadget == GEM_WINDOW_NIL) {
			manager->control_state = GEM_WINDOW_CONTROL_IGNORE;
			return FALSE;
		}
		slot = gem_window_slot_at(manager, (UWORD) handle);
		if (!(slot->flags & GEM_WINDOW_VF_INTREE)) {
			manager->control_state = GEM_WINDOW_CONTROL_IGNORE;
			return FALSE;
		}
		manager->control_handle = handle;
		manager->control_gadget = gadget;
		manager->control_start_x = input->mouse_x;
		manager->control_start_y = input->mouse_y;
		object = gem_window_tree_at(manager, (UWORD) handle);
		gem_window_object_rect(object, &manager->control_start);

		/* Any point in an inactive window asks its owner to top it. */
		if (manager->top != handle) {
			manager->control_state = GEM_WINDOW_CONTROL_RELEASE;
			(void) gem_window_resident_control_message(manager, handle,
				GEM_WINDOW_WM_TOPPED, (const GRECT *) 0, effects);
			return TRUE;
		}

		/* The top window's work rectangle belongs to the application. */
		if (gadget == GEM_WINDOW_W_WORK) {
			manager->control_state = GEM_WINDOW_CONTROL_IGNORE;
			return FALSE;
		}
		if (gadget == GEM_WINDOW_W_CLOSER) {
			manager->control_state = GEM_WINDOW_CONTROL_WATCH;
			if (slot->kind & GEM_WINDOW_HOTCLOSE) {
				manager->control_state = GEM_WINDOW_CONTROL_RELEASE;
				(void) gem_window_resident_control_message(manager, handle,
					GEM_WINDOW_WM_CLOSED, (const GRECT *) 0,
					effects);
			}
			return TRUE;
		}
		if (gadget == GEM_WINDOW_W_FULLER) {
			manager->control_state = GEM_WINDOW_CONTROL_WATCH;
			return TRUE;
		}
		if (gadget == GEM_WINDOW_W_NAME) {
			manager->control_state = (slot->kind & GEM_WINDOW_MOVER)
				? GEM_WINDOW_CONTROL_MOVE
				: GEM_WINDOW_CONTROL_RELEASE;
			return TRUE;
		}
		if (gadget == GEM_WINDOW_W_SIZER) {
			manager->control_state = (slot->kind & GEM_WINDOW_SIZER)
				? GEM_WINDOW_CONTROL_SIZE
				: GEM_WINDOW_CONTROL_RELEASE;
			return TRUE;
		}

		parameter.g_x = 0;
		parameter.g_y = 0;
		parameter.g_w = 0;
		parameter.g_h = 0;
		action = GEM_WINDOW_NIL;
		if (gadget == GEM_WINDOW_W_UPARROW)
			action = GEM_WINDOW_WA_UPLINE;
		else if (gadget == GEM_WINDOW_W_DNARROW)
			action = GEM_WINDOW_WA_DNLINE;
		else if (gadget == GEM_WINDOW_W_LFARROW)
			action = GEM_WINDOW_WA_LFLINE;
		else if (gadget == GEM_WINDOW_W_RTARROW)
			action = GEM_WINDOW_WA_RTLINE;
		else if (gadget == GEM_WINDOW_W_VSLIDE
			 || gadget == GEM_WINDOW_W_HSLIDE) {
			objects = gem_window_resident_build_active(manager, handle);
			if (objects
			    && gem_window_active_absolute(objects,
				gadget == GEM_WINDOW_W_VSLIDE
				? GEM_WINDOW_W_VELEV : GEM_WINDOW_W_HELEV,
				&elevator)) {
				if (gadget == GEM_WINDOW_W_VSLIDE)
					action = input->mouse_y < elevator.g_y
						? GEM_WINDOW_WA_UPPAGE
						: GEM_WINDOW_WA_DNPAGE;
				else
					action = input->mouse_x < elevator.g_x
						? GEM_WINDOW_WA_LFPAGE
						: GEM_WINDOW_WA_RTPAGE;
			}
		}
		if (action != GEM_WINDOW_NIL) {
			manager->control_state = GEM_WINDOW_CONTROL_RELEASE;
			parameter.g_x = action;
			(void) gem_window_resident_control_message(manager, handle,
				GEM_WINDOW_WM_ARROWED, &parameter, effects);
			return TRUE;
		}

		if (gadget == GEM_WINDOW_W_VELEV
		    || gadget == GEM_WINDOW_W_HELEV) {
			objects = gem_window_resident_build_active(manager, handle);
			if (objects
			    && gem_window_active_absolute(objects, gadget,
				&manager->control_start)
			    && gem_window_active_absolute(objects,
				gadget == GEM_WINDOW_W_VELEV
				? GEM_WINDOW_W_VSLIDE : GEM_WINDOW_W_HSLIDE,
				&manager->control_track)) {
				manager->control_state = gadget == GEM_WINDOW_W_VELEV
					? GEM_WINDOW_CONTROL_VSLIDE
					: GEM_WINDOW_CONTROL_HSLIDE;
				return TRUE;
			}
		}
		manager->control_state = GEM_WINDOW_CONTROL_RELEASE;
		return TRUE;
	}

	/* Every retained controller interaction owns all held-button samples. */
	if (button_down)
		return TRUE;
	state = manager->control_state;
	handle = manager->control_handle;
	gadget = manager->control_gadget;
	message = 0;
	rectangle = manager->control_start;
	parameter.g_x = 0;
	parameter.g_y = 0;
	parameter.g_w = 0;
	parameter.g_h = 0;
	if (handle <= GEM_WINDOW_ROOT || handle >= GEM_WINDOW_OPEN_LIMIT) {
		gem_window_control_clear(manager);
		return TRUE;
	}
	slot = gem_window_slot_at(manager, (UWORD) handle);
	if (!(slot->flags & GEM_WINDOW_VF_INTREE)) {
		gem_window_control_clear(manager);
		return TRUE;
	}

	if (state == GEM_WINDOW_CONTROL_WATCH) {
		release_handle = GEM_WINDOW_NIL;
		release_gadget = gem_window_resident_gadget(manager,
			input->mouse_x, input->mouse_y, &release_handle);
		if (release_handle == handle && release_gadget == gadget)
			message = gadget == GEM_WINDOW_W_CLOSER
				? GEM_WINDOW_WM_CLOSED : GEM_WINDOW_WM_FULLED;
	} else if (state == GEM_WINDOW_CONTROL_MOVE) {
		delta_x = input->mouse_x - manager->control_start_x;
		delta_y = input->mouse_y - manager->control_start_y;
		if (!gem_window_add(rectangle.g_x, delta_x, &rectangle.g_x))
			rectangle.g_x = delta_x < 0 ? 0 : manager->screen_width;
		if (!gem_window_add(rectangle.g_y, delta_y, &rectangle.g_y))
			rectangle.g_y = delta_y < 0
				? manager->box_height : manager->screen_height;
		maximum = manager->screen_width - rectangle.g_w;
		if (maximum < 0)
			maximum = 0;
		rectangle.g_x = gem_window_control_clamp(rectangle.g_x, 0,
			maximum);
		maximum = manager->screen_height - rectangle.g_h;
		if (maximum < manager->box_height)
			maximum = manager->box_height;
		rectangle.g_y = gem_window_control_clamp(rectangle.g_y,
			manager->box_height, maximum);
		message = GEM_WINDOW_WM_MOVED;
	} else if (state == GEM_WINDOW_CONTROL_SIZE) {
		delta_x = input->mouse_x - manager->control_start_x;
		delta_y = input->mouse_y - manager->control_start_y;
		if (!gem_window_add(rectangle.g_w, delta_x, &rectangle.g_w))
			rectangle.g_w = delta_x < 0 ? 1 : manager->screen_width;
		if (!gem_window_add(rectangle.g_h, delta_y, &rectangle.g_h))
			rectangle.g_h = delta_y < 0 ? 1 : manager->screen_height;
		maximum = manager->screen_width - rectangle.g_x;
		minimum = gem_window_control_minimum(manager->box_width, maximum);
		rectangle.g_w = gem_window_control_clamp(rectangle.g_w, minimum,
			maximum);
		maximum = manager->screen_height - rectangle.g_y;
		minimum = gem_window_control_minimum(manager->box_height, maximum);
		rectangle.g_h = gem_window_control_clamp(rectangle.g_h, minimum,
			maximum);
		message = GEM_WINDOW_WM_SIZED;
	} else if (state == GEM_WINDOW_CONTROL_HSLIDE
		   || state == GEM_WINDOW_CONTROL_VSLIDE) {
		if (state == GEM_WINDOW_CONTROL_VSLIDE) {
			span = manager->control_track.g_h - rectangle.g_h;
			position = rectangle.g_y - manager->control_track.g_y
				+ input->mouse_y - manager->control_start_y;
			message = GEM_WINDOW_WM_VSLID;
		} else {
			span = manager->control_track.g_w - rectangle.g_w;
			position = rectangle.g_x - manager->control_track.g_x
				+ input->mouse_x - manager->control_start_x;
			message = GEM_WINDOW_WM_HSLID;
		}
		if (span <= 0)
			parameter.g_x = 0;
		else {
			position = gem_window_control_clamp(position, 0, span);
			parameter.g_x = (WORD) gem_window_scale((UWORD) position,
				1000U, (UWORD) span);
		}
	}

	/* Clear before enqueueing so a client response can start a new gesture. */
	gem_window_control_clear(manager);
	if (!message)
		return TRUE;
	if (message == GEM_WINDOW_WM_HSLID
	    || message == GEM_WINDOW_WM_VSLID)
		(void) gem_window_resident_control_message(manager, handle, message,
			&parameter, effects);
	else
		(void) gem_window_resident_control_message(manager, handle, message,
			&rectangle, effects);
	return TRUE;
}
