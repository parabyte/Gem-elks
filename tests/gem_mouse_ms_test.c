#include <assert.h>
#include <stdio.h>

#include "drivers/gem_mouse_ms.h"

int
main(void)
{
	int x, y, buttons;
	GEM_VDI_COORD dx, dy;
	GEM_VDI_WORD state;
	unsigned char header;

	/* all signed movement values and both buttons, including CR, LF,
	 * XON and XOFF in the data bytes */
	for (buttons = 0; buttons < 4; buttons++)
		for (x = -128; x <= 127; x++)
			for (y = -128; y <= 127; y++) {
				header = 0x40 | (((unsigned char) x >> 6) & 3)
					| (((unsigned char) y >> 4) & 12)
					| ((buttons & 1) ? 0x20 : 0)
					| ((buttons & 2) ? 0x10 : 0);
				assert(!gem_mouse_ms_parse(header, &dx, &dy, &state));
				assert(!gem_mouse_ms_parse(x & 63, &dx, &dy, &state));
				assert(gem_mouse_ms_parse(y & 63, &dx, &dy, &state));
				assert(dx == x && dy == y && state == buttons);
			}
	gem_mouse_ms_reset();
	assert(!gem_mouse_ms_parse(12, &dx, &dy, &state));
	assert(!gem_mouse_ms_parse(0x60, &dx, &dy, &state));
	assert(!gem_mouse_ms_parse(3, &dx, &dy, &state));
	/* a new header replaces an incomplete packet */
	assert(!gem_mouse_ms_parse(0x50, &dx, &dy, &state));
	assert(!gem_mouse_ms_parse(1, &dx, &dy, &state));
	assert(gem_mouse_ms_parse(2, &dx, &dy, &state));
	assert(dx == 1 && dy == 2 && state == GEM_VDI_BUTTON_RIGHT);
	assert(!gem_mouse_ms_parse(0x60, &dx, &dy, &state));
	gem_mouse_ms_reset();
	assert(!gem_mouse_ms_parse(1, &dx, &dy, &state));
	assert(!gem_mouse_ms_parse(2, &dx, &dy, &state));
	puts("Microsoft mouse packet checks passed");
	return 0;
}
