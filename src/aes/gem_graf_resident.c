/*
 * gem_graf_resident.c - original GEM graphics interactions on ELKS tasks.
 *
 * This is a bounded direct port of the GPL-released Digital Research
 * GEMGRLIB.C gr_clamp(), gr_scale(), gr_stepcalc(), gr_rubbox(),
 * gr_dragbox(), gr_movebox(), gr_growbox(), gr_shrinkbox(), gr_watchbox(),
 * gr_slidebox(), and gr_mkstate(), plus the GRAF cases in GEMSUPER.C.
 * GEMCTRL.C is the provenance for using the same drag/rubber results to make
 * WM_MOVED and WM_SIZED messages.
 *
 * ELKS replaces only GEM's UDA-stack wait and dispatcher loop.  A held-button
 * call remains attached to its exact twelve-PD owner/generation while the
 * resident owner receives nonblocking VDI samples.  XOR drawing, OBJECT
 * state changes, RSC/client far pointers, output words, and scale-1000 slide
 * results retain the original representation.
 *
 * Target arithmetic is entirely byte/word based.  The one scale-1000 helper
 * represents its necessary intermediate product as explicit high/low words;
 * carry is propagated by comparisons.  It emits no wide scalar, MUL, DIV,
 * floating-point operation, heap allocation, or instruction newer than an
 * 8088/8086.
 */

#include "gem_graf_resident.h"

#if defined(ELKS) && ELKS
#include "gem_vdi_resident.h"
#define GEM_GRAF_FAR __far
#else
#define GEM_GRAF_FAR
#endif

#define GEM_GRAF_PD_FREE                0U
#define GEM_GRAF_PD_WAITING             1U
#define GEM_GRAF_PD_READY               2U

#define GEM_GRAF_LEFT_BUTTON            0x0001U
#define GEM_GRAF_OBJECT_BYTES           24U
#define GEM_GRAF_RSHDR_BYTES            36U
#define GEM_GRAF_SLIDE_SCALE            1000U
#define GEM_GRAF_WORD_MAX               32767

typedef struct gem_graf_pd {
	const GEM_RESOURCE_RESIDENT *resource;
	GEM_BINDINGS_POINTER_SLOT tree;
	GEM_VDI_RECT rectangle;
	GEM_VDI_RECT constraint;
	UWORD generation_lo;
	UWORD generation_hi;
	UWORD client_segment;
	UWORD client_limit;
	UWORD opcode;
	UWORD object;
	UWORD inside_state;
	UWORD outside_state;
	UWORD output_count;
	UWORD output[GEM_GRAF_OUTPUT_WORDS];
	WORD origin_x;
	WORD origin_y;
	WORD minimum_width;
	WORD minimum_height;
	UBYTE state;
	UBYTE outline_visible;
	UBYTE watch_inside;
	UBYTE vertical;
	UBYTE update_held;
} GEM_GRAF_PD;

#if defined(ELKS) && ELKS
/* Twelve exact 64-byte PD records consume 768 resident near-data bytes. */
typedef BYTE GEM_GRAF_PD_MUST_BE_64_BYTES
	[(sizeof(GEM_GRAF_PD) == 64) ? 1 : -1];
#endif

static GEM_GRAF_PD gem_graf_pds[GEM_GRAF_PD_COUNT];

static WORD gem_graf_mouse_x;
static WORD gem_graf_mouse_y;
static UWORD gem_graf_mouse_buttons;
static UWORD gem_graf_key_state;

#if defined(ELKS) && ELKS
/* An ia16 far data pointer is the unchanged offset:segment slot. */
typedef union gem_graf_far_pointer {
	VOID GEM_GRAF_FAR *pointer;
	UBYTE GEM_GRAF_FAR *bytes;
	OBJECT GEM_GRAF_FAR *object;
	RSHDR GEM_GRAF_FAR *header;
	GEM_BINDINGS_POINTER_SLOT address;
} GEM_GRAF_FAR_POINTER;

typedef BYTE GEM_GRAF_FAR_POINTER_MUST_BE_4_BYTES
	[(sizeof(GEM_GRAF_FAR_POINTER) == 4) ? 1 : -1];
#endif

static GEM_GRAF_PD *
gem_graf_pd_at(UWORD owner)
{
	GEM_GRAF_PD *pd;

	if (owner >= GEM_GRAF_PD_COUNT)
		return (GEM_GRAF_PD *) 0;
	pd = gem_graf_pds;
	while (owner--)
		pd++;
	return pd;
}

static VOID
gem_graf_clear_effects(GEM_GRAF_EFFECTS *effects)
{
	if (!effects)
		return;
	effects->begin_update = FALSE;
	effects->end_update = FALSE;
}

static VOID
gem_graf_clear_completion(GEM_GRAF_COMPLETION *completion)
{
	UWORD *output;
	UWORD count;

	completion->owner = GEM_GRAF_OWNER_NONE;
	completion->generation_lo = 0;
	completion->generation_hi = 0;
	completion->output_count = 0;
	output = completion->int_out;
	count = GEM_GRAF_OUTPUT_WORDS;
	while (count--)
		*output++ = 0;
	gem_graf_clear_effects(&completion->effects);
}

static VOID
gem_graf_clear_pd(GEM_GRAF_PD *pd)
{
	UWORD *output;
	UWORD count;

	pd->resource = (const GEM_RESOURCE_RESIDENT *) 0;
	pd->tree.lo = 0;
	pd->tree.hi = 0;
	pd->rectangle.x = 0;
	pd->rectangle.y = 0;
	pd->rectangle.width = 0;
	pd->rectangle.height = 0;
	pd->constraint.x = 0;
	pd->constraint.y = 0;
	pd->constraint.width = 0;
	pd->constraint.height = 0;
	pd->generation_lo = 0;
	pd->generation_hi = 0;
	pd->client_segment = 0;
	pd->client_limit = 0;
	pd->opcode = 0;
	pd->object = 0;
	pd->inside_state = 0;
	pd->outside_state = 0;
	pd->output_count = 0;
	output = pd->output;
	count = GEM_GRAF_OUTPUT_WORDS;
	while (count--)
		*output++ = 0;
	pd->origin_x = 0;
	pd->origin_y = 0;
	pd->minimum_width = 0;
	pd->minimum_height = 0;
	pd->state = GEM_GRAF_PD_FREE;
	pd->outline_visible = FALSE;
	pd->watch_inside = FALSE;
	pd->vertical = FALSE;
	pd->update_held = FALSE;
}

static WORD
gem_graf_pd_matches(const GEM_GRAF_PD *pd, UWORD generation_lo,
	UWORD generation_hi)
{
	return pd && pd->state != GEM_GRAF_PD_FREE
		&& pd->generation_lo == generation_lo
		&& pd->generation_hi == generation_hi;
}

static GEM_VDI_COLOR
gem_graf_native_white(const GEM_VDI_SCREEN *screen)
{
	/* Original logical white is EGA palette 15; mono maps nonzero to white. */
	if (screen && screen->colors > 2U)
		return 15U;
	return 1U;
}

/*
 * Reject any rectangle whose edge addition could overflow a signed word.
 * Width/height may extend past the physical screen: original GEMCTRL.C uses
 * a 10000 by 10000 drag constraint as its deliberately unbounded window
 * mover.  The resident VDI clips that outline to the physical screen just as
 * GSX did; requiring the top-left pixel on-screen keeps the hot path simple.
 */
static WORD
gem_graf_rectangle_valid(const GEM_VDI_SCREEN *screen,
	const GEM_VDI_RECT *rectangle)
{
	if (!screen || !rectangle || screen->xres <= 0 || screen->yres <= 0
	    || rectangle->x < 0 || rectangle->y < 0
	    || rectangle->width <= 0 || rectangle->height <= 0)
		return FALSE;
	if (rectangle->x >= screen->xres || rectangle->y >= screen->yres)
		return FALSE;
	return rectangle->width <= GEM_GRAF_WORD_MAX - rectangle->x
		&& rectangle->height <= GEM_GRAF_WORD_MAX - rectangle->y;
}

static WORD
gem_graf_rectangles_equal(const GEM_VDI_RECT *left,
	const GEM_VDI_RECT *right)
{
	return left->x == right->x && left->y == right->y
		&& left->width == right->width
		&& left->height == right->height;
}

/*
 * Draw/erase up to two original XOR outlines under one cursor hide and one
 * flush.  The caller supplies validated, screen-bounded rectangles, keeping
 * every coordinate addition inside the signed 8086 word range.
 */
static WORD
gem_graf_xor_pair(const GEM_VDI_RECT *first, UBYTE draw_first,
	const GEM_VDI_RECT *second, UBYTE draw_second)
{
	GEM_VDI_SCREEN *screen;

	screen = gem_vdi_resident_screen();
	if (!screen)
		return FALSE;
	if ((draw_first && !gem_graf_rectangle_valid(screen, first))
	    || (draw_second && !gem_graf_rectangle_valid(screen, second)))
		return FALSE;
	if (!draw_first && !draw_second)
		return TRUE;
	gem_vdi_set_clip(screen, 0, (const GEM_VDI_RECT *) 0);
	gem_vdi_hide_cursor(screen);
	gem_vdi_set_mode(GEM_VDI_XOR);
	gem_vdi_set_foreground(screen, gem_graf_native_white(screen));
	if (draw_first)
		gem_vdi_rect(screen, first->x, first->y,
			first->width, first->height);
	if (draw_second)
		gem_vdi_rect(screen, second->x, second->y,
			second->width, second->height);
	gem_vdi_set_mode(GEM_VDI_REPLACE);
	gem_vdi_show_cursor(screen);
	gem_vdi_flush(screen);
	return TRUE;
}

/* Unsigned word division with exactly sixteen shift/subtract iterations. */
static UWORD
gem_graf_divide_word(UWORD numerator, UWORD denominator)
{
	UWORD quotient;
	UWORD remainder;
	UWORD bit;
	UBYTE carry;

	if (!denominator)
		return 0xffffU;
	quotient = 0;
	remainder = 0;
	bit = 0x8000U;
	while (bit) {
		carry = (UBYTE) (remainder >> 15);
		remainder <<= 1;
		if (numerator & bit)
			remainder |= 1U;
		if (carry || remainder >= denominator) {
			remainder = (UWORD) (remainder - denominator);
			quotient |= bit;
		}
		bit >>= 1;
	}
	return quotient;
}

/* Direct gr_scale(), retaining its average-distance/log2 step count. */
static VOID
gem_graf_scale(UWORD x_distance, UWORD y_distance, UWORD *count,
	UWORD *x_step, UWORD *y_step)
{
	UWORD distance;
	UWORD value;

	/* floor((x+y)/2) without overflowing the one-word sum. */
	distance = (x_distance >> 1) + (y_distance >> 1);
	if ((x_distance & 1U) && (y_distance & 1U))
		distance++;
	value = distance;
	*count = 0;
	while (value) {
		(*count)++;
		value >>= 1;
	}
	if (*count) {
		*x_step = gem_graf_divide_word(x_distance, *count);
		*y_step = gem_graf_divide_word(y_distance, *count);
		if (!*x_step)
			*x_step = 1U;
		if (!*y_step)
			*y_step = 1U;
	} else {
		*x_step = 1U;
		*y_step = 1U;
	}
}

/* Absolute difference is safe because all admitted coordinates are >= 0. */
static UWORD
gem_graf_distance(WORD left, WORD right)
{
	if (left < right)
		return (UWORD) (right - left);
	return (UWORD) (left - right);
}

/*
 * Exact nonnegative (value * 1000) / divisor for GRAF_SLIDEBOX.  The product
 * needs more than one word even though both input and final API value are
 * one word, so it is isolated as explicit high/low halves.  Shifts propagate
 * carry manually and quotient overflow saturates to the original 1000 cap.
 */
static UWORD
gem_graf_scale_1000(UWORD value, UWORD divisor)
{
	UWORD product_lo;
	UWORD product_hi;
	UWORD add_lo;
	UWORD add_hi;
	UWORD multiplier;
	UWORD old_lo;
	UWORD quotient_lo;
	UWORD quotient_hi;
	UWORD remainder;
	UWORD numerator_bit;
	UWORD count;
	UBYTE carry;
	UBYTE subtract;

	if (!divisor)
		return 0;
	if (value >= divisor)
		return GEM_GRAF_SLIDE_SCALE;

	product_lo = 0;
	product_hi = 0;
	add_lo = value;
	add_hi = 0;
	multiplier = GEM_GRAF_SLIDE_SCALE;
	while (multiplier) {
		if (multiplier & 1U) {
			old_lo = product_lo;
			product_lo = (UWORD) (product_lo + add_lo);
			product_hi = (UWORD) (product_hi + add_hi);
			if (product_lo < old_lo)
				product_hi++;
		}
		add_hi = (UWORD) ((add_hi << 1) | (add_lo >> 15));
		add_lo <<= 1;
		multiplier >>= 1;
	}

	remainder = 0;
	quotient_lo = 0;
	quotient_hi = 0;
	count = 32U;
	while (count--) {
		numerator_bit = product_hi >> 15;
		product_hi = (UWORD) ((product_hi << 1)
			| (product_lo >> 15));
		product_lo <<= 1;
		carry = (UBYTE) (remainder >> 15);
		remainder = (UWORD) ((remainder << 1) | numerator_bit);
		subtract = (UBYTE) (carry || remainder >= divisor);
		if (subtract)
			remainder = (UWORD) (remainder - divisor);
		quotient_hi = (UWORD) ((quotient_hi << 1)
			| (quotient_lo >> 15));
		quotient_lo = (UWORD) ((quotient_lo << 1) | subtract);
	}
	if (quotient_hi || quotient_lo > GEM_GRAF_SLIDE_SCALE)
		return GEM_GRAF_SLIDE_SCALE;
	return quotient_lo;
}

static WORD
gem_graf_animation_begin(GEM_VDI_SCREEN **screen)
{
	*screen = gem_vdi_resident_screen();
	if (!*screen)
		return FALSE;
	gem_vdi_set_clip(*screen, 0, (const GEM_VDI_RECT *) 0);
	gem_vdi_hide_cursor(*screen);
	gem_vdi_set_mode(GEM_VDI_XOR);
	gem_vdi_set_foreground(*screen, gem_graf_native_white(*screen));
	return TRUE;
}

static VOID
gem_graf_animation_end(GEM_VDI_SCREEN *screen)
{
	gem_vdi_set_mode(GEM_VDI_REPLACE);
	gem_vdi_show_cursor(screen);
	gem_vdi_flush(screen);
}

/* Draw only validated frames; normal Desktop geometry keeps every frame in. */
static VOID
gem_graf_animation_rect(GEM_VDI_SCREEN *screen,
	const GEM_VDI_RECT *rectangle)
{
	if (gem_graf_rectangle_valid(screen, rectangle))
		gem_vdi_rect(screen, rectangle->x, rectangle->y,
			rectangle->width, rectangle->height);
}

/* Direct gr_movebox()/gr_2box(): lay down the path, then erase the path. */
static WORD
gem_graf_movebox(const GEM_VDI_RECT *source, WORD destination_x,
	WORD destination_y)
{
	GEM_VDI_SCREEN *screen;
	GEM_VDI_RECT destination;
	GEM_VDI_RECT rectangle;
	UWORD count;
	UWORD x_step;
	UWORD y_step;
	UWORD frames;
	UWORD pass;
	WORD signed_x_step;
	WORD signed_y_step;

	destination = *source;
	destination.x = destination_x;
	destination.y = destination_y;
	if (!gem_graf_rectangle_valid(gem_vdi_resident_screen(), source)
	    || !gem_graf_rectangle_valid(gem_vdi_resident_screen(),
		&destination))
		return FALSE;
	gem_graf_scale(gem_graf_distance(source->x, destination_x),
		gem_graf_distance(source->y, destination_y),
		&count, &x_step, &y_step);
	signed_x_step = source->x < destination_x
		? (WORD) (0U - x_step) : (WORD) x_step;
	signed_y_step = source->y < destination_y
		? (WORD) (0U - y_step) : (WORD) y_step;
	if (!gem_graf_animation_begin(&screen))
		return FALSE;
	pass = 0;
	while (pass < 2U) {
		rectangle = *source;
		frames = count + 1U;
		while (frames--) {
			gem_graf_animation_rect(screen, &rectangle);
			rectangle.x = (WORD) ((UWORD) rectangle.x
				- (UWORD) signed_x_step);
			rectangle.y = (WORD) ((UWORD) rectangle.y
				- (UWORD) signed_y_step);
		}
		gem_vdi_flush(screen);
		pass++;
	}
	gem_graf_animation_end(screen);
	return TRUE;
}

/* Direct gr_2box() growth/shrink path, drawn twice to erase every outline. */
static WORD
gem_graf_size_path(const GEM_VDI_RECT *start, UWORD count,
	WORD x_step, WORD y_step)
{
	GEM_VDI_SCREEN *screen;
	GEM_VDI_RECT rectangle;
	UWORD frames;
	UWORD pass;
	WORD width_step;
	WORD height_step;

	if (!gem_graf_rectangle_valid(gem_vdi_resident_screen(), start)
	    || !gem_graf_animation_begin(&screen))
		return FALSE;
	width_step = (WORD) ((UWORD) x_step + (UWORD) x_step);
	height_step = (WORD) ((UWORD) y_step + (UWORD) y_step);
	pass = 0;
	while (pass < 2U) {
		rectangle = *start;
		frames = count + 1U;
		while (frames--) {
			gem_graf_animation_rect(screen, &rectangle);
			rectangle.x = (WORD) ((UWORD) rectangle.x
				- (UWORD) x_step);
			rectangle.y = (WORD) ((UWORD) rectangle.y
				- (UWORD) y_step);
			rectangle.width = (WORD) ((UWORD) rectangle.width
				+ (UWORD) width_step);
			rectangle.height = (WORD) ((UWORD) rectangle.height
				+ (UWORD) height_step);
		}
		gem_vdi_flush(screen);
		pass++;
	}
	gem_graf_animation_end(screen);
	return TRUE;
}

static WORD
gem_graf_stepcalc(const GEM_VDI_RECT *small,
	const GEM_VDI_RECT *target, WORD *center_x, WORD *center_y,
	UWORD *count, UWORD *x_step, UWORD *y_step)
{
	WORD x_distance;
	WORD y_distance;

	if (target->width < small->width || target->height < small->height)
		return FALSE;
	x_distance = (target->width >> 1) - (small->width >> 1);
	y_distance = (target->height >> 1) - (small->height >> 1);
	gem_graf_scale((UWORD) x_distance, (UWORD) y_distance,
		count, x_step, y_step);
	*center_x = target->x + x_distance;
	*center_y = target->y + y_distance;
	return TRUE;
}

static WORD
gem_graf_growbox(const GEM_VDI_RECT *small, const GEM_VDI_RECT *target)
{
	GEM_VDI_RECT centered;
	WORD center_x;
	WORD center_y;
	UWORD count;
	UWORD x_step;
	UWORD y_step;

	if (!gem_graf_rectangle_valid(gem_vdi_resident_screen(), small)
	    || !gem_graf_rectangle_valid(gem_vdi_resident_screen(), target)
	    || !gem_graf_stepcalc(small, target, &center_x, &center_y,
		&count, &x_step, &y_step)
	    || !gem_graf_movebox(small, center_x, center_y))
		return FALSE;
	centered = *small;
	centered.x = center_x;
	centered.y = center_y;
	return gem_graf_size_path(&centered, count,
		(WORD) x_step, (WORD) y_step);
}

static WORD
gem_graf_shrinkbox(const GEM_VDI_RECT *small, const GEM_VDI_RECT *target)
{
	GEM_VDI_RECT centered;
	WORD center_x;
	WORD center_y;
	UWORD count;
	UWORD x_step;
	UWORD y_step;

	if (!gem_graf_rectangle_valid(gem_vdi_resident_screen(), small)
	    || !gem_graf_rectangle_valid(gem_vdi_resident_screen(), target)
	    || !gem_graf_stepcalc(small, target, &center_x, &center_y,
		&count, &x_step, &y_step)
	    || !gem_graf_size_path(target, count,
		(WORD) (0U - x_step), (WORD) (0U - y_step)))
		return FALSE;
	centered = *small;
	centered.x = center_x;
	centered.y = center_y;
	return gem_graf_movebox(&centered, small->x, small->y);
}

static WORD
gem_graf_range(UWORD offset, UWORD bytes, UWORD limit)
{
	if (offset > limit)
		return FALSE;
	return bytes <= (UWORD) (limit - offset);
}

static VOID GEM_GRAF_FAR *
gem_graf_pointer(const GEM_GRAF_CALL *call,
	GEM_BINDINGS_POINTER_SLOT address, UWORD bytes)
{
	UWORD available;

	if (!call || !address.hi)
		return (VOID GEM_GRAF_FAR *) 0;
	if (call->resource
	    && (call->resource->flags & GEM_RESOURCE_RESIDENT_LOADED)
	    && address.hi == call->resource->storage.base.hi
	    && address.lo <= call->resource->storage.bytes) {
		available = (UWORD) (call->resource->storage.bytes - address.lo);
		if (bytes > available)
			return (VOID GEM_GRAF_FAR *) 0;
#if defined(ELKS) && ELKS
		{
			GEM_GRAF_FAR_POINTER pointer;

			pointer.address = address;
			return pointer.pointer;
		}
#else
		if (!call->resource->host_bytes)
			return (VOID GEM_GRAF_FAR *) 0;
		return call->resource->host_bytes + address.lo;
#endif
	}
	if (address.hi != call->client_segment
	    || !gem_graf_range(address.lo, bytes, call->client_limit))
		return (VOID GEM_GRAF_FAR *) 0;
#if defined(ELKS) && ELKS
	{
		GEM_GRAF_FAR_POINTER pointer;

		pointer.address = address;
		return pointer.pointer;
	}
#else
	return (VOID GEM_GRAF_FAR *) 0;
#endif
}

/*
 * Open one direct OBJECT record after a LASTOB-bounded scan.  The public
 * object manager separately validates parent/sibling links during OFFSET;
 * this helper only proves the requested record is inside the same tree.
 */
static OBJECT GEM_GRAF_FAR *
gem_graf_object_at(const GEM_GRAF_CALL *call, UWORD object_index)
{
	GEM_BINDINGS_POINTER_SLOT tree;
	GEM_BINDINGS_POINTER_SLOT address;
	OBJECT GEM_GRAF_FAR *object;
	UWORD remaining;
	UWORD available;
	UWORD count;

	if (!call->addr_in)
		return (OBJECT GEM_GRAF_FAR *) 0;
	tree = call->addr_in[0];
	remaining = 0;
	if (call->resource
	    && (call->resource->flags & GEM_RESOURCE_RESIDENT_LOADED)
	    && tree.hi == call->resource->storage.base.hi) {
		RSHDR GEM_GRAF_FAR *header;
		UWORD offset;

		address = call->resource->storage.base;
		header = (RSHDR GEM_GRAF_FAR *) gem_graf_pointer(call,
			address, GEM_GRAF_RSHDR_BYTES);
		if (!header || !header->rsh_nobs
		    || header->rsh_rssize != call->resource->storage.bytes)
			return (OBJECT GEM_GRAF_FAR *) 0;
		offset = header->rsh_object;
		remaining = header->rsh_nobs;
		while (remaining && offset != tree.lo) {
			if (!gem_graf_range(offset, GEM_GRAF_OBJECT_BYTES,
				call->resource->storage.bytes)
			    || offset > (UWORD) (0xffffU - GEM_GRAF_OBJECT_BYTES))
				return (OBJECT GEM_GRAF_FAR *) 0;
			offset = (UWORD) (offset + GEM_GRAF_OBJECT_BYTES);
			remaining--;
		}
		if (!remaining || offset != tree.lo)
			return (OBJECT GEM_GRAF_FAR *) 0;
	} else {
		if (tree.hi != call->client_segment
		    || tree.lo > call->client_limit)
			return (OBJECT GEM_GRAF_FAR *) 0;
		available = (UWORD) (call->client_limit - tree.lo);
		while (available >= GEM_GRAF_OBJECT_BYTES) {
			remaining++;
			available = (UWORD) (available - GEM_GRAF_OBJECT_BYTES);
		}
	}
	object = (OBJECT GEM_GRAF_FAR *) gem_graf_pointer(call, tree,
		GEM_GRAF_OBJECT_BYTES);
	if (!object)
		return (OBJECT GEM_GRAF_FAR *) 0;
	count = 0;
	while (remaining) {
		count++;
		if (object->ob_flags & LASTOB)
			break;
		object++;
		remaining--;
	}
	if (!count || !(object->ob_flags & LASTOB) || object_index >= count)
		return (OBJECT GEM_GRAF_FAR *) 0;
	object = (OBJECT GEM_GRAF_FAR *) gem_graf_pointer(call, tree,
		GEM_GRAF_OBJECT_BYTES);
	while (object_index--)
		object++;
	return object;
}

static WORD
gem_graf_object_offset(const GEM_GRAF_CALL *call, UWORD object,
	WORD *x, WORD *y)
{
	UWORD control[5];
	UWORD input[1];
	UWORD output[3];
	GEM_OBJECT_RESIDENT_CALL object_call;
	WORD handled;

	control[0] = GEM_OBJECT_OBJC_OFFSET;
	control[1] = 1U;
	control[2] = 3U;
	control[3] = 1U;
	control[4] = 0;
	input[0] = object;
	output[0] = 0;
	output[1] = 0;
	output[2] = 0;
	object_call.resource = call->resource;
	object_call.client_segment = call->client_segment;
	object_call.client_limit = call->client_limit;
	object_call.control = control;
	object_call.int_in = input;
	object_call.int_out = output;
	object_call.addr_in = call->addr_in;
	if (!gem_object_resident_dispatch(&object_call, &handled) || !handled)
		return FALSE;
	*x = (WORD) output[1];
	*y = (WORD) output[2];
	return TRUE;
}

static WORD
gem_graf_object_change_values(const GEM_RESOURCE_RESIDENT *resource,
	UWORD client_segment, UWORD client_limit,
	GEM_BINDINGS_POINTER_SLOT tree, UWORD object, UWORD state)
{
	UWORD control[5];
	UWORD input[8];
	UWORD output[1];
	GEM_BINDINGS_POINTER_SLOT address[1];
	GEM_OBJECT_RESIDENT_CALL object_call;
	GEM_VDI_SCREEN *screen;
	WORD handled;

	screen = gem_vdi_resident_screen();
	if (!screen)
		return FALSE;
	control[0] = GEM_OBJECT_OBJC_CHANGE;
	control[1] = 8U;
	control[2] = 1U;
	control[3] = 1U;
	control[4] = 0;
	input[0] = object;
	input[1] = 0;
	input[2] = 0;
	input[3] = 0;
	input[4] = (UWORD) screen->xres;
	input[5] = (UWORD) screen->yres;
	input[6] = state;
	input[7] = TRUE;
	output[0] = 0;
	address[0] = tree;
	object_call.resource = resource;
	object_call.client_segment = client_segment;
	object_call.client_limit = client_limit;
	object_call.control = control;
	object_call.int_in = input;
	object_call.int_out = output;
	object_call.addr_in = address;
	return gem_object_resident_dispatch(&object_call, &handled)
		&& handled && output[0];
}

static WORD
gem_graf_object_change_call(const GEM_GRAF_CALL *call, UWORD object,
	UWORD state)
{
	return gem_graf_object_change_values(call->resource,
		call->client_segment, call->client_limit, call->addr_in[0],
		object, state);
}

static WORD
gem_graf_inside(WORD x, WORD y, const GEM_VDI_RECT *rectangle)
{
	WORD right;
	WORD bottom;

	right = rectangle->x + rectangle->width;
	bottom = rectangle->y + rectangle->height;
	return x >= rectangle->x && y >= rectangle->y
		&& x < right && y < bottom;
}

static WORD
gem_graf_call_shape(const GEM_GRAF_CALL *call, UWORD input_count,
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
	if (address_count && !call->addr_in)
		return FALSE;
	return TRUE;
}

static WORD
gem_graf_malformed(const GEM_GRAF_CALL *call, WORD *handled)
{
	*handled = TRUE;
	if (call && call->control && call->int_out && call->control[2])
		call->int_out[0] = FALSE;
	return FALSE;
}

static VOID
gem_graf_copy_call_to_pd(GEM_GRAF_PD *pd, const GEM_GRAF_CALL *call,
	UWORD opcode)
{
	gem_graf_clear_pd(pd);
	pd->resource = call->resource;
	pd->generation_lo = call->generation_lo;
	pd->generation_hi = call->generation_hi;
	pd->client_segment = call->client_segment;
	pd->client_limit = call->client_limit;
	pd->opcode = opcode;
	if (call->addr_in)
		pd->tree = call->addr_in[0];
	pd->state = GEM_GRAF_PD_WAITING;
}

static VOID
gem_graf_finish_tracker(GEM_GRAF_PD *pd)
{
	if (pd->outline_visible) {
		gem_graf_xor_pair(&pd->rectangle, TRUE,
			(const GEM_VDI_RECT *) 0, FALSE);
		pd->outline_visible = FALSE;
	}
	pd->output[0] = TRUE;
	if (pd->opcode == GEM_GRAF_RUBBOX) {
		pd->output_count = 3U;
		pd->output[1] = (UWORD) pd->rectangle.width;
		pd->output[2] = (UWORD) pd->rectangle.height;
	} else if (pd->opcode == GEM_GRAF_DRAGBOX) {
		pd->output_count = 3U;
		pd->output[1] = (UWORD) pd->rectangle.x;
		pd->output[2] = (UWORD) pd->rectangle.y;
	} else {
		UWORD distance;
		UWORD span;

		pd->output_count = 1U;
		if (pd->vertical) {
			distance = (UWORD) (pd->rectangle.y
				- pd->constraint.y);
			span = (UWORD) (pd->constraint.height
				- pd->rectangle.height);
		} else {
			distance = (UWORD) (pd->rectangle.x
				- pd->constraint.x);
			span = (UWORD) (pd->constraint.width
				- pd->rectangle.width);
		}
		pd->output[0] = gem_graf_scale_1000(distance, span);
	}
	pd->state = GEM_GRAF_PD_READY;
}

static WORD
gem_graf_rubber_rectangle(const GEM_GRAF_PD *pd,
	WORD mouse_x, WORD mouse_y, GEM_VDI_RECT *rectangle)
{
	GEM_VDI_SCREEN *screen;
	WORD width;
	WORD height;

	screen = gem_vdi_resident_screen();
	if (!screen)
		return FALSE;
	width = mouse_x - pd->origin_x + 1;
	height = mouse_y - pd->origin_y + 1;
	if (width < pd->minimum_width)
		width = pd->minimum_width;
	if (height < pd->minimum_height)
		height = pd->minimum_height;
	if (width > screen->xres - pd->origin_x)
		width = screen->xres - pd->origin_x;
	if (height > screen->yres - pd->origin_y)
		height = screen->yres - pd->origin_y;
	rectangle->x = pd->origin_x;
	rectangle->y = pd->origin_y;
	rectangle->width = width;
	rectangle->height = height;
	return gem_graf_rectangle_valid(screen, rectangle);
}

/* Direct rc_constrain() after width/height have been validated to fit. */
static WORD
gem_graf_drag_rectangle(const GEM_GRAF_PD *pd,
	WORD mouse_x, WORD mouse_y, GEM_VDI_RECT *rectangle)
{
	WORD x;
	WORD y;
	WORD right;
	WORD bottom;

	x = mouse_x - pd->origin_x;
	y = mouse_y - pd->origin_y;
	right = pd->constraint.x + pd->constraint.width;
	bottom = pd->constraint.y + pd->constraint.height;
	if (x < pd->constraint.x)
		x = pd->constraint.x;
	if (y < pd->constraint.y)
		y = pd->constraint.y;
	if (x + pd->rectangle.width > right)
		x = right - pd->rectangle.width;
	if (y + pd->rectangle.height > bottom)
		y = bottom - pd->rectangle.height;
	*rectangle = pd->rectangle;
	rectangle->x = x;
	rectangle->y = y;
	return gem_graf_rectangle_valid(gem_vdi_resident_screen(), rectangle);
}

static WORD
gem_graf_update_tracker(GEM_GRAF_PD *pd, WORD mouse_x, WORD mouse_y)
{
	GEM_VDI_RECT next;
	WORD valid;

	if (pd->opcode == GEM_GRAF_RUBBOX)
		valid = gem_graf_rubber_rectangle(pd, mouse_x, mouse_y, &next);
	else
		valid = gem_graf_drag_rectangle(pd, mouse_x, mouse_y, &next);
	if (!valid)
		return FALSE;
	if (gem_graf_rectangles_equal(&pd->rectangle, &next))
		return TRUE;
	if (!gem_graf_xor_pair(&pd->rectangle, pd->outline_visible,
		&next, TRUE))
		return FALSE;
	pd->rectangle = next;
	pd->outline_visible = TRUE;
	return TRUE;
}

VOID
gem_graf_resident_reset(VOID)
{
	GEM_GRAF_PD *pd;
	UWORD count;

	pd = gem_graf_pds;
	count = GEM_GRAF_PD_COUNT;
	while (count--) {
		gem_graf_clear_pd(pd);
		pd++;
	}
	gem_graf_mouse_x = 0;
	gem_graf_mouse_y = 0;
	gem_graf_mouse_buttons = 0;
	gem_graf_key_state = 0;
}

WORD
gem_graf_resident_input(const GEM_GRAF_INPUT *input)
{
	GEM_GRAF_PD *pd;
	GEM_VDI_SCREEN *screen;
	WORD inside;

	if (!input || input->owner >= GEM_GRAF_PD_COUNT)
		return FALSE;
	screen = gem_vdi_resident_screen();
	if (!screen || input->mouse_x < 0 || input->mouse_y < 0
	    || input->mouse_x >= screen->xres || input->mouse_y >= screen->yres)
		return FALSE;
	gem_graf_mouse_x = input->mouse_x;
	gem_graf_mouse_y = input->mouse_y;
	gem_graf_mouse_buttons = input->mouse_buttons;
	gem_graf_key_state = input->key_state;

	pd = gem_graf_pd_at(input->owner);
	if (!gem_graf_pd_matches(pd, input->generation_lo,
		input->generation_hi) || pd->state != GEM_GRAF_PD_WAITING)
		return TRUE;

	/* Original gr_wait() returns on button-up before its caller recomputes. */
	if (!(input->mouse_buttons & GEM_GRAF_LEFT_BUTTON)) {
		if (pd->opcode == GEM_GRAF_WATCHBOX) {
			pd->output_count = 1U;
			pd->output[0] = pd->watch_inside ? TRUE : FALSE;
			pd->state = GEM_GRAF_PD_READY;
		} else {
			gem_graf_finish_tracker(pd);
		}
		return TRUE;
	}

	if (pd->opcode != GEM_GRAF_WATCHBOX) {
		if (!gem_graf_update_tracker(pd, input->mouse_x, input->mouse_y)) {
			if (pd->outline_visible) {
				gem_graf_xor_pair(&pd->rectangle, TRUE,
					(const GEM_VDI_RECT *) 0, FALSE);
				pd->outline_visible = FALSE;
			}
			pd->output_count = 1U;
			pd->output[0] = FALSE;
			pd->state = GEM_GRAF_PD_READY;
		}
		return TRUE;
	}

	inside = gem_graf_inside(input->mouse_x, input->mouse_y,
		&pd->rectangle);
	if ((inside != 0) != (pd->watch_inside != 0)) {
		if (!gem_graf_object_change_values(pd->resource,
			pd->client_segment, pd->client_limit, pd->tree,
			pd->object, inside ? pd->inside_state : pd->outside_state)) {
			pd->output_count = 1U;
			pd->output[0] = FALSE;
			pd->state = GEM_GRAF_PD_READY;
			return TRUE;
		}
		pd->watch_inside = (UBYTE) (inside != 0);
	}
	return TRUE;
}

WORD
gem_graf_resident_dispatch(const GEM_GRAF_CALL *call,
	GEM_GRAF_EFFECTS *effects, WORD *handled)
{
	GEM_GRAF_PD *pd;
	GEM_VDI_SCREEN *screen;
	GEM_VDI_RECT first;
	GEM_VDI_RECT second;
	OBJECT GEM_GRAF_FAR *object;
	WORD parent_x;
	WORD parent_y;
	WORD object_x;
	WORD object_y;
	WORD result;
	UWORD opcode;

	if (!handled)
		return FALSE;
	*handled = FALSE;
	gem_graf_clear_effects(effects);
	if (!call || !call->control)
		return FALSE;
	opcode = call->control[0];
	if (opcode != GEM_GRAF_RUBBOX && opcode != GEM_GRAF_DRAGBOX
	    && opcode != GEM_GRAF_MBOX && opcode != GEM_GRAF_GROWBOX
	    && opcode != GEM_GRAF_SHRINKBOX && opcode != GEM_GRAF_WATCHBOX
	    && opcode != GEM_GRAF_SLIDEBOX && opcode != GEM_GRAF_MKSTATE)
		return FALSE;

	if ((opcode == GEM_GRAF_RUBBOX
	     && !gem_graf_call_shape(call, 4U, 3U, 0U))
	    || (opcode == GEM_GRAF_DRAGBOX
		&& !gem_graf_call_shape(call, 8U, 3U, 0U))
	    || (opcode == GEM_GRAF_MBOX
		&& !gem_graf_call_shape(call, 6U, 1U, 0U))
	    || ((opcode == GEM_GRAF_GROWBOX || opcode == GEM_GRAF_SHRINKBOX)
		&& !gem_graf_call_shape(call, 8U, 1U, 0U))
	    || (opcode == GEM_GRAF_WATCHBOX
		&& !gem_graf_call_shape(call, 4U, 1U, 1U))
	    || (opcode == GEM_GRAF_SLIDEBOX
		&& !gem_graf_call_shape(call, 3U, 1U, 1U))
	    || (opcode == GEM_GRAF_MKSTATE
		&& !gem_graf_call_shape(call, 0U, 5U, 0U)))
		return gem_graf_malformed(call, handled);
	*handled = TRUE;
	screen = gem_vdi_resident_screen();
	if (!screen)
		return gem_graf_malformed(call, handled);

	if (opcode == GEM_GRAF_MKSTATE) {
		call->int_out[0] = TRUE;
		call->int_out[1] = (UWORD) gem_graf_mouse_x;
		call->int_out[2] = (UWORD) gem_graf_mouse_y;
		call->int_out[3] = gem_graf_mouse_buttons;
		call->int_out[4] = gem_graf_key_state;
		return TRUE;
	}

	if (opcode == GEM_GRAF_MBOX || opcode == GEM_GRAF_GROWBOX
	    || opcode == GEM_GRAF_SHRINKBOX) {
		first.x = (WORD) call->int_in[0];
		first.y = (WORD) call->int_in[1];
		first.width = (WORD) call->int_in[2];
		first.height = (WORD) call->int_in[3];
		if (opcode == GEM_GRAF_MBOX) {
			/* MBOX orders width,height,source x/y,destination x/y. */
			first.width = (WORD) call->int_in[0];
			first.height = (WORD) call->int_in[1];
			first.x = (WORD) call->int_in[2];
			first.y = (WORD) call->int_in[3];
			result = gem_graf_movebox(&first,
				(WORD) call->int_in[4], (WORD) call->int_in[5]);
		} else {
			second.x = (WORD) call->int_in[4];
			second.y = (WORD) call->int_in[5];
			second.width = (WORD) call->int_in[6];
			second.height = (WORD) call->int_in[7];
			result = opcode == GEM_GRAF_GROWBOX
				? gem_graf_growbox(&first, &second)
				: gem_graf_shrinkbox(&first, &second);
		}
		call->int_out[0] = result ? TRUE : FALSE;
		return result;
	}

	if (call->owner >= GEM_GRAF_PD_COUNT)
		return gem_graf_malformed(call, handled);
	pd = gem_graf_pd_at(call->owner);
	if (pd->state != GEM_GRAF_PD_FREE) {
		/* XIF permits one retained request per channel; reject a second. */
		return gem_graf_malformed(call, handled);
	}
	gem_graf_copy_call_to_pd(pd, call, opcode);

	if (opcode == GEM_GRAF_RUBBOX) {
		pd->origin_x = (WORD) call->int_in[0];
		pd->origin_y = (WORD) call->int_in[1];
		pd->minimum_width = (WORD) call->int_in[2];
		pd->minimum_height = (WORD) call->int_in[3];
		if (pd->origin_x < 0 || pd->origin_y < 0
		    || pd->minimum_width <= 0 || pd->minimum_height <= 0
		    || !gem_graf_rubber_rectangle(pd, gem_graf_mouse_x,
			gem_graf_mouse_y, &pd->rectangle))
			goto malformed_pd;
		pd->update_held = TRUE;
		if (effects)
			effects->begin_update = TRUE;
	} else if (opcode == GEM_GRAF_DRAGBOX) {
		pd->rectangle.width = (WORD) call->int_in[0];
		pd->rectangle.height = (WORD) call->int_in[1];
		pd->rectangle.x = (WORD) call->int_in[2];
		pd->rectangle.y = (WORD) call->int_in[3];
		pd->constraint.x = (WORD) call->int_in[4];
		pd->constraint.y = (WORD) call->int_in[5];
		pd->constraint.width = (WORD) call->int_in[6];
		pd->constraint.height = (WORD) call->int_in[7];
		if (!gem_graf_rectangle_valid(screen, &pd->rectangle)
		    || !gem_graf_rectangle_valid(screen, &pd->constraint)
		    || pd->rectangle.width > pd->constraint.width
		    || pd->rectangle.height > pd->constraint.height)
			goto malformed_pd;
		pd->origin_x = gem_graf_mouse_x - pd->rectangle.x;
		pd->origin_y = gem_graf_mouse_y - pd->rectangle.y;
		if (pd->origin_x < 0)
			pd->origin_x = 0;
		if (pd->origin_y < 0)
			pd->origin_y = 0;
		if (!gem_graf_drag_rectangle(pd, gem_graf_mouse_x,
			gem_graf_mouse_y, &pd->rectangle))
			goto malformed_pd;
		pd->update_held = TRUE;
		if (effects)
			effects->begin_update = TRUE;
	} else if (opcode == GEM_GRAF_WATCHBOX) {
		/* int_in[0] is the historical unused/padding word. */
		pd->object = call->int_in[1];
		pd->inside_state = call->int_in[2];
		pd->outside_state = call->int_in[3];
		object = gem_graf_object_at(call, pd->object);
		if (!object || !gem_graf_object_offset(call, pd->object,
			&object_x, &object_y))
			goto malformed_pd;
		pd->rectangle.x = object_x;
		pd->rectangle.y = object_y;
		pd->rectangle.width = (WORD) object->ob_width;
		pd->rectangle.height = (WORD) object->ob_height;
		if (!gem_graf_rectangle_valid(screen, &pd->rectangle)
		    || !gem_graf_object_change_call(call, pd->object,
			pd->inside_state))
			goto malformed_pd;
		pd->watch_inside = TRUE;
	} else {
		pd->object = call->int_in[1];
		pd->vertical = (UBYTE) (call->int_in[2] != 0);
		object = gem_graf_object_at(call, call->int_in[0]);
		if (!object || !gem_graf_object_offset(call, call->int_in[0],
			&parent_x, &parent_y))
			goto malformed_pd;
		pd->constraint.x = parent_x;
		pd->constraint.y = parent_y;
		pd->constraint.width = (WORD) object->ob_width;
		pd->constraint.height = (WORD) object->ob_height;
		object = gem_graf_object_at(call, pd->object);
		if (!object || !gem_graf_object_offset(call, pd->object,
			&object_x, &object_y))
			goto malformed_pd;
		pd->rectangle.x = object_x;
		pd->rectangle.y = object_y;
		pd->rectangle.width = (WORD) object->ob_width;
		pd->rectangle.height = (WORD) object->ob_height;
		if (!gem_graf_rectangle_valid(screen, &pd->constraint)
		    || !gem_graf_rectangle_valid(screen, &pd->rectangle)
		    || pd->rectangle.width > pd->constraint.width
		    || pd->rectangle.height > pd->constraint.height)
			goto malformed_pd;
		pd->origin_x = gem_graf_mouse_x - pd->rectangle.x;
		pd->origin_y = gem_graf_mouse_y - pd->rectangle.y;
		if (pd->origin_x < 0)
			pd->origin_x = 0;
		if (pd->origin_y < 0)
			pd->origin_y = 0;
		pd->update_held = TRUE;
		if (effects)
			effects->begin_update = TRUE;
	}

	/* The classic quick check returns immediately if the button is already up. */
	if (!(gem_graf_mouse_buttons & GEM_GRAF_LEFT_BUTTON)) {
		if (opcode == GEM_GRAF_WATCHBOX) {
			pd->output_count = 1U;
			pd->output[0] = TRUE;
			pd->state = GEM_GRAF_PD_READY;
		} else {
			if (!gem_graf_xor_pair(&pd->rectangle, TRUE,
				&pd->rectangle, TRUE))
				goto malformed_pd;
			gem_graf_finish_tracker(pd);
		}
		if (pd->output_count) {
			UWORD index;

			index = 0;
			while (index < pd->output_count) {
				call->int_out[index] = pd->output[index];
				index++;
			}
			if (effects && pd->update_held)
				effects->end_update = TRUE;
			result = (WORD) pd->output[0];
			gem_graf_clear_pd(pd);
			return result;
		}
	}

	if (opcode != GEM_GRAF_WATCHBOX) {
		if (!gem_graf_xor_pair((const GEM_VDI_RECT *) 0, FALSE,
			&pd->rectangle, TRUE))
			goto malformed_pd;
		pd->outline_visible = TRUE;
	}
	return GEM_GRAF_RESIDENT_DEFERRED;

malformed_pd:
	if (pd->outline_visible)
		gem_graf_xor_pair(&pd->rectangle, TRUE,
			(const GEM_VDI_RECT *) 0, FALSE);
	if (effects && pd->update_held)
		effects->end_update = TRUE;
	gem_graf_clear_pd(pd);
	return gem_graf_malformed(call, handled);
}

WORD
gem_graf_resident_service(UWORD owner, UWORD generation_lo,
	UWORD generation_hi, GEM_GRAF_COMPLETION *completion)
{
	GEM_GRAF_PD *pd;
	UWORD index;

	if (!completion)
		return FALSE;
	gem_graf_clear_completion(completion);
	pd = gem_graf_pd_at(owner);
	if (!gem_graf_pd_matches(pd, generation_lo, generation_hi)
	    || pd->state != GEM_GRAF_PD_READY)
		return FALSE;
	completion->owner = owner;
	completion->generation_lo = generation_lo;
	completion->generation_hi = generation_hi;
	completion->output_count = pd->output_count;
	index = 0;
	while (index < pd->output_count && index < GEM_GRAF_OUTPUT_WORDS) {
		completion->int_out[index] = pd->output[index];
		index++;
	}
	if (pd->update_held)
		completion->effects.end_update = TRUE;
	gem_graf_clear_pd(pd);
	return TRUE;
}

VOID
gem_graf_resident_detach(UWORD owner, UWORD generation_lo,
	UWORD generation_hi, GEM_GRAF_EFFECTS *effects)
{
	GEM_GRAF_PD *pd;

	gem_graf_clear_effects(effects);
	pd = gem_graf_pd_at(owner);
	if (!gem_graf_pd_matches(pd, generation_lo, generation_hi))
		return;
	if (pd->outline_visible)
		gem_graf_xor_pair(&pd->rectangle, TRUE,
			(const GEM_VDI_RECT *) 0, FALSE);
	if (effects && pd->update_held)
		effects->end_update = TRUE;
	gem_graf_clear_pd(pd);
}

WORD
gem_graf_resident_waiting(UWORD owner, UWORD generation_lo,
	UWORD generation_hi)
{
	GEM_GRAF_PD *pd;

	pd = gem_graf_pd_at(owner);
	return gem_graf_pd_matches(pd, generation_lo, generation_hi)
		&& pd->state == GEM_GRAF_PD_WAITING;
}
