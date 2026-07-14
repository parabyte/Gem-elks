/*
 * gem_menu_object_resident.c - original GEM menu/object bar in resident AES.
 *
 * The traversal and drawing flow in this file is a direct 16-bit port of
 * Digital Research GEMOBJOP.C everyobj()/get_par(), GEMOBLIB.C ob_draw(),
 * ob_offset(), just_draw(), XSTATE.C ystate(), and GEMMNLIB.C mn_bar().
 * Caldera released those sources under the GNU GPL in 1999.
 *
 * Small ELKS adaptations are deliberately confined to the boundaries:
 *
 *   - OBJECT and string addresses stay as original offset:segment words in
 *     the per-PD RSC segment instead of becoming process-local C pointers;
 *   - the resident native VDI replaces GSX's process-local global arrays;
 *   - a fixed owner/generation pair replaces the historical PD pointer; and
 *   - the callback in everyobj() is a direct call, avoiding an expensive
 *     indirect far-code call on an 8088.
 *
 * No resource copy, converted tree, heap block, recursion, multiplication,
 * division, wide scalar, or floating-point operation exists in this path.
 */

#include "gem_menu_object_resident.h"
#include "gem_vdi_resident.h"

#if !defined(ELKS) || !ELKS
#error gem_menu_object_resident.c requires the ELKS IA-16 far-segment ABI
#endif

#define GEM_MENU_THEBAR             1
#define GEM_MENU_THEACTIVE          2
#define GEM_MENU_MAX_DEPTH          8U
#define GEM_MENU_COORD_STACK        10U
#define GEM_MENU_MAX_TEXT           80U
#define GEM_MENU_NO_UNDERLINE       0xffffU
#define GEM_MENU_OBJECT_BYTES       24U
#define GEM_MENU_LOGICAL_WHITE      0
#define GEM_MENU_LOGICAL_BLACK      1
#define GEM_MENU_NATIVE_BLACK       0U
#define GEM_MENU_NATIVE_WHITE       15U
#define GEM_MENU_IP_HOLLOW          0U
#define GEM_MENU_IP_SOLID           7U

/*
 * A far pointer is formed only at the already-validated RSC boundary.  The
 * union copy is exactly two 8086 words and emits no compiler wide helper.
 */
typedef union gem_menu_far_pointer {
	VOID __far *pointer;
	UBYTE __far *bytes;
	GEM_U32_WORDS __far *pair;
	RSHDR __far *header;
	OBJECT __far *object;
	GEM_FAR_ADDRESS address;
} GEM_MENU_FAR_POINTER;

typedef BYTE GEM_MENU_FAR_POINTER_MUST_BE_FOUR_BYTES
	[(sizeof(GEM_MENU_FAR_POINTER) == 4) ? 1 : -1];

typedef struct gem_menu_draw_context {
	const GEM_RESOURCE_RESIDENT *resource;
	GEM_FAR_ADDRESS tree;
	UWORD object_count;
	UWORD character_width;
	UWORD character_height;
	GEM_VDI_SCREEN *screen;
} GEM_MENU_DRAW_CONTEXT;

static VOID __far *
gem_menu_pointer(GEM_FAR_ADDRESS address)
{
	GEM_MENU_FAR_POINTER value;

	value.address.lo = address.lo;
	value.address.hi = address.hi;
	return value.pointer;
}

/* Validate one half-open byte range without allowing 16-bit wrap. */
static WORD
gem_menu_range(UWORD offset, UWORD count, UWORD limit)
{
	if (offset > limit)
		return FALSE;
	return count <= (UWORD) (limit - offset);
}

/*
 * Walk by one 24-byte OBJECT at a time.  This preserves original tree index
 * semantics while preventing ia16-gcc from selecting an 8086 MUL for
 * tree[index].  The caller has already bounded index by object_count.
 */
static OBJECT __far *
gem_menu_object_at(GEM_FAR_ADDRESS tree, UWORD index)
{
	OBJECT __far *object;
	volatile UWORD remaining;

	object = (OBJECT __far *) gem_menu_pointer(tree);
	remaining = index;
	while (remaining) {
		object++;
		remaining--;
	}
	return object;
}

static WORD
gem_menu_link_valid(WORD link, UWORD count)
{
	return link == NIL || (link >= ROOT && (UWORD) link < count);
}

/*
 * Locate a tree root on an exact OBJECT record boundary and retain its local
 * LASTOB extent.  The loader has already validated the full RSC; this second,
 * cheap pass changes global RSC link bounds into the local bounds required by
 * everyobj().
 */
static WORD
gem_menu_validate_tree(const GEM_RESOURCE_RESIDENT *resource,
	GEM_FAR_ADDRESS tree, UWORD *object_count)
{
	GEM_FAR_ADDRESS header_address;
	RSHDR __far *header;
	OBJECT __far *object;
	UWORD offset;
	UWORD remaining;
	UWORD count;
	UWORD index;
	WORD menus;

	if (!resource || !object_count
	    || !(resource->flags & GEM_RESOURCE_RESIDENT_LOADED)
	    || !resource->storage.bytes
	    || tree.hi != resource->storage.base.hi)
		return FALSE;

	header_address = resource->storage.base;
	header = (RSHDR __far *) gem_menu_pointer(header_address);
	if (header->rsh_rssize != resource->storage.bytes
	    || !header->rsh_nobs
	    || !gem_menu_range(header->rsh_object,
				 GEM_MENU_OBJECT_BYTES,
				 resource->storage.bytes))
		return FALSE;

	/* Find tree.lo using additions only; a non-record address is rejected. */
	offset = header->rsh_object;
	remaining = header->rsh_nobs;
	while (remaining && offset != tree.lo) {
		if (!gem_menu_range(offset, GEM_MENU_OBJECT_BYTES,
				    resource->storage.bytes))
			return FALSE;
		offset = (UWORD) (offset + GEM_MENU_OBJECT_BYTES);
		remaining--;
	}
	if (!remaining || offset != tree.lo)
		return FALSE;

	/* LASTOB is the original resource compiler's local-tree terminator. */
	object = (OBJECT __far *) gem_menu_pointer(tree);
	count = 0;
	while (remaining) {
		if (!gem_menu_range(offset, GEM_MENU_OBJECT_BYTES,
				    resource->storage.bytes))
			return FALSE;
		count++;
		if (object->ob_flags & LASTOB)
			break;
		object++;
		offset = (UWORD) (offset + GEM_MENU_OBJECT_BYTES);
		remaining--;
	}
	if (!count || !(object->ob_flags & LASTOB))
		return FALSE;

	/* Every local link must stay inside this one menu tree. */
	object = (OBJECT __far *) gem_menu_pointer(tree);
	index = 0;
	while (index < count) {
		if (!gem_menu_link_valid(object->ob_next, count)
		    || !gem_menu_link_valid(object->ob_head, count)
		    || !gem_menu_link_valid(object->ob_tail, count))
			return FALSE;
		object++;
		index++;
	}

	/*
	 * Qualify the classic ROOT/BAR/ACTIVE/MENUS hierarchy.  THEMENUS is a
	 * resource-generated object number, not an AES constant: Desktop's four
	 * titles place it at seven, while the original two-title SETTINGS menu
	 * places it at five.  Original GEM followed ROOT.ob_tail, as the resident
	 * pull-down manager already does.  Retain that link after bounding it by
	 * this local LASTOB extent; no table, conversion, or copied tree is needed.
	 */
	if (count <= GEM_MENU_THEACTIVE)
		return FALSE;
	object = gem_menu_object_at(tree, ROOT);
	menus = object->ob_tail;
	if (object->ob_head != GEM_MENU_THEBAR
	    || menus <= GEM_MENU_THEACTIVE || (UWORD) menus >= count)
		return FALSE;
	object = gem_menu_object_at(tree, GEM_MENU_THEBAR);
	if ((object->ob_type & 0x00ffU) != G_BOX
	    || object->ob_head != GEM_MENU_THEACTIVE
	    || object->ob_next != menus)
		return FALSE;
	object = gem_menu_object_at(tree, GEM_MENU_THEACTIVE);
	if ((object->ob_type & 0x00ffU) != G_IBOX
	    || object->ob_head == NIL || object->ob_tail == NIL)
		return FALSE;
	object = gem_menu_object_at(tree, (UWORD) menus);
	if ((object->ob_type & 0x00ffU) != G_IBOX
	    || object->ob_head == NIL || object->ob_tail == NIL)
		return FALSE;

	*object_count = count;
	return TRUE;
}

WORD
gem_menu_object_resident_tree_count(const GEM_RESOURCE_RESIDENT *resource,
	GEM_FAR_ADDRESS tree, UWORD *object_count)
{
	return gem_menu_validate_tree(resource, tree, object_count);
}

/* Checked signed pixel addition; success is exact, never saturated. */
static WORD
gem_menu_add_coordinate(WORD left, WORD right, WORD *result)
{
	if (!result)
		return FALSE;
	if (right > 0 && left > 32767 - right)
		return FALSE;
	if (right < 0 && left < (-32767 - 1) - right)
		return FALSE;
	*result = left + right;
	return TRUE;
}

/* Direct offset:segment pair load for an original INDIRECT ob_spec. */
static WORD
gem_menu_spec(const GEM_MENU_DRAW_CONTEXT *context,
	const OBJECT __far *object, GEM_FAR_ADDRESS *spec)
{
	GEM_U32_WORDS __far *indirect;

	if (!context || !object || !spec)
		return FALSE;
	spec->lo = object->ob_spec.lo;
	spec->hi = object->ob_spec.hi;
	if (!(object->ob_flags & INDIRECT))
		return TRUE;
	if (spec->hi != context->resource->storage.base.hi
	    || !gem_menu_range(spec->lo, sizeof(GEM_U32_WORDS),
				context->resource->storage.bytes))
		return FALSE;
	indirect = (GEM_U32_WORDS __far *) gem_menu_pointer(*spec);
	spec->lo = indirect->lo;
	spec->hi = indirect->hi;
	return TRUE;
}

/* Original gr_crack(): unpack one low-word GEM box color specification. */
static VOID
gem_menu_crack(UWORD color, UWORD *border, UWORD *text,
	UWORD *pattern, UWORD *inside)
{
	UWORD high_byte;
	UWORD low_byte;

	high_byte = color >> 8;
	low_byte = color & 0x00ffU;
	*border = (high_byte >> 4) & 0x000fU;
	*text = high_byte & 0x000fU;
	*pattern = (low_byte >> 4) & 0x000fU;
	*inside = low_byte & 0x000fU;
	if (*pattern & 0x0008U)
		*pattern &= 0x0007U;
}

static WORD
gem_menu_native_color(UWORD logical, GEM_VDI_COLOR *native)
{
	if (logical == GEM_MENU_LOGICAL_WHITE) {
		*native = GEM_MENU_NATIVE_WHITE;
		return TRUE;
	}
	if (logical == GEM_MENU_LOGICAL_BLACK) {
		*native = GEM_MENU_NATIVE_BLACK;
		return TRUE;
	}
	return FALSE;
}

/* Original gr_inside(), with one-bit shifts instead of multiplication. */
static WORD
gem_menu_inside(GRECT *rectangle, WORD thickness)
{
	WORD twice;

	if (!rectangle)
		return FALSE;
	if (!gem_menu_add_coordinate(thickness, thickness, &twice)
	    || !gem_menu_add_coordinate(rectangle->g_x, thickness,
					&rectangle->g_x)
	    || !gem_menu_add_coordinate(rectangle->g_y, thickness,
					&rectangle->g_y)
	    || !gem_menu_add_coordinate(rectangle->g_w, -twice,
					&rectangle->g_w)
	    || !gem_menu_add_coordinate(rectangle->g_h, -twice,
					&rectangle->g_h))
		return FALSE;
	return rectangle->g_w > 0 && rectangle->g_h > 0;
}

/* Original gr_box() loop, retaining inward and outward thickness signs. */
static WORD
gem_menu_draw_border(GEM_MENU_DRAW_CONTEXT *context, const GRECT *rectangle,
	WORD thickness, UWORD logical_color)
{
	GEM_VDI_COLOR native;
	GRECT current;
	WORD step;

	if (!thickness)
		return TRUE;
	if (!gem_menu_native_color(logical_color, &native))
		return FALSE;
	gem_vdi_set_mode(GEM_VDI_REPLACE);
	gem_vdi_set_foreground(context->screen, native);
	if (thickness < 0)
		thickness--;
	do {
		step = thickness > 0 ? -1 : 1;
		thickness += step;
		current = *rectangle;
		if (!gem_menu_inside(&current, thickness))
			return FALSE;
		gem_vdi_rect(context->screen, current.g_x, current.g_y,
			current.g_w, current.g_h);
	} while (thickness);
	return TRUE;
}

/* G_BOX/G_IBOX portion of original just_draw(). */
static WORD
gem_menu_draw_box(GEM_MENU_DRAW_CONTEXT *context, UWORD type,
	GEM_FAR_ADDRESS spec, const GRECT *rectangle, WORD thickness)
{
	GEM_VDI_COLOR native;
	UWORD border;
	UWORD text;
	UWORD pattern;
	UWORD inside;
	GRECT interior;
	WORD positive_thickness;

	gem_menu_crack(spec.lo, &border, &text, &pattern, &inside);
	(void) text;
	if (!gem_menu_draw_border(context, rectangle, thickness, border))
		return FALSE;
	if (type == G_IBOX)
		return TRUE;

	positive_thickness = thickness < 0 ? 0 : thickness;
	interior = *rectangle;
	if (positive_thickness
	    && !gem_menu_inside(&interior, positive_thickness))
		return FALSE;

	/* ADMENU uses the original hollow white bar; solid is also exact. */
	if (pattern == GEM_MENU_IP_HOLLOW)
		return TRUE;
	if (pattern != GEM_MENU_IP_SOLID
	    || !gem_menu_native_color(inside, &native))
		return FALSE;
	gem_vdi_set_mode(GEM_VDI_REPLACE);
	gem_vdi_set_foreground(context->screen, native);
	gem_vdi_fill_rect(context->screen, interior.g_x, interior.g_y,
		interior.g_w, interior.g_h);
	return TRUE;
}

/*
 * Original LBWMOV() bound for ystate().  The scan remains in the RSC segment
 * and rejects a string without a NUL inside both the resource and 80-byte GEM
 * text limit.  underline receives the first '_' byte or 0xffff.
 */
static WORD
gem_menu_string_length(const GEM_MENU_DRAW_CONTEXT *context,
	GEM_FAR_ADDRESS string, UWORD *length, UWORD *underline)
{
	UBYTE __far *source;
	UWORD count;
	UWORD available;

	if (!context || !length || !underline
	    || string.hi != context->resource->storage.base.hi
	    || string.lo >= context->resource->storage.bytes)
		return FALSE;
	source = (UBYTE __far *) gem_menu_pointer(string);
	available = (UWORD) (context->resource->storage.bytes - string.lo);
	count = 0;
	*underline = GEM_MENU_NO_UNDERLINE;
	while (count < GEM_MENU_MAX_TEXT && count < available) {
		if (!*source) {
			*length = count;
			return TRUE;
		}
		if (*source == '_' && *underline == GEM_MENU_NO_UNDERLINE)
			*underline = count;
		source++;
		count++;
	}
	return FALSE;
}

/* G_TITLE/G_STRING portion of original ystate(), including '_' overlay. */
static WORD
gem_menu_draw_text(GEM_MENU_DRAW_CONTEXT *context, UWORD object_type,
	GEM_FAR_ADDRESS string, const GRECT *rectangle)
{
	GEM_FAR_ADDRESS remainder;
	UWORD length;
	UWORD underline;
	UWORD prefix;
	UWORD index;
	WORD x;
	WORD y;
	WORD vertical;

	if (!gem_menu_string_length(context, string, &length, &underline))
		return FALSE;
	if (!length)
		return TRUE;

	x = rectangle->g_x;
	y = rectangle->g_y;
	vertical = rectangle->g_h - (WORD) context->character_height;
	if (vertical > 0)
		y += vertical >> 1;

	/* A nonzero extended object type suppresses classic '_' handling. */
	if ((object_type >> 8) || underline == GEM_MENU_NO_UNDERLINE)
		return gem_vdi_resident_text(string, length, x, y,
			GEM_MENU_LOGICAL_BLACK);

	/*
	 * ystate first draws through '_', then overlays the remaining string one
	 * character cell to the left.  Accumulating cells avoids 8086 MUL.
	 */
	prefix = underline + 1U;
	if (!gem_vdi_resident_text(string, prefix, x, y,
					GEM_MENU_LOGICAL_BLACK))
		return FALSE;
	remainder = string;
	if (prefix > (UWORD) (0xffffU - remainder.lo))
		return FALSE;
	remainder.lo = (UWORD) (remainder.lo + prefix);
	if (prefix == length)
		return TRUE;
	index = 0;
	while (index < underline) {
		if (!gem_menu_add_coordinate(x, (WORD) context->character_width,
					&x))
			return FALSE;
		index++;
	}
	return gem_vdi_resident_text(remainder, (UWORD) (length - prefix),
		x, y, GEM_MENU_LOGICAL_BLACK);
}

static WORD
gem_menu_selected(GEM_MENU_DRAW_CONTEXT *context, UWORD state,
	const GRECT *rectangle, WORD thickness)
{
	GRECT interior;

	if (!(state & SELECTED))
		return TRUE;
	interior = *rectangle;
	if (thickness > 0 && !gem_menu_inside(&interior, thickness))
		return FALSE;
	/* Original xstate() applies positive thickness, then inverts the object. */
	gem_vdi_set_mode(GEM_VDI_XOR);
	gem_vdi_set_foreground(context->screen, GEM_MENU_NATIVE_WHITE);
	gem_vdi_fill_rect(context->screen, interior.g_x, interior.g_y,
		interior.g_w, interior.g_h);
	gem_vdi_set_mode(GEM_VDI_REPLACE);
	return TRUE;
}

/* Bounded G_BOX/G_IBOX/G_TITLE direct port of original just_draw(). */
static WORD
gem_menu_just_draw(GEM_MENU_DRAW_CONTEXT *context, WORD object_index,
	WORD screen_x, WORD screen_y)
{
	OBJECT __far *object;
	GEM_FAR_ADDRESS spec;
	GRECT rectangle;
	UWORD type;
	UWORD byte;
	WORD thickness;

	if (object_index < ROOT
	    || (UWORD) object_index >= context->object_count)
		return FALSE;
	object = gem_menu_object_at(context->tree, (UWORD) object_index);
	if (object->ob_flags & HIDETREE)
		return TRUE;
	if (!gem_menu_spec(context, object, &spec))
		return FALSE;
	if (spec.lo == 0xffffU && spec.hi == 0xffffU)
		return TRUE;

	rectangle.g_x = screen_x;
	rectangle.g_y = screen_y;
	rectangle.g_w = (WORD) object->ob_width;
	rectangle.g_h = (WORD) object->ob_height;
	if (rectangle.g_w <= 0 || rectangle.g_h <= 0)
		return FALSE;
	type = object->ob_type & 0x00ffU;
	if (type == G_TITLE) {
		/* Original ob_sst() assigns menu titles a one-pixel thickness. */
		thickness = 1;
	} else {
		byte = spec.hi & 0x00ffU;
		thickness = (WORD) byte;
		if (thickness > 127)
			thickness -= 256;
	}

	switch (type) {
	case G_BOX:
	case G_IBOX:
		if (!gem_menu_draw_box(context, type, spec, &rectangle,
				       thickness))
			return FALSE;
		break;
	case G_TITLE:
		if (!gem_menu_draw_text(context, object->ob_type, spec,
					&rectangle))
			return FALSE;
		break;
	default:
		/* THEBAR closure intentionally rejects unrelated object classes. */
		return FALSE;
	}
	return gem_menu_selected(context, object->ob_state, &rectangle,
		thickness);
}

/* Direct GEMOBJOP.C get_par(), with a local-tree step bound. */
static WORD
gem_menu_get_parent(GEM_MENU_DRAW_CONTEXT *context, WORD object_index,
	WORD *next_object, WORD *valid)
{
	OBJECT __far *object;
	WORD parent;
	WORD next;
	UWORD steps;

	*valid = FALSE;
	parent = object_index;
	next = NIL;
	if (object_index == ROOT) {
		*next_object = NIL;
		*valid = TRUE;
		return NIL;
	}
	steps = context->object_count;
	do {
		if (!steps || parent < ROOT
		    || (UWORD) parent >= context->object_count)
			return NIL;
		steps--;
		object_index = parent;
		object = gem_menu_object_at(context->tree,
			(UWORD) object_index);
		parent = object->ob_next;
		if (next == NIL)
			next = parent;
		if (parent < ROOT)
			break;
		if ((UWORD) parent >= context->object_count)
			return NIL;
		object = gem_menu_object_at(context->tree, (UWORD) parent);
	} while (object->ob_tail != object_index);
	*next_object = next;
	*valid = TRUE;
	return parent;
}

/* Direct GEMOBLIB.C ob_offset(), retaining the non-recursive parent walk. */
static WORD
gem_menu_object_offset(GEM_MENU_DRAW_CONTEXT *context, WORD object_index,
	WORD *x, WORD *y)
{
	OBJECT __far *object;
	WORD next;
	WORD valid;
	UWORD steps;

	*x = 0;
	*y = 0;
	steps = context->object_count;
	do {
		if (!steps || object_index < ROOT
		    || (UWORD) object_index >= context->object_count)
			return FALSE;
		steps--;
		object = gem_menu_object_at(context->tree,
			(UWORD) object_index);
		if (!gem_menu_add_coordinate(*x, (WORD) object->ob_x, x)
		    || !gem_menu_add_coordinate(*y, (WORD) object->ob_y, y))
			return FALSE;
		object_index = gem_menu_get_parent(context, object_index,
			&next, &valid);
		if (!valid)
			return FALSE;
	} while (object_index != NIL);
	return TRUE;
}

/*
 * Direct non-recursive GEMOBJOP.C everyobj().  x/y stacks are unscaled screen
 * pixels.  Ten entries cover ROOT plus original MAX_DEPTH eight without the
 * historical x[9] edge overrun.
 */
static WORD
gem_menu_every_object(GEM_MENU_DRAW_CONTEXT *context, WORD current,
	WORD last, WORD start_x, WORD start_y, UWORD maximum_depth)
{
	OBJECT __far *object;
	WORD x[GEM_MENU_COORD_STACK];
	WORD y[GEM_MENU_COORD_STACK];
	WORD next;
	UWORD depth;

	x[0] = start_x;
	y[0] = start_y;
	depth = 1U;

child:
	if (current == last)
		return TRUE;
	if (current < ROOT || (UWORD) current >= context->object_count
	    || depth >= GEM_MENU_COORD_STACK)
		return FALSE;
	object = gem_menu_object_at(context->tree, (UWORD) current);
	if (!gem_menu_add_coordinate(x[depth - 1U], (WORD) object->ob_x,
					&x[depth])
	    || !gem_menu_add_coordinate(y[depth - 1U], (WORD) object->ob_y,
					&y[depth]))
		return FALSE;
	if (!gem_menu_just_draw(context, current, x[depth], y[depth]))
		return FALSE;

	next = object->ob_head;
	if (next != NIL && !(object->ob_flags & HIDETREE)
	    && depth <= maximum_depth) {
		depth++;
		current = next;
		goto child;
	}

sibling:
	object = gem_menu_object_at(context->tree, (UWORD) current);
	next = object->ob_next;
	if (next == last || current == ROOT)
		return TRUE;
	if (next < ROOT || (UWORD) next >= context->object_count)
		return FALSE;
	object = gem_menu_object_at(context->tree, (UWORD) next);
	if (object->ob_tail != current) {
		current = next;
		goto child;
	}
	if (depth <= 1U)
		return FALSE;
	depth--;
	current = next;
	goto sibling;
}

/* Direct GEMOBLIB.C ob_draw() for the original THEBAR subtree. */
static WORD
gem_menu_object_draw(GEM_MENU_DRAW_CONTEXT *context, WORD object_index,
	UWORD depth)
{
	WORD parent;
	WORD last;
	WORD start_x;
	WORD start_y;
	WORD valid;

	parent = gem_menu_get_parent(context, object_index, &last, &valid);
	if (!valid)
		return FALSE;
	if (parent != NIL) {
		if (!gem_menu_object_offset(context, parent, &start_x, &start_y))
			return FALSE;
	} else {
		start_x = 0;
		start_y = 0;
	}
	return gem_menu_every_object(context, object_index, last,
		start_x, start_y, depth);
}

VOID
gem_menu_object_resident_init(GEM_MENU_OBJECT_RESIDENT *menu)
{
	if (!menu)
		return;
	menu->tree.lo = 0;
	menu->tree.hi = 0;
	menu->object_count = 0;
	menu->owner = -1;
	menu->generation_lo = 0;
	menu->generation_hi = 0;
	menu->visible = GEM_MENU_OBJECT_RESIDENT_HIDDEN;
}

VOID
gem_menu_object_resident_detach(GEM_MENU_OBJECT_RESIDENT *menu,
	WORD owner, UWORD generation_lo, UWORD generation_hi)
{
	if (!menu || menu->visible != GEM_MENU_OBJECT_RESIDENT_VISIBLE
	    || menu->owner != owner
	    || menu->generation_lo != generation_lo
	    || menu->generation_hi != generation_hi)
		return;
	gem_menu_object_resident_init(menu);
}

WORD
gem_menu_object_resident_bar(GEM_MENU_OBJECT_RESIDENT *menu,
	const GEM_RESOURCE_RESIDENT *resource, GEM_FAR_ADDRESS tree,
	UWORD show, WORD owner, UWORD generation_lo, UWORD generation_hi)
{
	GEM_MENU_DRAW_CONTEXT context;
	OBJECT __far *bar;
	WORD line_y;
	WORD result;

	if (!menu || owner < 0)
		return FALSE;
	if (!show) {
		if (menu->visible == GEM_MENU_OBJECT_RESIDENT_VISIBLE
		    && (menu->owner != owner
			|| menu->generation_lo != generation_lo
			|| menu->generation_hi != generation_hi))
			return FALSE;
		gem_menu_object_resident_init(menu);
		return TRUE;
	}

	if (!gem_menu_validate_tree(resource, tree, &context.object_count))
		return FALSE;
	context.resource = resource;
	context.tree = tree;
	context.character_width = resource->metrics.character_width;
	context.character_height = resource->metrics.character_height;
	context.screen = gem_vdi_resident_screen();
	if (!context.screen || !context.character_width
	    || !context.character_height
	    || resource->metrics.screen_width != (UWORD) context.screen->xres
	    || resource->metrics.screen_height != (UWORD) context.screen->yres)
		return FALSE;

	/* Original mn_bar(): THEBAR always reaches the right screen edge. */
	bar = gem_menu_object_at(tree, GEM_MENU_THEBAR);
	if ((WORD) bar->ob_x < 0
	    || bar->ob_x >= resource->metrics.screen_width)
		return FALSE;
	bar->ob_width = (UWORD) (resource->metrics.screen_width - bar->ob_x);

	/* Original gsx_sclip(gl_rzero), gsx_moff(), ob_draw(), gsx_mon(). */
	gem_vdi_set_clip(context.screen, 0, NULL);
	gem_vdi_hide_cursor(context.screen);
	result = gem_menu_object_draw(&context, GEM_MENU_THEBAR,
		GEM_MENU_MAX_DEPTH);
	if (result) {
		/* Original gl_hbox is gl_hchar + 3; draw its black lower rule. */
		if (context.character_height > 32765U)
			result = FALSE;
		else {
			line_y = (WORD) context.character_height + 2;
			gem_vdi_set_mode(GEM_VDI_REPLACE);
			gem_vdi_set_foreground(context.screen,
				GEM_MENU_NATIVE_BLACK);
			gem_vdi_line(context.screen, 0, line_y,
				context.screen->xres - 1, line_y, TRUE);
		}
	}
	gem_vdi_show_cursor(context.screen);
	if (!result)
		return FALSE;
	gem_vdi_flush(context.screen);

	menu->tree = tree;
	menu->object_count = context.object_count;
	menu->owner = owner;
	menu->generation_lo = generation_lo;
	menu->generation_hi = generation_hi;
	menu->visible = GEM_MENU_OBJECT_RESIDENT_VISIBLE;
	return TRUE;
}
