/*
 * gem_input.c - native GEM keyboard and mouse input for ELKS
 *
 * tty gives bytes, GEM gets IBM PC scan codes and 16-bit screen coords back
 * keyboard is /dev/tty1 or /dev/console (CONSOLE overrides), ANSI cursor keys
 * turn into BIOS scan codes, mouse is a 1200-baud ms serial mouse on
 * /dev/ttyS0, MOUSE_PORT and MOUSE_PROTOCOL (ms/ps2/amstrad) override
 * this is just the core, the hardware knowhow lives in drivers/ (gem_kbd_ansi,
 * gem_mouse_ms/ps2/amstrad, the amstrad port is counter ports, no fd)
 */

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "vdi.h"
#include "drivers/gem_kbd_ansi.h"
#include "drivers/gem_kbd_raw.h"
#include "drivers/gem_mouse_amstrad.h"
#include "drivers/gem_mouse_ms.h"
#include "drivers/gem_mouse_ps2.h"

/* set by the video driver once it holds the console graphics lock and
 * turns on DCSET_KRAW, then the keyboard fd gives raw set-1 scancodes
 * instead of ascii, needed for the Amstrad mouse buttons */
extern GEM_VDI_WORD gem_console_kraw;

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
#define GEM_MOUSE_PROTOCOL_AMSTRAD	2

/*
 * ELKS termios keeps each flag field as a 32-bit ABI value, this
 * little-endian mirror splits each into 16-bit halves, cast to struct
 * termios only at the libc boundary
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

/* break the IA-16 build if the libc ABI layout ever changes */
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
static GEM_VDI_UBYTE gem_mouse_protocol;
static GEM_VDI_UBYTE gem_mouse_active;

static GEM_VDI_SCREEN *gem_input_screen;
static GEM_VDI_COORD gem_mouse_x;
static GEM_VDI_COORD gem_mouse_y;
static GEM_VDI_WORD gem_mouse_buttons;

static void
gem_termios_make_keyboard_raw(GEM_TERMIOS_ABI *settings)
{
	/* ELKS keeps all these flags below bit 16, only the low words change */
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
gem_termios_make_mouse_raw(GEM_TERMIOS_ABI *settings, GEM_VDI_UBYTE protocol)
{
	/* ms packets carry seven bits, PS/2 packets use all eight */
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
	if (tcsetattr(descriptor, TCSAFLUSH, (struct termios *) &settings) < 0) {
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
	gem_mouse_active = 0;
	/* default is the PC1640 mouse, MOUSE_PROTOCOL=ms/ps2 picks another */
	protocol = getenv("MOUSE_PROTOCOL");
	if (!protocol || !strcmp(protocol, "amstrad"))
		gem_mouse_protocol = GEM_MOUSE_PROTOCOL_AMSTRAD;
	else if (!strcmp(protocol, "ms"))
		gem_mouse_protocol = GEM_MOUSE_PROTOCOL_MS;
	else if (!strcmp(protocol, "ps2"))
		gem_mouse_protocol = GEM_MOUSE_PROTOCOL_PS2;
	else
		return;

	if (gem_mouse_protocol == GEM_MOUSE_PROTOCOL_AMSTRAD) {
		/* amstrad mouse port is counter I/O ports, not a file, nothing to open */
		gem_mouse_amstrad_reset();
		gem_mouse_active = 1;
		return;
	}

	path = getenv("MOUSE_PORT");
	if (!path)
		path = GEM_MOUSE_DEFAULT;
	if (!strcmp(path, "none"))
		return;

	gem_mouse_fd = (GEM_VDI_WORD) open(path,
		O_RDONLY | O_NONBLOCK | O_NOCTTY);
	if (gem_mouse_fd < 0)
		return;
	gem_mouse_active = 1;
	if (gem_mouse_protocol == GEM_MOUSE_PROTOCOL_PS2)
		gem_mouse_ps2_reset();
	else
		gem_mouse_ms_reset();

	/* some packet devices dont do termios, theyre already raw, a failed
	 * tcgetattr dont mean the open failed */
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

static void
gem_return_ascii(GEM_VDI_UBYTE byte, GEM_VDI_UWORD extra_modifiers,
	GEM_VDI_UWORD *character, GEM_VDI_UWORD *modifiers,
	GEM_VDI_UWORD *scan_code)
{
	/* ELKS terminals may send LF or CR for Enter, DEL for Backspace */
	if (byte == 10)
		byte = 13;
	else if (byte == 127)
		byte = 8;
	if (character)
		*character = byte;
	if (modifiers)
		*modifiers = gem_kbd_ansi_modifiers(byte) | extra_modifiers;
	if (scan_code)
		*scan_code = gem_kbd_ansi_scan(byte);
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

static GEM_VDI_COORD
gem_accelerate_delta(GEM_VDI_COORD delta)
{
	/* above five only the excess doubles, 6 becomes 7, 20 becomes 35 */
	if (delta > 5)
		delta += delta - 5;
	else if (delta < -5)
		delta += delta + 5;
	return delta;
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
	gem_kbd_raw_reset();
	gem_mouse_next = 0;
	gem_mouse_count = 0;
	gem_mouse_buttons = 0;
	if (screen) {
		gem_mouse_x = screen->xres >> 1;
		gem_mouse_y = screen->yres >> 1;
	} else {
		gem_mouse_x = 0;
		gem_mouse_y = 0;
	}
	gem_open_keyboard();
	gem_open_mouse();
	/* AES owns the visible cursor, only it knows the forms hot spot */
	return gem_keyboard_fd >= 0 || gem_mouse_active;
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
	gem_mouse_active = 0;
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

	/* at most eight bytes per call, buffers persist so no key is lost */
	for (steps = 0; steps < GEM_INPUT_BUFFER_SIZE; steps++) {
		status = gem_keyboard_get_byte(&byte);
		if (status < 0)
			return GEM_VDI_KEY_ERROR;
		if (!status)
			return GEM_VDI_KEY_NONE;

		/* amstrad mouse buttons ride the keyboard stream as raw
		 * scancodes 7E/FE and 7D/FD, give them to that driver
		 * instead of treating them as keys */
		if (gem_mouse_protocol == GEM_MOUSE_PROTOCOL_AMSTRAD
			&& gem_mouse_active
			&& gem_mouse_amstrad_key_byte(byte))
			continue;

		/* in DCSET_KRAW mode every byte is a raw set-1 scancode,
		 * decode it ourselves. with no raw mode the tty gives ascii
		 * so hand the byte back as-is */
		if (gem_console_kraw) {
			if (gem_kbd_raw_scancode(byte, character, modifiers,
					scan_code) == GEM_VDI_KEY_PRESS)
				return GEM_VDI_KEY_PRESS;
			continue;
		}
		gem_return_ascii(byte, 0, character, modifiers, scan_code);
		return GEM_VDI_KEY_PRESS;
	}
	return GEM_VDI_KEY_NONE;
}

/*
 * take one finished movement/button report, run it through the shared
 * acceleration and screen clamp, publish it when it changes anything,
 * returns nonzero when an event went out
 */
static GEM_VDI_WORD
gem_mouse_apply(GEM_VDI_COORD delta_x, GEM_VDI_COORD delta_y,
	GEM_VDI_WORD new_buttons, GEM_VDI_COORD *x, GEM_VDI_COORD *y,
	GEM_VDI_WORD *buttons)
{
	GEM_VDI_COORD new_x;
	GEM_VDI_COORD new_y;

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
		/* a packet that changes nothing is not an input event */
		return 0;
	gem_mouse_x = new_x;
	gem_mouse_y = new_y;
	gem_mouse_buttons = new_buttons;
	/* no drawing here, AES applies the forms hot spot and does the one
	 * cursor move, drawing in both places redrew it twice per packet,
	 * once at the wrong origin */
	if (x)
		*x = gem_mouse_x;
	if (y)
		*y = gem_mouse_y;
	if (buttons)
		*buttons = gem_mouse_buttons;
	return 1;
}

GEM_VDI_WORD
gem_vdi_read_mouse(GEM_VDI_COORD *x, GEM_VDI_COORD *y, GEM_VDI_WORD *buttons)
{
	GEM_VDI_COORD delta_x;
	GEM_VDI_COORD delta_y;
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
	if (!gem_mouse_active || !gem_input_screen)
		return 0;

	if (gem_mouse_protocol == GEM_MOUSE_PROTOCOL_AMSTRAD) {
		/* amstrad counters accumulate in hardware, one read per tick
		 * collects everything since the last, buttons came from the
		 * keyboard stream */
		gem_mouse_amstrad_poll(&delta_x, &delta_y);
		return gem_mouse_apply(delta_x, delta_y,
			gem_mouse_amstrad_buttons(), x, y, buttons);
	}

	for (steps = 0; steps < GEM_MOUSE_BUFFER_SIZE; steps++) {
		status = gem_mouse_get_byte(&byte);
		if (status < 0)
			return -1;
		if (!status)
			return 0;
		if (gem_mouse_protocol == GEM_MOUSE_PROTOCOL_PS2)
			complete = gem_mouse_ps2_parse(byte, &delta_x,
				&delta_y, &new_buttons);
		else
			complete = gem_mouse_ms_parse(byte, &delta_x,
				&delta_y, &new_buttons);
		if (!complete)
			continue;
		if (gem_mouse_apply(delta_x, delta_y, new_buttons,
				x, y, buttons))
			return 1;
	}
	return 0;
}
