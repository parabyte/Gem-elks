/*
 * Native GEM keyboard and serial-mouse input for ELKS.
 *
 * Copyright (c) 2026 elks-gem contributors
 *
 * This module deliberately keeps the operating-system boundary small.  GEM
 * receives IBM PC scan codes and 16-bit screen coordinates; the ELKS tty
 * devices supply bytes.  No dynamic storage, floating-point operation, wide
 * arithmetic, multiplication, or division is used here.
 *
 * The keyboard defaults to /dev/tty1 and falls back to /dev/console.  The
 * CONSOLE environment variable can select another ELKS tty.  ANSI cursor-key
 * sequences are translated to the scan codes returned by an IBM PC BIOS.
 *
 * The mouse defaults to a 1200-baud Microsoft serial mouse on /dev/ttyS0.
 * MOUSE_PORT selects another device (or "none"), and MOUSE_PROTOCOL selects
 * "ms" or "ps2".  The PS/2 setting is useful when an emulator or a small
 * device adapter exposes three-byte PS/2 packets through an ELKS character
 * device.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "vdi.h"

#define GEM_KEYBOARD_PRIMARY	"/dev/tty1"
#define GEM_KEYBOARD_FALLBACK	"/dev/console"
#define GEM_MOUSE_DEFAULT	"/dev/ttyS0"

#define GEM_INPUT_BUFFER_SIZE	8
#define GEM_MOUSE_BUFFER_SIZE	6

#define GEM_KEY_STATE_NORMAL	0
#define GEM_KEY_STATE_ESCAPE	1
#define GEM_KEY_STATE_CSI	2
#define GEM_KEY_STATE_SS3	3

#define GEM_MOUSE_PROTOCOL_MS	0
#define GEM_MOUSE_PROTOCOL_PS2	1

/* IBM PC/XT set-1 scan codes used by ANSI terminal escape sequences. */
#define GEM_SCAN_ESCAPE		0x01
#define GEM_SCAN_BACKSPACE	0x0e
#define GEM_SCAN_TAB		0x0f
#define GEM_SCAN_ENTER		0x1c
#define GEM_SCAN_SPACE		0x39
#define GEM_SCAN_HOME		0x47
#define GEM_SCAN_UP		0x48
#define GEM_SCAN_PAGE_UP	0x49
#define GEM_SCAN_LEFT		0x4b
#define GEM_SCAN_RIGHT		0x4d
#define GEM_SCAN_END		0x4f
#define GEM_SCAN_DOWN		0x50
#define GEM_SCAN_PAGE_DOWN	0x51
#define GEM_SCAN_INSERT		0x52
#define GEM_SCAN_DELETE		0x53

/*
 * ELKS termios stores each flag field as a 32-bit ABI value.  Application
 * input flags themselves fit in the low word, but using a C long here would
 * pull wide operations into an 8086 hot path.  This little-endian PC mirror
 * makes the ABI width explicit as low/high 16-bit halves.  High halves are
 * preserved without arithmetic.  The cast is confined to calls across the
 * libc ABI boundary.
 */
typedef struct gem_termios_abi {
	GEM_VDI_UWORD iflag_low;
	GEM_VDI_UWORD iflag_high;
	GEM_VDI_UWORD oflag_low;
	GEM_VDI_UWORD oflag_high;
	GEM_VDI_UWORD cflag_low;
	GEM_VDI_UWORD cflag_high;
	GEM_VDI_UWORD lflag_low;
	GEM_VDI_UWORD lflag_high;
	GEM_VDI_UBYTE line;
	GEM_VDI_UBYTE control[NCCS];
} GEM_TERMIOS_ABI;

/* Cause an IA-16 build error if the libc ABI layout ever changes. */
typedef char gem_termios_layout_check[
	(sizeof(GEM_TERMIOS_ABI) == sizeof(struct termios)) ? 1 : -1];

static GEM_VDI_WORD gem_keyboard_fd = -1;
static GEM_VDI_WORD gem_mouse_fd = -1;
static GEM_VDI_WORD gem_keyboard_termios_valid;
static GEM_VDI_WORD gem_mouse_termios_valid;
static GEM_TERMIOS_ABI gem_keyboard_old_termios;
static GEM_TERMIOS_ABI gem_mouse_old_termios;

static GEM_VDI_UBYTE gem_keyboard_buffer[GEM_INPUT_BUFFER_SIZE];
static GEM_VDI_UBYTE gem_keyboard_next;
static GEM_VDI_UBYTE gem_keyboard_count;
static GEM_VDI_UBYTE gem_keyboard_replay;
static GEM_VDI_UBYTE gem_keyboard_replay_valid;
static GEM_VDI_UBYTE gem_keyboard_state;
static GEM_VDI_UBYTE gem_keyboard_parameter;
static GEM_VDI_UBYTE gem_keyboard_idle_poll;

static GEM_VDI_UBYTE gem_mouse_buffer[GEM_MOUSE_BUFFER_SIZE];
static GEM_VDI_UBYTE gem_mouse_next;
static GEM_VDI_UBYTE gem_mouse_count;
static GEM_VDI_UBYTE gem_mouse_packet[3];
static GEM_VDI_UBYTE gem_mouse_packet_count;
static GEM_VDI_UBYTE gem_mouse_protocol;

static GEM_VDI_SCREEN *gem_input_screen;
static GEM_VDI_COORD gem_mouse_x;
static GEM_VDI_COORD gem_mouse_y;
static GEM_VDI_WORD gem_mouse_buttons;

/*
 * Scan codes for A through Z.  A table saves comparisons and avoids any
 * arithmetic that assumes alphabetic scan codes are consecutive (they are
 * not consecutive across the PC keyboard rows).
 */
static const GEM_VDI_UBYTE gem_letter_scan[26] = {
	0x1e, 0x30, 0x2e, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17,
	0x24, 0x25, 0x26, 0x32, 0x31, 0x18, 0x19, 0x10, 0x13,
	0x1f, 0x14, 0x16, 0x2f, 0x11, 0x2d, 0x15, 0x2c
};

/*
 * A Microsoft header carries the two high movement bits in different bit
 * positions.  These small tables avoid multi-count shifts, which are slow on
 * an 8088 because the processor must take the shift count through CL.
 */
static const GEM_VDI_UBYTE gem_ms_x_high[4] = {
	0x00, 0x40, 0x80, 0xc0
};

static const GEM_VDI_UBYTE gem_ms_y_high[16] = {
	0x00, 0x00, 0x00, 0x00,
	0x40, 0x00, 0x00, 0x00,
	0x80, 0x00, 0x00, 0x00,
	0xc0, 0x00, 0x00, 0x00
};

static void
gem_termios_make_keyboard_raw(GEM_TERMIOS_ABI *settings)
{
	/*
	 * Only low words are changed.  ELKS currently defines all of these
	 * flags below bit 16, so the saved high words remain exactly intact.
	 */
	settings->lflag_low &= (GEM_VDI_UWORD) ~(ECHO | ECHONL | ICANON
		| IEXTEN | ISIG);
	settings->iflag_low &= (GEM_VDI_UWORD) ~(ICRNL | INPCK | ISTRIP
		| IXON | BRKINT);
	settings->cflag_low &= (GEM_VDI_UWORD) ~(CSIZE | PARENB);
	settings->cflag_low |= (GEM_VDI_UWORD) (CS8 | CREAD | CLOCAL);
	settings->control[VMIN] = 0;
	settings->control[VTIME] = 0;
}

static void
gem_termios_make_mouse_raw(GEM_TERMIOS_ABI *settings,
	GEM_VDI_UBYTE protocol)
{
	/*
	 * Microsoft packets contain seven significant bits; PS/2 packets use
	 * all eight.  B1200 is placed directly in the low CFLAG word so libc
	 * never needs a 32-bit speed helper for this operation.
	 */
	settings->lflag_low &= (GEM_VDI_UWORD) ~(ECHO | ECHONL | ICANON
		| IEXTEN | ISIG);
	settings->iflag_low &= (GEM_VDI_UWORD) ~(ICRNL | INPCK | ISTRIP
		| IXON | BRKINT | IGNBRK);
	settings->oflag_low &= (GEM_VDI_UWORD) ~OPOST;
	settings->cflag_low &= (GEM_VDI_UWORD) ~(CBAUD | CSIZE | PARENB);
	settings->cflag_low |= (GEM_VDI_UWORD) (B1200 | CREAD | CLOCAL);
	if (protocol == GEM_MOUSE_PROTOCOL_PS2)
		settings->cflag_low |= CS8;
	else
		settings->cflag_low |= CS7;
	settings->control[VMIN] = 0;
	settings->control[VTIME] = 0;
}

static GEM_VDI_WORD
gem_open_keyboard_path(const char *path)
{
	GEM_VDI_WORD descriptor;
	GEM_TERMIOS_ABI settings;

	descriptor = (GEM_VDI_WORD) open(path,
		O_RDONLY | O_NONBLOCK | O_NOCTTY);
	if (descriptor < 0)
		return -1;
	if (tcgetattr(descriptor,
		(struct termios *) &gem_keyboard_old_termios) < 0) {
		close(descriptor);
		return -1;
	}
	settings = gem_keyboard_old_termios;
	gem_termios_make_keyboard_raw(&settings);
	if (tcsetattr(descriptor, TCSAFLUSH,
		(struct termios *) &settings) < 0) {
		close(descriptor);
		return -1;
	}
	gem_keyboard_termios_valid = 1;
	return descriptor;
}

static void
gem_open_keyboard(void)
{
	const char *path;

	gem_keyboard_termios_valid = 0;
	path = getenv("CONSOLE");
	if (path) {
		gem_keyboard_fd = gem_open_keyboard_path(path);
		return;
	}
	gem_keyboard_fd = gem_open_keyboard_path(GEM_KEYBOARD_PRIMARY);
	if (gem_keyboard_fd < 0)
		gem_keyboard_fd = gem_open_keyboard_path(GEM_KEYBOARD_FALLBACK);
}

static void
gem_open_mouse(void)
{
	const char *path;
	const char *protocol;
	GEM_TERMIOS_ABI settings;

	gem_mouse_termios_valid = 0;
	gem_mouse_fd = -1;
	protocol = getenv("MOUSE_PROTOCOL");
	if (!protocol || !strcmp(protocol, "ms"))
		gem_mouse_protocol = GEM_MOUSE_PROTOCOL_MS;
	else if (!strcmp(protocol, "ps2"))
		gem_mouse_protocol = GEM_MOUSE_PROTOCOL_PS2;
	else
		return;

	path = getenv("MOUSE_PORT");
	if (!path)
		path = GEM_MOUSE_DEFAULT;
	if (!strcmp(path, "none"))
		return;

	gem_mouse_fd = (GEM_VDI_WORD) open(path,
		O_RDONLY | O_NONBLOCK | O_NOCTTY);
	if (gem_mouse_fd < 0)
		return;

	/*
	 * Some packet devices do not implement termios.  They are already raw,
	 * so failure to fetch attributes is not a mouse-open failure.  A serial
	 * tty, on the other hand, is changed to the required packet format and
	 * restored when GEM exits.
	 */
	if (tcgetattr(gem_mouse_fd,
		(struct termios *) &gem_mouse_old_termios) == 0) {
		settings = gem_mouse_old_termios;
		gem_termios_make_mouse_raw(&settings, gem_mouse_protocol);
		if (tcsetattr(gem_mouse_fd, TCSAFLUSH,
			(struct termios *) &settings) == 0)
			gem_mouse_termios_valid = 1;
	}
}

static GEM_VDI_WORD
gem_keyboard_get_byte(GEM_VDI_UBYTE *byte)
{
	GEM_VDI_WORD count;

	if (gem_keyboard_replay_valid) {
		*byte = gem_keyboard_replay;
		gem_keyboard_replay_valid = 0;
		return 1;
	}
	if (gem_keyboard_next < gem_keyboard_count) {
		*byte = gem_keyboard_buffer[gem_keyboard_next++];
		return 1;
	}
	gem_keyboard_next = 0;
	gem_keyboard_count = 0;
	count = (GEM_VDI_WORD) read(gem_keyboard_fd, gem_keyboard_buffer,
		GEM_INPUT_BUFFER_SIZE);
	if (count < 0) {
		if (errno == EINTR || errno == EAGAIN)
			return 0;
		return -1;
	}
	if (!count)
		return 0;
	gem_keyboard_count = (GEM_VDI_UBYTE) count;
	*byte = gem_keyboard_buffer[gem_keyboard_next++];
	return 1;
}

static GEM_VDI_UWORD
gem_ascii_scan(GEM_VDI_UBYTE character)
{
	GEM_VDI_UBYTE lower;

	if (character >= 'A' && character <= 'Z')
		lower = character | 0x20;
	else
		lower = character;
	if (lower >= 'a' && lower <= 'z')
		return gem_letter_scan[lower - 'a'];
	/* These editing controls share values with Ctrl-H/I/J/M. */
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

static GEM_VDI_UWORD
gem_ascii_modifiers(GEM_VDI_UBYTE character)
{
	if ((character >= 'A' && character <= 'Z')
	    || (character && strchr("!@#$%^&*()_+{}:\"|<>?~", character)))
		return GEM_VDI_MOD_LSHIFT;
	/*
	 * A raw tty cannot distinguish Ctrl-H from Backspace, Ctrl-I from Tab,
	 * or Ctrl-M from Enter.  Preserve those familiar editing keys; expose
	 * the other control bytes with GEM's control modifier bit.
	 */
	if (character >= 1 && character <= 26 && character != 8
	    && character != 9 && character != 10 && character != 13)
		return GEM_VDI_MOD_CTRL;
	return 0;
}

static GEM_VDI_UWORD
gem_csi_scan(GEM_VDI_UBYTE final, GEM_VDI_UBYTE parameter)
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

static void
gem_return_ascii(GEM_VDI_UBYTE byte, GEM_VDI_UWORD extra_modifiers,
	GEM_VDI_UWORD *character, GEM_VDI_UWORD *modifiers,
	GEM_VDI_UWORD *scan_code)
{
	/* ELKS terminals may send either LF or CR for Enter and DEL for BS. */
	if (byte == 10)
		byte = 13;
	else if (byte == 127)
		byte = 8;
	if (character)
		*character = byte;
	if (modifiers)
		*modifiers = gem_ascii_modifiers(byte) | extra_modifiers;
	if (scan_code)
		*scan_code = gem_ascii_scan(byte);
}

static void
gem_return_extended(GEM_VDI_UWORD scan, GEM_VDI_UWORD *character,
	GEM_VDI_UWORD *modifiers, GEM_VDI_UWORD *scan_code)
{
	if (character)
		*character = 0;
	if (modifiers)
		*modifiers = 0;
	if (scan_code)
		*scan_code = scan;
}

static GEM_VDI_WORD
gem_mouse_get_byte(GEM_VDI_UBYTE *byte)
{
	GEM_VDI_WORD count;

	if (gem_mouse_next < gem_mouse_count) {
		*byte = gem_mouse_buffer[gem_mouse_next++];
		return 1;
	}
	gem_mouse_next = 0;
	gem_mouse_count = 0;
	count = (GEM_VDI_WORD) read(gem_mouse_fd, gem_mouse_buffer,
		GEM_MOUSE_BUFFER_SIZE);
	if (count < 0) {
		if (errno == EINTR || errno == EAGAIN)
			return 0;
		return -1;
	}
	if (!count)
		return 0;
	gem_mouse_count = (GEM_VDI_UBYTE) count;
	*byte = gem_mouse_buffer[gem_mouse_next++];
	return 1;
}

static GEM_VDI_WORD
gem_signed_packet_byte(GEM_VDI_UBYTE byte)
{
	if (byte & 0x80)
		return (GEM_VDI_WORD) byte - 256;
	return byte;
}

static GEM_VDI_COORD
gem_accelerate_delta(GEM_VDI_COORD delta)
{
	/*
	 * Above five counts, double only the excess.  For example 6 becomes
	 * 7 and 20 becomes 35.  A normal packet is at most 128 counts; a PS/2
	 * overflow is saturated to 255, so this calculation remains safely
	 * inside a signed 16-bit word (maximum magnitude 505).
	 */
	if (delta > 5)
		delta += delta - 5;
	else if (delta < -5)
		delta += delta + 5;
	return delta;
}

static GEM_VDI_WORD
gem_parse_ms_mouse(GEM_VDI_UBYTE byte, GEM_VDI_COORD *delta_x,
	GEM_VDI_COORD *delta_y, GEM_VDI_WORD *buttons)
{
	GEM_VDI_UWORD x_value;
	GEM_VDI_UWORD y_value;

	/* Bit 6 identifies a Microsoft packet header; data bytes are six bit. */
	if (!gem_mouse_packet_count) {
		if (!(byte & 0x40))
			return 0;
		gem_mouse_packet[0] = byte;
		gem_mouse_packet_count = 1;
		return 0;
	}
	if (byte & 0x40) {
		/* A new header repairs alignment after a missing data byte. */
		gem_mouse_packet[0] = byte;
		gem_mouse_packet_count = 1;
		return 0;
	}
	gem_mouse_packet[gem_mouse_packet_count++] = byte;
	if (gem_mouse_packet_count < 3)
		return 0;
	gem_mouse_packet_count = 0;

	x_value = gem_ms_x_high[gem_mouse_packet[0] & 0x03];
	x_value |= gem_mouse_packet[1] & 0x3f;
	y_value = gem_ms_y_high[gem_mouse_packet[0] & 0x0c];
	y_value |= gem_mouse_packet[2] & 0x3f;
	*delta_x = (GEM_VDI_COORD) x_value;
	*delta_y = (GEM_VDI_COORD) y_value;
	if (*delta_x > 127)
		*delta_x -= 256;
	if (*delta_y > 127)
		*delta_y -= 256;

	*buttons = 0;
	if (gem_mouse_packet[0] & 0x20)
		*buttons |= GEM_VDI_BUTTON_LEFT;
	if (gem_mouse_packet[0] & 0x10)
		*buttons |= GEM_VDI_BUTTON_RIGHT;
	return 1;
}

static GEM_VDI_WORD
gem_parse_ps2_mouse(GEM_VDI_UBYTE byte, GEM_VDI_COORD *delta_x,
	GEM_VDI_COORD *delta_y, GEM_VDI_WORD *buttons)
{
	if (!gem_mouse_packet_count) {
		/* A valid first PS/2 byte always has its synchronization bit set. */
		if (!(byte & 0x08))
			return 0;
		gem_mouse_packet[0] = byte;
		gem_mouse_packet_count = 1;
		return 0;
	}
	gem_mouse_packet[gem_mouse_packet_count++] = byte;
	if (gem_mouse_packet_count < 3)
		return 0;
	gem_mouse_packet_count = 0;

	if (gem_mouse_packet[0] & 0x40)
		*delta_x = (gem_mouse_packet[0] & 0x10) ? -255 : 255;
	else
		*delta_x = gem_signed_packet_byte(gem_mouse_packet[1]);
	if (gem_mouse_packet[0] & 0x80)
		*delta_y = (gem_mouse_packet[0] & 0x20) ? 255 : -255;
	else
		*delta_y = -gem_signed_packet_byte(gem_mouse_packet[2]);

	*buttons = 0;
	if (gem_mouse_packet[0] & 0x01)
		*buttons |= GEM_VDI_BUTTON_LEFT;
	if (gem_mouse_packet[0] & 0x02)
		*buttons |= GEM_VDI_BUTTON_RIGHT;
	if (gem_mouse_packet[0] & 0x04)
		*buttons |= GEM_VDI_BUTTON_MIDDLE;
	return 1;
}

GEM_VDI_WORD
gem_vdi_open_input(GEM_VDI_SCREEN *screen)
{
	gem_vdi_close_input();
	gem_input_screen = screen;
	gem_keyboard_next = 0;
	gem_keyboard_count = 0;
	gem_keyboard_replay_valid = 0;
	gem_keyboard_state = GEM_KEY_STATE_NORMAL;
	gem_keyboard_parameter = 0;
	gem_keyboard_idle_poll = 0;
	gem_mouse_next = 0;
	gem_mouse_count = 0;
	gem_mouse_packet_count = 0;
	gem_mouse_buttons = 0;
	if (screen) {
		/* Shifts divide by two exactly with no division helper or rounding. */
		gem_mouse_x = screen->xres >> 1;
		gem_mouse_y = screen->yres >> 1;
	} else {
		gem_mouse_x = 0;
		gem_mouse_y = 0;
	}
	gem_open_keyboard();
	gem_open_mouse();
	/*
	 * This layer owns only the physical mouse coordinates and packet state.
	 * AES owns the visible cursor position because only AES knows the active
	 * GEM form's hot spot.  Keeping raster work out of input initialization
	 * also prevents a raw, unadjusted cursor position from being installed
	 * before AES has loaded the original GEM.CFG form.
	 */
	return gem_keyboard_fd >= 0 || gem_mouse_fd >= 0;
}

void
gem_vdi_close_input(void)
{
	if (gem_keyboard_fd >= 0) {
		if (gem_keyboard_termios_valid)
			tcsetattr(gem_keyboard_fd, TCSANOW,
				(struct termios *) &gem_keyboard_old_termios);
		close(gem_keyboard_fd);
	}
	if (gem_mouse_fd >= 0) {
		if (gem_mouse_termios_valid)
			tcsetattr(gem_mouse_fd, TCSANOW,
				(struct termios *) &gem_mouse_old_termios);
		close(gem_mouse_fd);
	}
	gem_keyboard_fd = -1;
	gem_mouse_fd = -1;
	gem_keyboard_termios_valid = 0;
	gem_mouse_termios_valid = 0;
	gem_input_screen = 0;
}

GEM_VDI_WORD
gem_vdi_read_keyboard(GEM_VDI_UWORD *character, GEM_VDI_UWORD *modifiers,
	GEM_VDI_UWORD *scan_code)
{
	GEM_VDI_UBYTE byte;
	GEM_VDI_UWORD scan;
	GEM_VDI_WORD status;
	GEM_VDI_UBYTE steps;

	if (character)
		*character = 0;
	if (modifiers)
		*modifiers = 0;
	if (scan_code)
		*scan_code = 0;
	if (gem_keyboard_fd < 0)
		return GEM_VDI_KEY_NONE;

	/*
	 * One call performs at most eight byte-state transitions.  This bound
	 * keeps event polling responsive on a 4.77 MHz 8088 while persistent
	 * buffers ensure that no later key in the read is discarded.
	 */
	for (steps = 0; steps < GEM_INPUT_BUFFER_SIZE; steps++) {
		status = gem_keyboard_get_byte(&byte);
		if (status < 0)
			return GEM_VDI_KEY_ERROR;
		if (!status) {
			if (gem_keyboard_state != GEM_KEY_STATE_NORMAL) {
				if (!gem_keyboard_idle_poll) {
					gem_keyboard_idle_poll = 1;
					return GEM_VDI_KEY_NONE;
				}
				/*
				 * No continuation arrived over two polls.  Treat a lone or
				 * damaged sequence as Escape instead of blocking it forever.
				 */
				gem_keyboard_state = GEM_KEY_STATE_NORMAL;
				gem_keyboard_parameter = 0;
				gem_keyboard_idle_poll = 0;
				gem_return_ascii(27, 0, character, modifiers,
					scan_code);
				return GEM_VDI_KEY_PRESS;
			}
			return GEM_VDI_KEY_NONE;
		}
		gem_keyboard_idle_poll = 0;

		switch (gem_keyboard_state) {
		case GEM_KEY_STATE_NORMAL:
			if (byte == 27) {
				gem_keyboard_state = GEM_KEY_STATE_ESCAPE;
				continue;
			}
			gem_return_ascii(byte, 0, character, modifiers,
				scan_code);
			return GEM_VDI_KEY_PRESS;

		case GEM_KEY_STATE_ESCAPE:
			if (byte == '[') {
				gem_keyboard_state = GEM_KEY_STATE_CSI;
				gem_keyboard_parameter = 0;
				continue;
			}
			if (byte == 'O') {
				gem_keyboard_state = GEM_KEY_STATE_SS3;
				continue;
			}
			gem_keyboard_state = GEM_KEY_STATE_NORMAL;
			if (byte == 27) {
				/* Return the first Escape and retain the second. */
				gem_keyboard_replay = byte;
				gem_keyboard_replay_valid = 1;
				gem_return_ascii(27, 0, character, modifiers,
					scan_code);
			} else {
				/* ESC followed by a normal byte represents an Alt key. */
				gem_return_ascii(byte, GEM_VDI_MOD_ALT, character,
					modifiers, scan_code);
			}
			return GEM_VDI_KEY_PRESS;

		case GEM_KEY_STATE_CSI:
			if (byte >= '0' && byte <= '9') {
				if (!gem_keyboard_parameter)
					gem_keyboard_parameter = byte;
				continue;
			}
			if (byte == ';')
				continue;
			scan = gem_csi_scan(byte, gem_keyboard_parameter);
			gem_keyboard_state = GEM_KEY_STATE_NORMAL;
			gem_keyboard_parameter = 0;
			if (scan) {
				gem_return_extended(scan, character, modifiers,
					scan_code);
				return GEM_VDI_KEY_PRESS;
			}
			break;

		case GEM_KEY_STATE_SS3:
			scan = gem_csi_scan(byte, 0);
			gem_keyboard_state = GEM_KEY_STATE_NORMAL;
			if (scan) {
				gem_return_extended(scan, character, modifiers,
					scan_code);
				return GEM_VDI_KEY_PRESS;
			}
			break;
		}
	}
	return GEM_VDI_KEY_NONE;
}

GEM_VDI_WORD
gem_vdi_read_mouse(GEM_VDI_COORD *x, GEM_VDI_COORD *y,
	GEM_VDI_WORD *buttons)
{
	GEM_VDI_COORD delta_x;
	GEM_VDI_COORD delta_y;
	GEM_VDI_COORD new_x;
	GEM_VDI_COORD new_y;
	GEM_VDI_WORD new_buttons;
	GEM_VDI_WORD status;
	GEM_VDI_WORD complete;
	GEM_VDI_UBYTE byte;
	GEM_VDI_UBYTE steps;

	if (x)
		*x = gem_mouse_x;
	if (y)
		*y = gem_mouse_y;
	if (buttons)
		*buttons = gem_mouse_buttons;
	if (gem_mouse_fd < 0 || !gem_input_screen)
		return 0;

	for (steps = 0; steps < GEM_MOUSE_BUFFER_SIZE; steps++) {
		status = gem_mouse_get_byte(&byte);
		if (status < 0)
			return -1;
		if (!status)
			return 0;
		if (gem_mouse_protocol == GEM_MOUSE_PROTOCOL_PS2)
			complete = gem_parse_ps2_mouse(byte, &delta_x,
				&delta_y, &new_buttons);
		else
			complete = gem_parse_ms_mouse(byte, &delta_x,
				&delta_y, &new_buttons);
		if (!complete)
			continue;

		delta_x = gem_accelerate_delta(delta_x);
		delta_y = gem_accelerate_delta(delta_y);
		new_x = gem_mouse_x + delta_x;
		new_y = gem_mouse_y + delta_y;
		if (new_x < 0)
			new_x = 0;
		else if (new_x >= gem_input_screen->xres)
			new_x = gem_input_screen->xres - 1;
		if (new_y < 0)
			new_y = 0;
		else if (new_y >= gem_input_screen->yres)
			new_y = gem_input_screen->yres - 1;

		if (new_x == gem_mouse_x && new_y == gem_mouse_y
		    && new_buttons == gem_mouse_buttons)
			/*
			 * A redundant packet is not an input transition.  Continue within
			 * the existing six-byte bound so a second complete packet already
			 * buffered behind it need not wait for the next 20 ms owner tick.
			 */
			continue;
		gem_mouse_x = new_x;
		gem_mouse_y = new_y;
		gem_mouse_buttons = new_buttons;
		/*
		 * Do not draw here.  gem_vdi_read_mouse() reports one completed,
		 * state-changing packet to AES, which applies the current form's hot
		 * spot and performs exactly one cursor move.  Drawing both here and
		 * in AES used to save and restore the 16 by 16 cursor twice for every
		 * serial packet, first at the wrong raw-coordinate origin.
		 */
		if (x)
			*x = gem_mouse_x;
		if (y)
			*y = gem_mouse_y;
		if (buttons)
			*buttons = gem_mouse_buttons;
		return 1;
	}
	return 0;
}
