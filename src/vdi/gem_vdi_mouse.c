/*
 * gem_vdi_mouse.c - the resident VDI's pointer
 *
 * mouse shape changes and hide/show, the eight built-in shapes come from the
 * AES's own GEM.RSC so none is drawn here, a client's own shape arrives as the
 * same 74-byte MFORM and goes through the same conversion
 */

#include "gem_vdi_internal.h"

#include "gem_system_resource.h"

static GEM_VDI_CURSOR gem_vdi_cursor;

static GEM_VDI_RESIDENT_MFORM gem_vdi_mouse_form;

WORD gem_vdi_cursor_hot_x;

WORD gem_vdi_cursor_hot_y;

/*
 * the eight built-in mouse shapes come from the AES's GEM.RSC, each shape's
 * data is one 74-byte MFORM
 */
static WORD __attribute__((optimize("Os")))
	gem_vdi_resident_system_form(WORD number)
{
	GEM_FAR_ADDRESS slot;
	GEM_BINDINGS_POINTER_SLOT data;

	if (number < 0 || number > 7)
		return FALSE;
	if (!gem_system_resource_gaddr(R_BIPDATA,
			(UWORD) (GEM_SYSTEM_MICE00 + (UWORD) number), &slot)
		|| !slot.hi)
		return FALSE;
	/* the slot points at the image data, follow it once */
	gem_resident_memory_from(slot.hi, slot.lo, &data, sizeof(data));
	if (!data.hi)
		return FALSE;
	gem_resident_memory_from(data.hi, data.lo, &gem_vdi_mouse_form,
		sizeof(gem_vdi_mouse_form));
	return TRUE;
}

/*
 * turn the MFORM into the driver's cursor record, both the built-in shapes and
 * a client's own shape come through here
 */
static WORD __attribute__((optimize("Os")))
	gem_vdi_resident_form_to_cursor(VOID)
{
	UWORD index;

	if (gem_vdi_mouse_form.planes != 1)
		return FALSE;
	gem_vdi_cursor.width = 16;
	gem_vdi_cursor.height = 16;
	gem_vdi_cursor.hot_x = gem_vdi_mouse_form.hot_x;
	gem_vdi_cursor.hot_y = gem_vdi_mouse_form.hot_y;
	gem_vdi_cursor.foreground =
		gem_vdi_resident_color(gem_vdi_mouse_form.foreground);
	gem_vdi_cursor.background =
		gem_vdi_resident_color(gem_vdi_mouse_form.background);
	index = 0;
	while (index < 16U) {
		gem_vdi_cursor.mask[index] = gem_vdi_mouse_form.mask[index];
		gem_vdi_cursor.image[index] = gem_vdi_mouse_form.image[index];
		index++;
	}
	return TRUE;
}

VOID __attribute__((optimize("Os")))
	gem_vdi_resident_apply_cursor(const GEM_VDI_CURSOR *cursor)
{
	GEM_VDI_COORD x;
	GEM_VDI_COORD y;
	GEM_VDI_WORD buttons;

	gem_vdi_set_cursor(cursor);
	gem_vdi_cursor_hot_x = cursor->hot_x;
	gem_vdi_cursor_hot_y = cursor->hot_y;
	gem_vdi_read_mouse(&x, &y, &buttons);
	gem_vdi_move_cursor(x - cursor->hot_x, y - cursor->hot_y);
}

WORD __attribute__((optimize("Os")))
	gem_vdi_resident_default_mouse(VOID)
{
	GEM_VDI_COORD mouse_x;
	GEM_VDI_COORD mouse_y;
	GEM_VDI_WORD mouse_buttons;

	if (!gem_vdi_screen)
		return FALSE;
	if (!gem_vdi_resident_system_form(0)
		|| !gem_vdi_resident_form_to_cursor())
		return FALSE;
	gem_vdi_set_cursor(&gem_vdi_cursor);
	gem_vdi_cursor_hot_x = gem_vdi_cursor.hot_x;
	gem_vdi_cursor_hot_y = gem_vdi_cursor.hot_y;
	mouse_x = 0;
	mouse_y = 0;
	mouse_buttons = 0;
	(void) gem_vdi_read_mouse(&mouse_x, &mouse_y, &mouse_buttons);
	gem_vdi_move_cursor(mouse_x - gem_vdi_cursor_hot_x,
		mouse_y - gem_vdi_cursor_hot_y);
	gem_vdi_show_cursor(gem_vdi_screen);
	return TRUE;
}

WORD __attribute__((optimize("Os")))
	gem_vdi_resident_mouse(const struct gemtrap_request *request,
	WORD application, WORD number, GEM_BINDINGS_POINTER_SLOT form)
{
	if (!gem_vdi_screen || application < 0)
		return FALSE;
	if (number == 256) {
		gem_vdi_hide_cursor(gem_vdi_screen);
		return TRUE;
	}
	if (number == 257) {
		gem_vdi_show_cursor(gem_vdi_screen);
		return TRUE;
	}

	if (number >= 0 && number <= 7) {
		if (!gem_vdi_resident_system_form(number)
			|| !gem_vdi_resident_form_to_cursor())
			return FALSE;
		gem_vdi_resident_apply_cursor(&gem_vdi_cursor);
		return TRUE;
	}
	if (number != 255 || !request
		|| !gem_resident_memory_pointer(request, form,
			sizeof(gem_vdi_mouse_form)))
		return FALSE;

	if (!gem_vdi_resident_form_to_cursor())
		return FALSE;
	gem_vdi_resident_apply_cursor(&gem_vdi_cursor);
	return TRUE;
}

/*
 * VSC_FORM passes a 37-word MFORM in intin, not a pointer, so read the words
 * straight out of the array
 */
WORD
gem_vdi_resident_set_form(VOID)
{
	UWORD index;

	if (gem_vdi_control[3] < 37)
		return FALSE;
	gem_vdi_cursor.width = 16;
	gem_vdi_cursor.height = 16;
	gem_vdi_cursor.hot_x = (WORD) gem_vdi_intin[0];
	gem_vdi_cursor.hot_y = (WORD) gem_vdi_intin[1];
	gem_vdi_cursor.foreground = gem_vdi_resident_color(
		(WORD) gem_vdi_intin[3]);
	gem_vdi_cursor.background = gem_vdi_resident_color(
		(WORD) gem_vdi_intin[4]);
	index = 0;
	while (index < 16U) {
		gem_vdi_cursor.mask[index] = gem_vdi_intin[index + 5U];
		gem_vdi_cursor.image[index] = gem_vdi_intin[index + 21U];
		index++;
	}
	gem_vdi_set_cursor(&gem_vdi_cursor);
	return TRUE;
}
