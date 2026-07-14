/*
 * gem_menu_pull_resident.c - resident original GEM menu state machine.
 *
 * This file is a bounded GNU-C transliteration of the GPL-released Digital
 * Research sources identified in docs/ORIGINAL_SOURCE_PROVENANCE.md:
 *
 *   GEMMNLIB.C do_chg(), menu_set(), menu_sub(), menu_down(), menu_keys(),
 *                menu_item_find(), mn_do(), mn_register()/mn_unregister();
 *   GEMCTRL.C  hctl_rect() MN_SELECTED and AC_OPEN routing; and
 *   GEMSUPER.C MENU_ICHECK through MENU_CLICK dispatch.
 *
 * GEM's EVB/UDA dispatcher and blocking loop are intentionally absent.  ELKS
 * schedules processes, while gem_menu_pull_resident_input() advances one
 * INBAR/OUTTITLE/INBARECT/OUTITEM transition per physical sample.  Screen
 * save, object draw, restore and message writes are emitted as effects for
 * the resident AES/VDI owner.  The original RSC OBJECT records are changed in
 * place; no widget conversion, heap tree or wrapper process is introduced.
 *
 * Every target scalar and arithmetic operation is one 8086 byte or word.
 * Object and accessory walks use pointer increments.  There is no multiply,
 * divide, floating point, recursion, or variable-sized stack object.
 */

#include "gem_menu_pull_resident.h"

#define GEM_MENU_PULL_NIL              (-1)
#define GEM_MENU_PULL_ROOT             0
#define GEM_MENU_PULL_G_BOX            20U
#define GEM_MENU_PULL_G_STRING         28U
#define GEM_MENU_PULL_MAX_DEPTH        8U

#define GEM_MENU_PULL_INBAR            0U
#define GEM_MENU_PULL_OUTTITLE         1U
#define GEM_MENU_PULL_INBARECT         2U
#define GEM_MENU_PULL_OUTITEM          3U

#define GEM_MENU_PULL_KIND_NONE        0U
#define GEM_MENU_PULL_KIND_MAIN        1U
#define GEM_MENU_PULL_KIND_DESK        2U

#define GEM_MENU_PULL_LEFT_BUTTON      0x0001U
#define GEM_MENU_PULL_HIGH_ITEM        0x8000U
#define GEM_MENU_PULL_ITEM_MASK        0x7fffU
#define GEM_MENU_PULL_NO_OWNER         0xffffU

typedef struct __attribute__((packed)) gem_menu_pull_pair {
	UWORD lo;
	UWORD hi;
} GEM_MENU_PULL_PAIR;

typedef struct gem_menu_pull_accessory {
	UWORD owner;
	UWORD generation_lo;
	UWORD generation_hi;
	UBYTE name[GEM_MENU_PULL_NAME_BYTES];
} GEM_MENU_PULL_ACCESSORY;

typedef struct gem_menu_pull_state {
	GEM_MENU_PULL_TREE tree;
	UWORD owner;
	UWORD generation_lo;
	UWORD generation_hi;
	WORD bar;
	WORD active_titles;
	WORD menus;
	WORD first_title;
	WORD last_title;
	WORD first_menu;
	WORD last_menu;
	WORD desk_title;
	WORD desk_menu;
	WORD current_title;
	WORD current_menu;
	WORD current_item;
	WORD armed_item;
	UWORD title_base_state;
	UWORD item_base_state;
	UWORD previous_buttons;
	UWORD menu_state;
	UWORD current_kind;
	UWORD click_mode;
	UBYTE active;
	UBYTE tracking;
	UBYTE drag_select;
	UBYTE latched;
	UBYTE outside_press;
	GEM_MENU_PULL_OBJECT desk_objects[GEM_MENU_PULL_DESK_OBJECTS];
} GEM_MENU_PULL_STATE;

static GEM_MENU_PULL_STATE gem_menu_pull;
static GEM_MENU_PULL_ACCESSORY
	gem_menu_accessories[GEM_MENU_PULL_ACCESSORIES];
static UWORD gem_menu_accessory_count;

#if defined(ELKS) && ELKS
/*
 * Far pointers are formed only from an already-validated offset and segment.
 * Copying the union moves exactly two words; no wide integer or normalization
 * helper is required on the 8086.
 */
typedef union gem_menu_pull_far_pointer {
	VOID GEM_MENU_PULL_FAR *pointer;
	UBYTE GEM_MENU_PULL_FAR *bytes;
	GEM_MENU_PULL_OBJECT GEM_MENU_PULL_FAR *object;
	GEM_MENU_PULL_PAIR GEM_MENU_PULL_FAR *pair;
	struct __attribute__((packed)) {
		UWORD lo;
		UWORD hi;
	} address;
} GEM_MENU_PULL_FAR_POINTER;

typedef char GEM_MENU_PULL_FAR_POINTER_MUST_BE_FOUR_BYTES
	[(sizeof(GEM_MENU_PULL_FAR_POINTER) == 4) ? 1 : -1];
#endif

/*
 * Build fixed record byte offsets with 8086-cheap word additions.
 *
 * Keep each intermediate assignment visible in the source.  Array subscripts
 * and pointer increments let a size-oriented compiler replace the operation
 * with MUL, even though these structures have compile-time sizes.  These
 * helpers instead express 24, 28 and 14 as sums of powers of two.  All callers
 * validate their indexes first, so the unsigned word sums cannot overflow.
 */
static UWORD
gem_menu_scale_24(UWORD value)
{
	UWORD eight;
	UWORD result;

	eight = value;
	__asm__ volatile ("addw %0,%0" : "+r" (eight) : : "cc");
	__asm__ volatile ("addw %0,%0" : "+r" (eight) : : "cc");
	__asm__ volatile ("addw %0,%0" : "+r" (eight) : : "cc");
	result = eight;
	__asm__ volatile ("addw %0,%0" : "+r" (result) : : "cc");
	__asm__ volatile ("addw %1,%0"
		: "+r" (result) : "r" (eight) : "cc");
	return result;
}

static UWORD
gem_menu_scale_28(UWORD value)
{
	UWORD four;
	UWORD eight;
	UWORD result;

	four = value;
	__asm__ volatile ("addw %0,%0" : "+r" (four) : : "cc");
	__asm__ volatile ("addw %0,%0" : "+r" (four) : : "cc");
	eight = four;
	__asm__ volatile ("addw %0,%0" : "+r" (eight) : : "cc");
	result = eight;
	__asm__ volatile ("addw %0,%0" : "+r" (result) : : "cc");
	__asm__ volatile ("addw %1,%0"
		: "+r" (result) : "r" (eight) : "cc");
	__asm__ volatile ("addw %1,%0"
		: "+r" (result) : "r" (four) : "cc");
	return result;
}

static UWORD
gem_menu_scale_14(UWORD value)
{
	UWORD two;
	UWORD four;
	UWORD eight;
	UWORD result;

	two = value;
	__asm__ volatile ("addw %0,%0" : "+r" (two) : : "cc");
	four = two;
	__asm__ volatile ("addw %0,%0" : "+r" (four) : : "cc");
	eight = four;
	__asm__ volatile ("addw %0,%0" : "+r" (eight) : : "cc");
	result = eight;
	__asm__ volatile ("addw %1,%0"
		: "+r" (result) : "r" (four) : "cc");
	__asm__ volatile ("addw %1,%0"
		: "+r" (result) : "r" (two) : "cc");
	return result;
}

/* Locate one fixed record with additions, never a run-time scale multiply. */
static GEM_MENU_PULL_ACCESSORY *
gem_menu_accessory_at(UWORD index)
{
	UBYTE *bytes;

	bytes = (UBYTE *) gem_menu_accessories;
	return (GEM_MENU_PULL_ACCESSORY *)
		(bytes + gem_menu_scale_28(index));
}

static GEM_MENU_PULL_OBJECT *
gem_menu_desk_at(UWORD index)
{
	UBYTE *bytes;

	bytes = (UBYTE *) gem_menu_pull.desk_objects;
	return (GEM_MENU_PULL_OBJECT *)
		(bytes + gem_menu_scale_24(index));
}

/* A half-open unscaled byte span is valid only when subtraction cannot wrap. */
static WORD
gem_menu_range(UWORD offset, UWORD count, UWORD limit)
{
	if (offset > limit)
		return FALSE;
	return count <= (UWORD) (limit - offset);
}

static UBYTE GEM_MENU_PULL_FAR *
gem_menu_resource_at(const GEM_MENU_PULL_TREE *tree, UWORD offset)
{
#if defined(ELKS) && ELKS
	GEM_MENU_PULL_FAR_POINTER pointer;

	pointer.address.lo = offset;
	pointer.address.hi = tree->segment;
	return pointer.bytes;
#else
	return tree->resource + offset;
#endif
}

static GEM_MENU_PULL_OBJECT GEM_MENU_PULL_FAR *
gem_menu_object_at(const GEM_MENU_PULL_TREE *tree, UWORD index)
{
	GEM_MENU_PULL_OBJECT GEM_MENU_PULL_FAR *object;
	UWORD offset;

	offset = (UWORD) (tree->tree_offset + gem_menu_scale_24(index));

#if defined(ELKS) && ELKS
	GEM_MENU_PULL_FAR_POINTER pointer;

	pointer.address.lo = offset;
	pointer.address.hi = tree->segment;
	object = pointer.object;
#else
	object = (GEM_MENU_PULL_OBJECT *)
		(tree->resource + offset);
#endif
	return object;
}

static WORD
gem_menu_link_valid(WORD link, UWORD count)
{
	return link == GEM_MENU_PULL_NIL
		|| (link >= GEM_MENU_PULL_ROOT && (UWORD) link < count);
}

/*
 * Validate the exact local LASTOB extent with additions only.  The resource
 * loader has already checked the complete RSC, but the menu manager retains
 * this cheap boundary proof because it stores the view beyond one trap call.
 */
static WORD
gem_menu_tree_range(const GEM_MENU_PULL_TREE *tree)
{
	UWORD offset;
	UWORD remaining;

	if (!tree || !tree->resource || !tree->object_count
	    || tree->object_count > 0x7fffU)
		return FALSE;
	offset = tree->tree_offset;
	remaining = tree->object_count;
	while (remaining--) {
		if (!gem_menu_range(offset, 24U, tree->bytes))
			return FALSE;
		offset = (UWORD) (offset + 24U);
	}
	return TRUE;
}

#if defined(ELKS) && ELKS
WORD
gem_menu_pull_resident_tree_from_resource(GEM_MENU_PULL_TREE *view,
	const GEM_RESOURCE_RESIDENT *resource, GEM_FAR_ADDRESS address,
	UWORD object_count)
{
	GEM_MENU_PULL_FAR_POINTER pointer;

	if (!view || !resource
	    || !(resource->flags & GEM_RESOURCE_RESIDENT_LOADED)
	    || !resource->storage.bytes
	    || resource->storage.base.lo != 0
	    || address.hi != resource->storage.base.hi) {
		if (view) {
			view->resource = (GEM_MENU_PULL_BYTE_POINTER) 0;
			view->bytes = 0;
			view->segment = 0;
			view->tree_offset = 0;
			view->object_count = 0;
		}
		return FALSE;
	}
	pointer.address.lo = resource->storage.base.lo;
	pointer.address.hi = resource->storage.base.hi;
	view->resource = pointer.bytes;
	view->bytes = resource->storage.bytes;
	view->segment = resource->storage.base.hi;
	view->tree_offset = address.lo;
	view->object_count = object_count;
	if (gem_menu_tree_range(view))
		return TRUE;
	view->resource = (GEM_MENU_PULL_BYTE_POINTER) 0;
	view->bytes = 0;
	view->segment = 0;
	view->tree_offset = 0;
	view->object_count = 0;
	return FALSE;
}
#endif

/* Bound one sibling chain by object_count so malformed RSC cycles terminate. */
static WORD
gem_menu_chain_last(const GEM_MENU_PULL_TREE *tree, WORD parent,
	WORD first, WORD declared_last, WORD *actual_last)
{
	GEM_MENU_PULL_OBJECT GEM_MENU_PULL_FAR *object;
	WORD current;
	WORD next;
	UWORD remaining;

	if (!gem_menu_link_valid(parent, tree->object_count)
	    || !gem_menu_link_valid(first, tree->object_count)
	    || !gem_menu_link_valid(declared_last, tree->object_count)
	    || first == GEM_MENU_PULL_NIL || declared_last == GEM_MENU_PULL_NIL)
		return FALSE;
	current = first;
	remaining = tree->object_count;
	while (remaining--) {
		if (current < 0 || (UWORD) current >= tree->object_count)
			return FALSE;
		if (current == declared_last) {
			*actual_last = current;
			return TRUE;
		}
		object = gem_menu_object_at(tree, (UWORD) current);
		next = object->ob_next;
		if (next == parent || next == GEM_MENU_PULL_NIL)
			return FALSE;
		current = next;
	}
	return FALSE;
}

/* Qualify the original ROOT/BAR/ACTIVE/MENUS menu hierarchy. */
static WORD
gem_menu_validate_tree(const GEM_MENU_PULL_TREE *tree, WORD *bar,
	WORD *active_titles, WORD *menus, WORD *first_title, WORD *last_title,
	WORD *first_menu, WORD *last_menu)
{
	GEM_MENU_PULL_OBJECT GEM_MENU_PULL_FAR *object;
	UWORD index;
	WORD ignored;

	if (!gem_menu_tree_range(tree))
		return FALSE;
	index = 0;
	while (index < tree->object_count) {
		object = gem_menu_object_at(tree, index);
		if (!gem_menu_link_valid(object->ob_next, tree->object_count)
		    || !gem_menu_link_valid(object->ob_head, tree->object_count)
		    || !gem_menu_link_valid(object->ob_tail, tree->object_count))
			return FALSE;
		index++;
	}
	object = gem_menu_object_at(tree,
		(UWORD) (tree->object_count - 1U));
	if (!(object->ob_flags & GEM_MENU_PULL_LASTOB))
		return FALSE;

	object = gem_menu_object_at(tree, GEM_MENU_PULL_ROOT);
	*bar = object->ob_head;
	*menus = object->ob_tail;
	if (*bar <= GEM_MENU_PULL_ROOT || *menus <= GEM_MENU_PULL_ROOT
	    || (UWORD) *bar >= tree->object_count
	    || (UWORD) *menus >= tree->object_count)
		return FALSE;
	object = gem_menu_object_at(tree, (UWORD) *bar);
	*active_titles = object->ob_head;
	if (*active_titles < 0
	    || (UWORD) *active_titles >= tree->object_count)
		return FALSE;
	object = gem_menu_object_at(tree, (UWORD) *active_titles);
	*first_title = object->ob_head;
	*last_title = object->ob_tail;
	if (!gem_menu_chain_last(tree, *active_titles, *first_title,
		*last_title, &ignored))
		return FALSE;
	object = gem_menu_object_at(tree, (UWORD) *menus);
	*first_menu = object->ob_head;
	*last_menu = object->ob_tail;
	if (!gem_menu_chain_last(tree, *menus, *first_menu,
		*last_menu, &ignored))
		return FALSE;
	return TRUE;
}

static WORD
gem_menu_same_tree(const GEM_MENU_PULL_TREE *left,
	const GEM_MENU_PULL_TREE *right)
{
	return left && right && left->segment == right->segment
		&& left->tree_offset == right->tree_offset
		&& left->bytes == right->bytes
		&& left->object_count == right->object_count;
}

/* Original 8086 coordinate addition wraps in one unsigned word. */
static WORD
gem_menu_coordinate_add(WORD left, WORD right)
{
	return (WORD) ((UWORD) left + (UWORD) right);
}

/* Find a child's parent using the original threaded sibling links. */
static WORD
gem_menu_parent(const GEM_MENU_PULL_TREE *tree, WORD child)
{
	GEM_MENU_PULL_OBJECT GEM_MENU_PULL_FAR *parent_object;
	GEM_MENU_PULL_OBJECT GEM_MENU_PULL_FAR *object;
	WORD parent;
	WORD current;
	UWORD outer;
	UWORD inner;

	if (child == GEM_MENU_PULL_ROOT)
		return GEM_MENU_PULL_NIL;
	parent = GEM_MENU_PULL_ROOT;
	outer = tree->object_count;
	while (outer--) {
		parent_object = gem_menu_object_at(tree, (UWORD) parent);
		current = parent_object->ob_head;
		inner = tree->object_count;
		while (current != GEM_MENU_PULL_NIL && inner--) {
			if (current == child)
				return parent;
			if (current == parent_object->ob_tail)
				break;
			object = gem_menu_object_at(tree, (UWORD) current);
			current = object->ob_next;
			if (current == parent)
				break;
		}
		parent++;
		if ((UWORD) parent >= tree->object_count)
			break;
	}
	return GEM_MENU_PULL_NIL;
}

/* Direct iterative form of GEMOBLIB.C ob_offset(). */
static WORD
gem_menu_object_rectangle(const GEM_MENU_PULL_TREE *tree, WORD object_index,
	GEM_MENU_PULL_RECTANGLE *rectangle)
{
	GEM_MENU_PULL_OBJECT GEM_MENU_PULL_FAR *object;
	WORD current;
	WORD parent;
	UWORD depth;

	if (!tree || !rectangle || object_index < GEM_MENU_PULL_ROOT
	    || (UWORD) object_index >= tree->object_count)
		return FALSE;
	object = gem_menu_object_at(tree, (UWORD) object_index);
	rectangle->x = (WORD) object->ob_x;
	rectangle->y = (WORD) object->ob_y;
	rectangle->width = (WORD) object->ob_width;
	rectangle->height = (WORD) object->ob_height;
	current = object_index;
	depth = GEM_MENU_PULL_MAX_DEPTH;
	while (current != GEM_MENU_PULL_ROOT && depth--) {
		parent = gem_menu_parent(tree, current);
		if (parent == GEM_MENU_PULL_NIL)
			return FALSE;
		object = gem_menu_object_at(tree, (UWORD) parent);
		rectangle->x = gem_menu_coordinate_add(rectangle->x,
			(WORD) object->ob_x);
		rectangle->y = gem_menu_coordinate_add(rectangle->y,
			(WORD) object->ob_y);
		current = parent;
	}
	return current == GEM_MENU_PULL_ROOT;
}

static WORD
gem_menu_point_inside(WORD x, WORD y,
	const GEM_MENU_PULL_RECTANGLE *rectangle)
{
	WORD right;
	WORD bottom;

	right = gem_menu_coordinate_add(rectangle->x, rectangle->width);
	bottom = gem_menu_coordinate_add(rectangle->y, rectangle->height);
	return x >= rectangle->x && y >= rectangle->y
		&& x < right && y < bottom;
}

static VOID
gem_menu_clear_effects(GEM_MENU_PULL_EFFECTS *effects)
{
	UWORD *word;
	UWORD count;

	if (!effects)
		return;
	effects->target_owner = GEM_MENU_PULL_NO_OWNER;
	effects->target_generation_lo = 0;
	effects->target_generation_hi = 0;
	word = effects->message;
	count = 8U;
	while (count--)
		*word++ = 0;
	effects->draw_count = 0;
	effects->redraw_all = FALSE;
	effects->message_ready = FALSE;
	effects->consume_mouse = FALSE;
	effects->consume_key = FALSE;
}

static GEM_MENU_PULL_DRAW_EFFECT *
gem_menu_draw_effect_at(GEM_MENU_PULL_EFFECTS *effects, UWORD index)
{
	UBYTE *bytes;

	bytes = (UBYTE *) effects->draw;
	return (GEM_MENU_PULL_DRAW_EFFECT *)
		(bytes + gem_menu_scale_14(index));
}

static WORD
gem_menu_append_effect(GEM_MENU_PULL_EFFECTS *effects, UBYTE action,
	UBYTE tree_kind, WORD object, UWORD state,
	const GEM_MENU_PULL_RECTANGLE *rectangle)
{
	GEM_MENU_PULL_DRAW_EFFECT *draw;

	if (!effects)
		return TRUE;
	if (effects->draw_count >= GEM_MENU_PULL_DRAW_EFFECTS) {
		/*
		 * This cannot occur in the bounded transition table.  If a future
		 * extension adds another operation, request one complete bar redraw
		 * instead of exposing a partially ordered effect sequence.
		 */
		effects->draw_count = 0;
		effects->redraw_all = TRUE;
		return FALSE;
	}
	draw = gem_menu_draw_effect_at(effects, effects->draw_count);
	draw->action = action;
	draw->tree_kind = tree_kind;
	draw->object = object;
	draw->state = state;
	draw->rectangle.x = rectangle->x;
	draw->rectangle.y = rectangle->y;
	draw->rectangle.width = rectangle->width;
	draw->rectangle.height = rectangle->height;
	effects->draw_count++;
	return TRUE;
}

static WORD
gem_menu_main_object_effect(GEM_MENU_PULL_EFFECTS *effects, WORD object,
	UWORD state)
{
	GEM_MENU_PULL_RECTANGLE rectangle;

	if (!gem_menu_object_rectangle(&gem_menu_pull.tree, object, &rectangle))
		return FALSE;
	return gem_menu_append_effect(effects, GEM_MENU_PULL_DRAW_OBJECT,
		GEM_MENU_PULL_TREE_MAIN, object, state, &rectangle);
}

static WORD
gem_menu_desk_rectangle(WORD object_index,
	GEM_MENU_PULL_RECTANGLE *rectangle)
{
	GEM_MENU_PULL_OBJECT *root;
	GEM_MENU_PULL_OBJECT *object;

	if (!rectangle || object_index < GEM_MENU_PULL_ROOT
	    || (UWORD) object_index >= GEM_MENU_PULL_DESK_OBJECTS)
		return FALSE;
	root = gem_menu_desk_at(GEM_MENU_PULL_ROOT);
	object = gem_menu_desk_at((UWORD) object_index);
	rectangle->x = (WORD) object->ob_x;
	rectangle->y = (WORD) object->ob_y;
	if (object_index != GEM_MENU_PULL_ROOT) {
		rectangle->x = gem_menu_coordinate_add(rectangle->x,
			(WORD) root->ob_x);
		rectangle->y = gem_menu_coordinate_add(rectangle->y,
			(WORD) root->ob_y);
	}
	rectangle->width = (WORD) object->ob_width;
	rectangle->height = (WORD) object->ob_height;
	return TRUE;
}

static WORD
gem_menu_desk_object_effect(GEM_MENU_PULL_EFFECTS *effects, WORD object,
	UWORD state)
{
	GEM_MENU_PULL_RECTANGLE rectangle;

	if (!gem_menu_desk_rectangle(object, &rectangle))
		return FALSE;
	return gem_menu_append_effect(effects, GEM_MENU_PULL_DRAW_OBJECT,
		GEM_MENU_PULL_TREE_DESK, object, state, &rectangle);
}

static WORD
gem_menu_current_rectangle(GEM_MENU_PULL_RECTANGLE *rectangle)
{
	if (gem_menu_pull.current_kind == GEM_MENU_PULL_KIND_DESK)
		return gem_menu_desk_rectangle(GEM_MENU_PULL_ROOT, rectangle);
	if (gem_menu_pull.current_kind != GEM_MENU_PULL_KIND_MAIN)
		return FALSE;
	return gem_menu_object_rectangle(&gem_menu_pull.tree,
		gem_menu_pull.current_menu, rectangle);
}

static WORD
gem_menu_current_area_effect(GEM_MENU_PULL_EFFECTS *effects, UBYTE action)
{
	GEM_MENU_PULL_RECTANGLE rectangle;
	UBYTE kind;
	WORD object;

	if (!gem_menu_current_rectangle(&rectangle))
		return FALSE;
	kind = gem_menu_pull.current_kind == GEM_MENU_PULL_KIND_DESK
		? GEM_MENU_PULL_TREE_DESK : GEM_MENU_PULL_TREE_MAIN;
	object = gem_menu_pull.current_kind == GEM_MENU_PULL_KIND_DESK
		? GEM_MENU_PULL_ROOT : gem_menu_pull.current_menu;
	/*
	 * Direct GEMMNLIB.C menu_sr() expands the saved rectangle by one menu
	 * thickness on the left, five across its width, and five below.  MTH is
	 * one scale-one pixel in this resident driver.  SAVE_AREA owns no bitmap
	 * here, but RESTORE_AREA must retain that original extent: the outward
	 * G_BOX border otherwise overwrites the two right-hand pixels of a window
	 * closer which begins immediately beneath the Desk pull-down.
	 *
	 * Menu coordinates have already passed the bounded OBJECT-tree walk and
	 * fit the physical screen.  Cast through UWORD to preserve original 8086
	 * one-word wrap semantics; the window damage seam then clips to screen.
	 */
	if (action == GEM_MENU_PULL_RESTORE_AREA) {
		rectangle.x = (WORD) ((UWORD) rectangle.x - 1U);
		rectangle.width = (WORD) ((UWORD) rectangle.width + 5U);
		rectangle.height = (WORD) ((UWORD) rectangle.height + 5U);
	}
	return gem_menu_append_effect(effects, action, kind, object, 0,
		&rectangle);
}

static VOID
gem_menu_clear_object(GEM_MENU_PULL_OBJECT *object)
{
	object->ob_next = GEM_MENU_PULL_NIL;
	object->ob_head = GEM_MENU_PULL_NIL;
	object->ob_tail = GEM_MENU_PULL_NIL;
	object->ob_type = 0;
	object->ob_flags = 0;
	object->ob_state = 0;
	object->ob_spec.lo = 0;
	object->ob_spec.hi = 0;
	object->ob_x = 0;
	object->ob_y = 0;
	object->ob_width = 0;
	object->ob_height = 0;
}

/* Build Digital Research's one static M_DESK tree with fixed near storage. */
static WORD
gem_menu_build_desk(VOID)
{
	GEM_MENU_PULL_OBJECT GEM_MENU_PULL_FAR *menu;
	GEM_MENU_PULL_OBJECT GEM_MENU_PULL_FAR *source;
	GEM_MENU_PULL_OBJECT *object;
	GEM_MENU_PULL_RECTANGLE menu_rectangle;
	WORD information;
	WORD separator;
	WORD next;
	UWORD index;
	UWORD rows;
	UWORD height;
	UWORD row_height;
	UWORD y;

	index = 0;
	while (index < GEM_MENU_PULL_DESK_OBJECTS)
		gem_menu_clear_object(gem_menu_desk_at(index++));
	if (!gem_menu_pull.active || gem_menu_pull.desk_menu < 0
	    || !gem_menu_object_rectangle(&gem_menu_pull.tree,
		gem_menu_pull.desk_menu, &menu_rectangle))
		return FALSE;
	menu = gem_menu_object_at(&gem_menu_pull.tree,
		(UWORD) gem_menu_pull.desk_menu);
	information = menu->ob_head;
	if (information < 0
	    || (UWORD) information >= gem_menu_pull.tree.object_count)
		return FALSE;
	source = gem_menu_object_at(&gem_menu_pull.tree, (UWORD) information);
	separator = source->ob_next;
	if (separator < 0
	    || (UWORD) separator >= gem_menu_pull.tree.object_count)
		return FALSE;

	object = gem_menu_desk_at(GEM_MENU_PULL_ROOT);
	object->ob_head = 1;
	object->ob_tail = (WORD) (2U + gem_menu_accessory_count);
	object->ob_type = GEM_MENU_PULL_G_BOX;
	object->ob_spec.lo = 0x1100U;
	object->ob_spec.hi = 0x00ffU;
	object->ob_x = (UWORD) menu_rectangle.x;
	object->ob_y = (UWORD) menu_rectangle.y;
	object->ob_width = (UWORD) menu_rectangle.width;

	row_height = source->ob_height;
	if (!row_height)
		return FALSE;
	rows = (UWORD) (2U + gem_menu_accessory_count);
	height = 0;
	index = rows;
	while (index--) {
		if ((UWORD) (0xffffU - height) < row_height)
			return FALSE;
		height = (UWORD) (height + row_height);
	}
	object->ob_height = height;

	y = source->ob_y;
	index = 1U;
	while (index <= rows) {
		object = gem_menu_desk_at(index);
		next = index == rows ? GEM_MENU_PULL_ROOT : (WORD) (index + 1U);
		object->ob_next = next;
		object->ob_type = GEM_MENU_PULL_G_STRING;
		object->ob_x = source->ob_x;
		object->ob_y = y;
		object->ob_width = source->ob_width;
		object->ob_height = row_height;
		if (index == 1U) {
			object->ob_state = source->ob_state;
			object->ob_spec.lo = source->ob_spec.lo;
			object->ob_spec.hi = source->ob_spec.hi;
		} else if (index == 2U) {
			source = gem_menu_object_at(&gem_menu_pull.tree,
				(UWORD) separator);
			object->ob_state = source->ob_state
				| GEM_MENU_PULL_DISABLED;
			object->ob_spec.lo = source->ob_spec.lo;
			object->ob_spec.hi = source->ob_spec.hi;
		}
		if (index == rows)
			object->ob_flags |= GEM_MENU_PULL_LASTOB;
		y = (UWORD) (y + row_height);
		index++;
	}
	return TRUE;
}

/* Resolve direct or INDIRECT ob_spec without flattening the far address. */
static WORD
gem_menu_object_spec(const GEM_MENU_PULL_TREE *tree,
	const GEM_MENU_PULL_OBJECT GEM_MENU_PULL_FAR *object,
	GEM_MENU_PULL_PAIR *spec)
{
	GEM_MENU_PULL_PAIR GEM_MENU_PULL_FAR *indirect;
	UBYTE GEM_MENU_PULL_FAR *bytes;

	spec->lo = object->ob_spec.lo;
	spec->hi = object->ob_spec.hi;
	if (!(object->ob_flags & GEM_MENU_PULL_INDIRECT))
		return TRUE;
	if (spec->hi != tree->segment
	    || !gem_menu_range(spec->lo, 4U, tree->bytes))
		return FALSE;
	bytes = gem_menu_resource_at(tree, spec->lo);
	indirect = (GEM_MENU_PULL_PAIR GEM_MENU_PULL_FAR *) bytes;
	spec->lo = indirect->lo;
	spec->hi = indirect->hi;
	return TRUE;
}

static UBYTE GEM_MENU_PULL_FAR *
gem_menu_object_string(const GEM_MENU_PULL_TREE *tree, WORD object_index,
	UWORD *available)
{
	GEM_MENU_PULL_OBJECT GEM_MENU_PULL_FAR *object;
	GEM_MENU_PULL_PAIR spec;

	if (!tree || object_index < 0
	    || (UWORD) object_index >= tree->object_count)
		return (UBYTE GEM_MENU_PULL_FAR *) 0;
	object = gem_menu_object_at(tree, (UWORD) object_index);
	if (!gem_menu_object_spec(tree, object, &spec)
	    || spec.hi != tree->segment || spec.lo >= tree->bytes)
		return (UBYTE GEM_MENU_PULL_FAR *) 0;
	*available = (UWORD) (tree->bytes - spec.lo);
	return gem_menu_resource_at(tree, spec.lo);
}

static UBYTE
gem_menu_upper(UBYTE value)
{
	if (value >= (UBYTE) 'a' && value <= (UBYTE) 'z')
		return (UBYTE) (value - ((UBYTE) 'a' - (UBYTE) 'A'));
	return value;
}

/* Return the letter after the first underscore, as original menu_keys(). */
static UBYTE
gem_menu_shortcut_letter(const GEM_MENU_PULL_TREE *tree, WORD object_index)
{
	UBYTE GEM_MENU_PULL_FAR *string;
	UWORD available;

	string = gem_menu_object_string(tree, object_index, &available);
	if (!string)
		return 0;
	while (available--) {
		if (!*string)
			break;
		if (*string == (UBYTE) '_' && available)
			return gem_menu_upper(*(string + 1));
		string++;
	}
	return 0;
}

/* Original GEMMNLIB.C ALT_A through ALT_Z BIOS scan words. */
static const UWORD gem_menu_alt_keys[26] = {
	0x1e00U, 0x3000U, 0x2e00U, 0x2000U, 0x1200U, 0x2100U,
	0x2200U, 0x2300U, 0x1700U, 0x2400U, 0x2500U, 0x2600U,
	0x3200U, 0x3100U, 0x1800U, 0x1900U, 0x1000U, 0x1300U,
	0x1f00U, 0x1400U, 0x1600U, 0x2f00U, 0x1100U, 0x2d00U,
	0x1500U, 0x2c00U
};

static UBYTE
gem_menu_key_letter(UWORD key)
{
	const UWORD *scan;
	UBYTE letter;
	UWORD count;

	letter = gem_menu_upper((UBYTE) (key & 0x00ffU));
	if (letter >= (UBYTE) 'A' && letter <= (UBYTE) 'Z')
		return letter;
	scan = gem_menu_alt_keys;
	count = 0;
	while (count < 26U) {
		if (*scan++ == key)
			return (UBYTE) ((UBYTE) 'A' + (UBYTE) count);
		count++;
	}
	return 0;
}

/* Copy a bounded far RSC string into one fixed near Desk snapshot buffer. */
static VOID
gem_menu_copy_display_string(UBYTE *destination,
	UBYTE GEM_MENU_PULL_FAR *source, UWORD available)
{
	UWORD remaining;

	remaining = GEM_MENU_PULL_NAME_BYTES - 1U;
	while (remaining && available && *source) {
		*destination++ = *source++;
		remaining--;
		available--;
	}
	*destination++ = 0;
	while (remaining--)
		*destination++ = 0;
}

/* Copy one fixed name with no libc or variable-size transfer helper. */
static VOID
gem_menu_copy_accessory_name(UBYTE *destination, const UBYTE *source)
{
	UWORD count;

	count = GEM_MENU_PULL_NAME_BYTES;
	while (count--)
		*destination++ = *source++;
}

/* Correlate a title ordinal to the parallel menu-box sibling chain. */
static WORD
gem_menu_submenu_for_title(WORD title)
{
	GEM_MENU_PULL_OBJECT GEM_MENU_PULL_FAR *object;
	WORD current_title;
	WORD current_menu;
	UWORD remaining;

	current_title = gem_menu_pull.first_title;
	current_menu = gem_menu_pull.first_menu;
	remaining = gem_menu_pull.tree.object_count;
	while (remaining--) {
		if (current_title == title)
			return current_menu;
		if (current_title == gem_menu_pull.last_title
		    || current_menu == gem_menu_pull.last_menu)
			break;
		object = gem_menu_object_at(&gem_menu_pull.tree,
			(UWORD) current_title);
		current_title = object->ob_next;
		object = gem_menu_object_at(&gem_menu_pull.tree,
			(UWORD) current_menu);
		current_menu = object->ob_next;
	}
	return GEM_MENU_PULL_NIL;
}

/* Hit only enabled children of an original tree or its static M_DESK. */
static WORD
gem_menu_list_at_point(UBYTE desk, WORD parent, WORD x, WORD y)
{
	GEM_MENU_PULL_OBJECT GEM_MENU_PULL_FAR *far_parent;
	GEM_MENU_PULL_OBJECT GEM_MENU_PULL_FAR *far_object;
	GEM_MENU_PULL_OBJECT *near_parent;
	GEM_MENU_PULL_OBJECT *near_object;
	GEM_MENU_PULL_RECTANGLE rectangle;
	WORD current;
	WORD tail;
	WORD next;
	UWORD state;
	UWORD remaining;

	if (desk) {
		near_parent = gem_menu_desk_at((UWORD) parent);
		current = near_parent->ob_head;
		tail = near_parent->ob_tail;
		remaining = GEM_MENU_PULL_DESK_OBJECTS;
	} else {
		far_parent = gem_menu_object_at(&gem_menu_pull.tree,
			(UWORD) parent);
		current = far_parent->ob_head;
		tail = far_parent->ob_tail;
		remaining = gem_menu_pull.tree.object_count;
	}
	while (current != GEM_MENU_PULL_NIL && remaining--) {
		if (desk) {
			near_object = gem_menu_desk_at((UWORD) current);
			state = near_object->ob_state;
			next = near_object->ob_next;
		} else {
			far_object = gem_menu_object_at(&gem_menu_pull.tree,
				(UWORD) current);
			state = far_object->ob_state;
			next = far_object->ob_next;
		}
		if (!(state & GEM_MENU_PULL_DISABLED)
		    && (desk ? gem_menu_desk_rectangle(current, &rectangle)
			: gem_menu_object_rectangle(&gem_menu_pull.tree,
				current, &rectangle))
		    && gem_menu_point_inside(x, y, &rectangle))
			return current;
		if (current == tail)
			break;
		current = next;
		if (current == parent)
			break;
	}
	return GEM_MENU_PULL_NIL;
}

static WORD
gem_menu_title_at_point(WORD x, WORD y)
{
	return gem_menu_list_at_point(FALSE, gem_menu_pull.active_titles, x, y);
}

static WORD
gem_menu_item_at_point(WORD x, WORD y)
{
	if (gem_menu_pull.current_kind == GEM_MENU_PULL_KIND_DESK)
		return gem_menu_list_at_point(TRUE, GEM_MENU_PULL_ROOT, x, y);
	return gem_menu_list_at_point(FALSE, gem_menu_pull.current_menu, x, y);
}

static WORD
gem_menu_point_in_current(WORD x, WORD y)
{
	GEM_MENU_PULL_RECTANGLE rectangle;

	return gem_menu_current_rectangle(&rectangle)
		&& gem_menu_point_inside(x, y, &rectangle);
}

static UWORD
gem_menu_current_item_state(WORD item)
{
	if (gem_menu_pull.current_kind == GEM_MENU_PULL_KIND_DESK)
		return gem_menu_desk_at((UWORD) item)->ob_state;
	return gem_menu_object_at(&gem_menu_pull.tree,
		(UWORD) item)->ob_state;
}

static VOID
gem_menu_write_current_item_state(WORD item, UWORD state)
{
	if (gem_menu_pull.current_kind == GEM_MENU_PULL_KIND_DESK)
		gem_menu_desk_at((UWORD) item)->ob_state = state;
	else
		gem_menu_object_at(&gem_menu_pull.tree,
			(UWORD) item)->ob_state = state;
}

static WORD
gem_menu_current_item_effect(GEM_MENU_PULL_EFFECTS *effects, WORD item,
	UWORD state)
{
	if (gem_menu_pull.current_kind == GEM_MENU_PULL_KIND_DESK)
		return gem_menu_desk_object_effect(effects, item, state);
	return gem_menu_main_object_effect(effects, item, state);
}

/* Highlight only the changed row, matching menu_set()/do_chg(). */
static VOID
gem_menu_set_current_item(WORD item, GEM_MENU_PULL_EFFECTS *effects)
{
	UWORD state;

	if (item == gem_menu_pull.current_item)
		return;
	if (gem_menu_pull.current_item != GEM_MENU_PULL_NIL) {
		gem_menu_write_current_item_state(gem_menu_pull.current_item,
			gem_menu_pull.item_base_state);
		gem_menu_current_item_effect(effects,
			gem_menu_pull.current_item,
			gem_menu_pull.item_base_state);
	}
	gem_menu_pull.current_item = item;
	if (item == GEM_MENU_PULL_NIL)
		return;
	state = gem_menu_current_item_state(item);
	gem_menu_pull.item_base_state = state;
	state |= GEM_MENU_PULL_SELECTED;
	gem_menu_write_current_item_state(item, state);
	gem_menu_current_item_effect(effects, item, state);
}

/*
 * Direct menu_item_find() for either a resource sibling list or M_DESK.
 * Keeping one loop avoids duplicating the original forward/backward wrap
 * rules while retaining cheap near accesses for the synthetic Desk tree.
 */
static WORD
gem_menu_list_find(UBYTE desk, WORD parent_index, WORD current,
	WORD direction)
{
	GEM_MENU_PULL_OBJECT GEM_MENU_PULL_FAR *far_parent;
	GEM_MENU_PULL_OBJECT GEM_MENU_PULL_FAR *far_object;
	GEM_MENU_PULL_OBJECT *near_parent;
	GEM_MENU_PULL_OBJECT *near_object;
	WORD first_enabled;
	WORD previous_enabled;
	WORD before_current;
	WORD candidate;
	WORD item;
	WORD tail;
	WORD next;
	UWORD state;
	UWORD remaining;

	if (desk) {
		near_parent = gem_menu_desk_at((UWORD) parent_index);
		item = near_parent->ob_head;
		tail = near_parent->ob_tail;
		remaining = GEM_MENU_PULL_DESK_OBJECTS;
	} else {
		far_parent = gem_menu_object_at(&gem_menu_pull.tree,
			(UWORD) parent_index);
		item = far_parent->ob_head;
		tail = far_parent->ob_tail;
		remaining = gem_menu_pull.tree.object_count;
	}
	first_enabled = GEM_MENU_PULL_NIL;
	previous_enabled = GEM_MENU_PULL_NIL;
	before_current = GEM_MENU_PULL_NIL;
	candidate = GEM_MENU_PULL_NIL;
	while (item != GEM_MENU_PULL_NIL && remaining--) {
		if (desk) {
			near_object = gem_menu_desk_at((UWORD) item);
			state = near_object->ob_state;
			next = near_object->ob_next;
		} else {
			far_object = gem_menu_object_at(&gem_menu_pull.tree,
				(UWORD) item);
			state = far_object->ob_state;
			next = far_object->ob_next;
		}
		if (!(state & GEM_MENU_PULL_DISABLED)) {
			if (first_enabled == GEM_MENU_PULL_NIL)
				first_enabled = item;
			if (direction && previous_enabled == current)
				return item;
			if (!direction && item == current)
				before_current = previous_enabled;
			previous_enabled = item;
			candidate = item;
		}
		if (item == tail)
			break;
		item = next;
	}
	if (current == GEM_MENU_PULL_NIL)
		return direction ? first_enabled : candidate;
	return direction ? first_enabled
		: (before_current != GEM_MENU_PULL_NIL
			? before_current : candidate);
}

static WORD
gem_menu_current_item_find(WORD current, WORD direction)
{
	if (gem_menu_pull.current_kind == GEM_MENU_PULL_KIND_DESK)
		return gem_menu_list_find(TRUE, GEM_MENU_PULL_ROOT, current,
			direction);
	return gem_menu_list_find(FALSE, gem_menu_pull.current_menu, current,
		direction);
}

/* Find a title before/after current with original wrap semantics. */
static WORD
gem_menu_title_find(WORD current, WORD direction)
{
	return gem_menu_list_find(FALSE, gem_menu_pull.active_titles, current,
		direction);
}

/* Match an underscore shortcut among one current menu's enabled children. */
static WORD
gem_menu_main_shortcut(UBYTE letter)
{
	GEM_MENU_PULL_OBJECT GEM_MENU_PULL_FAR *parent;
	GEM_MENU_PULL_OBJECT GEM_MENU_PULL_FAR *object;
	WORD item;
	UWORD remaining;

	parent = gem_menu_object_at(&gem_menu_pull.tree,
		(UWORD) gem_menu_pull.current_menu);
	item = parent->ob_head;
	remaining = gem_menu_pull.tree.object_count;
	while (item != GEM_MENU_PULL_NIL && remaining--) {
		object = gem_menu_object_at(&gem_menu_pull.tree, (UWORD) item);
		if (!(object->ob_state & GEM_MENU_PULL_DISABLED)
		    && gem_menu_shortcut_letter(&gem_menu_pull.tree, item) == letter)
			return item;
		if (item == parent->ob_tail)
			break;
		item = object->ob_next;
	}
	return GEM_MENU_PULL_NIL;
}

static WORD
gem_menu_title_shortcut(UBYTE letter)
{
	GEM_MENU_PULL_OBJECT GEM_MENU_PULL_FAR *parent;
	GEM_MENU_PULL_OBJECT GEM_MENU_PULL_FAR *object;
	WORD title;
	UWORD remaining;

	parent = gem_menu_object_at(&gem_menu_pull.tree,
		(UWORD) gem_menu_pull.active_titles);
	title = parent->ob_head;
	remaining = gem_menu_pull.tree.object_count;
	while (title != GEM_MENU_PULL_NIL && remaining--) {
		object = gem_menu_object_at(&gem_menu_pull.tree, (UWORD) title);
		if (!(object->ob_state & GEM_MENU_PULL_DISABLED)
		    && gem_menu_shortcut_letter(&gem_menu_pull.tree, title)
			== letter)
			return title;
		if (title == parent->ob_tail)
			break;
		title = object->ob_next;
	}
	return GEM_MENU_PULL_NIL;
}

static WORD
gem_menu_desk_shortcut(UBYTE letter)
{
	GEM_MENU_PULL_ACCESSORY *accessory;
	GEM_MENU_PULL_OBJECT GEM_MENU_PULL_FAR *menu;
	UBYTE *name;
	UBYTE found;
	UWORD index;
	UWORD remaining;

	menu = gem_menu_object_at(&gem_menu_pull.tree,
		(UWORD) gem_menu_pull.desk_menu);
	if (gem_menu_shortcut_letter(&gem_menu_pull.tree, menu->ob_head)
	    == letter)
		return 1;
	index = 0;
	while (index < gem_menu_accessory_count) {
		accessory = gem_menu_accessory_at(index);
		name = accessory->name;
		remaining = GEM_MENU_PULL_NAME_BYTES;
		found = 0;
		while (remaining-- && *name) {
			if (*name == (UBYTE) '_' && remaining) {
				name++;
				found = gem_menu_upper(*name);
				break;
			}
			name++;
		}
		if (found == letter)
			return (WORD) (index + GEM_MENU_PULL_DESK_FIXED);
		index++;
	}
	return GEM_MENU_PULL_NIL;
}

static WORD
gem_menu_current_shortcut(UBYTE letter)
{
	if (gem_menu_pull.current_kind == GEM_MENU_PULL_KIND_DESK)
		return gem_menu_desk_shortcut(letter);
	return gem_menu_main_shortcut(letter);
}

/* Open one title and emit the original select/save/draw order. */
static WORD
gem_menu_open_title(WORD title, UBYTE drag_select,
	GEM_MENU_PULL_EFFECTS *effects)
{
	GEM_MENU_PULL_OBJECT GEM_MENU_PULL_FAR *object;
	GEM_MENU_PULL_RECTANGLE rectangle;
	WORD menu;
	UBYTE kind;

	menu = gem_menu_submenu_for_title(title);
	if (menu == GEM_MENU_PULL_NIL)
		return FALSE;
	kind = title == gem_menu_pull.desk_title
		? GEM_MENU_PULL_KIND_DESK : GEM_MENU_PULL_KIND_MAIN;
	if (kind == GEM_MENU_PULL_KIND_DESK) {
		if (!gem_menu_build_desk()
		    || !gem_menu_desk_rectangle(GEM_MENU_PULL_ROOT, &rectangle))
			return FALSE;
	} else if (!gem_menu_object_rectangle(&gem_menu_pull.tree,
		menu, &rectangle))
		return FALSE;

	gem_menu_pull.current_title = title;
	gem_menu_pull.current_menu = menu;
	gem_menu_pull.current_kind = kind;
	gem_menu_pull.current_item = GEM_MENU_PULL_NIL;
	gem_menu_pull.armed_item = GEM_MENU_PULL_NIL;
	gem_menu_pull.drag_select = drag_select;
	gem_menu_pull.latched = FALSE;
	gem_menu_pull.outside_press = FALSE;
	gem_menu_pull.menu_state = GEM_MENU_PULL_OUTTITLE;
	gem_menu_pull.tracking = TRUE;

	object = gem_menu_object_at(&gem_menu_pull.tree, (UWORD) title);
	gem_menu_pull.title_base_state = object->ob_state;
	object->ob_state |= GEM_MENU_PULL_SELECTED;
	gem_menu_main_object_effect(effects, title, object->ob_state);
	gem_menu_append_effect(effects, GEM_MENU_PULL_SAVE_AREA,
		kind == GEM_MENU_PULL_KIND_DESK
			? GEM_MENU_PULL_TREE_DESK : GEM_MENU_PULL_TREE_MAIN,
		kind == GEM_MENU_PULL_KIND_DESK ? GEM_MENU_PULL_ROOT : menu,
		0, &rectangle);
	gem_menu_append_effect(effects, GEM_MENU_PULL_DRAW_MENU,
		kind == GEM_MENU_PULL_KIND_DESK
			? GEM_MENU_PULL_TREE_DESK : GEM_MENU_PULL_TREE_MAIN,
		kind == GEM_MENU_PULL_KIND_DESK ? GEM_MENU_PULL_ROOT : menu,
		0, &rectangle);
	return TRUE;
}

static VOID
gem_menu_clear_tracking_words(VOID)
{
	gem_menu_pull.current_title = GEM_MENU_PULL_NIL;
	gem_menu_pull.current_menu = GEM_MENU_PULL_NIL;
	gem_menu_pull.current_item = GEM_MENU_PULL_NIL;
	gem_menu_pull.armed_item = GEM_MENU_PULL_NIL;
	gem_menu_pull.current_kind = GEM_MENU_PULL_KIND_NONE;
	gem_menu_pull.menu_state = GEM_MENU_PULL_INBAR;
	gem_menu_pull.tracking = FALSE;
	gem_menu_pull.drag_select = FALSE;
	gem_menu_pull.latched = FALSE;
	gem_menu_pull.outside_press = FALSE;
}

static VOID
gem_menu_make_message(GEM_MENU_PULL_EFFECTS *effects, UWORD type,
	UWORD target_owner, UWORD target_generation_lo,
	UWORD target_generation_hi, UWORD title, UWORD item)
{
	if (!effects)
		return;
	effects->target_owner = target_owner;
	effects->target_generation_lo = target_generation_lo;
	effects->target_generation_hi = target_generation_hi;
	effects->message[0] = type;
	effects->message[1] = gem_menu_pull.owner;
	effects->message[2] = 0;
	effects->message[3] = title;
	effects->message[4] = item;
	effects->message[5] = 0;
	effects->message[6] = 0;
	effects->message[7] = 0;
	effects->message_ready = TRUE;
}

/* Restore a pull-down and optionally publish MN_SELECTED or AC_OPEN. */
static VOID
gem_menu_close(WORD selected, GEM_MENU_PULL_EFFECTS *effects)
{
	GEM_MENU_PULL_ACCESSORY *accessory;
	GEM_MENU_PULL_OBJECT GEM_MENU_PULL_FAR *title;
	WORD title_index;
	WORD main_item;
	UWORD title_state;
	UWORD accessory_index;

	if (!gem_menu_pull.tracking)
		return;
	title_index = gem_menu_pull.current_title;
	if (gem_menu_pull.current_item != GEM_MENU_PULL_NIL)
		gem_menu_write_current_item_state(gem_menu_pull.current_item,
			gem_menu_pull.item_base_state);
	gem_menu_current_area_effect(effects, GEM_MENU_PULL_RESTORE_AREA);
	title = gem_menu_object_at(&gem_menu_pull.tree, (UWORD) title_index);

	if (selected != GEM_MENU_PULL_NIL
	    && gem_menu_pull.current_kind == GEM_MENU_PULL_KIND_DESK
	    && selected >= (WORD) GEM_MENU_PULL_DESK_FIXED) {
		accessory_index = (UWORD)
			(selected - (WORD) GEM_MENU_PULL_DESK_FIXED);
		title_state = gem_menu_pull.title_base_state
			& (UWORD) ~GEM_MENU_PULL_SELECTED;
		title->ob_state = title_state;
		gem_menu_main_object_effect(effects, title_index, title_state);
		if (accessory_index < gem_menu_accessory_count) {
			accessory = gem_menu_accessory_at(accessory_index);
			gem_menu_make_message(effects, GEM_MENU_PULL_AC_OPEN,
				accessory->owner, accessory->generation_lo,
				accessory->generation_hi, (UWORD) title_index,
				accessory_index);
		}
	} else if (selected != GEM_MENU_PULL_NIL) {
		/* Successful application selections remain selected for TNORMAL. */
		title->ob_state = gem_menu_pull.title_base_state
			| GEM_MENU_PULL_SELECTED;
		main_item = selected;
		if (gem_menu_pull.current_kind == GEM_MENU_PULL_KIND_DESK) {
			/* Synthetic Information maps back to the original Desk item. */
			GEM_MENU_PULL_OBJECT GEM_MENU_PULL_FAR *menu;

			menu = gem_menu_object_at(&gem_menu_pull.tree,
				(UWORD) gem_menu_pull.desk_menu);
			main_item = menu->ob_head;
		}
		gem_menu_make_message(effects, GEM_MENU_PULL_MN_SELECTED,
			gem_menu_pull.owner, gem_menu_pull.generation_lo,
			gem_menu_pull.generation_hi, (UWORD) title_index,
			(UWORD) main_item);
	} else {
		title_state = gem_menu_pull.title_base_state;
		title->ob_state = title_state;
		gem_menu_main_object_effect(effects, title_index, title_state);
	}
	gem_menu_clear_tracking_words();
}

/* Switch pull-downs in original unselect/restore/select/save/draw order. */
static WORD
gem_menu_switch_title(WORD title, GEM_MENU_PULL_EFFECTS *effects,
	UBYTE keyboard)
{
	GEM_MENU_PULL_OBJECT GEM_MENU_PULL_FAR *object;

	if (title == GEM_MENU_PULL_NIL || title == gem_menu_pull.current_title)
		return TRUE;
	if (gem_menu_pull.current_item != GEM_MENU_PULL_NIL)
		gem_menu_write_current_item_state(gem_menu_pull.current_item,
			gem_menu_pull.item_base_state);
	object = gem_menu_object_at(&gem_menu_pull.tree,
		(UWORD) gem_menu_pull.current_title);
	object->ob_state = gem_menu_pull.title_base_state;
	gem_menu_main_object_effect(effects, gem_menu_pull.current_title,
		object->ob_state);
	gem_menu_current_area_effect(effects, GEM_MENU_PULL_RESTORE_AREA);
	gem_menu_clear_tracking_words();
	if (!gem_menu_open_title(title, FALSE, effects))
		return FALSE;
	if (keyboard)
		gem_menu_set_current_item(
			gem_menu_current_item_find(GEM_MENU_PULL_NIL, TRUE),
			effects);
	return TRUE;
}

/* Update one menu row selection from a physical mouse sample. */
static VOID
gem_menu_mouse_tracking(const GEM_MENU_PULL_INPUT *input, UBYTE pressed,
	UBYTE released, UBYTE just_opened, GEM_MENU_PULL_EFFECTS *effects)
{
	WORD title;
	WORD item;

	title = gem_menu_title_at_point(input->mouse_x, input->mouse_y);
	if (title != GEM_MENU_PULL_NIL
	    && title != gem_menu_pull.current_title)
		gem_menu_switch_title(title, effects, FALSE);
	item = gem_menu_item_at_point(input->mouse_x, input->mouse_y);
	gem_menu_set_current_item(item, effects);
	if (item != GEM_MENU_PULL_NIL)
		gem_menu_pull.menu_state = GEM_MENU_PULL_OUTITEM;
	else if (gem_menu_point_in_current(input->mouse_x, input->mouse_y))
		gem_menu_pull.menu_state = GEM_MENU_PULL_INBARECT;
	else
		gem_menu_pull.menu_state = GEM_MENU_PULL_OUTTITLE;

	if (pressed && !just_opened) {
		gem_menu_pull.armed_item = item;
		gem_menu_pull.outside_press = item == GEM_MENU_PULL_NIL
			&& title == GEM_MENU_PULL_NIL
			&& !gem_menu_point_in_current(input->mouse_x,
				input->mouse_y);
	}
	if (released) {
		if (item != GEM_MENU_PULL_NIL
		    && (gem_menu_pull.drag_select
			|| gem_menu_pull.armed_item == item)) {
			gem_menu_close(item, effects);
			return;
		}
		if (title != GEM_MENU_PULL_NIL
		    || gem_menu_point_in_current(input->mouse_x,
			input->mouse_y))
			gem_menu_pull.latched = TRUE;
		else if (gem_menu_pull.outside_press
			 || gem_menu_pull.armed_item != GEM_MENU_PULL_NIL) {
			gem_menu_close(GEM_MENU_PULL_NIL, effects);
			return;
		}
		gem_menu_pull.drag_select = FALSE;
		gem_menu_pull.armed_item = GEM_MENU_PULL_NIL;
		gem_menu_pull.outside_press = FALSE;
	}
	if (!(input->mouse_buttons & GEM_MENU_PULL_LEFT_BUTTON)
	    && !gem_menu_pull.latched && !gem_menu_pull.drag_select
	    && title == GEM_MENU_PULL_NIL && item == GEM_MENU_PULL_NIL
	    && !gem_menu_point_in_current(input->mouse_x, input->mouse_y))
		gem_menu_close(GEM_MENU_PULL_NIL, effects);
}

static WORD
gem_menu_keyboard(UWORD key, GEM_MENU_PULL_EFFECTS *effects)
{
	WORD title;
	WORD item;
	UBYTE letter;

	if (!gem_menu_pull.tracking) {
		if (key == GEM_MENU_PULL_KEY_F10)
			title = gem_menu_title_find(GEM_MENU_PULL_NIL, TRUE);
		else if (key == GEM_MENU_PULL_KEY_F1)
			title = gem_menu_title_find(GEM_MENU_PULL_NIL, FALSE);
		else {
			letter = gem_menu_key_letter(key);
			title = letter ? gem_menu_title_shortcut(letter)
				: GEM_MENU_PULL_NIL;
		}
		if (title != GEM_MENU_PULL_NIL
		    && gem_menu_open_title(title, FALSE, effects)) {
			gem_menu_set_current_item(
				gem_menu_current_item_find(GEM_MENU_PULL_NIL,
					TRUE), effects);
			return TRUE;
		}
		return FALSE;
	}

	switch (key) {
	case GEM_MENU_PULL_KEY_ESCAPE:
		gem_menu_close(GEM_MENU_PULL_NIL, effects);
		return TRUE;
	case GEM_MENU_PULL_KEY_RIGHT:
		title = gem_menu_title_find(gem_menu_pull.current_title, TRUE);
		gem_menu_switch_title(title, effects, TRUE);
		return TRUE;
	case GEM_MENU_PULL_KEY_LEFT:
		title = gem_menu_title_find(gem_menu_pull.current_title, FALSE);
		gem_menu_switch_title(title, effects, TRUE);
		return TRUE;
	case GEM_MENU_PULL_KEY_DOWN:
		item = gem_menu_current_item_find(gem_menu_pull.current_item, TRUE);
		gem_menu_set_current_item(item, effects);
		return TRUE;
	case GEM_MENU_PULL_KEY_UP:
		item = gem_menu_current_item_find(gem_menu_pull.current_item, FALSE);
		gem_menu_set_current_item(item, effects);
		return TRUE;
	case GEM_MENU_PULL_KEY_ENTER:
		if (gem_menu_pull.current_item != GEM_MENU_PULL_NIL)
			gem_menu_close(gem_menu_pull.current_item, effects);
		return TRUE;
	default:
		letter = gem_menu_key_letter(key);
		item = letter ? gem_menu_current_shortcut(letter)
			: GEM_MENU_PULL_NIL;
		if (item != GEM_MENU_PULL_NIL)
			gem_menu_close(item, effects);
		else
			gem_menu_close(GEM_MENU_PULL_NIL, effects);
		return TRUE;
	}
}

/* Copy text only inside the current target string's proven capacity. */
static WORD
gem_menu_text(const GEM_MENU_PULL_CALL *call)
{
	UBYTE GEM_MENU_PULL_FAR *destination;
	GEM_MENU_PULL_TEXT_POINTER source;
	UWORD available;
	UWORD destination_length;
	UWORD source_length;
	UWORD remaining;

	if (!call->text || !call->text_limit)
		return FALSE;
	destination = gem_menu_object_string(&call->tree,
		(WORD) call->int_in[0], &available);
	if (!destination)
		return FALSE;
	destination_length = 0;
	remaining = available;
	while (remaining && destination[destination_length]) {
		destination_length++;
		remaining--;
	}
	if (!remaining)
		return FALSE;
	source = call->text;
	source_length = 0;
	remaining = call->text_limit;
	while (remaining && source[source_length]) {
		source_length++;
		remaining--;
	}
	if (!remaining || source_length > destination_length)
		return FALSE;
	remaining = source_length;
	while (remaining--)
		*destination++ = *source++;
	*destination = 0;
	return TRUE;
}

static WORD
gem_menu_register(const GEM_MENU_PULL_CALL *call)
{
	GEM_MENU_PULL_ACCESSORY *accessory;
	GEM_MENU_PULL_TEXT_POINTER source;
	UBYTE *destination;
	UWORD remaining;
	UWORD available;

	if (!call->text || !call->text_limit
	    || call->int_in[0] != call->owner
	    || gem_menu_accessory_count >= GEM_MENU_PULL_ACCESSORIES)
		return GEM_MENU_PULL_NIL;
	accessory = gem_menu_accessory_at(gem_menu_accessory_count);
	accessory->owner = call->owner;
	accessory->generation_lo = call->generation_lo;
	accessory->generation_hi = call->generation_hi;
	source = call->text;
	destination = accessory->name;
	remaining = GEM_MENU_PULL_NAME_BYTES - 1U;
	available = call->text_limit;
	while (remaining && available && *source) {
		*destination++ = *source++;
		remaining--;
		available--;
	}
	*destination++ = 0;
	while (remaining--)
		*destination++ = 0;
	gem_menu_accessory_count++;
	gem_menu_build_desk();
	return (WORD) (gem_menu_accessory_count - 1U);
}

static VOID
gem_menu_compact_accessory(UWORD removed)
{
	GEM_MENU_PULL_ACCESSORY *destination;
	GEM_MENU_PULL_ACCESSORY *source;
	UWORD index;

	index = removed;
	while (index + 1U < gem_menu_accessory_count) {
		destination = gem_menu_accessory_at(index);
		source = gem_menu_accessory_at((UWORD) (index + 1U));
		destination->owner = source->owner;
		destination->generation_lo = source->generation_lo;
		destination->generation_hi = source->generation_hi;
		gem_menu_copy_accessory_name(destination->name, source->name);
		index++;
	}
	gem_menu_accessory_count--;
	destination = gem_menu_accessory_at(gem_menu_accessory_count);
	destination->owner = GEM_MENU_PULL_NO_OWNER;
	destination->generation_lo = 0;
	destination->generation_hi = 0;
	index = 0;
	while (index < GEM_MENU_PULL_NAME_BYTES)
		destination->name[index++] = 0;
}

static WORD
gem_menu_unregister(const GEM_MENU_PULL_CALL *call)
{
	GEM_MENU_PULL_ACCESSORY *accessory;
	WORD requested;
	UWORD index;

	requested = (WORD) call->int_in[0];
	index = 0;
	if (requested >= 0)
		index = (UWORD) requested;
	else {
		while (index < gem_menu_accessory_count) {
			accessory = gem_menu_accessory_at(index);
			if (accessory->owner == call->owner
			    && accessory->generation_lo == call->generation_lo
			    && accessory->generation_hi == call->generation_hi)
				break;
			index++;
		}
	}
	if (index >= gem_menu_accessory_count)
		return FALSE;
	accessory = gem_menu_accessory_at(index);
	if (accessory->owner != call->owner
	    || accessory->generation_lo != call->generation_lo
	    || accessory->generation_hi != call->generation_hi)
		return FALSE;
	gem_menu_compact_accessory(index);
	gem_menu_build_desk();
	return TRUE;
}

static WORD
gem_menu_call_shape(const GEM_MENU_PULL_CALL *call, UWORD input_count,
	UWORD output_count, UWORD address_count)
{
	if (!call || !call->control || call->control[1] < input_count
	    || call->control[2] < output_count
	    || call->control[3] < address_count)
		return FALSE;
	if (input_count && !call->int_in)
		return FALSE;
	if (output_count && !call->int_out)
		return FALSE;
	return TRUE;
}

static WORD
gem_menu_malformed(const GEM_MENU_PULL_CALL *call, WORD *handled)
{
	*handled = TRUE;
	if (call && call->control && call->int_out && call->control[2])
		call->int_out[0] = FALSE;
	return FALSE;
}

VOID
gem_menu_pull_resident_reset(VOID)
{
	GEM_MENU_PULL_ACCESSORY *accessory;
	UWORD index;
	UWORD byte;

	gem_menu_pull.tree.resource = (GEM_MENU_PULL_BYTE_POINTER) 0;
	gem_menu_pull.tree.bytes = 0;
	gem_menu_pull.tree.segment = 0;
	gem_menu_pull.tree.tree_offset = 0;
	gem_menu_pull.tree.object_count = 0;
	gem_menu_pull.owner = GEM_MENU_PULL_NO_OWNER;
	gem_menu_pull.generation_lo = 0;
	gem_menu_pull.generation_hi = 0;
	gem_menu_pull.bar = GEM_MENU_PULL_NIL;
	gem_menu_pull.active_titles = GEM_MENU_PULL_NIL;
	gem_menu_pull.menus = GEM_MENU_PULL_NIL;
	gem_menu_pull.first_title = GEM_MENU_PULL_NIL;
	gem_menu_pull.last_title = GEM_MENU_PULL_NIL;
	gem_menu_pull.first_menu = GEM_MENU_PULL_NIL;
	gem_menu_pull.last_menu = GEM_MENU_PULL_NIL;
	gem_menu_pull.desk_title = GEM_MENU_PULL_NIL;
	gem_menu_pull.desk_menu = GEM_MENU_PULL_NIL;
	gem_menu_pull.previous_buttons = 0;
	gem_menu_pull.click_mode = 0;
	gem_menu_pull.active = FALSE;
	gem_menu_clear_tracking_words();
	index = 0;
	while (index < GEM_MENU_PULL_DESK_OBJECTS)
		gem_menu_clear_object(gem_menu_desk_at(index++));
	index = 0;
	while (index < GEM_MENU_PULL_ACCESSORIES) {
		accessory = gem_menu_accessory_at(index++);
		accessory->owner = GEM_MENU_PULL_NO_OWNER;
		accessory->generation_lo = 0;
		accessory->generation_hi = 0;
		byte = 0;
		while (byte < GEM_MENU_PULL_NAME_BYTES)
			accessory->name[byte++] = 0;
	}
	gem_menu_accessory_count = 0;
}

WORD
gem_menu_pull_resident_activate(const GEM_MENU_PULL_TREE *tree,
	UWORD owner, UWORD generation_lo, UWORD generation_hi,
	GEM_MENU_PULL_EFFECTS *effects)
{
	WORD bar;
	WORD active_titles;
	WORD menus;
	WORD first_title;
	WORD last_title;
	WORD first_menu;
	WORD last_menu;

	gem_menu_clear_effects(effects);
	if (!gem_menu_validate_tree(tree, &bar, &active_titles, &menus,
		&first_title, &last_title, &first_menu, &last_menu))
		return FALSE;
	if (gem_menu_pull.tracking)
		gem_menu_close(GEM_MENU_PULL_NIL, effects);
	/* Five direct stores avoid a general structure-copy helper on an XT. */
	gem_menu_pull.tree.resource = tree->resource;
	gem_menu_pull.tree.bytes = tree->bytes;
	gem_menu_pull.tree.segment = tree->segment;
	gem_menu_pull.tree.tree_offset = tree->tree_offset;
	gem_menu_pull.tree.object_count = tree->object_count;
	gem_menu_pull.owner = owner;
	gem_menu_pull.generation_lo = generation_lo;
	gem_menu_pull.generation_hi = generation_hi;
	gem_menu_pull.bar = bar;
	gem_menu_pull.active_titles = active_titles;
	gem_menu_pull.menus = menus;
	gem_menu_pull.first_title = first_title;
	gem_menu_pull.last_title = last_title;
	gem_menu_pull.first_menu = first_menu;
	gem_menu_pull.last_menu = last_menu;
	gem_menu_pull.desk_title = first_title;
	gem_menu_pull.desk_menu = first_menu;
	gem_menu_pull.previous_buttons = 0;
	gem_menu_pull.active = TRUE;
	gem_menu_clear_tracking_words();
	if (!gem_menu_build_desk())
		return FALSE;
	/*
	 * Original mn_bar() replaces the complete menu strip when gl_mntree
	 * changes.  G_TITLE glyphs are transparent, so drawing only the new tree
	 * leaves titles from a longer previous owner's menu behind.  Request the
	 * existing bounded full-bar effect on every successful ownership switch;
	 * the resident drawing adapter supplies the opaque backing without a save
	 * bitmap, converted tree, allocation, or application wrapper.
	 */
	if (effects)
		effects->redraw_all = TRUE;
	return TRUE;
}

WORD
gem_menu_pull_resident_deactivate(UWORD owner, UWORD generation_lo,
	UWORD generation_hi, GEM_MENU_PULL_EFFECTS *effects)
{
	gem_menu_clear_effects(effects);
	if (!gem_menu_pull.active || gem_menu_pull.owner != owner
	    || gem_menu_pull.generation_lo != generation_lo
	    || gem_menu_pull.generation_hi != generation_hi)
		return FALSE;
	if (gem_menu_pull.tracking)
		gem_menu_close(GEM_MENU_PULL_NIL, effects);
	gem_menu_pull.active = FALSE;
	gem_menu_pull.owner = GEM_MENU_PULL_NO_OWNER;
	gem_menu_pull.tree.resource = (GEM_MENU_PULL_BYTE_POINTER) 0;
	gem_menu_pull.tree.bytes = 0;
	gem_menu_pull.tree.object_count = 0;
	return TRUE;
}

WORD
gem_menu_pull_resident_dispatch(const GEM_MENU_PULL_CALL *call,
	GEM_MENU_PULL_EFFECTS *effects, WORD *handled)
{
	GEM_MENU_PULL_OBJECT GEM_MENU_PULL_FAR *object;
	GEM_MENU_PULL_RECTANGLE rectangle;
	UWORD opcode;
	UWORD item;
	WORD result;

	if (!handled)
		return FALSE;
	*handled = FALSE;
	if (!call || !call->control)
		return FALSE;
	opcode = call->control[0];
	if (opcode < GEM_MENU_PULL_ICHECK || opcode > GEM_MENU_PULL_CLICK)
		return FALSE;
	gem_menu_clear_effects(effects);
	*handled = TRUE;
	result = FALSE;

	switch (opcode) {
	case GEM_MENU_PULL_ICHECK:
	case GEM_MENU_PULL_IENABLE:
	case GEM_MENU_PULL_TNORMAL:
		if (!gem_menu_call_shape(call, 2U, 1U, 1U))
			return gem_menu_malformed(call, handled);
		break;
	case GEM_MENU_PULL_TEXT:
		if (!gem_menu_call_shape(call, 1U, 1U, 2U))
			return gem_menu_malformed(call, handled);
		break;
	case GEM_MENU_PULL_REGISTER:
		if (!gem_menu_call_shape(call, 1U, 1U, 1U))
			return gem_menu_malformed(call, handled);
		break;
	case GEM_MENU_PULL_UNREGISTER:
		if (!gem_menu_call_shape(call, 1U, 1U, 0U))
			return gem_menu_malformed(call, handled);
		break;
	case GEM_MENU_PULL_CLICK:
		if (!gem_menu_call_shape(call, 2U, 1U, 0U))
			return gem_menu_malformed(call, handled);
		break;
	default:
		return gem_menu_malformed(call, handled);
	}

	if (opcode >= GEM_MENU_PULL_ICHECK && opcode <= GEM_MENU_PULL_TEXT) {
		item = call->int_in[0] & GEM_MENU_PULL_ITEM_MASK;
		if (!gem_menu_tree_range(&call->tree)
		    || item >= call->tree.object_count)
			goto finished;
		object = gem_menu_object_at(&call->tree, item);
		switch (opcode) {
		case GEM_MENU_PULL_ICHECK:
			if (call->int_in[1])
				object->ob_state |= GEM_MENU_PULL_CHECKED;
			else
				object->ob_state &=
					(UWORD) ~GEM_MENU_PULL_CHECKED;
			result = TRUE;
			break;
		case GEM_MENU_PULL_IENABLE:
			if (call->int_in[1])
				object->ob_state &=
					(UWORD) ~GEM_MENU_PULL_DISABLED;
			else
				object->ob_state |= GEM_MENU_PULL_DISABLED;
			if ((call->int_in[0] & GEM_MENU_PULL_HIGH_ITEM)
			    && gem_menu_pull.active
			    && gem_menu_same_tree(&call->tree,
				&gem_menu_pull.tree)
			    && gem_menu_object_rectangle(&call->tree,
				(WORD) item, &rectangle))
				gem_menu_append_effect(effects,
					GEM_MENU_PULL_DRAW_OBJECT,
					GEM_MENU_PULL_TREE_MAIN,
					(WORD) item, object->ob_state,
					&rectangle);
			result = TRUE;
			break;
		case GEM_MENU_PULL_TNORMAL:
			if (gem_menu_pull.active
			    && gem_menu_same_tree(&call->tree,
				&gem_menu_pull.tree)) {
				if (call->int_in[1])
					object->ob_state &=
						(UWORD) ~GEM_MENU_PULL_SELECTED;
				else
					object->ob_state |= GEM_MENU_PULL_SELECTED;
				gem_menu_main_object_effect(effects,
					(WORD) item, object->ob_state);
			}
			result = TRUE;
			break;
		case GEM_MENU_PULL_TEXT:
			result = gem_menu_text(call);
			break;
		default:
			break;
		}
	} else if (opcode == GEM_MENU_PULL_REGISTER) {
		if (gem_menu_pull.tracking
		    && gem_menu_pull.current_kind == GEM_MENU_PULL_KIND_DESK)
			gem_menu_close(GEM_MENU_PULL_NIL, effects);
		result = gem_menu_register(call);
	} else if (opcode == GEM_MENU_PULL_UNREGISTER) {
		if (gem_menu_pull.tracking
		    && gem_menu_pull.current_kind == GEM_MENU_PULL_KIND_DESK)
			gem_menu_close(GEM_MENU_PULL_NIL, effects);
		result = gem_menu_unregister(call);
	} else if (opcode == GEM_MENU_PULL_CLICK) {
		if (call->int_in[1])
			gem_menu_pull.click_mode = call->int_in[0];
		result = (WORD) gem_menu_pull.click_mode;
	}

finished:
	call->int_out[0] = (UWORD) result;
	return result;
}

WORD
gem_menu_pull_resident_input(const GEM_MENU_PULL_INPUT *input,
	GEM_MENU_PULL_EFFECTS *effects)
{
	UWORD current_button;
	UWORD previous_button;
	WORD title;
	UBYTE pressed;
	UBYTE released;
	UBYTE just_opened;
	WORD consumed;

	gem_menu_clear_effects(effects);
	if (!input || !gem_menu_pull.active)
		return FALSE;
	current_button = input->mouse_buttons & GEM_MENU_PULL_LEFT_BUTTON;
	previous_button = gem_menu_pull.previous_buttons
		& GEM_MENU_PULL_LEFT_BUTTON;
	pressed = current_button && !previous_button;
	released = !current_button && previous_button;
	gem_menu_pull.previous_buttons = input->mouse_buttons;
	consumed = gem_menu_pull.tracking;
	just_opened = FALSE;

	if (input->key_ready) {
		consumed = gem_menu_keyboard(input->key_code, effects);
		if (consumed && effects)
			effects->consume_key = TRUE;
		return consumed;
	}
	if (!gem_menu_pull.tracking && pressed) {
		title = gem_menu_title_at_point(input->mouse_x, input->mouse_y);
		if (title != GEM_MENU_PULL_NIL
		    && gem_menu_open_title(title, TRUE, effects)) {
			consumed = TRUE;
			just_opened = TRUE;
		}
	}
	if (gem_menu_pull.tracking) {
		gem_menu_mouse_tracking(input, pressed, released, just_opened,
			effects);
		consumed = TRUE;
	}
	if (consumed && effects)
		effects->consume_mouse = TRUE;
	return consumed;
}

VOID
gem_menu_pull_resident_detach(UWORD owner, UWORD generation_lo,
	UWORD generation_hi, GEM_MENU_PULL_EFFECTS *effects)
{
	GEM_MENU_PULL_ACCESSORY *accessory;
	UWORD index;
	UBYTE changed;

	gem_menu_clear_effects(effects);
	if (gem_menu_pull.active && gem_menu_pull.owner == owner
	    && gem_menu_pull.generation_lo == generation_lo
	    && gem_menu_pull.generation_hi == generation_hi) {
		if (gem_menu_pull.tracking)
			gem_menu_close(GEM_MENU_PULL_NIL, effects);
		gem_menu_pull.active = FALSE;
		gem_menu_pull.owner = GEM_MENU_PULL_NO_OWNER;
	}
	/* Geometry must not change beneath an already saved Desk pull-down. */
	changed = FALSE;
	index = 0;
	while (index < gem_menu_accessory_count) {
		accessory = gem_menu_accessory_at(index++);
		if (accessory->owner == owner
		    && accessory->generation_lo == generation_lo
		    && accessory->generation_hi == generation_hi) {
			changed = TRUE;
			break;
		}
	}
	if (changed && gem_menu_pull.tracking
	    && gem_menu_pull.current_kind == GEM_MENU_PULL_KIND_DESK)
		gem_menu_close(GEM_MENU_PULL_NIL, effects);
	index = 0;
	while (index < gem_menu_accessory_count) {
		accessory = gem_menu_accessory_at(index);
		if (accessory->owner == owner
		    && accessory->generation_lo == generation_lo
		    && accessory->generation_hi == generation_hi) {
			gem_menu_compact_accessory(index);
		} else
			index++;
	}
	if (changed && gem_menu_pull.active)
		gem_menu_build_desk();
}

WORD
gem_menu_pull_resident_desk_object(UWORD object_index,
	GEM_MENU_PULL_DESK_OBJECT *snapshot)
{
	GEM_MENU_PULL_ACCESSORY *accessory;
	GEM_MENU_PULL_OBJECT GEM_MENU_PULL_FAR *menu;
	GEM_MENU_PULL_OBJECT GEM_MENU_PULL_FAR *information;
	GEM_MENU_PULL_OBJECT *object;
	UBYTE GEM_MENU_PULL_FAR *source;
	UBYTE *destination;
	UWORD available;
	UWORD index;

	if (!snapshot || !gem_menu_pull.active
	    || object_index > (UWORD) (2U + gem_menu_accessory_count))
		return FALSE;
	object = gem_menu_desk_at(object_index);
	snapshot->next = object->ob_next;
	snapshot->head = object->ob_head;
	snapshot->tail = object->ob_tail;
	snapshot->type = object->ob_type;
	snapshot->flags = object->ob_flags;
	snapshot->state = object->ob_state;
	snapshot->rectangle.x = (WORD) object->ob_x;
	snapshot->rectangle.y = (WORD) object->ob_y;
	snapshot->rectangle.width = (WORD) object->ob_width;
	snapshot->rectangle.height = (WORD) object->ob_height;
	destination = snapshot->text;
	index = 0;
	while (index < GEM_MENU_PULL_NAME_BYTES)
		destination[index++] = 0;
	if (object_index == GEM_MENU_PULL_ROOT)
		return TRUE;
	if (object_index <= 2U) {
		menu = gem_menu_object_at(&gem_menu_pull.tree,
			(UWORD) gem_menu_pull.desk_menu);
		information = gem_menu_object_at(&gem_menu_pull.tree,
			(UWORD) menu->ob_head);
		source = gem_menu_object_string(&gem_menu_pull.tree,
			object_index == 1U
				? menu->ob_head : information->ob_next,
			&available);
		if (!source)
			return FALSE;
		gem_menu_copy_display_string(snapshot->text, source, available);
		return TRUE;
	}
	accessory = gem_menu_accessory_at(
		(UWORD) (object_index - GEM_MENU_PULL_DESK_FIXED));
	gem_menu_copy_accessory_name(snapshot->text, accessory->name);
	return TRUE;
}

UWORD
gem_menu_pull_resident_desk_count(VOID)
{
	return gem_menu_pull.active
		? (UWORD) (2U + gem_menu_accessory_count) : 0U;
}
