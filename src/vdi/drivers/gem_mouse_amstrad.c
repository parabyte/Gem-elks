/*
 * gem_mouse_amstrad.c - Amstrad PC1512/PC1640 mouse-port driver
 *
 * the PC1512/PC1640 have a mouse port on the board, two hardware
 * quadrature counters read straight from I/O, port 78h is X and 7Ah
 * is Y, each a signed 8-bit count since last cleared, writing the
 * port clears it, so we just read and clear both once per tick, no
 * packets, no baud rate, no fd
 *
 * the buttons dont come through the mouse port, the keyboard
 * controller injects them into the scan stream as scancodes, 7Eh/FEh
 * make/break for button one, 7Dh/FDh for button two, the input core
 * feeds those raw codes to gem_mouse_amstrad_key_byte() when this
 * driver is picked. this needs raw scancodes so the console must be in
 * DCSET_KRAW mode, a cooked tty keymaps them away and drops the breaks
 *
 * up on the mouse bumps the Y counter and screen Y grows down, so Y is negated
 */

#include <arch/io.h>

#include "gem_mouse_amstrad.h"

#define GEM_AMSTRAD_PORT_X	0x78
#define GEM_AMSTRAD_PORT_Y	0x7a

#define GEM_AMSTRAD_BUTTON1_MAKE	0x7e
#define GEM_AMSTRAD_BUTTON2_MAKE	0x7d
#define GEM_AMSTRAD_BUTTON1_BREAK	0xfe
#define GEM_AMSTRAD_BUTTON2_BREAK	0xfd

static GEM_VDI_WORD gem_amstrad_buttons;

static GEM_VDI_WORD
gem_amstrad_signed(GEM_VDI_UBYTE value)
{
	if (value & 0x80)
		return (GEM_VDI_WORD) value - 256;
	return value;
}

void
gem_mouse_amstrad_reset(void)
{
	/* writing the counter ports clears them */
	outb(0, GEM_AMSTRAD_PORT_X);
	outb(0, GEM_AMSTRAD_PORT_Y);
	gem_amstrad_buttons = 0;
}

GEM_VDI_WORD
gem_mouse_amstrad_poll(GEM_VDI_COORD *delta_x, GEM_VDI_COORD *delta_y)
{
	GEM_VDI_UBYTE x_count;
	GEM_VDI_UBYTE y_count;

	/* read then clear each counter, a count between read and clear is lost but at 50 polls/sec thats one step, period drivers did the same */
	x_count = (GEM_VDI_UBYTE) inb(GEM_AMSTRAD_PORT_X);
	outb(0, GEM_AMSTRAD_PORT_X);
	y_count = (GEM_VDI_UBYTE) inb(GEM_AMSTRAD_PORT_Y);
	outb(0, GEM_AMSTRAD_PORT_Y);

	*delta_x = gem_amstrad_signed(x_count);
	*delta_y = -gem_amstrad_signed(y_count);
	return *delta_x != 0 || *delta_y != 0;
}

GEM_VDI_WORD
gem_mouse_amstrad_key_byte(GEM_VDI_UBYTE byte)
{
	switch (byte) {
	case GEM_AMSTRAD_BUTTON1_MAKE:
		gem_amstrad_buttons |= GEM_VDI_BUTTON_LEFT;
		return 1;
	case GEM_AMSTRAD_BUTTON1_BREAK:
		gem_amstrad_buttons &= (GEM_VDI_WORD) ~GEM_VDI_BUTTON_LEFT;
		return 1;
	case GEM_AMSTRAD_BUTTON2_MAKE:
		gem_amstrad_buttons |= GEM_VDI_BUTTON_RIGHT;
		return 1;
	case GEM_AMSTRAD_BUTTON2_BREAK:
		gem_amstrad_buttons &= (GEM_VDI_WORD) ~GEM_VDI_BUTTON_RIGHT;
		return 1;
	default:
		return 0;
	}
}

GEM_VDI_WORD
gem_mouse_amstrad_buttons(void)
{
	return gem_amstrad_buttons;
}
