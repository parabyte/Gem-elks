/*
 * gem_kbd_raw.c - raw XT set-1 scancodes to GEM key events
 *
 * used when the console is in DCSET_KRAW mode. one make or break code
 * comes in, shift/ctrl/alt/caps are tracked here, a make of a real key
 * goes out as a set-1 scan code plus its ascii, breaks and modifier
 * keys are swallowed. GEM already speaks set-1 so the scan code is the
 * code itself
 */

#include "gem_kbd_raw.h"

/* unshifted ascii per set-1 make code up to 3F, 0 means a special key
 * that carries only a scan code. codes 40 and up (function and keypad
 * keys) are handled below, only keypad - and + carry a character */
static const GEM_VDI_UBYTE gem_kbd_base[0x40] = {
/* 00 */ 0, 27, '1', '2', '3', '4', '5', '6',
/* 08 */ '7', '8', '9', '0', '-', '=', 8, 9,
/* 10 */ 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i',
/* 18 */ 'o', 'p', '[', ']', 13, 0, 'a', 's',
/* 20 */ 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';',
/* 28 */ '\'', '`', 0, '\\', 'z', 'x', 'c', 'v',
/* 30 */ 'b', 'n', 'm', ',', '.', '/', 0, '*',
/* 38 */ 0, ' ', 0, 0, 0, 0, 0, 0
};

/* the same codes with Shift held */
static const GEM_VDI_UBYTE gem_kbd_shift[0x40] = {
/* 00 */ 0, 27, '!', '@', '#', '$', '%', '^',
/* 08 */ '&', '*', '(', ')', '_', '+', 8, 9,
/* 10 */ 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I',
/* 18 */ 'O', 'P', '{', '}', 13, 0, 'A', 'S',
/* 20 */ 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':',
/* 28 */ '"', '~', 0, '|', 'Z', 'X', 'C', 'V',
/* 30 */ 'B', 'N', 'M', '<', '>', '?', 0, '*',
/* 38 */ 0, ' ', 0, 0, 0, 0, 0, 0
};

static GEM_VDI_WORD gem_kbd_lshift;
static GEM_VDI_WORD gem_kbd_rshift;
static GEM_VDI_WORD gem_kbd_ctrl;
static GEM_VDI_WORD gem_kbd_alt;
static GEM_VDI_WORD gem_kbd_caps;
static GEM_VDI_WORD gem_kbd_e0;

void
gem_kbd_raw_reset(void)
{
	gem_kbd_lshift = 0;
	gem_kbd_rshift = 0;
	gem_kbd_ctrl = 0;
	gem_kbd_alt = 0;
	gem_kbd_caps = 0;
	gem_kbd_e0 = 0;
}

static GEM_VDI_UWORD
gem_kbd_mods(void)
{
	GEM_VDI_UWORD mods = 0;

	if (gem_kbd_lshift)
		mods |= GEM_VDI_MOD_LSHIFT;
	if (gem_kbd_rshift)
		mods |= GEM_VDI_MOD_RSHIFT;
	if (gem_kbd_ctrl)
		mods |= GEM_VDI_MOD_CTRL;
	if (gem_kbd_alt)
		mods |= GEM_VDI_MOD_ALT;
	return mods;
}

GEM_VDI_UWORD
gem_kbd_raw_modifiers(void)
{
	return gem_kbd_mods();
}

GEM_VDI_WORD
gem_kbd_raw_scancode(GEM_VDI_UBYTE code, GEM_VDI_UWORD *character,
	GEM_VDI_UWORD *modifiers, GEM_VDI_UWORD *scan_code)
{
	GEM_VDI_UBYTE key;
	GEM_VDI_WORD released;
	GEM_VDI_WORD ext;
	GEM_VDI_UWORD ascii;
	GEM_VDI_WORD shift;

	/* E0 prefixes the grey duplicate keys, remember it for the next code */
	if (code == 0xe0) {
		gem_kbd_e0 = 1;
		return GEM_VDI_KEY_NONE;
	}
	ext = gem_kbd_e0;
	gem_kbd_e0 = 0;
	released = (code & 0x80) ? 1 : 0;
	key = (GEM_VDI_UBYTE) (code & 0x7f);

	/* modifier keys set on make, clear on break, never reach GEM */
	switch (key) {
	case 0x2a:		/* left shift, ignore the E0 2A fake shift */
		if (!ext)
			gem_kbd_lshift = !released;
		return GEM_VDI_KEY_NONE;
	case 0x36:		/* right shift, ignore the E0 36 fake shift */
		if (!ext)
			gem_kbd_rshift = !released;
		return GEM_VDI_KEY_NONE;
	case 0x1d:		/* left or E0 right control */
		gem_kbd_ctrl = !released;
		return GEM_VDI_KEY_NONE;
	case 0x38:		/* left or E0 right alt */
		gem_kbd_alt = !released;
		return GEM_VDI_KEY_NONE;
	case 0x3a:		/* caps lock flips on make */
		if (!released)
			gem_kbd_caps = !gem_kbd_caps;
		return GEM_VDI_KEY_NONE;
	case 0x45:		/* num lock, we treat the keypad as arrows */
	case 0x46:		/* scroll lock */
		return GEM_VDI_KEY_NONE;
	default:
		break;
	}

	/* GEM wants key presses, drop the breaks and anything off the table */
	if (released || key >= 0x54)
		return GEM_VDI_KEY_NONE;

	shift = (gem_kbd_lshift || gem_kbd_rshift) ? 1 : 0;
	if (key >= 0x40) {
		/* function and keypad keys are scan-code only, bar keypad -/+ */
		if (key == 0x4a)
			ascii = '-';
		else if (key == 0x4e)
			ascii = '+';
		else
			ascii = 0;
	} else if (gem_kbd_base[key] >= 'a' && gem_kbd_base[key] <= 'z') {
		/* a letter, caps lock flips the effect of shift */
		if (shift ^ gem_kbd_caps)
			ascii = gem_kbd_shift[key];
		else
			ascii = gem_kbd_base[key];
		if (gem_kbd_ctrl)
			ascii = (GEM_VDI_UWORD) (gem_kbd_base[key] & 0x1f);
	} else {
		ascii = shift ? gem_kbd_shift[key] : gem_kbd_base[key];
	}

	if (character)
		*character = ascii;
	if (modifiers)
		*modifiers = gem_kbd_mods();
	if (scan_code)
		*scan_code = key;
	return GEM_VDI_KEY_PRESS;
}
