/*
 * gem_alert_resident.c - AES alert manager
 *
 * shows one alert at a time, splits the "[icon][message][buttons]" string,
 * sizes the alert box round it and runs the buttons
 */

#include "gem_form_internal.h"

#include "gem_resident_memory.h"
#include "gem_system_resource.h"

/*
 * one shared alert tree for the whole AES, its GEM.RSC's own, only the text and
 * size change per alert
 */
static GEM_FORM_CONTEXT gem_form_alert_context;

static UBYTE gem_form_alert_strings
	[GEM_FORM_ALERT_MESSAGE_ROWS + GEM_FORM_ALERT_BUTTONS]
	[GEM_FORM_ALERT_TEXT_BYTES];

static UBYTE gem_form_alert_source[GEM_FORM_MAX_ALERT_SOURCE];

/* the template before we fill in its markers */
static UBYTE gem_form_alert_template[GEM_FORM_MAX_ALERT_SOURCE];

static UWORD gem_form_alert_owner;

static UWORD gem_form_alert_generation_lo;

static UWORD gem_form_alert_generation_hi;

static UWORD gem_form_alert_resident_segment;

static UWORD gem_form_alert_default_button;

static UWORD gem_form_alert_button_count;

static UBYTE gem_form_alert_active;

OBJECT GEM_FORM_FAR *
gem_form_alert_object_at(UWORD object)
{
	OBJECT GEM_FORM_FAR *entry;

	entry = gem_form_alert_context.objects;
	while (object--)
		entry++;
	return entry;
}

UBYTE *
gem_form_alert_string_at(UWORD string_index)
{
	UBYTE *text;

	text = &gem_form_alert_strings[0][0];
	while (string_index--) {
		text += GEM_FORM_ALERT_TEXT_BYTES;
	}
	return text;
}

VOID
gem_form_clear_alert_strings(VOID)
{
	UBYTE *text;
	UWORD strings;
	UWORD bytes;

	text = &gem_form_alert_strings[0][0];
	strings = GEM_FORM_ALERT_MESSAGE_ROWS + GEM_FORM_ALERT_BUTTONS;
	while (strings--) {
		bytes = GEM_FORM_ALERT_TEXT_BYTES;
		while (bytes--)
			*text++ = 0;
	}
}

static VOID
gem_form_set_alert_string_spec(OBJECT GEM_FORM_FAR *object, UBYTE *text,
	UWORD resident_segment)
{
	object->ob_spec.lo = (UWORD) text;
	object->ob_spec.hi = resident_segment;
}

static VOID
gem_form_alert_object(OBJECT GEM_FORM_FAR *object, WORD next, WORD head,
	WORD tail,
	UWORD type, UWORD flags, UWORD state, UWORD x, UWORD y,
	UWORD width, UWORD height)
{
	object->ob_next = next;
	object->ob_head = head;
	object->ob_tail = tail;
	object->ob_type = type;
	object->ob_flags = flags;
	object->ob_state = state;
	object->ob_spec.lo = 0;
	object->ob_spec.hi = 0;
	object->ob_x = x;
	object->ob_y = y;
	object->ob_width = width;
	object->ob_height = height;
}

/* copy one caller alert string into the static parse buffer */
WORD
gem_form_alert_source_from_call(const GEM_FORM_CALL *call,
	GEM_BINDINGS_POINTER_SLOT address)
{
	GEM_FORM_CONTEXT memory;
	UBYTE GEM_FORM_FAR *source;
	UWORD available;
	UWORD count;

	gem_form_clear_context(&memory);
	memory.resource = call->resource;
	memory.client_segment = call->client_segment;
	memory.client_limit = call->client_limit;
	if (!gem_form_available(&memory, address, &available) || !available)
		return FALSE;
	if (available > GEM_FORM_MAX_ALERT_SOURCE)
		available = GEM_FORM_MAX_ALERT_SOURCE;
	source = (UBYTE GEM_FORM_FAR *) gem_form_pointer(&memory, address, 1U);
	if (!source)
		return FALSE;
	count = 0;
	while (count < available) {
		gem_form_alert_source[count] = *source;
		if (!*source)
			return TRUE;
		source++;
		count++;
	}
	gem_form_alert_source[GEM_FORM_MAX_ALERT_SOURCE - 1U] = 0;
	return FALSE;
}

/* split one bracket section on `|`, a doubled `|` is one literal */
static WORD
gem_form_alert_section(UBYTE **cursor, UWORD first_string, UWORD maximum,
	UWORD *items, UWORD *maximum_length)
{
	UBYTE *source;
	UBYTE *destination;
	UWORD item;
	UWORD length;
	UWORD longest;
	UBYTE character;
	UBYTE next;

	if (!cursor || !*cursor || **cursor != '[' || !maximum)
		return FALSE;
	source = *cursor + 1;
	item = 0;
	length = 0;
	longest = 0;
	destination = gem_form_alert_string_at(first_string);
	for (;;) {
		character = *source++;
		if (!character)
			return FALSE;
		next = *source;
		if (character == '|' || character == ']') {
			if (next == character) {
				source++;
			} else {
				*destination = 0;
				if (length > longest)
					longest = length;
				item++;
				if (character == ']')
					break;
				if (item >= maximum)
					return FALSE;
				destination = gem_form_alert_string_at(
					(UWORD) (first_string + item));
				length = 0;
				continue;
			}
		}
		if (length + 1U < GEM_FORM_ALERT_TEXT_BYTES) {
			*destination++ = character;
			length++;
		}
	}
	*cursor = source;
	*items = item;
	*maximum_length = longest;
	return item != 0;
}

/* icon: types 1 to 3 are the note, question and stop images */
static UWORD
gem_form_alert_icon_image(UBYTE icon)
{
	if (icon == '2')
		return GEM_SYSTEM_QUESTBB;
	if (icon == '3')
		return GEM_SYSTEM_STOPBB;
	return GEM_SYSTEM_NOTEBB;
}

/* resize the alert box round the message and buttons, aim its icon */
static WORD
gem_form_alert_build(UWORD default_button, UWORD resident_segment,
	UWORD screen_width, UWORD screen_height, UWORD character_width,
	UWORD character_height)
{
	GEM_FAR_ADDRESS icon_image;
	OBJECT GEM_FORM_FAR *object;
	UBYTE *cursor;
	UBYTE icon;
	UWORD message_count;
	UWORD message_width;
	UWORD button_count;
	UWORD button_width;
	UWORD have_icon;
	UWORD icon_columns;
	UWORD message_x_cells;
	UWORD button_x_cells;
	UWORD root_columns;
	UWORD root_rows;
	UWORD rows;
	UWORD index;
	UWORD output[5];
	UWORD flags;
	UWORD x;
	UWORD y;
	UWORD width;
	UWORD height;

	if (!character_width || !character_height
		|| gem_form_alert_source[0] != '[' || !gem_form_alert_source[1]
		|| gem_form_alert_source[2] != ']')
		return FALSE;
	icon = gem_form_alert_source[1];
	cursor = &gem_form_alert_source[3];
	gem_form_clear_alert_strings();
	if (!gem_form_alert_section(&cursor, 0,
			GEM_FORM_ALERT_MESSAGE_ROWS, &message_count,
			&message_width)
		|| !gem_form_alert_section(&cursor, GEM_FORM_ALERT_MESSAGE_ROWS,
			GEM_FORM_ALERT_BUTTONS, &button_count, &button_width))
		return FALSE;
	have_icon = icon != '0';
	icon_columns = have_icon ? 6U : 0U;
	message_x_cells = (UWORD) (2U + icon_columns);
	button_x_cells = (UWORD) (message_x_cells + message_width + 2U);
	root_columns = (UWORD) (button_x_cells + button_width + 4U);
	root_rows = (UWORD) (message_count + 3U);
	if (have_icon && root_rows < 7U)
		root_rows = 7U;
	rows = 3U;
	index = button_count;
	while (index--)
		rows = (UWORD) (rows + 2U);
	if (rows > root_rows)
		root_rows = rows;
	width = gem_form_cells(root_columns, character_width);
	height = gem_form_cells(root_rows, character_height);
	if (!width || !height || width > screen_width || height > screen_height)
		return FALSE;

	/* ROOT and all nine children in a fixed order */
	object = gem_form_alert_object_at(0);
	gem_form_alert_object(object, GEM_FORM_NIL,
		1, 9, GEM_FORM_G_BOX, 0, GEM_FORM_OUTLINED,
		0, 0, width, height);
	object->ob_spec.lo = 0x1170U;
	object->ob_spec.hi = 0x0001U;
	x = gem_form_cells(2U, character_width);
	y = gem_form_cells(2U, character_height);
	object = gem_form_alert_object_at(1);
	gem_form_alert_object(object, 2,
		GEM_FORM_NIL, GEM_FORM_NIL, GEM_FORM_G_IMAGE,
		have_icon ? 0 : GEM_FORM_HIDETREE, GEM_FORM_NORMAL,
		x, y, gem_form_cells(4U, character_width),
		gem_form_cells(4U, character_height));
	if (!gem_system_resource_gaddr(R_FRIMG,
			gem_form_alert_icon_image(icon), &icon_image))
		return FALSE;
	object->ob_spec.lo = icon_image.lo;
	object->ob_spec.hi = icon_image.hi;

	index = 0;
	while (index < GEM_FORM_ALERT_MESSAGE_ROWS) {
		object = gem_form_alert_object_at(
			(UWORD) (GEM_FORM_ALERT_FIRST_MESSAGE + index));
		flags = index < message_count ? 0 : GEM_FORM_HIDETREE;
		x = gem_form_cells(message_x_cells, character_width);
		y = gem_form_cells((UWORD) (2U + index), character_height);
		gem_form_alert_object(object,
			(WORD) (GEM_FORM_ALERT_FIRST_MESSAGE + index + 1U),
			GEM_FORM_NIL, GEM_FORM_NIL, GEM_FORM_G_STRING,
			flags, GEM_FORM_NORMAL, x, y,
			gem_form_cells(message_width, character_width),
			character_height);
		gem_form_set_alert_string_spec(object,
			gem_form_alert_string_at(index), resident_segment);
		index++;
	}

	index = 0;
	while (index < GEM_FORM_ALERT_BUTTONS) {
		object = gem_form_alert_object_at(
			(UWORD) (GEM_FORM_ALERT_FIRST_BUTTON + index));
		flags = GEM_FORM_SELECTABLE | GEM_FORM_EXIT;
		if (index >= button_count)
			flags |= GEM_FORM_HIDETREE;
		if ((default_button & 0x00ffU) == index + 1U)
			flags |= GEM_FORM_DEFAULT;
		if ((default_button >> 8) == index + 1U)
			flags |= GEM_FORM_ESCCANCEL;
		if (index + 1U == GEM_FORM_ALERT_BUTTONS)
			flags |= GEM_FORM_LASTOB;
		x = gem_form_cells(button_x_cells, character_width);
		y = 2U;
		rows = index;
		while (rows--)
			y = (UWORD) (y + 2U);
		y = gem_form_cells(y, character_height);
		gem_form_alert_object(object,
			index + 1U < GEM_FORM_ALERT_BUTTONS
			? (WORD) (GEM_FORM_ALERT_FIRST_BUTTON + index + 1U)
			: GEM_FORM_ROOT,
			GEM_FORM_NIL, GEM_FORM_NIL, GEM_FORM_G_BUTTON,
			flags, GEM_FORM_NORMAL, x, y,
			gem_form_cells((UWORD) (button_width + 2U),
				character_width), character_height);
		gem_form_set_alert_string_spec(object,
			gem_form_alert_string_at(
				(UWORD) (GEM_FORM_ALERT_MESSAGE_ROWS + index)),
			resident_segment);
		index++;
	}
	gem_form_alert_context.screen_width = screen_width;
	gem_form_alert_context.screen_height = screen_height;
	gem_form_alert_context.character_width = character_width;
	gem_form_alert_context.character_height = character_height;
	if (!gem_form_center_tree(&gem_form_alert_context, output))
		return FALSE;
	gem_form_alert_default_button = default_button & 0x00ffU;
	gem_form_alert_button_count = button_count;
	gem_form_alert_resident_segment = resident_segment;
	return TRUE;
}

VOID
gem_form_alert_rectangle(GEM_FORM_RECTANGLE *rectangle)
{
	OBJECT GEM_FORM_FAR *root;
	UWORD x;
	UWORD y;
	UWORD width;
	UWORD height;

	root = gem_form_alert_object_at(0);
	if (!root) {
		rectangle->x = 0;
		rectangle->y = 0;
		rectangle->width = 0;
		rectangle->height = 0;
		return;
	}
	x = root->ob_x;
	y = root->ob_y;
	width = root->ob_width;
	height = root->ob_height;
	/* the 8 pixel OUTLINED margin counts too */
	if (root->ob_state & GEM_FORM_OUTLINED) {
		x = x >= 8U ? (UWORD) (x - 8U) : 0;
		y = y >= 8U ? (UWORD) (y - 8U) : 0;
		if (width <= 0xffefU)
			width = (UWORD) (width + 16U);
		if (height <= 0xffefU)
			height = (UWORD) (height + 16U);
	}
	rectangle->x = (WORD) x;
	rectangle->y = (WORD) y;
	rectangle->width = (WORD) width;
	rectangle->height = (WORD) height;
}

static VOID
gem_form_alert_effect(GEM_FORM_EFFECTS *effects)
{
	gem_form_clear_effects(effects);
	/* records live in the resource, only the string specs point back
	 * into resident memory */
	effects->tree = gem_form_alert_context.tree;
	effects->resident_segment = gem_form_alert_resident_segment;
	effects->tree_kind = GEM_FORM_TREE_ALERT;
	effects->draw_tree = TRUE;
	gem_form_alert_rectangle(&effects->rectangle);
}

static WORD
gem_form_alert_find_button(WORD x, WORD y)
{
	OBJECT GEM_FORM_FAR *root;
	OBJECT GEM_FORM_FAR *button;
	UWORD index;
	WORD absolute_x;
	WORD absolute_y;

	root = gem_form_alert_object_at(0);
	index = 0;
	while (index < gem_form_alert_button_count) {
		button = gem_form_alert_object_at(
			(UWORD) (GEM_FORM_ALERT_FIRST_BUTTON + index));
		absolute_x = (WORD) ((UWORD) root->ob_x + button->ob_x);
		absolute_y = (WORD) ((UWORD) root->ob_y + button->ob_y);
		if (!(button->ob_flags & GEM_FORM_HIDETREE)
			&& gem_form_inside(x, y, absolute_x, absolute_y,
				(WORD) button->ob_width,
				(WORD) button->ob_height))
			return (WORD) (GEM_FORM_ALERT_FIRST_BUTTON + index);
		index++;
	}
	return GEM_FORM_NIL;
}

static WORD
gem_form_alert_flag_button(UWORD flag)
{
	OBJECT GEM_FORM_FAR *button;
	UWORD index;

	index = 0;
	while (index < gem_form_alert_button_count) {
		button = gem_form_alert_object_at(
			(UWORD) (GEM_FORM_ALERT_FIRST_BUTTON + index));
		if (button->ob_flags & flag)
			return (WORD) (GEM_FORM_ALERT_FIRST_BUTTON + index);
		index++;
	}
	return GEM_FORM_NIL;
}

static VOID
gem_form_alert_complete(GEM_FORM_PD *pd, WORD object)
{
	UWORD button;

	if (object < (WORD) GEM_FORM_ALERT_FIRST_BUTTON
		|| object >= (WORD) (GEM_FORM_ALERT_FIRST_BUTTON
			+ GEM_FORM_ALERT_BUTTONS))
		return;
	button = (UWORD) (object - (WORD) GEM_FORM_ALERT_FIRST_BUTTON + 1);
	if (pd->kind == GEM_FORM_KIND_ERROR)
		button = button != 1U;
	gem_form_finish(pd, button);
}

/*
 * open GEM.RSC's alert tree, its the AES's own so we open it once and reuse it,
 * later we just resize and rewrite its text
 */
static WORD
gem_form_alert_open_tree(const GEM_FORM_CALL *call)
{
	GEM_FORM_CALL system_call;
	GEM_FAR_ADDRESS tree;

	if (gem_form_alert_context.objects
		&& gem_form_alert_context.object_count
		>= GEM_FORM_ALERT_OBJECTS)
		return TRUE;
	if (!gem_system_resource_gaddr(R_TREE, GEM_SYSTEM_TREE_ALERT, &tree)
		|| !tree.hi)
		return FALSE;
	system_call = *call;
	system_call.resource = gem_system_resource();
	if (!gem_form_open_tree(&gem_form_alert_context, &system_call, tree))
		return FALSE;
	return gem_form_alert_context.object_count >= GEM_FORM_ALERT_OBJECTS;
}

WORD
gem_form_begin_alert(const GEM_FORM_CALL *call, UWORD kind,
	UWORD default_button, GEM_FORM_EFFECTS *effects)
{
	GEM_FORM_PD *pd;
	UWORD screen_width;
	UWORD screen_height;
	UWORD character_width;
	UWORD character_height;

	pd = gem_form_pd_at(call->owner);
	if (!pd || pd->state != GEM_FORM_PD_FREE || gem_form_alert_active
		|| !gem_form_alert_open_tree(call))
		return FALSE;
	gem_form_metrics(call, &screen_width, &screen_height,
		&character_width, &character_height);
	if (!gem_form_alert_build(default_button, call->resident_segment,
			screen_width, screen_height, character_width,
			character_height))
		return FALSE;
	gem_form_clear_pd(pd);
	pd->owner = (UBYTE) call->owner;
	pd->generation_lo = call->generation_lo;
	pd->generation_hi = call->generation_hi;
	pd->kind = (UBYTE) kind;
	pd->tree_kind = GEM_FORM_TREE_ALERT;
	pd->state = GEM_FORM_PD_WAITING;
	pd->pressed_object = GEM_FORM_NIL;
	gem_form_alert_owner = call->owner;
	gem_form_alert_generation_lo = call->generation_lo;
	gem_form_alert_generation_hi = call->generation_hi;
	gem_form_alert_active = TRUE;
	gem_form_alert_effect(effects);
	effects->begin_update = TRUE;
	return TRUE;
}

/*
 * copy template to dest, swap each marker for the next parms entry, %% is a
 * literal percent, %W a word and %L a long as unsigned decimal, %S a string
 * inserted as is, unknown marker emits nothing
 */
static VOID
gem_form_merge_str(UBYTE *destination, const UBYTE *source, const UWORD *parms)
{
	UBYTE digits[12];
	unsigned long value;
	const UBYTE *string;
	UWORD out;
	UWORD num;
	UWORD count;
	WORD do_value;

	out = 0;
	num = 0;
	while (*source && out < GEM_FORM_MAX_ALERT_SOURCE - 1U) {
		if (*source != '%') {
			destination[out++] = *source++;
			continue;
		}
		source++;
		do_value = FALSE;
		value = 0;
		switch (*source++) {
		case '%':
			destination[out++] = '%';
			break;
		case 'W':
			value = parms[num];
			num++;
			do_value = TRUE;
			break;
		case 'L':
			value = *((const unsigned long *) &parms[num]);
			num += 2;
			do_value = TRUE;
			break;
		case 'S':
			string = (const UBYTE *) parms[num];
			num++;
			while (*string && out < GEM_FORM_MAX_ALERT_SOURCE - 1U)
				destination[out++] = *string++;
			break;
		default:
			break;
		}
		if (!do_value)
			continue;
		count = 0;
		do {
			digits[count++] =
				(UBYTE) ('0' + (UWORD) (value % 10UL));
			value /= 10UL;
		} while (value && count < sizeof(digits));
		while (count && out < GEM_FORM_MAX_ALERT_SOURCE - 1U)
			destination[out++] = digits[--count];
	}
	destination[out] = 0;
}

/*
 * pull one alert string out of GEM.RSC and merge it, form_error is the only
 * caller and passes one word, the error number
 */
WORD
gem_form_alert_source_string(UWORD index, UWORD parameter)
{
	UWORD parms[1];

	if (gem_system_resource_string(index, (BYTE *) gem_form_alert_template,
			GEM_FORM_MAX_ALERT_SOURCE) < 0)
		return FALSE;
	parms[0] = parameter;
	gem_form_merge_str(gem_form_alert_source, gem_form_alert_template,
		parms);
	return gem_form_alert_source[0] == '[';
}

VOID
gem_form_alert_input(GEM_FORM_PD *pd, const GEM_FORM_INPUT *input,
	GEM_FORM_EFFECTS *effects)
{
	OBJECT GEM_FORM_FAR *button;
	WORD object;
	WORD release_object;
	UWORD down;
	UWORD was_down;

	if (input->key_ready) {
		object = GEM_FORM_NIL;
		if (input->key_code == GEM_FORM_KEY_RETURN) {
			object = gem_form_alert_flag_button(GEM_FORM_DEFAULT);
			if (object == GEM_FORM_NIL
				&& gem_form_alert_default_button
				&& gem_form_alert_default_button <=
				gem_form_alert_button_count)
				object = (WORD) (GEM_FORM_ALERT_FIRST_BUTTON +
					gem_form_alert_default_button - 1U);
		} else if (input->key_code == GEM_FORM_KEY_ESCAPE) {
			object = gem_form_alert_flag_button(GEM_FORM_ESCCANCEL);
		}
		if (object != GEM_FORM_NIL) {
			button = gem_form_alert_object_at((UWORD) object);
			button->ob_state |= GEM_FORM_SELECTED;
			gem_form_alert_effect(effects);
			gem_form_alert_complete(pd, object);
			return;
		}
	}
	down = input->mouse_buttons & GEM_FORM_LEFT_BUTTON;
	was_down = pd->previous_buttons & GEM_FORM_LEFT_BUTTON;
	if (down && !was_down) {
		object = gem_form_alert_find_button(input->mouse_x,
			input->mouse_y);
		pd->pressed_object = object;
		if (object != GEM_FORM_NIL) {
			button = gem_form_alert_object_at((UWORD) object);
			pd->pressed_state = button->ob_state;
			button->ob_state ^= GEM_FORM_SELECTED;
			gem_form_alert_effect(effects);
		}
	} else if (!down && was_down && pd->pressed_object != GEM_FORM_NIL) {
		object = pd->pressed_object;
		release_object = gem_form_alert_find_button(input->mouse_x,
			input->mouse_y);
		button = gem_form_alert_object_at((UWORD) object);
		if (release_object == object) {
			gem_form_alert_complete(pd, object);
		} else {
			button->ob_state = pd->pressed_state;
			gem_form_alert_effect(effects);
		}
		pd->pressed_object = GEM_FORM_NIL;
	}
	pd->previous_buttons = input->mouse_buttons;
}

/* --- the seams gem_form_resident.c reaches the alert through --- */

WORD
gem_form_alert_owns(UWORD owner, UWORD generation_lo, UWORD generation_hi)
{
	return gem_form_alert_active && gem_form_alert_owner == owner
		&& gem_form_alert_generation_lo == generation_lo
		&& gem_form_alert_generation_hi == generation_hi;
}

VOID
gem_form_alert_release(VOID)
{
	gem_form_alert_active = FALSE;
	gem_form_alert_owner = GEM_FORM_OWNER_NONE;
}

VOID
gem_form_alert_reset(VOID)
{
	gem_form_clear_alert_strings();
	gem_form_alert_source[0] = 0;
	gem_form_alert_owner = GEM_FORM_OWNER_NONE;
	gem_form_alert_generation_lo = 0;
	gem_form_alert_generation_hi = 0;
	gem_form_alert_resident_segment = 0;
	gem_form_alert_default_button = 0;
	gem_form_alert_button_count = 0;
	gem_form_alert_active = FALSE;
}
