/*
 * gem_form_resident.c - form manager for the resident ELKS AES.
 *
 * C translation of the GPL-released Digital Research GEMFMLIB.C,
 * GEMOBED.C, GEMFMALT.C, and GEMSUPER.C (see docs/PROVENANCE.txt).
 *
 * the kernel owns blocking and input on ELKS so a form or alert cant sit in
 * a loop, each dialog lives in one fixed slot and gets stepped whenever
 * input arrives
 */

#include "gem_form_internal.h"
#include "gem_aes_call.h"
#include "gem_resident_memory.h"
#include "gem_system_resource.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

static GEM_FORM_PD gem_form_pds[GEM_FORM_PD_COUNT];

/*
 * an ia16 far pointer is just two words like a GEM pointer slot, only
 * build one after the segment and byte range check out
 */
typedef union gem_form_far_pointer {
	VOID GEM_FORM_FAR *pointer;
	UBYTE GEM_FORM_FAR *bytes;
	GEM_BINDINGS_POINTER_SLOT GEM_FORM_FAR *slot;
	OBJECT GEM_FORM_FAR *object;
	TEDINFO GEM_FORM_FAR *tedinfo;
	GEM_BINDINGS_POINTER_SLOT address;
} GEM_FORM_FAR_POINTER;

typedef UBYTE GEM_FORM_FAR_POINTER_MUST_BE_FOUR_BYTES
	[(sizeof(GEM_FORM_FAR_POINTER) == 4) ? 1 : -1];

GEM_FORM_PD *
gem_form_pd_at(UWORD owner)
{
	GEM_FORM_PD *pd;

	if (owner >= GEM_FORM_PD_COUNT)
		return (GEM_FORM_PD *) 0;
	pd = gem_form_pds;
	while (owner--)
		pd++;
	return pd;
}

static VOID
gem_form_clear_rectangle(GEM_FORM_RECTANGLE *rectangle)
{
	rectangle->x = 0;
	rectangle->y = 0;
	rectangle->width = 0;
	rectangle->height = 0;
}

VOID
gem_form_clear_effects(GEM_FORM_EFFECTS *effects)
{
	if (!effects)
		return;
	effects->tree.lo = 0;
	effects->tree.hi = 0;
	gem_form_clear_rectangle(&effects->rectangle);
	effects->resident_segment = 0;
	effects->tree_kind = GEM_FORM_TREE_NONE;
	effects->begin_update = FALSE;
	effects->end_update = FALSE;
	effects->draw_tree = FALSE;
	effects->redraw_background = FALSE;
}

VOID
gem_form_clear_context(GEM_FORM_CONTEXT *context)
{
	context->resource = (const GEM_RESOURCE_RESIDENT *) 0;
	context->tree.lo = 0;
	context->tree.hi = 0;
	context->objects = (OBJECT GEM_FORM_FAR *) 0;
	context->object_count = 0;
	context->client_segment = 0;
	context->client_limit = 0;
	context->screen_width = 640U;
	context->screen_height = 400U;
	context->character_width = 8U;
	context->character_height = 16U;
}

VOID
gem_form_clear_pd(GEM_FORM_PD *pd)
{
	gem_form_clear_context(&pd->context);
	pd->generation_lo = 0;
	pd->generation_hi = 0;
	pd->result = 0;
	pd->edit_object = GEM_FORM_ROOT;
	pd->next_object = GEM_FORM_ROOT;
	pd->saved_default = GEM_FORM_ROOT;
	pd->pressed_object = GEM_FORM_NIL;
	pd->edit_index = 0;
	pd->pressed_state = 0;
	pd->previous_buttons = 0;
	pd->owner = (UBYTE) GEM_FORM_OWNER_NONE;
	pd->state = GEM_FORM_PD_FREE;
	pd->kind = GEM_FORM_KIND_NONE;
	pd->tree_kind = GEM_FORM_TREE_NONE;
	gem_form_clear_effects(&pd->ready_effects);
}

static WORD
gem_form_pd_matches(const GEM_FORM_PD *pd, UWORD generation_lo,
	UWORD generation_hi)
{
	return pd && pd->state != GEM_FORM_PD_FREE
		&& pd->generation_lo == generation_lo
		&& pd->generation_hi == generation_hi;
}

/* bytes left in a segment the caller can use */
WORD
gem_form_available(const GEM_FORM_CONTEXT *context,
	GEM_BINDINGS_POINTER_SLOT address, UWORD *available)
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
	return FALSE;
}

VOID GEM_FORM_FAR *
gem_form_pointer(const GEM_FORM_CONTEXT *context,
	GEM_BINDINGS_POINTER_SLOT address, UWORD count)
{
	UWORD available;

	if (!gem_form_available(context, address, &available)
		|| count > available)
		return (VOID GEM_FORM_FAR *) 0;
	{
		GEM_FORM_FAR_POINTER pointer;

		pointer.address.lo = address.lo;
		pointer.address.hi = address.hi;
		return pointer.pointer;
	}
}

OBJECT GEM_FORM_FAR *
gem_form_object_at(const GEM_FORM_CONTEXT *context, UWORD object)
{
	OBJECT GEM_FORM_FAR *entry;

	entry = context->objects;
	while (object--)
		entry++;
	return entry;
}

static WORD
gem_form_link_valid(WORD link, UWORD count)
{
	return link == GEM_FORM_NIL
		|| (link >= GEM_FORM_ROOT && (UWORD) link < count);
}

/*
 * check the tree LASTOB extent in the caller/RSC segment so a held
 * FORM_DO cant walk into the next tree
 */
WORD
gem_form_open_tree(GEM_FORM_CONTEXT *context, const GEM_FORM_CALL *call,
	GEM_BINDINGS_POINTER_SLOT tree)
{
	OBJECT GEM_FORM_FAR *object;
	GEM_VDI_SCREEN *screen;
	UWORD available;
	UWORD count;
	UWORD index;
	UBYTE found;

	gem_form_clear_context(context);
	if (!call || !tree.hi || (tree.lo & 1U))
		return FALSE;
	context->resource = call->resource;
	context->tree = tree;
	context->client_segment = call->client_segment;
	context->client_limit = call->client_limit;
	if (call->resource && call->resource->metrics.character_width
		&& call->resource->metrics.character_height) {
		context->screen_width = call->resource->metrics.screen_width;
		context->screen_height = call->resource->metrics.screen_height;
		context->character_width =
			call->resource->metrics.character_width;
		context->character_height =
			call->resource->metrics.character_height;
	} else {
		screen = gem_vdi_resident_screen();
		if (screen) {
			context->screen_width = (UWORD) screen->xres;
			context->screen_height = (UWORD) screen->yres;
		}
	}
	if (!context->screen_width || !context->screen_height)
		return FALSE;
	if (!gem_form_available(context, tree, &available)
		|| available < GEM_FORM_OBJECT_BYTES)
		return FALSE;
	context->objects = (OBJECT GEM_FORM_FAR *) gem_form_pointer(context,
		tree, GEM_FORM_OBJECT_BYTES);
	if (!context->objects)
		return FALSE;

	object = context->objects;
	count = 0;
	found = FALSE;
	while (available >= GEM_FORM_OBJECT_BYTES && count < 0x7fffU) {
		count++;
		if (object->ob_flags & GEM_FORM_LASTOB) {
			found = TRUE;
			break;
		}
		object++;
		available = (UWORD) (available - GEM_FORM_OBJECT_BYTES);
	}
	if (!found || !count)
		return FALSE;
	object = context->objects;
	index = 0;
	while (index < count) {
		if (!gem_form_link_valid(object->ob_next, count)
			|| !gem_form_link_valid(object->ob_head, count)
			|| !gem_form_link_valid(object->ob_tail, count))
			return FALSE;
		object++;
		index++;
	}
	context->object_count = count;
	return TRUE;
}

WORD
gem_form_copy_context(GEM_FORM_CONTEXT *destination,
	const GEM_FORM_CONTEXT *source)
{
	if (!destination || !source || !source->objects
		|| !source->object_count)
		return FALSE;
	destination->resource = source->resource;
	destination->tree = source->tree;
	destination->objects = source->objects;
	destination->object_count = source->object_count;
	destination->client_segment = source->client_segment;
	destination->client_limit = source->client_limit;
	destination->screen_width = source->screen_width;
	destination->screen_height = source->screen_height;
	destination->character_width = source->character_width;
	destination->character_height = source->character_height;
	return TRUE;
}

/* read an object spec, follow one INDIRECT hop if it has one */
static WORD
gem_form_object_spec(const GEM_FORM_CONTEXT *context,
	const OBJECT GEM_FORM_FAR *object, GEM_BINDINGS_POINTER_SLOT *spec)
{
	GEM_BINDINGS_POINTER_SLOT GEM_FORM_FAR *indirect;

	spec->lo = object->ob_spec.lo;
	spec->hi = object->ob_spec.hi;
	if (!(object->ob_flags & GEM_FORM_INDIRECT))
		return TRUE;
	indirect = (GEM_BINDINGS_POINTER_SLOT GEM_FORM_FAR *)
		gem_form_pointer(context, *spec,
		(UWORD) sizeof(GEM_BINDINGS_POINTER_SLOT));
	if (!indirect)
		return FALSE;
	spec->lo = indirect->lo;
	spec->hi = indirect->hi;
	return TRUE;
}

WORD
gem_form_tedinfo(const GEM_FORM_CONTEXT *context, UWORD object_index,
	TEDINFO *tedinfo)
{
	OBJECT GEM_FORM_FAR *object;
	TEDINFO GEM_FORM_FAR *source;
	GEM_BINDINGS_POINTER_SLOT spec;
	UWORD type;

	if (object_index >= context->object_count)
		return FALSE;
	object = gem_form_object_at(context, object_index);
	type = object->ob_type & 0x00ffU;
	if (type != GEM_FORM_G_TEXT && type != GEM_FORM_G_BOXTEXT
		&& type != GEM_FORM_G_FTEXT && type != GEM_FORM_G_FBOXTEXT)
		return FALSE;
	if (!gem_form_object_spec(context, object, &spec))
		return FALSE;
	source = (TEDINFO GEM_FORM_FAR *) gem_form_pointer(context, spec,
		GEM_FORM_TEDINFO_BYTES);
	if (!source)
		return FALSE;
	tedinfo->te_ptext.lo = source->te_ptext.lo;
	tedinfo->te_ptext.hi = source->te_ptext.hi;
	tedinfo->te_ptmplt.lo = source->te_ptmplt.lo;
	tedinfo->te_ptmplt.hi = source->te_ptmplt.hi;
	tedinfo->te_pvalid.lo = source->te_pvalid.lo;
	tedinfo->te_pvalid.hi = source->te_pvalid.hi;
	tedinfo->te_font = source->te_font;
	tedinfo->te_junk1 = source->te_junk1;
	tedinfo->te_just = source->te_just;
	tedinfo->te_color = source->te_color;
	tedinfo->te_junk2 = source->te_junk2;
	tedinfo->te_thickness = source->te_thickness;
	tedinfo->te_txtlen = source->te_txtlen;
	tedinfo->te_tmplen = source->te_tmplen;
	return TRUE;
}

static WORD
gem_form_string_length(const GEM_FORM_CONTEXT *context,
	GEM_BINDINGS_POINTER_SLOT address, UWORD maximum, UWORD *length)
{
	UBYTE GEM_FORM_FAR *text;
	UWORD available;
	UWORD count;

	if (!gem_form_available(context, address, &available) || !available)
		return FALSE;
	if (maximum > available)
		maximum = available;
	text = (UBYTE GEM_FORM_FAR *) gem_form_pointer(context, address, 1U);
	if (!text)
		return FALSE;
	count = 0;
	while (count < maximum) {
		if (!*text) {
			*length = count;
			return TRUE;
		}
		text++;
		count++;
	}
	return FALSE;
}

static WORD
gem_form_draw_object(const GEM_FORM_CONTEXT *context, UWORD object, UWORD depth)
{
	UWORD control[5];
	UWORD input[6];
	UWORD output[1];
	GEM_BINDINGS_POINTER_SLOT address[1];
	GEM_OBJECT_RESIDENT_CALL call;
	WORD handled;

	control[0] = GEM_OBJECT_OBJC_DRAW;
	control[1] = 6U;
	control[2] = 1U;
	control[3] = 1U;
	control[4] = 0;
	input[0] = object;
	input[1] = depth;
	input[2] = 0;
	input[3] = 0;
	input[4] = context->screen_width;
	input[5] = context->screen_height;
	output[0] = FALSE;
	address[0] = context->tree;
	call.resource = context->resource;
	call.client_segment = context->client_segment;
	call.client_limit = context->client_limit;
	call.resident_segment = 0;
	call.control = control;
	call.int_in = input;
	call.int_out = output;
	call.addr_in = address;
	handled = FALSE;
	(void) gem_object_resident_dispatch(&call, &handled);
	return handled && output[0];
}

static WORD
gem_form_change_object(const GEM_FORM_CONTEXT *context, UWORD object,
	UWORD state, UWORD redraw)
{
	UWORD control[5];
	UWORD input[8];
	UWORD output[1];
	GEM_BINDINGS_POINTER_SLOT address[1];
	GEM_OBJECT_RESIDENT_CALL call;
	WORD handled;

	control[0] = GEM_OBJECT_OBJC_CHANGE;
	control[1] = 8U;
	control[2] = 1U;
	control[3] = 1U;
	control[4] = 0;
	input[0] = object;
	input[1] = 0;
	input[2] = 0;
	input[3] = 0;
	input[4] = context->screen_width;
	input[5] = context->screen_height;
	input[6] = state;
	input[7] = redraw;
	output[0] = FALSE;
	address[0] = context->tree;
	call.resource = context->resource;
	call.client_segment = context->client_segment;
	call.client_limit = context->client_limit;
	call.resident_segment = 0;
	call.control = control;
	call.int_in = input;
	call.int_out = output;
	call.addr_in = address;
	handled = FALSE;
	(void) gem_object_resident_dispatch(&call, &handled);
	return handled && output[0];
}

/* reverse-z OBJC_FIND, hitting ROOT (zero) is a hit not a fail */
static WORD
gem_form_find_at(const GEM_FORM_CONTEXT *context, WORD x, WORD y)
{
	UWORD control[5];
	UWORD input[4];
	UWORD output[1];
	GEM_BINDINGS_POINTER_SLOT address[1];
	GEM_OBJECT_RESIDENT_CALL call;
	WORD handled;

	control[0] = GEM_OBJECT_OBJC_FIND;
	control[1] = 4U;
	control[2] = 1U;
	control[3] = 1U;
	control[4] = 0;
	input[0] = GEM_FORM_ROOT;
	input[1] = GEM_FORM_MAX_DEPTH;
	input[2] = (UWORD) x;
	input[3] = (UWORD) y;
	output[0] = (UWORD) GEM_FORM_NIL;
	address[0] = context->tree;
	call.resource = context->resource;
	call.client_segment = context->client_segment;
	call.client_limit = context->client_limit;
	call.resident_segment = 0;
	call.control = control;
	call.int_in = input;
	call.int_out = output;
	call.addr_in = address;
	handled = FALSE;
	(void) gem_object_resident_dispatch(&call, &handled);
	if (!handled)
		return GEM_FORM_NIL;
	return (WORD) output[0];
}

/* find an objects parent, step limit so a bad tree cant loop */
static WORD
gem_form_parent(const GEM_FORM_CONTEXT *context, WORD object_index,
	WORD *next_object)
{
	WORD parent;
	WORD current;
	WORD next;
	UWORD steps;

	parent = object_index;
	next = GEM_FORM_NIL;
	if (object_index == GEM_FORM_ROOT) {
		*next_object = GEM_FORM_NIL;
		return GEM_FORM_NIL;
	}
	steps = context->object_count;
	do {
		if (!steps || parent < GEM_FORM_ROOT
			|| (UWORD) parent >= context->object_count) {
			*next_object = GEM_FORM_NIL;
			return GEM_FORM_NIL;
		}
		steps--;
		current = parent;
		parent = gem_form_object_at(context, (UWORD) current)->ob_next;
		if (next == GEM_FORM_NIL)
			next = parent;
		if (parent < GEM_FORM_ROOT)
			break;
		if ((UWORD) parent >= context->object_count) {
			*next_object = GEM_FORM_NIL;
			return GEM_FORM_NIL;
		}
		/* parent tail marks the end of the sibling chain */
		if (gem_form_object_at(context, (UWORD) parent)->ob_tail
			== current)
			break;
	} while (TRUE);
	*next_object = next;
	return parent;
}

/* walk objects in record order, LASTOB is where it stops */
static WORD
gem_form_find_object(const GEM_FORM_CONTEXT *context, WORD start_object,
	UWORD which, UWORD flag)
{
	OBJECT GEM_FORM_FAR *object;
	WORD index;
	WORD increment;

	index = GEM_FORM_ROOT;
	increment = 1;
	if (which == GEM_FORM_BACKWARD) {
		increment = -1;
		index = start_object - 1;
	} else if (which == GEM_FORM_FORWARD) {
		index = start_object + 1;
	} else {
		flag = GEM_FORM_DEFAULT;
	}
	while (index >= GEM_FORM_ROOT && (UWORD) index < context->object_count) {
		object = gem_form_object_at(context, (UWORD) index);
		if (!(object->ob_flags & GEM_FORM_HIDETREE)
			&& !(object->ob_state & GEM_FORM_DISABLED)
			&& (object->ob_flags & flag))
			return index;
		if (object->ob_flags & GEM_FORM_LASTOB)
			index = GEM_FORM_NIL;
		else
			index += increment;
	}
	return start_object;
}

static WORD
gem_form_initial_field(const GEM_FORM_CONTEXT *context, WORD start_field)
{
	if (!start_field)
		start_field = gem_form_find_object(context, GEM_FORM_ROOT,
			GEM_FORM_FORWARD, GEM_FORM_DEFAULT);
	return start_field;
}

/* ASCII-only uppercase */
static UBYTE
gem_form_upper(UBYTE character)
{
	if (character >= 'a' && character <= 'z')
		character = (UBYTE) (character - ('a' - 'A'));
	return character;
}

static WORD
gem_form_is_letter(UBYTE character)
{
	return (character >= 'A' && character <= 'Z')
		|| (character >= 'a' && character <= 'z');
}

static WORD
gem_form_is_digit(UBYTE character)
{
	return character >= '0' && character <= '9';
}

static WORD
gem_form_dos_punctuation(UBYTE character, UBYTE validation)
{
	if (character == '_' || character == '-' || character == '$'
		|| character == '~' || character == '!' || character == '#'
		|| character == '%' || character == '&' || character == '@'
		|| character == '^' || character == '(' || character == ')')
		return TRUE;
	if (validation == 'P'
		&& (character == '\\' || character == '?' || character == '*'
			|| character == ':' || character == '.'
			|| character == ','))
		return TRUE;
	if (validation == 'p'
		&& (character == '\\' || character == ':' || character == '.'))
		return TRUE;
	if (validation == 'F'
		&& (character == ':' || character == '?' || character == '*'
			|| character == '.'))
		return TRUE;
	if (validation == 'f' && character == '.')
		return TRUE;
	return FALSE;
}

/* character validation categories */
static WORD
gem_form_validate_character(UBYTE *character, UBYTE validation)
{
	UBYTE value;

	value = *character;
	switch (validation) {
	case '9':
		return gem_form_is_digit(value);
	case 'A':
		value = gem_form_upper(value);
		if ((value >= 'A' && value <= 'Z') || value == ' ') {
			*character = value;
			return TRUE;
		}
		return FALSE;
	case 'N':
		value = gem_form_upper(value);
		if (gem_form_is_digit(value)
			|| (value >= 'A' && value <= 'Z') || value == ' ') {
			*character = value;
			return TRUE;
		}
		return FALSE;
	case 'a':
		return gem_form_is_letter(value) || value == ' ';
	case 'n':
		return gem_form_is_letter(value) || gem_form_is_digit(value)
			|| value == ' ';
	case 'x':
		*character = gem_form_upper(value);
		return TRUE;
	case 'X':
		return TRUE;
	case 'P':
	case 'p':
	case 'F':
	case 'f':
		if (gem_form_is_letter(value) || gem_form_is_digit(value)
			|| value == ' '
			|| gem_form_dos_punctuation(value, validation)) {
			if (validation == 'P' || validation == 'F')
				*character = gem_form_upper(value);
			return TRUE;
		}
		return FALSE;
	default:
		return FALSE;
	}
}

static WORD
gem_form_validation_at(const GEM_FORM_CONTEXT *context,
	GEM_BINDINGS_POINTER_SLOT address, UWORD index, UBYTE *validation)
{
	UBYTE GEM_FORM_FAR *text;
	UWORD length;
	UWORD position;

	if (!gem_form_string_length(context, address,
			GEM_FORM_MAX_EDIT_TEXT, &length) || !length)
		return FALSE;
	position = index;
	if (position >= length)
		position = (UWORD) (length - 1U);
	text = (UBYTE GEM_FORM_FAR *) gem_form_pointer(context, address,
		(UWORD) (position + 1U));
	if (!text)
		return FALSE;
	while (position--)
		text++;
	*validation = *text;
	return TRUE;
}

/*
 * the field editor, text edited in place, the highlighted-field state marks
 * the active field
 */
static WORD
gem_form_edit(GEM_FORM_CONTEXT *context, UWORD object_index,
	UWORD input_character, UWORD *index, UWORD kind)
{
	TEDINFO tedinfo;
	UBYTE GEM_FORM_FAR *text;
	UBYTE character;
	UBYTE validation;
	UWORD length;
	UWORD position;
	UWORD limit;
	UBYTE changed;

	if (kind == GEM_FORM_EDSTART || object_index == GEM_FORM_ROOT)
		return TRUE;
	if (!gem_form_tedinfo(context, object_index, &tedinfo)
		|| tedinfo.te_txtlen <= 0
		|| (UWORD) tedinfo.te_txtlen > GEM_FORM_MAX_EDIT_TEXT)
		return FALSE;
	limit = (UWORD) tedinfo.te_txtlen;
	if (!gem_form_string_length(context, tedinfo.te_ptext, limit, &length))
		return FALSE;
	text = (UBYTE GEM_FORM_FAR *) gem_form_pointer(context,
		tedinfo.te_ptext, limit);
	if (!text)
		return FALSE;
	if (*index > length)
		*index = length;
	if (kind == GEM_FORM_EDINIT) {
		*index = length;
		return TRUE;
	}
	if (kind == GEM_FORM_EDEND)
		return TRUE;
	if (kind != GEM_FORM_EDCHAR)
		return FALSE;

	changed = FALSE;
	switch (input_character) {
	case GEM_FORM_KEY_BACKSPACE:
		if (*index) {
			*index = (UWORD) (*index - 1U);
			position = *index;
			while (position < length) {
				text[position] = text[position + 1U];
				position++;
			}
			changed = TRUE;
		}
		break;
	case GEM_FORM_KEY_DELETE:
		if (*index < length) {
			position = *index;
			while (position < length) {
				text[position] = text[position + 1U];
				position++;
			}
			changed = TRUE;
		}
		break;
	case GEM_FORM_KEY_LEFT:
		if (*index)
			*index = (UWORD) (*index - 1U);
		break;
	case GEM_FORM_KEY_RIGHT:
		if (*index < length)
			*index = (UWORD) (*index + 1U);
		break;
	case GEM_FORM_KEY_TAB:
	case GEM_FORM_KEY_BACKTAB:
	case GEM_FORM_KEY_RETURN:
		break;
	default:
		character = (UBYTE) (input_character & 0x00ffU);
		if (!character || length + 1U >= limit
			|| !gem_form_validation_at(context, tedinfo.te_pvalid,
				*index, &validation)
			|| !gem_form_validate_character(&character, validation))
			break;
		position = length;
		while (position > *index) {
			text[position] = text[position - 1U];
			position--;
		}
		text[*index] = character;
		*index = (UWORD) (*index + 1U);
		text[length + 1U] = 0;
		changed = TRUE;
		break;
	}
	if (changed || input_character == GEM_FORM_KEY_LEFT
		|| input_character == GEM_FORM_KEY_RIGHT)
		return gem_form_draw_object(context, object_index, 0U);
	return TRUE;
}

/* handle a key in a dialog, returns FALSE when RETURN picks a default exit */
static WORD
gem_form_keyboard(const GEM_FORM_CONTEXT *context, WORD object,
	UWORD *character, WORD *new_object)
{
	UWORD direction;

	direction = 0xffffU;
	switch (*character) {
	case GEM_FORM_KEY_RETURN:
		object = GEM_FORM_ROOT;
		direction = GEM_FORM_DEFLT;
		break;
	case GEM_FORM_KEY_BACKTAB:
	case GEM_FORM_KEY_UP:
		direction = GEM_FORM_BACKWARD;
		break;
	case GEM_FORM_KEY_TAB:
	case GEM_FORM_KEY_DOWN:
		direction = GEM_FORM_FORWARD;
		break;
	default:
		break;
	}
	if (direction != 0xffffU) {
		*character = 0;
		*new_object = gem_form_find_object(context, object, direction,
			GEM_FORM_EDITABLE);
		if (direction == GEM_FORM_DEFLT && *new_object != GEM_FORM_ROOT) {
			OBJECT GEM_FORM_FAR *entry;

			entry = gem_form_object_at(context,
				(UWORD) *new_object);
			(void) gem_form_change_object(context,
				(UWORD) *new_object,
				(UWORD) (entry->ob_state | GEM_FORM_SELECTED),
				TRUE);
			return FALSE;
		}
	}
	return TRUE;
}

/* record-order focus walks with wrap */
static WORD
gem_form_focus_forward(const GEM_FORM_CONTEXT *context, WORD start)
{
	OBJECT GEM_FORM_FAR *object;
	UWORD index;

	if (start < GEM_FORM_ROOT)
		start = GEM_FORM_ROOT;
	index = (UWORD) start;
	if (index + 1U < context->object_count)
		index++;
	else
		index = 0;
	while (index != (UWORD) start) {
		object = gem_form_object_at(context, index);
		if (!(object->ob_flags & GEM_FORM_HIDETREE)
			&& !(object->ob_state & GEM_FORM_DISABLED)
			&& (object->ob_flags
				& (GEM_FORM_EDITABLE | GEM_FORM_SELECTABLE)))
			return (WORD) index;
		if (index + 1U < context->object_count)
			index++;
		else
			index = 0;
	}
	return start;
}

static WORD
gem_form_focus_backward(const GEM_FORM_CONTEXT *context, WORD start)
{
	OBJECT GEM_FORM_FAR *object;
	UWORD index;
	UWORD stop;

	if (start < GEM_FORM_ROOT || (UWORD) start >= context->object_count)
		start = GEM_FORM_ROOT;
	stop = (UWORD) start;
	if (stop)
		index = (UWORD) (stop - 1U);
	else
		index = (UWORD) (context->object_count - 1U);
	while (index != stop) {
		object = gem_form_object_at(context, index);
		if (!(object->ob_flags & GEM_FORM_HIDETREE)
			&& !(object->ob_state & GEM_FORM_DISABLED)
			&& (object->ob_flags
				& (GEM_FORM_EDITABLE | GEM_FORM_SELECTABLE)))
			return (WORD) index;
		if (index)
			index--;
		else
			index = (UWORD) (context->object_count - 1U);
	}
	return start;
}

/* show the highlighted-field state through ob_change() */
static WORD
gem_form_field_cursor(GEM_FORM_CONTEXT *context, WORD object_index,
	UWORD enabled)
{
	OBJECT GEM_FORM_FAR *object;
	UWORD flags;
	UWORD state;

	if (object_index <= GEM_FORM_ROOT
		|| (UWORD) object_index >= context->object_count)
		return TRUE;
	object = gem_form_object_at(context, (UWORD) object_index);
	flags = object->ob_flags;
	state = object->ob_state;
	if (!(flags & (GEM_FORM_SELECTABLE | GEM_FORM_EDITABLE)))
		return TRUE;
	if (enabled && !(state & GEM_FORM_HIGHLIGHTED)) {
		if (flags & GEM_FORM_EXIT)
			object->ob_flags = (UWORD) (flags | GEM_FORM_DEFAULT);
		return gem_form_change_object(context, (UWORD) object_index,
			(UWORD) (state | GEM_FORM_HIGHLIGHTED), FALSE);
	}
	if (!enabled && (state & GEM_FORM_HIGHLIGHTED)) {
		if ((flags & GEM_FORM_EXIT) && (flags & GEM_FORM_DEFAULT))
			object->ob_flags = (UWORD) (flags & ~GEM_FORM_DEFAULT);
		return gem_form_change_object(context, (UWORD) object_index,
			(UWORD) (state & ~GEM_FORM_HIGHLIGHTED), FALSE);
	}
	return TRUE;
}

/*
 * button selection/radio/exit logic, previewed means the press already
 * XORed the state while waiting for release
 */
static WORD
gem_form_button(GEM_FORM_CONTEXT *context, WORD new_object, UWORD clicks,
	UWORD previewed, WORD *result_object)
{
	OBJECT GEM_FORM_FAR *entry;
	OBJECT GEM_FORM_FAR *parent_entry;
	OBJECT GEM_FORM_FAR *sibling;
	WORD parent;
	WORD next;
	WORD current;
	UWORD state;
	UWORD flags;
	UWORD sibling_state;
	UWORD output;
	UWORD steps;
	WORD cont;

	if (new_object < GEM_FORM_ROOT
		|| (UWORD) new_object >= context->object_count) {
		*result_object = GEM_FORM_ROOT;
		return TRUE;
	}
	entry = gem_form_object_at(context, (UWORD) new_object);
	state = entry->ob_state;
	flags = entry->ob_flags;
	cont = TRUE;
	output = (UWORD) new_object;
	if (flags & GEM_FORM_TOUCHEXIT) {
		if (clicks == 2U)
			output |= GEM_FORM_HIGH_OBJECT;
		cont = FALSE;
	}
	if ((flags & GEM_FORM_SELECTABLE) && !(state & GEM_FORM_DISABLED)) {
		if (flags & GEM_FORM_RBUTTON) {
			parent = gem_form_parent(context, new_object, &next);
			if (parent >= GEM_FORM_ROOT
				&& (UWORD) parent < context->object_count) {
				parent_entry = gem_form_object_at(context,
					(UWORD) parent);
				current = parent_entry->ob_head;
				steps = context->object_count;
				while (steps-- && current != parent
					&& current >= GEM_FORM_ROOT
					&& (UWORD) current <
					context->object_count) {
					sibling =
						gem_form_object_at(context,
						(UWORD) current);
					sibling_state = sibling->ob_state;
					if ((sibling->ob_flags &
							GEM_FORM_RBUTTON)
						&& ((sibling_state &
								GEM_FORM_SELECTED)
							|| current ==
							new_object)) {
						if (current == new_object)
							sibling_state |=
								GEM_FORM_SELECTED;
						else
							sibling_state &=
								~GEM_FORM_SELECTED;
						(void) gem_form_change_object
							(context,
							(UWORD) current,
							sibling_state, TRUE);
					}
					current = sibling->ob_next;
				}
				state = entry->ob_state;
			}
		} else if (!previewed) {
			state ^= GEM_FORM_SELECTED;
			(void) gem_form_change_object(context,
				(UWORD) new_object, state, TRUE);
		} else {
			state = entry->ob_state;
		}
	}
	if ((state & GEM_FORM_SELECTED) && (flags & GEM_FORM_EXIT))
		cont = FALSE;
	if (cont && ((flags & GEM_FORM_HIDETREE)
			|| (state & GEM_FORM_DISABLED)
			|| !(flags & GEM_FORM_EDITABLE)))
		output = GEM_FORM_ROOT;
	*result_object = (WORD) output;
	return cont;
}

WORD
gem_form_switch_field(GEM_FORM_PD *pd, WORD next_object)
{
	OBJECT GEM_FORM_FAR *old;
	OBJECT GEM_FORM_FAR *next;

	if (next_object <= GEM_FORM_ROOT
		|| (UWORD) next_object >= pd->context.object_count
		|| next_object == pd->edit_object)
		return TRUE;
	if (pd->edit_object > GEM_FORM_ROOT
		&& (UWORD) pd->edit_object < pd->context.object_count) {
		old = gem_form_object_at(&pd->context, (UWORD) pd->edit_object);
		if (old->ob_flags & GEM_FORM_EDITABLE)
			(void) gem_form_edit(&pd->context,
				(UWORD) pd->edit_object, 0, &pd->edit_index,
				GEM_FORM_EDEND);
		(void) gem_form_field_cursor(&pd->context, pd->edit_object,
			FALSE);
	}
	pd->edit_object = next_object;
	pd->next_object = GEM_FORM_ROOT;
	pd->edit_index = 0;
	next = gem_form_object_at(&pd->context, (UWORD) next_object);
	(void) gem_form_field_cursor(&pd->context, next_object, TRUE);
	if (next->ob_flags & GEM_FORM_EDITABLE)
		return gem_form_edit(&pd->context, (UWORD) next_object, 0,
			&pd->edit_index, GEM_FORM_EDINIT);
	return TRUE;
}

VOID
gem_form_finish(GEM_FORM_PD *pd, UWORD result)
{
	OBJECT GEM_FORM_FAR *entry;

	if (pd->tree_kind != GEM_FORM_TREE_ALERT) {
		if (pd->edit_object > GEM_FORM_ROOT
			&& (UWORD) pd->edit_object < pd->context.object_count) {
			entry = gem_form_object_at(&pd->context,
				(UWORD) pd->edit_object);
			if (entry->ob_flags & GEM_FORM_EDITABLE)
				(void) gem_form_edit(&pd->context,
					(UWORD) pd->edit_object, 0,
					&pd->edit_index, GEM_FORM_EDEND);
			(void) gem_form_field_cursor(&pd->context,
				pd->edit_object, FALSE);
		}
		if (pd->saved_default > GEM_FORM_ROOT
			&& (UWORD) pd->saved_default < pd->context.object_count)
			gem_form_object_at(&pd->context,
				(UWORD) pd->saved_default)->ob_flags |=
				GEM_FORM_DEFAULT;
	}
	pd->result = result;
	pd->pressed_object = GEM_FORM_NIL;
	pd->state = GEM_FORM_PD_READY;
	gem_form_clear_effects(&pd->ready_effects);
	pd->ready_effects.end_update = TRUE;
	if (pd->tree_kind == GEM_FORM_TREE_ALERT) {
		pd->ready_effects.redraw_background = TRUE;
		gem_form_alert_rectangle(&pd->ready_effects.rectangle);
	}
}

/* round down to the character-cell grid */
static UWORD
gem_form_align_down(UWORD value, UWORD quantum)
{
	UWORD aligned;

	if (!quantum)
		return value;
	aligned = 0;
	while (aligned <= value && quantum <= (UWORD) (value - aligned))
		aligned = (UWORD) (aligned + quantum);
	return aligned;
}

UWORD
gem_form_cells(UWORD cells, UWORD cell_size)
{
	UWORD pixels;

	pixels = 0;
	while (cells-- && cell_size <= (UWORD) (0xffffU - pixels))
		pixels = (UWORD) (pixels + cell_size);
	return pixels;
}

/* center a tree on screen */
WORD
gem_form_center_tree(GEM_FORM_CONTEXT *context, UWORD *output)
{
	OBJECT GEM_FORM_FAR *root;
	UWORD width;
	UWORD height;
	UWORD usable_height;
	UWORD twenty_five_rows;
	UWORD x;
	UWORD y;
	UWORD rectangle_x;
	UWORD rectangle_y;
	UWORD rectangle_width;
	UWORD rectangle_height;

	root = gem_form_object_at(context, GEM_FORM_ROOT);
	width = root->ob_width;
	height = root->ob_height;
	if (!width || !height || width > context->screen_width
		|| height > context->screen_height)
		return FALSE;
	x = (UWORD) ((context->screen_width - width) >> 1);
	x = gem_form_align_down(x, context->character_width);
	twenty_five_rows = gem_form_cells(25U, context->character_height);
	usable_height = context->screen_height;
	if (twenty_five_rows < usable_height)
		usable_height = twenty_five_rows;
	if (usable_height <= context->character_height
		|| height > (UWORD) (usable_height - context->character_height))
		y = context->character_height;
	else
		y = (UWORD) (context->character_height
			+ ((usable_height - context->character_height -
					height) >> 1));
	root->ob_x = x;
	root->ob_y = y;
	rectangle_x = x;
	rectangle_y = y;
	rectangle_width = width;
	rectangle_height = height;
	if (root->ob_state & (GEM_FORM_OUTLINED | GEM_FORM_SHADOWED)) {
		rectangle_x = x >= 8U ? (UWORD) (x - 8U) : 0;
		rectangle_y = y >= 8U ? (UWORD) (y - 8U) : 0;
		if (width <= 0xffefU)
			rectangle_width = (UWORD) (width + 16U);
		if (height <= 0xffefU)
			rectangle_height = (UWORD) (height + 16U);
	}
	output[0] = TRUE;
	output[1] = rectangle_x;
	output[2] = rectangle_y;
	output[3] = rectangle_width;
	output[4] = rectangle_height;
	return TRUE;
}

WORD
gem_form_inside(WORD x, WORD y, WORD left, WORD top, WORD width, WORD height)
{
	WORD right;
	WORD bottom;

	right = (WORD) ((UWORD) left + (UWORD) width);
	bottom = (WORD) ((UWORD) top + (UWORD) height);
	return x >= left && y >= top && x < right && y < bottom;
}

static VOID
gem_form_copy_effects(GEM_FORM_EFFECTS *destination,
	const GEM_FORM_EFFECTS *source)
{
	destination->tree.lo = source->tree.lo;
	destination->tree.hi = source->tree.hi;
	destination->rectangle.x = source->rectangle.x;
	destination->rectangle.y = source->rectangle.y;
	destination->rectangle.width = source->rectangle.width;
	destination->rectangle.height = source->rectangle.height;
	destination->resident_segment = source->resident_segment;
	destination->tree_kind = source->tree_kind;
	destination->begin_update = source->begin_update;
	destination->end_update = source->end_update;
	destination->draw_tree = source->draw_tree;
	destination->redraw_background = source->redraw_background;
}

static WORD
gem_form_call_shape(const GEM_FORM_CALL *call, UWORD input_count,
	UWORD output_count, UWORD address_count)
{
	if (!call)
		return FALSE;
	return gem_aes_call_counts(call->control, input_count, output_count,
		address_count)
		&& gem_aes_call_arrays(call->int_in, input_count,
		call->int_out, output_count, call->addr_in, address_count);
}

static WORD
gem_form_malformed(const GEM_FORM_CALL *call, WORD *handled)
{
	return gem_aes_call_malformed(call ? call->control : (const UWORD *) 0,
		call ? call->int_out : (UWORD *) 0, handled);
}

VOID
gem_form_resident_fsel_result(BYTE *path, UWORD path_size, BYTE *name,
	UWORD name_size)
{
	gem_form_fsel_result(path, path_size, name, name_size);
}

static WORD
gem_form_begin_do(const GEM_FORM_CALL *call, GEM_FORM_EFFECTS *effects)
{
	GEM_FORM_CONTEXT context;
	GEM_FORM_PD *pd;
	OBJECT GEM_FORM_FAR *entry;
	WORD start;
	WORD initial;
	WORD saved_default;

	pd = gem_form_pd_at(call->owner);
	if (!pd || pd->state != GEM_FORM_PD_FREE
		|| !gem_form_open_tree(&context, call, call->addr_in[0]))
		return FALSE;
	start = (WORD) call->int_in[0];
	if (start < GEM_FORM_ROOT || (UWORD) start >= context.object_count)
		return FALSE;
	gem_form_clear_pd(pd);
	if (!gem_form_copy_context(&pd->context, &context))
		return FALSE;
	pd->owner = (UBYTE) call->owner;
	pd->generation_lo = call->generation_lo;
	pd->generation_hi = call->generation_hi;
	pd->kind = GEM_FORM_KIND_DO;
	pd->tree_kind = GEM_FORM_TREE_CALLER;
	pd->state = GEM_FORM_PD_WAITING;
	pd->edit_object = GEM_FORM_ROOT;
	pd->next_object = GEM_FORM_ROOT;
	saved_default = gem_form_find_object(&pd->context, GEM_FORM_ROOT,
		GEM_FORM_FORWARD, GEM_FORM_DEFAULT);
	pd->saved_default = GEM_FORM_ROOT;
	if (saved_default > GEM_FORM_ROOT
		&& (UWORD) saved_default < pd->context.object_count) {
		entry = gem_form_object_at(&pd->context, (UWORD) saved_default);
		if (entry->ob_flags & GEM_FORM_DEFAULT)
			pd->saved_default = saved_default;
	}
	initial = gem_form_initial_field(&pd->context, start);
	if (initial > GEM_FORM_ROOT && !gem_form_switch_field(pd, initial)) {
		gem_form_clear_pd(pd);
		return FALSE;
	}
	gem_form_clear_effects(effects);
	effects->begin_update = TRUE;
	return TRUE;
}

VOID
gem_form_metrics(const GEM_FORM_CALL *call, UWORD *screen_width,
	UWORD *screen_height, UWORD *character_width, UWORD *character_height)
{
	GEM_VDI_SCREEN *screen;

	*screen_width = 640U;
	*screen_height = 400U;
	*character_width = 8U;
	*character_height = 16U;
	if (call->resource && call->resource->metrics.character_width
		&& call->resource->metrics.character_height) {
		*screen_width = call->resource->metrics.screen_width;
		*screen_height = call->resource->metrics.screen_height;
		*character_width = call->resource->metrics.character_width;
		*character_height = call->resource->metrics.character_height;
		return;
	}
	screen = gem_vdi_resident_screen();
	if (screen) {
		*screen_width = (UWORD) screen->xres;
		*screen_height = (UWORD) screen->yres;
	}
}

/*
 * map a DOS error number to an alert string, the text lives in GEM.RSC so
 * none of it is spelled out here
 */
static UWORD
gem_form_error_index(UWORD error)
{
	switch (error) {
	case 2U:
	case 3U:
	case 18U:
		return GEM_SYSTEM_AL18ERR;
	case 4U:
		return GEM_SYSTEM_AL04ERR;
	case 5U:
		return GEM_SYSTEM_AL05ERR;
	case 8U:
	case 10U:
	case 11U:
		return GEM_SYSTEM_AL08ERR;
	case 15U:
		return GEM_SYSTEM_AL15ERR;
	case 16U:
		return GEM_SYSTEM_AL16ERR;
	default:
		return GEM_SYSTEM_ALXXERR;
	}
}

static WORD
gem_form_dispatch_dial(const GEM_FORM_CALL *call, GEM_FORM_EFFECTS *effects)
{
	UWORD type;

	type = call->int_in[0];
	if (type > GEM_FORM_FMD_FINISH)
		return FALSE;
	gem_form_clear_effects(effects);
	if (type == GEM_FORM_FMD_FINISH) {
		effects->redraw_background = TRUE;
		effects->rectangle.x = (WORD) call->int_in[5];
		effects->rectangle.y = (WORD) call->int_in[6];
		effects->rectangle.width = (WORD) call->int_in[7];
		effects->rectangle.height = (WORD) call->int_in[8];
	}
	/* GROW/SHRINK draw no animation, final geometry is the same */
	call->int_out[0] = TRUE;
	return TRUE;
}

static WORD
gem_form_dispatch_edit(const GEM_FORM_CALL *call)
{
	GEM_FORM_CONTEXT context;
	UWORD index;
	WORD result;

	if (!gem_form_open_tree(&context, call, call->addr_in[0]))
		return FALSE;
	index = call->int_in[2];
	result = gem_form_edit(&context, call->int_in[0], call->int_in[1],
		&index, call->int_in[3]);
	call->int_out[0] = (UWORD) result;
	call->int_out[1] = index;
	return result;
}

static WORD
gem_form_dispatch_keyboard(const GEM_FORM_CALL *call)
{
	GEM_FORM_CONTEXT context;
	UWORD character;
	WORD next_object;
	WORD result;

	if (!gem_form_open_tree(&context, call, call->addr_in[0]))
		return FALSE;
	character = call->int_in[1];
	next_object = (WORD) call->int_in[2];
	result = gem_form_keyboard(&context, (WORD) call->int_in[0],
		&character, &next_object);
	call->int_out[0] = (UWORD) result;
	call->int_out[1] = (UWORD) next_object;
	call->int_out[2] = character;
	return result;
}

static WORD
gem_form_dispatch_button(const GEM_FORM_CALL *call)
{
	GEM_FORM_CONTEXT context;
	WORD next_object;
	WORD result;

	if (!gem_form_open_tree(&context, call, call->addr_in[0]))
		return FALSE;
	result = gem_form_button(&context, (WORD) call->int_in[0],
		call->int_in[1], FALSE, &next_object);
	call->int_out[0] = (UWORD) result;
	call->int_out[1] = (UWORD) next_object;
	return result;
}

WORD
gem_form_resident_dispatch(const GEM_FORM_CALL *call,
	GEM_FORM_EFFECTS *effects, WORD *handled)
{
	GEM_FORM_CONTEXT context;
	UWORD opcode;
	WORD result;

	if (!handled)
		return FALSE;
	*handled = FALSE;
	if (!call || !call->control || !effects)
		return FALSE;
	opcode = call->control[0];
	if (opcode != GEM_FORM_OBJC_EDIT && opcode != GEM_FORM_DO
		&& opcode != GEM_FORM_DIAL && opcode != GEM_FORM_ALERT
		&& opcode != GEM_FORM_ERROR && opcode != GEM_FORM_CENTER
		&& opcode != GEM_FORM_KEYBD && opcode != GEM_FORM_BUTTON
		&& opcode != GEM_FORM_FSEL_INPUT
		&& opcode != GEM_FORM_FSEL_EXINPUT)
		return FALSE;
	gem_form_clear_effects(effects);
	if ((opcode == GEM_FORM_OBJC_EDIT
			&& !gem_form_call_shape(call, 4U, 2U, 1U))
		|| (opcode == GEM_FORM_DO
			&& !gem_form_call_shape(call, 1U, 1U, 1U))
		|| (opcode == GEM_FORM_DIAL
			&& !gem_form_call_shape(call, 9U, 1U, 0U))
		|| (opcode == GEM_FORM_ALERT
			&& !gem_form_call_shape(call, 1U, 1U, 1U))
		|| (opcode == GEM_FORM_ERROR
			&& !gem_form_call_shape(call, 1U, 1U, 0U))
		|| (opcode == GEM_FORM_CENTER
			&& !gem_form_call_shape(call, 0U, 5U, 1U))
		|| (opcode == GEM_FORM_KEYBD
			&& !gem_form_call_shape(call, 3U, 3U, 1U))
		|| (opcode == GEM_FORM_BUTTON
			&& !gem_form_call_shape(call, 2U, 2U, 1U))
		|| ((opcode == GEM_FORM_FSEL_INPUT
				|| opcode == GEM_FORM_FSEL_EXINPUT)
			&& !gem_form_call_shape(call, 0U, 2U, 2U)))
		return gem_form_malformed(call, handled);
	*handled = TRUE;
	if (opcode == GEM_FORM_OBJC_EDIT) {
		result = gem_form_dispatch_edit(call);
		return result ? result : gem_form_malformed(call, handled);
	}
	if (opcode == GEM_FORM_DO) {
		if (!gem_form_begin_do(call, effects))
			return gem_form_malformed(call, handled);
		return GEM_FORM_RESIDENT_DEFERRED;
	}
	if (opcode == GEM_FORM_FSEL_INPUT || opcode == GEM_FORM_FSEL_EXINPUT) {
		/*
		 * FSEL_EXINPUT extra title string is taken and ignored,
		 * the selector uses its own title
		 */
		if (!gem_form_begin_fsel(call, effects))
			return gem_form_malformed(call, handled);
		return GEM_FORM_RESIDENT_DEFERRED;
	}
	if (opcode == GEM_FORM_DIAL) {
		if (!gem_form_dispatch_dial(call, effects))
			return gem_form_malformed(call, handled);
		return TRUE;
	}
	if (opcode == GEM_FORM_ALERT) {
		if (!gem_form_alert_source_from_call(call, call->addr_in[0])
			|| !gem_form_begin_alert(call, GEM_FORM_KIND_ALERT,
				call->int_in[0], effects))
			return gem_form_malformed(call, handled);
		return GEM_FORM_RESIDENT_DEFERRED;
	}
	if (opcode == GEM_FORM_ERROR) {
		if (call->int_in[0] > 63U) {
			call->int_out[0] = FALSE;
			return FALSE;
		}
		if (!gem_form_alert_source_string(gem_form_error_index
				(call->int_in[0]), call->int_in[0])
			|| !gem_form_begin_alert(call, GEM_FORM_KIND_ERROR, 1U,
				effects))
			return gem_form_malformed(call, handled);
		return GEM_FORM_RESIDENT_DEFERRED;
	}
	if (opcode == GEM_FORM_CENTER) {
		if (!gem_form_open_tree(&context, call, call->addr_in[0])
			|| !gem_form_center_tree(&context, call->int_out))
			return gem_form_malformed(call, handled);
		return TRUE;
	}
	if (opcode == GEM_FORM_KEYBD) {
		result = gem_form_dispatch_keyboard(call);
		/* FALSE is a real RETURN/default result, not malformed input */
		return result;
	}
	result = gem_form_dispatch_button(call);
	return result;
}

static VOID
gem_form_normal_key(GEM_FORM_PD *pd, UWORD key)
{
	OBJECT GEM_FORM_FAR *entry;
	WORD target;
	WORD result_object;
	WORD cont;

	target = GEM_FORM_NIL;
	entry = gem_form_object_at(&pd->context, (UWORD) pd->edit_object);
	switch (key) {
	case GEM_FORM_KEY_UP:
	case GEM_FORM_KEY_BACKTAB:
		target = gem_form_focus_backward(&pd->context, pd->edit_object);
		break;
	case GEM_FORM_KEY_DOWN:
	case GEM_FORM_KEY_TAB:
		target = gem_form_focus_forward(&pd->context, pd->edit_object);
		break;
	case GEM_FORM_KEY_LEFT:
	case GEM_FORM_KEY_RIGHT:
		if (entry->ob_flags & GEM_FORM_EDITABLE)
			(void) gem_form_edit(&pd->context,
				(UWORD) pd->edit_object, key, &pd->edit_index,
				GEM_FORM_EDCHAR);
		else if (key == GEM_FORM_KEY_LEFT)
			target = gem_form_focus_backward(&pd->context,
				pd->edit_object);
		else
			target = gem_form_focus_forward(&pd->context,
				pd->edit_object);
		break;
	case GEM_FORM_KEY_RETURN:
		if (entry->ob_flags & GEM_FORM_EXIT)
			target = pd->edit_object;
		else
			target = gem_form_find_object(&pd->context,
				pd->edit_object, GEM_FORM_DEFLT,
				GEM_FORM_SELECTABLE);
		if (target >= GEM_FORM_ROOT
			&& (UWORD) target < pd->context.object_count) {
			cont = gem_form_button(&pd->context, target, 1U,
				FALSE, &result_object);
			if (!cont) {
				gem_form_finish(pd, (UWORD) result_object);
				return;
			}
			target = result_object;
		}
		break;
	case GEM_FORM_KEY_ESCAPE:
		target = gem_form_find_object(&pd->context, GEM_FORM_ROOT,
			GEM_FORM_FORWARD, GEM_FORM_ESCCANCEL);
		if (target > GEM_FORM_ROOT
			&& (UWORD) target < pd->context.object_count
			&& (gem_form_object_at(&pd->context,
					(UWORD) target)->ob_flags &
				GEM_FORM_ESCCANCEL)) {
			cont = gem_form_button(&pd->context, target, 1U, FALSE,
				&result_object);
			if (!cont) {
				gem_form_finish(pd, (UWORD) result_object);
				return;
			}
		}
		target = GEM_FORM_NIL;
		break;
	case GEM_FORM_KEY_SPACE:
		cont = gem_form_button(&pd->context, pd->edit_object, 1U,
			FALSE, &result_object);
		if (!cont) {
			gem_form_finish(pd, (UWORD) result_object);
			return;
		}
		target = result_object;
		break;
	default:
		if (entry->ob_flags & GEM_FORM_EDITABLE)
			(void) gem_form_edit(&pd->context,
				(UWORD) pd->edit_object, key, &pd->edit_index,
				GEM_FORM_EDCHAR);
		break;
	}
	if (target > GEM_FORM_ROOT && (UWORD) target < pd->context.object_count)
		(void) gem_form_switch_field(pd, target);
}

static VOID
gem_form_normal_mouse(GEM_FORM_PD *pd, const GEM_FORM_INPUT *input)
{
	OBJECT GEM_FORM_FAR *entry;
	WORD object;
	WORD release_object;
	WORD result_object;
	WORD cont;
	UWORD down;
	UWORD was_down;
	UWORD clicks;

	down = input->mouse_buttons & GEM_FORM_LEFT_BUTTON;
	was_down = pd->previous_buttons & GEM_FORM_LEFT_BUTTON;
	if (down && !was_down) {
		object = gem_form_find_at(&pd->context,
			input->mouse_x, input->mouse_y);
		pd->pressed_object = object;
		if (object >= GEM_FORM_ROOT
			&& (UWORD) object < pd->context.object_count) {
			entry = gem_form_object_at(&pd->context,
				(UWORD) object);
			pd->pressed_state = entry->ob_state;
			if ((entry->ob_flags & GEM_FORM_SELECTABLE)
				&& !(entry->ob_flags & GEM_FORM_RBUTTON)
				&& !(entry->ob_state & GEM_FORM_DISABLED))
				(void) gem_form_change_object(&pd->context,
					(UWORD) object,
					(UWORD) (entry->ob_state
						^ GEM_FORM_SELECTED), TRUE);
		}
	} else if (!down && was_down && pd->pressed_object != GEM_FORM_NIL) {
		object = pd->pressed_object;
		release_object = gem_form_find_at(&pd->context,
			input->mouse_x, input->mouse_y);
		entry = object >= GEM_FORM_ROOT
			&& (UWORD) object < pd->context.object_count
			? gem_form_object_at(&pd->context, (UWORD) object)
			: (OBJECT GEM_FORM_FAR *) 0;
		if (release_object != object) {
			if (entry && (entry->ob_flags & GEM_FORM_SELECTABLE)
				&& !(entry->ob_flags & GEM_FORM_RBUTTON))
				(void) gem_form_change_object(&pd->context,
					(UWORD) object, pd->pressed_state,
					TRUE);
		} else if (entry) {
			clicks = input->clicks ? input->clicks : 1U;
			cont = gem_form_button(&pd->context, object, clicks,
				(entry->ob_flags & GEM_FORM_SELECTABLE)
				&& !(entry->ob_flags & GEM_FORM_RBUTTON),
				&result_object);
			if (!cont) {
				gem_form_finish(pd, (UWORD) result_object);
			} else if (result_object > GEM_FORM_ROOT
				&& (UWORD) result_object
				< pd->context.object_count) {
				(void) gem_form_switch_field(pd, result_object);
			}
		}
		pd->pressed_object = GEM_FORM_NIL;
	}
	pd->previous_buttons = input->mouse_buttons;
}

WORD
gem_form_resident_input(const GEM_FORM_INPUT *input, GEM_FORM_EFFECTS *effects)
{
	GEM_FORM_PD *pd;

	if (!effects)
		return FALSE;
	gem_form_clear_effects(effects);
	if (!input || input->owner >= GEM_FORM_PD_COUNT)
		return FALSE;
	pd = gem_form_pd_at(input->owner);
	if (!gem_form_pd_matches(pd, input->generation_lo, input->generation_hi)
		|| pd->state != GEM_FORM_PD_WAITING)
		return FALSE;
	if (pd->tree_kind == GEM_FORM_TREE_ALERT) {
		gem_form_alert_input(pd, input, effects);
		return TRUE;
	}
	gem_form_fsel_pointer(input->mouse_y);
	if (input->key_ready)
		gem_form_normal_key(pd, input->key_code);
	if (pd->state == GEM_FORM_PD_WAITING)
		gem_form_normal_mouse(pd, input);
	/*
	 * the file selector loop turned inside out, when its inner form
	 * settles on an object run one pass here and either loop again or
	 * let the parked call finish
	 */
	if (pd->kind == GEM_FORM_KIND_FSEL && pd->state == GEM_FORM_PD_READY)
		gem_form_fsel_progress(pd, effects);
	return TRUE;
}

WORD
gem_form_resident_service(UWORD owner, UWORD generation_lo,
	UWORD generation_hi, GEM_FORM_COMPLETION *completion)
{
	GEM_FORM_PD *pd;
	UWORD result;

	if (!completion)
		return FALSE;
	completion->owner = GEM_FORM_OWNER_NONE;
	completion->generation_lo = 0;
	completion->generation_hi = 0;
	completion->output_count = 0;
	completion->int_out[0] = 0;
	completion->int_out[1] = 0;
	completion->fsel = FALSE;
	gem_form_clear_effects(&completion->effects);
	pd = gem_form_pd_at(owner);
	if (!gem_form_pd_matches(pd, generation_lo, generation_hi)
		|| pd->state != GEM_FORM_PD_READY)
		return FALSE;
	result = pd->result;
	completion->owner = owner;
	completion->generation_lo = generation_lo;
	completion->generation_hi = generation_hi;
	completion->output_count = 1U;
	completion->int_out[0] = result;
	if (pd->kind == GEM_FORM_KIND_FSEL) {
		/*
		 * FSEL_INPUT answers TRUE with the exit button in the second
		 * word, the two strings go back through the slots checked when
		 * the selector opened
		 */
		completion->output_count = GEM_FORM_OUTPUT_WORDS;
		completion->int_out[0] = TRUE;
		completion->int_out[1] = (result == GEM_SYSTEM_FSOK) ? 1U : 0U;
		completion->fsel = TRUE;
		gem_form_fsel_release(&completion->fsel_path,
			&completion->fsel_name);
	}
	gem_form_copy_effects(&completion->effects, &pd->ready_effects);
	if (pd->tree_kind == GEM_FORM_TREE_ALERT
		&& gem_form_alert_owns(owner, generation_lo, generation_hi))
		gem_form_alert_release();
	gem_form_clear_pd(pd);
	return TRUE;
}

VOID
gem_form_resident_detach(UWORD owner, UWORD generation_lo,
	UWORD generation_hi, GEM_FORM_EFFECTS *effects)
{
	GEM_FORM_PD *pd;

	gem_form_clear_effects(effects);
	pd = gem_form_pd_at(owner);
	if (!gem_form_pd_matches(pd, generation_lo, generation_hi))
		return;
	if (pd->state == GEM_FORM_PD_WAITING || pd->state == GEM_FORM_PD_READY)
		effects->end_update = TRUE;
	if (pd->tree_kind == GEM_FORM_TREE_ALERT) {
		effects->redraw_background = TRUE;
		gem_form_alert_rectangle(&effects->rectangle);
		if (gem_form_alert_owns(owner, generation_lo, generation_hi))
			gem_form_alert_release();
	}
	gem_form_clear_pd(pd);
}

WORD
gem_form_resident_waiting(UWORD owner, UWORD generation_lo, UWORD generation_hi)
{
	GEM_FORM_PD *pd;

	pd = gem_form_pd_at(owner);
	return gem_form_pd_matches(pd, generation_lo, generation_hi)
		&& pd->state == GEM_FORM_PD_WAITING;
}

VOID
gem_form_resident_reset(VOID)
{
	GEM_FORM_PD *pd;
	UWORD count;

	pd = gem_form_pds;
	count = GEM_FORM_PD_COUNT;
	while (count--)
		gem_form_clear_pd(pd++);
	gem_form_alert_reset();
	gem_form_fsel_reset();
}
