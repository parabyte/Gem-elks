/*
 * gem_object_resident.c - original GEM object manager in resident AES.
 *
 * This is a bounded, direct 16-bit port of the GPL-released Digital Research
 * GEMOBJOP.C everyobj()/get_par(), GEMOBLIB.C ob_add(), ob_draw(), ob_find(),
 * ob_offset(), ob_order(), ob_change(), just_draw(), and XSTATE.C state
 * rendering.  PPDG000.C and GEMSUPER.C supply the unchanged AES array
 * contracts.
 *
 * ELKS adaptations are confined to the memory and display boundaries.  The
 * kernel-pinned client DS replaces GEM/XM's shared DOS arena; an original RSC
 * segment remains a second permitted segment; and the resident native VDI
 * replaces process-local GSX arrays.  OBJECT, TEDINFO, BITBLK, ICONBLK,
 * strings, and bitmap words are interpreted in place.  Bitmap rows are
 * streamed in bounded groups through 64 near words only because the native
 * VDI driver takes a near source pointer; no persistent or converted asset is
 * created.  A complete 32-by-32 icon fits in those 128 transient bytes, which
 * avoids repeated VGA plane setup on the 8088 while retaining the exact
 * original bitmap representation.
 *
 * All target arithmetic is byte or word arithmetic.  Pixel coordinates have
 * scale one and intentionally wrap modulo 65536 during parent accumulation,
 * as on the original 8086.  Pointer ranges use subtraction before addition,
 * so an offset wrap is rejected.  No multiplication, division, recursion,
 * wide scalar, heap allocation, or floating point exists in this module.
 */

#include "gem_object_resident.h"

#if defined(ELKS) && ELKS
#include "gem_vdi_resident.h"
#define GEM_OBJECT_FAR __far
#else
#define GEM_OBJECT_FAR
#endif

#define GEM_OBJECT_BYTES              24U
#define GEM_OBJECT_HEADER_BYTES       36U
#define GEM_OBJECT_MAX_DEPTH          8U
#define GEM_OBJECT_COORD_STACK        10U
#define GEM_OBJECT_MAX_TEXT           80U
#define GEM_OBJECT_NO_UNDERLINE       0xffffU
#define GEM_OBJECT_BITMAP_ROW_WORDS   8U
#define GEM_OBJECT_BITMAP_BUFFER_WORDS 64U
#define GEM_OBJECT_BITMAP_WIDTH       128U
#define GEM_OBJECT_LOGICAL_WHITE      0U
#define GEM_OBJECT_LOGICAL_BLACK      1U
#define GEM_OBJECT_IP_HOLLOW          0U
#define GEM_OBJECT_IP_SOLID           7U
#define GEM_OBJECT_TE_LEFT            0
#define GEM_OBJECT_TE_RIGHT           1
#define GEM_OBJECT_TE_CNTR            2
#define GEM_OBJECT_FLAG3D             0x1000U

/* Eight original GEM fill densities, indexed by the three-bit pattern. */
static const UBYTE gem_object_patterns[8][8] = {
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	{ 0x80, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40 },
	{ 0x88, 0x44, 0x22, 0x11, 0x88, 0x44, 0x22, 0x11 },
	{ 0x88, 0x22, 0x88, 0x22, 0x88, 0x22, 0x88, 0x22 },
	{ 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55 },
	{ 0x77, 0xee, 0xdd, 0xbb, 0x77, 0xee, 0xdd, 0xbb },
	{ 0x77, 0xbb, 0xdd, 0xee, 0x77, 0xbb, 0xdd, 0xee },
	{ 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff }
};

/* Original AES used fixed global formatting buffers for its serial manager. */
static UBYTE gem_object_raw[GEM_OBJECT_MAX_TEXT];
static UBYTE gem_object_template[GEM_OBJECT_MAX_TEXT];
static UBYTE gem_object_formatted[GEM_OBJECT_MAX_TEXT];
static GEM_VDI_BITS gem_object_bitmap_row[GEM_OBJECT_BITMAP_BUFFER_WORDS];

/*
 * Number of complete rows which fit in the fixed 64-word scratch buffer.
 * Index zero is invalid.  This small table replaces an expensive 8086 divide;
 * every result is floor(64 / words_per_row).  The widest permitted 128-pixel
 * form still advances eight complete rows per native VDI call.
 */
static const UBYTE gem_object_bitmap_rows[9] = {
	0, 64, 32, 21, 16, 12, 10, 9, 8
};

typedef struct gem_object_context {
	const GEM_RESOURCE_RESIDENT *resource;
	GEM_FAR_ADDRESS tree;
	OBJECT GEM_OBJECT_FAR *objects;
	UWORD object_count;
	UWORD client_segment;
	UWORD client_limit;
	UWORD resident_segment;
	UWORD character_width;
	UWORD character_height;
	GEM_VDI_SCREEN *screen;
} GEM_OBJECT_CONTEXT;

#if defined(ELKS) && ELKS
/* A far data pointer is exactly the original offset:segment pair on ia16. */
typedef union gem_object_far_pointer {
	VOID GEM_OBJECT_FAR *pointer;
	UBYTE GEM_OBJECT_FAR *bytes;
	UWORD GEM_OBJECT_FAR *words;
	OBJECT GEM_OBJECT_FAR *object;
	TEDINFO GEM_OBJECT_FAR *tedinfo;
	ICONBLK GEM_OBJECT_FAR *iconblk;
	BITBLK GEM_OBJECT_FAR *bitblk;
	RSHDR GEM_OBJECT_FAR *header;
	GEM_FAR_ADDRESS address;
} GEM_OBJECT_FAR_POINTER;

typedef BYTE GEM_OBJECT_FAR_POINTER_MUST_BE_4_BYTES
	[(sizeof(GEM_OBJECT_FAR_POINTER) == 4) ? 1 : -1];
#endif

static WORD
gem_object_range(UWORD offset, UWORD count, UWORD limit)
{
	if (offset > limit)
		return FALSE;
	return count <= (UWORD) (limit - offset);
}

/* Return the exact remaining bytes in one permitted segment. */
static WORD
gem_object_available(const GEM_OBJECT_CONTEXT *context,
	GEM_FAR_ADDRESS address, UWORD *available)
{
	if (!context || !available || !address.hi)
		return FALSE;
	if (context->resource
	    && (context->resource->flags & GEM_RESOURCE_RESIDENT_LOADED)
	    && address.hi == context->resource->storage.base.hi
	    && address.lo <= context->resource->storage.bytes) {
		*available = (UWORD) (context->resource->storage.bytes
			- address.lo);
		return TRUE;
	}
	if (address.hi == context->client_segment
	    && address.lo <= context->client_limit) {
		*available = (UWORD) (context->client_limit - address.lo);
		return TRUE;
	}
	/*
	 * Only a synthetic call made inside the resident AES sets this word.
	 * ffff is the largest exclusive boundary representable by one 8086
	 * word, so the final byte is deliberately inaccessible.  W_ACTIVE and
	 * its shared TEDINFO records are far below that boundary.
	 */
	if (context->resident_segment
	    && address.hi == context->resident_segment) {
		*available = (UWORD) (0xffffU - address.lo);
		return TRUE;
	}
	return FALSE;
}

/*
 * Form a far pointer only after a range is proven.  The host branch maps the
 * synthetic resource segment used by the native smoke and is not compiled
 * into the ELKS target.
 */
static VOID GEM_OBJECT_FAR *
gem_object_pointer(const GEM_OBJECT_CONTEXT *context,
	GEM_FAR_ADDRESS address, UWORD count)
{
	UWORD available;

	if (!gem_object_available(context, address, &available)
	    || count > available)
		return (VOID GEM_OBJECT_FAR *) 0;
#if defined(ELKS) && ELKS
	{
		GEM_OBJECT_FAR_POINTER pointer;

		pointer.address.lo = address.lo;
		pointer.address.hi = address.hi;
		return pointer.pointer;
	}
#else
	if (context->resource && context->resource->host_bytes
	    && address.hi == context->resource->storage.base.hi)
		return context->resource->host_bytes + address.lo;
	return (VOID GEM_OBJECT_FAR *) 0;
#endif
}

/* Add signed coordinates with the original 8086 modulo-word result. */
static WORD
gem_object_add(WORD left, WORD right)
{
	return (WORD) ((UWORD) left + (UWORD) right);
}

static WORD
gem_object_link_valid(WORD link, UWORD count)
{
	return link == NIL || (link >= ROOT && (UWORD) link < count);
}

/* Pointer increments preserve tree indexing without a structure-size MUL. */
static OBJECT GEM_OBJECT_FAR *
gem_object_at(const GEM_OBJECT_CONTEXT *context, UWORD index)
{
	OBJECT GEM_OBJECT_FAR *object;

	object = context->objects;
	while (index) {
		object++;
		index--;
	}
	return object;
}

/*
 * Locate the local LASTOB extent.  RSC trees must begin on an exact record in
 * the loader-validated object table.  Application-built trees may begin at
 * any aligned address in the pinned client DS and are bounded by data_limit.
 */
static WORD
gem_object_open_tree(GEM_OBJECT_CONTEXT *context)
{
	OBJECT GEM_OBJECT_FAR *object;
	GEM_FAR_ADDRESS address;
	UWORD available;
	UWORD remaining;
	UWORD count;
	UWORD offset;
	UWORD index;
	UBYTE found;
	UBYTE resource_tree;

	if (!context || !context->tree.hi || (context->tree.lo & 1U))
		return FALSE;

	remaining = 0;
	resource_tree = FALSE;
	if (context->resource
	    && (context->resource->flags & GEM_RESOURCE_RESIDENT_LOADED)
	    && context->tree.hi == context->resource->storage.base.hi) {
		RSHDR GEM_OBJECT_FAR *header;

		address = context->resource->storage.base;
		header = (RSHDR GEM_OBJECT_FAR *) gem_object_pointer(context,
			address, GEM_OBJECT_HEADER_BYTES);
		if (!header || header->rsh_rssize != context->resource->storage.bytes
		    || !header->rsh_nobs
		    || !gem_object_range(header->rsh_object,
			GEM_OBJECT_BYTES, context->resource->storage.bytes))
			return FALSE;
		offset = header->rsh_object;
		remaining = header->rsh_nobs;
		while (remaining && offset != context->tree.lo) {
			if (!gem_object_range(offset, GEM_OBJECT_BYTES,
				context->resource->storage.bytes)
			    || offset > (UWORD) (0xffffU - GEM_OBJECT_BYTES))
				return FALSE;
			offset = (UWORD) (offset + GEM_OBJECT_BYTES);
			remaining--;
		}
		if (!remaining || offset != context->tree.lo)
			return FALSE;
		resource_tree = TRUE;
	} else {
		if (context->tree.hi != context->client_segment
		    && context->tree.hi != context->resident_segment)
			return FALSE;
		if (!gem_object_available(context, context->tree, &available))
			return FALSE;
	}

	context->objects = (OBJECT GEM_OBJECT_FAR *) gem_object_pointer(context,
		context->tree, GEM_OBJECT_BYTES);
	if (!context->objects)
		return FALSE;

	object = context->objects;
	count = 0;
	found = FALSE;
	while ((resource_tree && remaining)
	       || (!resource_tree && available >= GEM_OBJECT_BYTES)) {
		count++;
		if (object->ob_flags & LASTOB) {
			found = TRUE;
			break;
		}
		object++;
		if (resource_tree)
			remaining--;
		else
			available = (UWORD) (available - GEM_OBJECT_BYTES);
	}
	if (!count || !found)
		return FALSE;

	object = context->objects;
	index = 0;
	while (index < count) {
		if (!gem_object_link_valid(object->ob_next, count)
		    || !gem_object_link_valid(object->ob_head, count)
		    || !gem_object_link_valid(object->ob_tail, count))
			return FALSE;
		object++;
		index++;
	}
	context->object_count = count;
	return TRUE;
}

/* Direct GEMOBJOP.C get_par(), guarded by one local-tree traversal bound. */
static WORD
gem_object_parent(const GEM_OBJECT_CONTEXT *context, WORD object_index,
	WORD *next_object, WORD *valid)
{
	OBJECT GEM_OBJECT_FAR *object;
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
		object = gem_object_at(context, (UWORD) object_index);
		parent = object->ob_next;
		if (next == NIL)
			next = parent;
		if (parent < ROOT)
			break;
		if ((UWORD) parent >= context->object_count)
			return NIL;
		object = gem_object_at(context, (UWORD) parent);
	} while (object->ob_tail != object_index);
	*next_object = next;
	*valid = TRUE;
	return parent;
}

/* Direct GEMOBLIB.C ob_offset() with explicit modulo-word accumulation. */
static WORD
gem_object_offset(const GEM_OBJECT_CONTEXT *context, WORD object_index,
	WORD *x, WORD *y)
{
	OBJECT GEM_OBJECT_FAR *object;
	WORD junk;
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
		object = gem_object_at(context, (UWORD) object_index);
		*x = gem_object_add(*x, (WORD) object->ob_x);
		*y = gem_object_add(*y, (WORD) object->ob_y);
		object_index = gem_object_parent(context, object_index,
			&junk, &valid);
		if (!valid)
			return FALSE;
	} while (object_index != NIL);
	return TRUE;
}

/* Direct GEMOBLIB.C get_prev(), bounded against damaged sibling cycles. */
static WORD
gem_object_previous(const GEM_OBJECT_CONTEXT *context, WORD parent,
	WORD object_index)
{
	OBJECT GEM_OBJECT_FAR *object;
	WORD previous;
	WORD next;
	UWORD steps;

	if (parent < ROOT || (UWORD) parent >= context->object_count)
		return NIL;
	object = gem_object_at(context, (UWORD) parent);
	previous = object->ob_head;
	if (previous == object_index)
		return NIL;
	steps = context->object_count;
	while (steps && previous >= ROOT
	       && (UWORD) previous < context->object_count) {
		steps--;
		object = gem_object_at(context, (UWORD) previous);
		next = object->ob_next;
		if (next == object_index)
			return previous;
		if (next == parent)
			break;
		previous = next;
	}
	return NIL;
}

/*
 * Validate one original parent-terminated sibling list and return its count.
 * GEMOBLIB.C uses the parent index, rather than NIL, after the tail child.
 * The bound is the already validated local LASTOB extent, so a damaged cycle
 * cannot turn either OBJC_ADD or OBJC_ORDER into an unbounded 8086 walk.
 */
static WORD
gem_object_children(const GEM_OBJECT_CONTEXT *context, WORD parent,
	UWORD *count)
{
	OBJECT GEM_OBJECT_FAR *parent_object;
	OBJECT GEM_OBJECT_FAR *object;
	WORD current;
	WORD previous;
	UWORD steps;

	if (!count || parent < ROOT
	    || (UWORD) parent >= context->object_count)
		return FALSE;
	parent_object = gem_object_at(context, (UWORD) parent);
	if (parent_object->ob_head == NIL || parent_object->ob_tail == NIL) {
		if (parent_object->ob_head != NIL
		    || parent_object->ob_tail != NIL)
			return FALSE;
		*count = 0;
		return TRUE;
	}

	current = parent_object->ob_head;
	previous = NIL;
	steps = context->object_count;
	*count = 0;
	while (current != parent) {
		if (!steps || current < ROOT
		    || (UWORD) current >= context->object_count)
			return FALSE;
		steps--;
		previous = current;
		object = gem_object_at(context, (UWORD) current);
		current = object->ob_next;
		(*count)++;
	}
	return previous == parent_object->ob_tail;
}

/* Direct bounded port of GEMOBLIB.C ob_add(). */
static WORD
gem_object_add_child(GEM_OBJECT_CONTEXT *context, WORD parent, WORD child)
{
	OBJECT GEM_OBJECT_FAR *parent_object;
	OBJECT GEM_OBJECT_FAR *child_object;
	OBJECT GEM_OBJECT_FAR *tail_object;
	WORD last_child;
	UWORD child_count;

	if (parent < ROOT || child <= ROOT || parent == child
	    || (UWORD) parent >= context->object_count
	    || (UWORD) child >= context->object_count
	    || !gem_object_children(context, parent, &child_count))
		return FALSE;
	parent_object = gem_object_at(context, (UWORD) parent);
	child_object = gem_object_at(context, (UWORD) child);
	/* Original callers mark unused objects with ob_next equal to NIL. */
	if (child_object->ob_next != NIL)
		return FALSE;

	child_object->ob_next = parent;
	last_child = parent_object->ob_tail;
	if (!child_count)
		parent_object->ob_head = child;
	else {
		tail_object = gem_object_at(context, (UWORD) last_child);
		tail_object->ob_next = child;
	}
	parent_object->ob_tail = child;
	return TRUE;
}

/*
 * Direct bounded port of GEMOBLIB.C ob_delete() plus ob_order().  Position
 * zero is the head and NIL is the tail.  A nonnegative position is checked
 * before unlinking, so malformed client input cannot leave a half-mutated
 * tree.  Reordering an only child is the natural no-op missing from the
 * original routine's unchecked single-child edge case.
 */
static WORD
gem_object_order(GEM_OBJECT_CONTEXT *context, WORD moved, WORD new_position)
{
	OBJECT GEM_OBJECT_FAR *parent_object;
	OBJECT GEM_OBJECT_FAR *moved_object;
	OBJECT GEM_OBJECT_FAR *object;
	WORD parent;
	WORD next;
	WORD previous;
	WORD current;
	WORD valid;
	UWORD child_count;
	UWORD position;

	if (moved <= ROOT || (UWORD) moved >= context->object_count)
		return FALSE;
	parent = gem_object_parent(context, moved, &next, &valid);
	if (!valid || parent < ROOT
	    || !gem_object_children(context, parent, &child_count)
	    || !child_count)
		return FALSE;
	if (new_position != NIL
	    && (new_position < 0 || (UWORD) new_position >= child_count))
		return FALSE;

	parent_object = gem_object_at(context, (UWORD) parent);
	moved_object = gem_object_at(context, (UWORD) moved);
	if (child_count == 1U) {
		if (parent_object->ob_head != moved
		    || parent_object->ob_tail != moved)
			return FALSE;
		moved_object->ob_next = parent;
		return TRUE;
	}

	/* Original ob_delete(): splice the object out of its sibling list. */
	previous = NIL;
	if (parent_object->ob_head == moved)
		parent_object->ob_head = next;
	else {
		previous = gem_object_previous(context, parent, moved);
		if (previous == NIL)
			return FALSE;
		object = gem_object_at(context, (UWORD) previous);
		object->ob_next = next;
	}
	if (parent_object->ob_tail == moved) {
		if (previous == NIL)
			return FALSE;
		parent_object->ob_tail = previous;
	}

	/* Original ob_order(): insert at head, tail, or after position - 1. */
	if (new_position == 0) {
		moved_object->ob_next = parent_object->ob_head;
		parent_object->ob_head = moved;
		return TRUE;
	}
	if (new_position == NIL)
		current = parent_object->ob_tail;
	else {
		current = parent_object->ob_head;
		position = 1U;
		while (position < (UWORD) new_position) {
			object = gem_object_at(context, (UWORD) current);
			current = object->ob_next;
			position++;
		}
	}
	object = gem_object_at(context, (UWORD) current);
	moved_object->ob_next = object->ob_next;
	object->ob_next = moved;
	if (moved_object->ob_next == parent)
		parent_object->ob_tail = moved;
	return TRUE;
}

/* Resolve an original direct or INDIRECT four-byte ob_spec. */
static WORD
gem_object_spec(const GEM_OBJECT_CONTEXT *context,
	const OBJECT GEM_OBJECT_FAR *object, GEM_FAR_ADDRESS *spec)
{
	UWORD GEM_OBJECT_FAR *words;

	spec->lo = object->ob_spec.lo;
	spec->hi = object->ob_spec.hi;
	if (!(object->ob_flags & INDIRECT))
		return TRUE;
	words = (UWORD GEM_OBJECT_FAR *) gem_object_pointer(context, *spec, 4U);
	if (!words)
		return FALSE;
	spec->lo = words[0];
	spec->hi = words[1];
	return TRUE;
}

/* Map original GEM logical color indexes to the resident PC driver's EGA. */
static GEM_VDI_COLOR
gem_object_native_color(UWORD logical)
{
	switch (logical & 15U) {
	case 0: return 15U;
	case 1: return 0U;
	case 2: return 4U;
	case 3: return 2U;
	case 4: return 1U;
	case 5: return 3U;
	case 6: return 14U;
	case 7: return 5U;
	case 8: return 7U;
	case 9: return 8U;
	case 10: return 12U;
	case 11: return 10U;
	case 12: return 9U;
	case 13: return 11U;
	case 14: return 6U;
	default: return 13U;
	}
}

/* Direct GEMGRAF.C gr_crack() for a packed low-word box color. */
static VOID
gem_object_crack(UWORD color, UWORD *border, UWORD *text,
	UWORD *pattern, UWORD *inside, WORD *replace)
{
	UWORD high_byte;
	UWORD low_byte;

	high_byte = color >> 8;
	low_byte = color & 0x00ffU;
	*border = (high_byte >> 4) & 15U;
	*text = high_byte & 15U;
	*pattern = (low_byte >> 4) & 15U;
	*inside = low_byte & 15U;
	*replace = (*pattern & 8U) != 0;
	*pattern &= 7U;
}

static WORD
gem_object_inside_rectangle(GEM_VDI_RECT *rectangle, WORD thickness)
{
	WORD twice;

	twice = gem_object_add(thickness, thickness);
	rectangle->x = gem_object_add(rectangle->x, thickness);
	rectangle->y = gem_object_add(rectangle->y, thickness);
	rectangle->width = gem_object_add(rectangle->width, (WORD) (0U
		- (UWORD) twice));
	rectangle->height = gem_object_add(rectangle->height, (WORD) (0U
		- (UWORD) twice));
	return rectangle->width > 0 && rectangle->height > 0;
}

/* Original gr_box() inward/outward thickness loop. */
static WORD
gem_object_border(GEM_OBJECT_CONTEXT *context,
	const GEM_VDI_RECT *rectangle, WORD thickness, UWORD color)
{
	GEM_VDI_RECT current;
	WORD step;

	if (!thickness)
		return TRUE;
	gem_vdi_set_mode(GEM_VDI_REPLACE);
	gem_vdi_set_foreground(context->screen, gem_object_native_color(color));
	if (thickness < 0)
		thickness--;
	do {
		step = thickness > 0 ? -1 : 1;
		thickness += step;
		current = *rectangle;
		if (!gem_object_inside_rectangle(&current, thickness))
			return FALSE;
		gem_vdi_rect(context->screen, current.x, current.y,
			current.width, current.height);
	} while (thickness);
	return TRUE;
}

static WORD
gem_object_fill(GEM_OBJECT_CONTEXT *context,
	const GEM_VDI_RECT *rectangle, UWORD color, UWORD pattern)
{
	if (rectangle->width <= 0 || rectangle->height <= 0)
		return FALSE;
	if (pattern == GEM_OBJECT_IP_HOLLOW)
		return TRUE;
	gem_vdi_set_mode(GEM_VDI_REPLACE);
	gem_vdi_set_foreground(context->screen,
		gem_object_native_color(color));
	if (pattern == GEM_OBJECT_IP_SOLID) {
		gem_vdi_fill_rect(context->screen, rectangle->x, rectangle->y,
			rectangle->width, rectangle->height);
		return TRUE;
	}
	if (pattern >= 8U)
		return FALSE;
	gem_vdi_set_background(context->screen,
		gem_object_native_color(GEM_OBJECT_LOGICAL_WHITE));
	gem_vdi_set_use_background(TRUE);
	gem_vdi_fill_pattern(context->screen, rectangle->x, rectangle->y,
		rectangle->width, rectangle->height,
		gem_object_patterns[pattern]);
	return TRUE;
}

static WORD
gem_object_string(const GEM_OBJECT_CONTEXT *context,
	GEM_FAR_ADDRESS address, UWORD maximum, UWORD *length,
	UWORD *underline)
{
	UBYTE GEM_OBJECT_FAR *source;
	UWORD available;
	UWORD count;

	if (!maximum || maximum > GEM_OBJECT_MAX_TEXT
	    || !gem_object_available(context, address, &available))
		return FALSE;
	source = (UBYTE GEM_OBJECT_FAR *) gem_object_pointer(context,
		address, 1U);
	if (!source)
		return FALSE;
	count = 0;
	*underline = GEM_OBJECT_NO_UNDERLINE;
	while (count < maximum && count < available) {
		if (!*source) {
			*length = count;
			return TRUE;
		}
		if (*source == '_' && *underline == GEM_OBJECT_NO_UNDERLINE)
			*underline = count;
		source++;
		count++;
	}
	return FALSE;
}

static WORD
gem_object_copy_string(const GEM_OBJECT_CONTEXT *context,
	GEM_FAR_ADDRESS address, UBYTE *destination, UWORD *length)
{
	UBYTE GEM_OBJECT_FAR *source;
	UWORD underline;
	UWORD count;

	if (!gem_object_string(context, address, GEM_OBJECT_MAX_TEXT - 1U,
		length, &underline))
		return FALSE;
	source = (UBYTE GEM_OBJECT_FAR *) gem_object_pointer(context,
		address, (UWORD) (*length + 1U));
	if (!source)
		return FALSE;
	count = *length;
	while (count) {
		*destination++ = *source++;
		count--;
	}
	*destination = 0;
	return TRUE;
}

/* Build a far slot for the original AES near formatting buffer. */
static GEM_BINDINGS_POINTER_SLOT
gem_object_near_slot(UBYTE *buffer)
{
	GEM_BINDINGS_POINTER_SLOT slot;

#if defined(ELKS) && ELKS
	slot.lo = (UWORD) buffer;
	__asm__ volatile ("movw %%ds,%0" : "=r" (slot.hi));
#else
	(void) buffer;
	slot.lo = 0;
	slot.hi = 0xfffeU;
#endif
	return slot;
}

/* Sum character cells with additions only; the 80-cell bound prevents wrap. */
static WORD
gem_object_text_pixels(const GEM_OBJECT_CONTEXT *context, UWORD characters)
{
	WORD pixels;

	pixels = 0;
	while (characters) {
		pixels = gem_object_add(pixels, (WORD) context->character_width);
		characters--;
	}
	return pixels;
}

static WORD
gem_object_text(GEM_OBJECT_CONTEXT *context, UWORD object_type,
	GEM_FAR_ADDRESS string, UWORD length, UWORD underline,
	const GEM_VDI_RECT *rectangle, WORD justification, UWORD color)
{
	GEM_FAR_ADDRESS remainder;
	UWORD prefix;
	UWORD index;
	WORD width;
	WORD difference;
	WORD x;
	WORD y;

	if (!length)
		return TRUE;
	width = gem_object_text_pixels(context, length);
	x = rectangle->x;
	y = rectangle->y;
	difference = rectangle->height - (WORD) context->character_height;
	if (difference > 0)
		y = gem_object_add(y, (WORD) ((difference + 1) >> 1));
	difference = rectangle->width - width;
	if (difference > 0) {
		if (justification == GEM_OBJECT_TE_CNTR)
			x = gem_object_add(x, (WORD) ((difference + 1) >> 1));
		else if (justification == GEM_OBJECT_TE_RIGHT)
			x = gem_object_add(x, difference);
	}

	if ((object_type >> 8) || underline == GEM_OBJECT_NO_UNDERLINE)
		return gem_vdi_resident_text(string, length, x, y, (WORD) color);

	/* Direct XSTATE.C underscore overlay without a temporary converted text. */
	prefix = underline + 1U;
	if (!gem_vdi_resident_text(string, prefix, x, y, (WORD) color))
		return FALSE;
	if (prefix == length || prefix > (UWORD) (0xffffU - string.lo))
		return prefix == length;
	remainder = string;
	remainder.lo = (UWORD) (remainder.lo + prefix);
	index = 0;
	while (index < underline) {
		x = gem_object_add(x, (WORD) context->character_width);
		index++;
	}
	return gem_vdi_resident_text(remainder, (UWORD) (length - prefix),
		x, y, (WORD) color);
}

/* Direct GEMOBLIB.C ob_format() into AES's fixed serial formatting buffer. */
static WORD
gem_object_format(GEM_OBJECT_CONTEXT *context, WORD justification,
	GEM_FAR_ADDRESS raw_address, GEM_FAR_ADDRESS template_address,
	GEM_BINDINGS_POINTER_SLOT *formatted, UWORD *formatted_length)
{
	UWORD raw_length;
	UWORD template_length;
	UWORD raw_index;
	UWORD template_index;
	UWORD count;

	if (!gem_object_copy_string(context, raw_address, gem_object_raw,
		&raw_length)
	    || !gem_object_copy_string(context, template_address,
		gem_object_template, &template_length))
		return FALSE;
	if (gem_object_raw[0] == '@') {
		gem_object_raw[0] = 0;
		raw_length = 0;
	}
	if (template_length >= GEM_OBJECT_MAX_TEXT)
		return FALSE;

	count = 0;
	while (count < template_length) {
		gem_object_formatted[count] = gem_object_template[count];
		count++;
	}
	gem_object_formatted[template_length] = 0;

	if (justification == GEM_OBJECT_TE_RIGHT) {
		raw_index = raw_length;
		template_index = template_length;
		while (template_index) {
			template_index--;
			if (gem_object_template[template_index] == '_'
			    && raw_index) {
				raw_index--;
				gem_object_formatted[template_index] =
					gem_object_raw[raw_index];
			}
		}
	} else {
		raw_index = 0;
		template_index = 0;
		while (template_index < template_length) {
			if (gem_object_template[template_index] == '_'
			    && raw_index < raw_length) {
				gem_object_formatted[template_index] =
					gem_object_raw[raw_index];
				raw_index++;
			}
			template_index++;
		}
	}
	*formatted = gem_object_near_slot(gem_object_formatted);
	*formatted_length = template_length;
	return TRUE;
}

static WORD
gem_object_draw_ted(GEM_OBJECT_CONTEXT *context, GEM_FAR_ADDRESS spec,
	UWORD object_type, const GEM_VDI_RECT *rectangle, WORD *thickness)
{
	TEDINFO GEM_OBJECT_FAR *source;
	TEDINFO ted;
	GEM_BINDINGS_POINTER_SLOT string;
	UWORD length;
	UWORD underline;
	UWORD border;
	UWORD text;
	UWORD pattern;
	UWORD inside;
	WORD replace;
	GEM_VDI_RECT interior;

	source = (TEDINFO GEM_OBJECT_FAR *) gem_object_pointer(context, spec,
		(UWORD) sizeof(TEDINFO));
	if (!source)
		return FALSE;
	/* Seven exact word-pair/word groups avoid a general far memcpy helper. */
	ted.te_ptext.lo = source->te_ptext.lo;
	ted.te_ptext.hi = source->te_ptext.hi;
	ted.te_ptmplt.lo = source->te_ptmplt.lo;
	ted.te_ptmplt.hi = source->te_ptmplt.hi;
	ted.te_pvalid.lo = source->te_pvalid.lo;
	ted.te_pvalid.hi = source->te_pvalid.hi;
	ted.te_font = source->te_font;
	ted.te_junk1 = source->te_junk1;
	ted.te_just = source->te_just;
	ted.te_color = source->te_color;
	ted.te_junk2 = source->te_junk2;
	ted.te_thickness = source->te_thickness;
	ted.te_txtlen = source->te_txtlen;
	ted.te_tmplen = source->te_tmplen;
	*thickness = ted.te_thickness;
	gem_object_crack((UWORD) ted.te_color, &border, &text, &pattern,
		&inside, &replace);
	(void) replace;

	interior = *rectangle;
	if ((object_type == G_BOXTEXT || object_type == G_FBOXTEXT)
	    && (!gem_object_border(context, rectangle, *thickness, border)
		|| (*thickness > 0
		    && !gem_object_inside_rectangle(&interior, *thickness))
		|| !gem_object_fill(context, &interior, inside, pattern)))
		return FALSE;

	if (object_type == G_FTEXT || object_type == G_FBOXTEXT) {
		if (!gem_object_format(context, ted.te_just, ted.te_ptext,
			ted.te_ptmplt, &string, &length))
			return FALSE;
		underline = GEM_OBJECT_NO_UNDERLINE;
	} else {
		string = ted.te_ptext;
		if (!gem_object_string(context, string, GEM_OBJECT_MAX_TEXT,
			&length, &underline))
			return FALSE;
		underline = GEM_OBJECT_NO_UNDERLINE;
	}
	return gem_object_text(context, object_type, string, length, underline,
		&interior, ted.te_just, text);
}

/*
 * Stream original one-bit words in the largest complete-row groups which fit
 * in the fixed 64-word scratch.  Source word ordering and bit 15 leftmost
 * semantics are unchanged.  A normal 32-pixel GEM icon uses two words per
 * row, so all 32 rows cross the near-pointer VDI boundary at once.  This
 * removes 31 of its 32 cursor and VGA-plane setup calls for each mask or data
 * form without converting the asset or using multiply and divide helpers.
 */
static WORD
gem_object_far_bitmap(GEM_OBJECT_CONTEXT *context,
	GEM_FAR_ADDRESS source_address, WORD x, WORD y, UWORD width,
	UWORD height, UWORD foreground, UWORD background, WORD transparent)
{
	UWORD GEM_OBJECT_FAR *source;
	UWORD words_per_row;
	UWORD row_bytes;
	UWORD rows_per_group;
	UWORD group_rows;
	UWORD copied_rows;
	UWORD buffer_word;
	UWORD remaining;
	UWORD row;
	UWORD word;

	if (!width || width > GEM_OBJECT_BITMAP_WIDTH || !height
	    || width > (UWORD) (0xffffU - 15U))
		return FALSE;
	words_per_row = (UWORD) ((width + 15U) >> 4);
	if (!words_per_row || words_per_row > GEM_OBJECT_BITMAP_ROW_WORDS)
		return FALSE;
	row_bytes = (UWORD) (words_per_row << 1);
	rows_per_group = gem_object_bitmap_rows[words_per_row];
	gem_vdi_set_mode(GEM_VDI_REPLACE);
	gem_vdi_set_foreground(context->screen,
		gem_object_native_color(foreground));
	gem_vdi_set_background(context->screen,
		gem_object_native_color(background));
	gem_vdi_set_use_background(!transparent);
	row = 0;
	remaining = height;
	while (remaining) {
		group_rows = rows_per_group;
		if (group_rows > remaining)
			group_rows = remaining;
		copied_rows = 0;
		buffer_word = 0;
		while (copied_rows < group_rows) {
			source = (UWORD GEM_OBJECT_FAR *) gem_object_pointer(context,
				source_address, row_bytes);
			if (!source)
				return FALSE;
			word = 0;
			while (word < words_per_row) {
				gem_object_bitmap_row[buffer_word] = source[word];
				buffer_word++;
				word++;
			}
			remaining--;
			copied_rows++;
			if (remaining) {
				if (row_bytes
				    > (UWORD) (0xffffU - source_address.lo))
					return FALSE;
				source_address.lo = (UWORD) (source_address.lo
					+ row_bytes);
			}
		}
		gem_vdi_bitmap(context->screen, x, gem_object_add(y, (WORD) row),
			(WORD) width, (WORD) group_rows, gem_object_bitmap_row);
		row = (UWORD) (row + group_rows);
	}
	return TRUE;
}

static WORD
gem_object_draw_image(GEM_OBJECT_CONTEXT *context, GEM_FAR_ADDRESS spec,
	const GEM_VDI_RECT *rectangle)
{
	BITBLK GEM_OBJECT_FAR *source;
	BITBLK image;
	UWORD skip;
	UWORD width;

	source = (BITBLK GEM_OBJECT_FAR *) gem_object_pointer(context, spec,
		(UWORD) sizeof(BITBLK));
	if (!source)
		return FALSE;
	image.bi_pdata.lo = source->bi_pdata.lo;
	image.bi_pdata.hi = source->bi_pdata.hi;
	image.bi_wb = source->bi_wb;
	image.bi_hl = source->bi_hl;
	image.bi_x = source->bi_x;
	image.bi_y = source->bi_y;
	image.bi_color = source->bi_color;
	if (image.bi_wb <= 0 || image.bi_wb > 16 || (image.bi_wb & 1)
	    || image.bi_hl <= 0 || image.bi_x != 0 || image.bi_y < 0)
		return FALSE;
	width = (UWORD) image.bi_wb << 3;
	skip = (UWORD) image.bi_y;
	while (skip) {
		if ((UWORD) image.bi_wb
		    > (UWORD) (0xffffU - image.bi_pdata.lo))
			return FALSE;
		image.bi_pdata.lo = (UWORD) (image.bi_pdata.lo
			+ (UWORD) image.bi_wb);
		skip--;
	}
	return gem_object_far_bitmap(context, image.bi_pdata, rectangle->x,
		rectangle->y, width, (UWORD) image.bi_hl,
		(UWORD) image.bi_color, GEM_OBJECT_LOGICAL_WHITE, TRUE);
}

static WORD
gem_object_copy_icon(GEM_OBJECT_CONTEXT *context, GEM_FAR_ADDRESS spec,
	ICONBLK *icon)
{
	ICONBLK GEM_OBJECT_FAR *source;

	source = (ICONBLK GEM_OBJECT_FAR *) gem_object_pointer(context, spec,
		(UWORD) sizeof(ICONBLK));
	if (!source)
		return FALSE;
	icon->ib_pmask.lo = source->ib_pmask.lo;
	icon->ib_pmask.hi = source->ib_pmask.hi;
	icon->ib_pdata.lo = source->ib_pdata.lo;
	icon->ib_pdata.hi = source->ib_pdata.hi;
	icon->ib_ptext.lo = source->ib_ptext.lo;
	icon->ib_ptext.hi = source->ib_ptext.hi;
	icon->ib_char = source->ib_char;
	icon->ib_xchar = source->ib_xchar;
	icon->ib_ychar = source->ib_ychar;
	icon->ib_xicon = source->ib_xicon;
	icon->ib_yicon = source->ib_yicon;
	icon->ib_wicon = source->ib_wicon;
	icon->ib_hicon = source->ib_hicon;
	icon->ib_xtext = source->ib_xtext;
	icon->ib_ytext = source->ib_ytext;
	icon->ib_wtext = source->ib_wtext;
	icon->ib_htext = source->ib_htext;
	return TRUE;
}

/* Direct monochrome gr_gicon(); G_CICON uses its original mono fallback. */
static WORD
gem_object_draw_icon(GEM_OBJECT_CONTEXT *context, GEM_FAR_ADDRESS spec,
	const GEM_VDI_RECT *object_rectangle, UWORD state)
{
	ICONBLK icon;
	GEM_VDI_RECT image;
	GEM_VDI_RECT text_rectangle;
	GEM_BINDINGS_POINTER_SLOT character_slot;
	UWORD foreground;
	UWORD background;
	UWORD text_foreground;
	UWORD text_background;
	UWORD temporary;
	UWORD length;
	UWORD underline;
	UBYTE character;

	if (!gem_object_copy_icon(context, spec, &icon)
	    || icon.ib_wicon <= 0 || icon.ib_hicon <= 0
	    || icon.ib_wicon > (WORD) GEM_OBJECT_BITMAP_WIDTH
	    || icon.ib_wtext < 0 || icon.ib_htext < 0)
		return FALSE;
	foreground = ((UWORD) icon.ib_char >> 12) & 15U;
	background = ((UWORD) icon.ib_char >> 8) & 15U;
	text_foreground = foreground;
	text_background = background;
	if (state & SELECTED) {
		temporary = text_foreground;
		text_foreground = text_background;
		text_background = temporary;
		if (!(state & DRAW3D)) {
			temporary = foreground;
			foreground = background;
			background = temporary;
		}
	}
	image.x = gem_object_add(object_rectangle->x, icon.ib_xicon);
	image.y = gem_object_add(object_rectangle->y, icon.ib_yicon);
	image.width = icon.ib_wicon;
	image.height = icon.ib_hicon;
	text_rectangle.x = gem_object_add(object_rectangle->x, icon.ib_xtext);
	text_rectangle.y = gem_object_add(object_rectangle->y, icon.ib_ytext);
	text_rectangle.width = icon.ib_wtext;
	text_rectangle.height = icon.ib_htext;

	if (!((state & WHITEBAK) && background == GEM_OBJECT_LOGICAL_WHITE)
	    && !gem_object_far_bitmap(context, icon.ib_pmask, image.x, image.y,
		(UWORD) image.width, (UWORD) image.height, background,
		foreground, TRUE))
		return FALSE;
	if (!((state & WHITEBAK)
	      && text_background == GEM_OBJECT_LOGICAL_WHITE)
	    && text_rectangle.width > 0
	    && !gem_object_fill(context, &text_rectangle, text_background,
		GEM_OBJECT_IP_SOLID))
		return FALSE;
	if (!gem_object_far_bitmap(context, icon.ib_pdata, image.x, image.y,
		(UWORD) image.width, (UWORD) image.height, foreground,
		background, TRUE))
		return FALSE;

	character = (UBYTE) ((UWORD) icon.ib_char & 0x00ffU);
	if (character) {
		gem_object_formatted[0] = character;
		gem_object_formatted[1] = 0;
		character_slot = gem_object_near_slot(gem_object_formatted);
		if (!gem_vdi_resident_text(character_slot, 1U,
			gem_object_add(image.x, icon.ib_xchar),
			gem_object_add(image.y, icon.ib_ychar),
			(WORD) foreground))
			return FALSE;
	}
	if (text_rectangle.width > 0 && text_rectangle.height > 0) {
		if (!gem_object_string(context, icon.ib_ptext,
			GEM_OBJECT_MAX_TEXT, &length, &underline)
		    || !gem_object_text(context, G_ICON, icon.ib_ptext, length,
			GEM_OBJECT_NO_UNDERLINE, &text_rectangle,
			GEM_OBJECT_TE_CNTR, text_foreground))
			return FALSE;
	}
	return TRUE;
}

/* Apply the original common xstate overlays after the base object. */
static WORD
gem_object_states(GEM_OBJECT_CONTEXT *context, UWORD type, UWORD state,
	UWORD flags, const GEM_VDI_RECT *rectangle, WORD thickness)
{
	GEM_VDI_RECT interior;
	GEM_VDI_RECT outline;
	WORD amount;

	(void) flags;
	if (!state)
		return TRUE;
	if (state & OUTLINED) {
		outline = *rectangle;
		if (!gem_object_inside_rectangle(&outline, -3)
		    || !gem_object_border(context, &outline, 1,
			GEM_OBJECT_LOGICAL_BLACK))
			return FALSE;
	}
	interior = *rectangle;
	if (thickness > 0) {
		if (!gem_object_inside_rectangle(&interior, thickness))
			return FALSE;
	} else {
		amount = (WORD) (0U - (UWORD) thickness);
		(void) amount;
	}
	if ((state & SHADOWED) && thickness) {
		amount = thickness < 0
			? (WORD) (0U - (UWORD) thickness) : thickness;
		gem_vdi_set_mode(GEM_VDI_REPLACE);
		gem_vdi_set_foreground(context->screen,
			gem_object_native_color(GEM_OBJECT_LOGICAL_BLACK));
		gem_vdi_fill_rect(context->screen,
			gem_object_add(rectangle->x, rectangle->width),
			gem_object_add(rectangle->y, amount), amount,
			rectangle->height);
		gem_vdi_fill_rect(context->screen,
			gem_object_add(rectangle->x, amount),
			gem_object_add(rectangle->y, rectangle->height),
			rectangle->width, amount);
	}
	if (state & CHECKED) {
		gem_vdi_set_mode(GEM_VDI_REPLACE);
		gem_vdi_set_foreground(context->screen,
			gem_object_native_color(GEM_OBJECT_LOGICAL_BLACK));
		gem_vdi_line(context->screen, gem_object_add(interior.x, 2),
			gem_object_add(interior.y, 7), gem_object_add(interior.x, 4),
			gem_object_add(interior.y, 9), TRUE);
		gem_vdi_line(context->screen, gem_object_add(interior.x, 4),
			gem_object_add(interior.y, 9), gem_object_add(interior.x, 8),
			gem_object_add(interior.y, 3), TRUE);
	}
	if (state & CROSSED) {
		gem_vdi_set_mode(GEM_VDI_REPLACE);
		gem_vdi_set_foreground(context->screen,
			gem_object_native_color(GEM_OBJECT_LOGICAL_BLACK));
		gem_vdi_line(context->screen, interior.x, interior.y,
			gem_object_add(interior.x, interior.width - 1),
			gem_object_add(interior.y, interior.height - 1), TRUE);
		gem_vdi_line(context->screen, interior.x,
			gem_object_add(interior.y, interior.height - 1),
			gem_object_add(interior.x, interior.width - 1), interior.y,
			TRUE);
	}
	if ((state & SELECTED) && type != G_ICON && type != G_CICON) {
		gem_vdi_set_mode(GEM_VDI_XOR);
		gem_vdi_set_foreground(context->screen,
			gem_object_native_color(GEM_OBJECT_LOGICAL_WHITE));
		gem_vdi_fill_rect(context->screen, interior.x, interior.y,
			interior.width, interior.height);
		gem_vdi_set_mode(GEM_VDI_REPLACE);
	}
	return TRUE;
}

/* Bounded direct port of GEMOBLIB.C just_draw(). */
static WORD
gem_object_just_draw(GEM_OBJECT_CONTEXT *context, WORD object_index,
	WORD screen_x, WORD screen_y)
{
	OBJECT GEM_OBJECT_FAR *object;
	GEM_FAR_ADDRESS spec;
	GEM_VDI_RECT rectangle;
	GEM_VDI_RECT interior;
	UWORD type;
	UWORD border;
	UWORD text;
	UWORD pattern;
	UWORD inside;
	UWORD length;
	UWORD underline;
	WORD replace;
	WORD thickness;
	UBYTE byte;

	if (object_index < ROOT
	    || (UWORD) object_index >= context->object_count)
		return FALSE;
	object = gem_object_at(context, (UWORD) object_index);
	if (object->ob_flags & HIDETREE)
		return TRUE;
	if (!gem_object_spec(context, object, &spec))
		return FALSE;
	if (spec.lo == 0xffffU && spec.hi == 0xffffU)
		return TRUE;
	type = object->ob_type & 0x00ffU;
	rectangle.x = screen_x;
	rectangle.y = screen_y;
	rectangle.width = (WORD) object->ob_width;
	rectangle.height = (WORD) object->ob_height;
	/*
	 * Original GEM's just_draw() returns VOID, so an empty placeholder does
	 * not stop everyobj() from visiting its later siblings.  DESKTOP.RSC uses
	 * exactly such a zero-width G_STRING as the first child of the green
	 * startup information form.  Treat a zero extent as a successful no-op;
	 * otherwise the resident walk paints the root and aborts before the title,
	 * bitmap, and copyright strings.  A negative word remains malformed.  No
	 * coordinate scaling or arithmetic is performed on this path.
	 */
	if (!rectangle.width || !rectangle.height)
		return TRUE;
	if (rectangle.width < 0 || rectangle.height < 0)
		return FALSE;
	thickness = 0;
	if (type == G_TITLE)
		thickness = 1;
	else if (type == G_BOX || type == G_BOXCHAR || type == G_IBOX) {
		byte = (UBYTE) (spec.hi & 0x00ffU);
		thickness = (WORD) byte;
		if (thickness > 127)
			thickness -= 256;
	} else if (type == G_BUTTON) {
		thickness = -1;
		if ((object->ob_flags & EXIT) && (object->ob_flags & DEFAULT))
			thickness--;
	}

	switch (type) {
	case G_BOX:
	case G_IBOX:
	case G_BOXCHAR:
		gem_object_crack(spec.lo, &border, &text, &pattern, &inside,
			&replace);
		(void) replace;
		if (!gem_object_border(context, &rectangle, thickness, border))
			return FALSE;
		interior = rectangle;
		if (thickness > 0
		    && !gem_object_inside_rectangle(&interior, thickness))
			return FALSE;
		if (type != G_IBOX
		    && !gem_object_fill(context, &interior, inside, pattern))
			return FALSE;
		if (type == G_BOXCHAR) {
			gem_object_formatted[0] = (UBYTE) (spec.hi >> 8);
			gem_object_formatted[1] = 0;
			if (gem_object_formatted[0]
			    && !gem_object_text(context, type,
				gem_object_near_slot(gem_object_formatted), 1U,
				GEM_OBJECT_NO_UNDERLINE, &interior,
				GEM_OBJECT_TE_CNTR, text))
				return FALSE;
		}
		break;
	case G_TEXT:
	case G_BOXTEXT:
	case G_FTEXT:
	case G_FBOXTEXT:
		if (!gem_object_draw_ted(context, spec, type, &rectangle,
			&thickness))
			return FALSE;
		break;
	case G_BUTTON:
		interior = rectangle;
		if (!gem_object_fill(context, &interior,
			GEM_OBJECT_LOGICAL_WHITE, GEM_OBJECT_IP_SOLID)
		    || !gem_object_border(context, &rectangle, thickness,
			GEM_OBJECT_LOGICAL_BLACK)
		    || !gem_object_string(context, spec, GEM_OBJECT_MAX_TEXT,
			&length, &underline)
		    || !gem_object_text(context, type, spec, length, underline,
			&rectangle, GEM_OBJECT_TE_CNTR,
			GEM_OBJECT_LOGICAL_BLACK))
			return FALSE;
		break;
	case G_STRING:
	case G_TITLE:
		if (!gem_object_string(context, spec, GEM_OBJECT_MAX_TEXT,
			&length, &underline)
		    || !gem_object_text(context, object->ob_type, spec, length,
			underline, &rectangle, GEM_OBJECT_TE_LEFT,
			GEM_OBJECT_LOGICAL_BLACK))
			return FALSE;
		break;
	case G_IMAGE:
		if (!gem_object_draw_image(context, spec, &rectangle))
			return FALSE;
		break;
	case G_ICON:
	case G_CICON:
		if (!gem_object_draw_icon(context, spec, &rectangle,
			object->ob_state))
			return FALSE;
		break;
	case G_USERDEF:
	case G_DTMFDB:
	default:
		/* A resident AES cannot safely execute client code in its task. */
		return FALSE;
	}
	return gem_object_states(context, type, object->ob_state,
		object->ob_flags, &rectangle, thickness);
}

/* Direct non-recursive GEMOBJOP.C everyobj(). */
static WORD
gem_object_every(GEM_OBJECT_CONTEXT *context, WORD current, WORD last,
	WORD start_x, WORD start_y, UWORD maximum_depth)
{
	OBJECT GEM_OBJECT_FAR *object;
	WORD x[GEM_OBJECT_COORD_STACK];
	WORD y[GEM_OBJECT_COORD_STACK];
	WORD next;
	UWORD depth;

	if (maximum_depth > GEM_OBJECT_MAX_DEPTH)
		maximum_depth = GEM_OBJECT_MAX_DEPTH;
	x[0] = start_x;
	y[0] = start_y;
	depth = 1U;

child:
	if (current == last)
		return TRUE;
	if (current < ROOT || (UWORD) current >= context->object_count
	    || depth >= GEM_OBJECT_COORD_STACK)
		return FALSE;
	object = gem_object_at(context, (UWORD) current);
	x[depth] = gem_object_add(x[depth - 1U], (WORD) object->ob_x);
	y[depth] = gem_object_add(y[depth - 1U], (WORD) object->ob_y);
	if (!gem_object_just_draw(context, current, x[depth], y[depth]))
		return FALSE;
	next = object->ob_head;
	if (next != NIL && !(object->ob_flags & HIDETREE)
	    && depth <= maximum_depth) {
		depth++;
		current = next;
		goto child;
	}

sibling:
	object = gem_object_at(context, (UWORD) current);
	next = object->ob_next;
	if (next == last || current == ROOT)
		return TRUE;
	if (next < ROOT || (UWORD) next >= context->object_count)
		return FALSE;
	object = gem_object_at(context, (UWORD) next);
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

static WORD
gem_object_draw(GEM_OBJECT_CONTEXT *context, WORD object_index, UWORD depth)
{
	WORD parent;
	WORD last;
	WORD start_x;
	WORD start_y;
	WORD valid;

	parent = gem_object_parent(context, object_index, &last, &valid);
	if (!valid)
		return FALSE;
	if (parent != NIL) {
		if (!gem_object_offset(context, parent, &start_x, &start_y))
			return FALSE;
	} else {
		start_x = 0;
		start_y = 0;
	}
	return gem_object_every(context, object_index, last, start_x, start_y,
		depth);
}

static WORD
gem_object_inside(WORD x, WORD y, const GEM_VDI_RECT *rectangle)
{
	WORD right;
	WORD bottom;

	right = gem_object_add(rectangle->x, rectangle->width);
	bottom = gem_object_add(rectangle->y, rectangle->height);
	return x >= rectangle->x && y >= rectangle->y
		&& x < right && y < bottom;
}

/* Direct reverse-z-order GEMOBLIB.C ob_find(). */
static WORD
gem_object_find(const GEM_OBJECT_CONTEXT *context, WORD current,
	UWORD depth, WORD mouse_x, WORD mouse_y)
{
	OBJECT GEM_OBJECT_FAR *object;
	GEM_VDI_RECT rectangle;
	GEM_VDI_RECT origin;
	WORD last_found;
	WORD child;
	WORD parent;
	WORD junk;
	WORD valid;
	WORD siblings;
	WORD done;
	UWORD steps;

	if (current < ROOT || (UWORD) current >= context->object_count)
		return NIL;
	last_found = NIL;
	origin.x = 0;
	origin.y = 0;
	origin.width = 0;
	origin.height = 0;
	if (current != ROOT) {
		parent = gem_object_parent(context, current, &junk, &valid);
		if (!valid)
			return NIL;
		if (parent != NIL
		    && !gem_object_offset(context, parent, &origin.x, &origin.y))
			return NIL;
	}
	done = FALSE;
	siblings = FALSE;
	steps = context->object_count;
	while (!done && steps) {
		steps--;
		object = gem_object_at(context, (UWORD) current);
		rectangle.x = gem_object_add(origin.x, (WORD) object->ob_x);
		rectangle.y = gem_object_add(origin.y, (WORD) object->ob_y);
		rectangle.width = (WORD) object->ob_width;
		rectangle.height = (WORD) object->ob_height;
		if (gem_object_inside(mouse_x, mouse_y, &rectangle)
		    && !(object->ob_flags & HIDETREE)) {
			last_found = current;
			child = object->ob_tail;
			if (child != NIL && depth) {
				current = child;
				depth--;
				origin.x = rectangle.x;
				origin.y = rectangle.y;
				siblings = TRUE;
			} else {
				done = TRUE;
			}
		} else if (siblings && last_found != NIL) {
			current = gem_object_previous(context, last_found, current);
			if (current == NIL)
				done = TRUE;
		} else {
			done = TRUE;
		}
	}
	return last_found;
}

static VOID
gem_object_set_clip(GEM_OBJECT_CONTEXT *context, const UWORD *input)
{
	GEM_VDI_RECT clip;

	clip.x = (WORD) input[2];
	clip.y = (WORD) input[3];
	clip.width = (WORD) input[4];
	clip.height = (WORD) input[5];
	if (clip.width > 0 && clip.height > 0)
		gem_vdi_set_clip(context->screen, 1, &clip);
	else
		gem_vdi_set_clip(context->screen, 0, (const GEM_VDI_RECT *) 0);
}

static WORD
gem_object_call_shape(const GEM_OBJECT_RESIDENT_CALL *call,
	UWORD input_count, UWORD output_count, UWORD address_count)
{
	if (!call || !call->control || call->control[1] < input_count
	    || call->control[2] < output_count
	    || call->control[3] < address_count)
		return FALSE;
	if (input_count && !call->int_in)
		return FALSE;
	if (output_count && !call->int_out)
		return FALSE;
	if (address_count && !call->addr_in)
		return FALSE;
	return TRUE;
}

static WORD
gem_object_open_call(const GEM_OBJECT_RESIDENT_CALL *call,
	GEM_OBJECT_CONTEXT *context)
{
	context->resource = call->resource;
	context->tree.lo = call->addr_in[0].lo;
	context->tree.hi = call->addr_in[0].hi;
	context->objects = (OBJECT GEM_OBJECT_FAR *) 0;
	context->object_count = 0;
	context->client_segment = call->client_segment;
	context->client_limit = call->client_limit;
	context->resident_segment = call->resident_segment;
	context->screen = gem_vdi_resident_screen();
	if (call->resource && call->resource->metrics.character_width
	    && call->resource->metrics.character_height) {
		context->character_width =
			call->resource->metrics.character_width;
		context->character_height =
			call->resource->metrics.character_height;
	} else {
		context->character_width = 8U;
		context->character_height = 16U;
	}
	return context->screen && gem_object_open_tree(context);
}

static WORD
gem_object_malformed(const GEM_OBJECT_RESIDENT_CALL *call, WORD *handled)
{
	*handled = TRUE;
	if (call && call->control && call->int_out && call->control[2])
		call->int_out[0] = FALSE;
	return FALSE;
}

WORD
gem_object_resident_dispatch(const GEM_OBJECT_RESIDENT_CALL *call,
	WORD *handled)
{
	GEM_OBJECT_CONTEXT context;
	OBJECT GEM_OBJECT_FAR *object;
	GEM_FAR_ADDRESS spec;
	GEM_VDI_RECT rectangle;
	UWORD opcode;
	UWORD old_state;
	UWORD new_state;
	UWORD changed;
	UWORD type;
	WORD thickness;
	WORD x;
	WORD y;
	WORD result;
	UBYTE byte;

	if (!handled)
		return FALSE;
	*handled = FALSE;
	if (!call || !call->control)
		return FALSE;
	opcode = call->control[0];
	if (opcode != GEM_OBJECT_OBJC_ADD
	    && opcode != GEM_OBJECT_OBJC_DRAW
	    && opcode != GEM_OBJECT_OBJC_FIND
	    && opcode != GEM_OBJECT_OBJC_OFFSET
	    && opcode != GEM_OBJECT_OBJC_ORDER
	    && opcode != GEM_OBJECT_OBJC_CHANGE)
		return FALSE;

	if ((opcode == GEM_OBJECT_OBJC_ADD
	     && !gem_object_call_shape(call, 2U, 1U, 1U))
	    || (opcode == GEM_OBJECT_OBJC_DRAW
	     && !gem_object_call_shape(call, 6U, 1U, 1U))
	    || (opcode == GEM_OBJECT_OBJC_FIND
		&& !gem_object_call_shape(call, 4U, 1U, 1U))
	    || (opcode == GEM_OBJECT_OBJC_OFFSET
		&& !gem_object_call_shape(call, 1U, 3U, 1U))
	    || (opcode == GEM_OBJECT_OBJC_ORDER
		&& !gem_object_call_shape(call, 2U, 1U, 1U))
	    || (opcode == GEM_OBJECT_OBJC_CHANGE
		&& !gem_object_call_shape(call, 8U, 1U, 1U)))
		return gem_object_malformed(call, handled);
	if (!gem_object_open_call(call, &context))
		return gem_object_malformed(call, handled);

	*handled = TRUE;
	if (opcode == GEM_OBJECT_OBJC_ADD) {
		result = gem_object_add_child(&context, (WORD) call->int_in[0],
			(WORD) call->int_in[1]);
		if (!result)
			return gem_object_malformed(call, handled);
		call->int_out[0] = TRUE;
		return TRUE;
	}
	if (opcode == GEM_OBJECT_OBJC_ORDER) {
		result = gem_object_order(&context, (WORD) call->int_in[0],
			(WORD) call->int_in[1]);
		if (!result)
			return gem_object_malformed(call, handled);
		call->int_out[0] = TRUE;
		return TRUE;
	}
	if (opcode == GEM_OBJECT_OBJC_FIND) {
		result = gem_object_find(&context, (WORD) call->int_in[0],
			call->int_in[1], (WORD) call->int_in[2],
			(WORD) call->int_in[3]);
		call->int_out[0] = (UWORD) result;
		return result;
	}
	if (opcode == GEM_OBJECT_OBJC_OFFSET) {
		if (!gem_object_offset(&context, (WORD) call->int_in[0], &x, &y))
			return gem_object_malformed(call, handled);
		call->int_out[0] = TRUE;
		call->int_out[1] = (UWORD) x;
		call->int_out[2] = (UWORD) y;
		return TRUE;
	}
	if (opcode == GEM_OBJECT_OBJC_DRAW) {
		gem_object_set_clip(&context, call->int_in);
		gem_vdi_hide_cursor(context.screen);
		result = gem_object_draw(&context, (WORD) call->int_in[0],
			call->int_in[1]);
		gem_vdi_show_cursor(context.screen);
		if (result)
			gem_vdi_flush(context.screen);
		call->int_out[0] = (UWORD) result;
		return result;
	}

	/* Direct ob_change(): write state first, then use the XOR fast path. */
	if ((WORD) call->int_in[0] < ROOT
	    || call->int_in[0] >= context.object_count)
		return gem_object_malformed(call, handled);
	object = gem_object_at(&context, call->int_in[0]);
	old_state = object->ob_state;
	new_state = call->int_in[6];
	if (!gem_object_spec(&context, object, &spec))
		return gem_object_malformed(call, handled);
	if (old_state == new_state
	    || (spec.lo == 0xffffU && spec.hi == 0xffffU)) {
		call->int_out[0] = TRUE;
		return TRUE;
	}
	object->ob_state = new_state;
	if (!call->int_in[7]) {
		call->int_out[0] = TRUE;
		return TRUE;
	}
	gem_object_set_clip(&context, call->int_in);
	if (!gem_object_offset(&context, (WORD) call->int_in[0], &x, &y))
		return gem_object_malformed(call, handled);
	type = object->ob_type & 0x00ffU;
	thickness = 0;
	if (type == G_TITLE)
		thickness = 1;
	else if (type == G_BOX || type == G_BOXCHAR || type == G_IBOX) {
		byte = (UBYTE) (spec.hi & 0x00ffU);
		thickness = (WORD) byte;
		if (thickness > 127)
			thickness -= 256;
	}
	changed = old_state ^ new_state;
	rectangle.x = x;
	rectangle.y = y;
	rectangle.width = (WORD) object->ob_width;
	rectangle.height = (WORD) object->ob_height;
	gem_vdi_hide_cursor(context.screen);
	if (type != G_ICON && type != G_CICON && type != G_BUTTON
	    && !(object->ob_flags & GEM_OBJECT_FLAG3D)
	    && !(new_state & DRAW3D) && !(old_state & DRAW3D)
	    && changed == SELECTED) {
		if (thickness < 0)
			thickness = 0;
		if (thickness > 0
		    && !gem_object_inside_rectangle(&rectangle, thickness))
			result = FALSE;
		else {
			gem_vdi_set_mode(GEM_VDI_XOR);
			gem_vdi_set_foreground(context.screen,
				gem_object_native_color(GEM_OBJECT_LOGICAL_WHITE));
			gem_vdi_fill_rect(context.screen, rectangle.x, rectangle.y,
				rectangle.width, rectangle.height);
			gem_vdi_set_mode(GEM_VDI_REPLACE);
			result = TRUE;
		}
	} else {
		result = gem_object_just_draw(&context,
			(WORD) call->int_in[0], x, y);
	}
	gem_vdi_show_cursor(context.screen);
	if (result)
		gem_vdi_flush(context.screen);
	call->int_out[0] = (UWORD) result;
	return result;
}
