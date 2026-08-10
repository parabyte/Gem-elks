/*
 * gem_kbd_ansi.c - maps ELKS tty bytes to IBM PC scan codes
 *
 * just the mapping tables, ASCII to set-1 scan code, ASCII to
 * modifiers, ANSI CSI/SS3 finals to cursor/editing keys, no state or
 * reading here, the input core owns that
 */

#include <string.h>

#include "gem_kbd_ansi.h"

/* scan codes for A-Z, PC keyboard row order not alphabetical */
static const GEM_VDI_UBYTE gem_letter_scan[26] = {
	0x1e, 0x30, 0x2e, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17,
	0x24, 0x25, 0x26, 0x32, 0x31, 0x18, 0x19, 0x10, 0x13,
	0x1f, 0x14, 0x16, 0x2f, 0x11, 0x2d, 0x15, 0x2c
};

GEM_VDI_UWORD
gem_kbd_ansi_scan(GEM_VDI_UBYTE character)
{
	GEM_VDI_UBYTE lower;

	if (character >= 'A' && character <= 'Z')
		lower = character | 0x20;
	else
		lower = character;
	if (lower >= 'a' && lower <= 'z')
		return gem_letter_scan[lower - 'a'];
	/* these editing keys share byte values with Ctrl-H/I/J/M */
	if (character == 8 || character == 127)
		return GEM_SCAN_BACKSPACE;
	if (character == 9)
		return GEM_SCAN_TAB;
	if (character == 10 || character == 13)
		return GEM_SCAN_ENTER;
	if (character >= 1 && character <= 26)
		return gem_letter_scan[character - 1];
	if (character >= '1' && character <= '9')
		return (GEM_VDI_UWORD) (character - '1' + 2);

	switch (character) {
	case 27:
		return GEM_SCAN_ESCAPE;
	case '0':
	case ')':
		return 0x0b;
	case '-':
	case '_':
		return 0x0c;
	case '=':
	case '+':
		return 0x0d;
	case '[':
	case '{':
		return 0x1a;
	case ']':
	case '}':
		return 0x1b;
	case ';':
	case ':':
		return 0x27;
	case '\'':
	case '"':
		return 0x28;
	case '`':
	case '~':
		return 0x29;
	case '\\':
	case '|':
		return 0x2b;
	case ',':
	case '<':
		return 0x33;
	case '.':
	case '>':
		return 0x34;
	case '/':
	case '?':
		return 0x35;
	case ' ':
		return GEM_SCAN_SPACE;
	case '!':
		return 0x02;
	case '@':
		return 0x03;
	case '#':
		return 0x04;
	case '$':
		return 0x05;
	case '%':
		return 0x06;
	case '^':
		return 0x07;
	case '&':
		return 0x08;
	case '*':
		return 0x09;
	case '(':
		return 0x0a;
	default:
		return 0;
	}
}

GEM_VDI_UWORD
gem_kbd_ansi_modifiers(GEM_VDI_UBYTE character)
{
	if ((character >= 'A' && character <= 'Z')
		|| (character && strchr("!@#$%^&*()_+{}:\"|<>?~", character)))
		return GEM_VDI_MOD_LSHIFT;
	/* a raw tty cant tell Ctrl-H/I/M from Backspace/Tab/Enter, those stay editing keys */
	if (character >= 1 && character <= 26 && character != 8
		&& character != 9 && character != 10 && character != 13)
		return GEM_VDI_MOD_CTRL;
	return 0;
}

GEM_VDI_UWORD
gem_kbd_ansi_csi(GEM_VDI_UBYTE final, GEM_VDI_UBYTE parameter)
{
	switch (final) {
	case 'A':
		return GEM_SCAN_UP;
	case 'B':
		return GEM_SCAN_DOWN;
	case 'C':
		return GEM_SCAN_RIGHT;
	case 'D':
		return GEM_SCAN_LEFT;
	case 'F':
		return GEM_SCAN_END;
	case 'H':
		return GEM_SCAN_HOME;
	case '~':
		switch (parameter) {
		case '1':
		case '7':
			return GEM_SCAN_HOME;
		case '2':
			return GEM_SCAN_INSERT;
		case '3':
			return GEM_SCAN_DELETE;
		case '4':
		case '8':
			return GEM_SCAN_END;
		case '5':
			return GEM_SCAN_PAGE_UP;
		case '6':
			return GEM_SCAN_PAGE_DOWN;
		default:
			return 0;
		}
	default:
		return 0;
	}
}
