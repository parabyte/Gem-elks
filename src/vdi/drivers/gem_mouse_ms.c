/*
 * gem_mouse_ms.c - Microsoft serial mouse driver
 *
 * classic 1200-baud Microsoft mouse, three-byte packets, header has
 * bit 6 set and carries buttons plus the two high movement bits, the
 * two data bytes carry six low bits each, anything with bit 6 set
 * restarts the packet so the stream resyncs after a lost byte
 */

#include "gem_mouse_ms.h"

static GEM_VDI_UBYTE gem_ms_packet[3];
static GEM_VDI_UBYTE gem_ms_packet_count;

/* the header byte holds the two high movement bits in odd spots, these tables place them */
static const GEM_VDI_UBYTE gem_ms_x_high[4] = {
	0x00, 0x40, 0x80, 0xc0
};

static const GEM_VDI_UBYTE gem_ms_y_high[16] = {
	0x00, 0x00, 0x00, 0x00,
	0x40, 0x00, 0x00, 0x00,
	0x80, 0x00, 0x00, 0x00,
	0xc0, 0x00, 0x00, 0x00
};

void
gem_mouse_ms_reset(void)
{
	gem_ms_packet_count = 0;
}

GEM_VDI_WORD
gem_mouse_ms_parse(GEM_VDI_UBYTE byte, GEM_VDI_COORD *delta_x,
	GEM_VDI_COORD *delta_y, GEM_VDI_WORD *buttons)
{
	GEM_VDI_UWORD x_value;
	GEM_VDI_UWORD y_value;

	/* bit 6 marks a packet header, data bytes carry six bits */
	if (!gem_ms_packet_count) {
		if (!(byte & 0x40))
			return 0;
		gem_ms_packet[0] = byte;
		gem_ms_packet_count = 1;
		return 0;
	}
	if (byte & 0x40) {
		/* a fresh header resyncs after a lost data byte */
		gem_ms_packet[0] = byte;
		gem_ms_packet_count = 1;
		return 0;
	}
	gem_ms_packet[gem_ms_packet_count++] = byte;
	if (gem_ms_packet_count < 3)
		return 0;
	gem_ms_packet_count = 0;

	x_value = gem_ms_x_high[gem_ms_packet[0] & 0x03];
	x_value |= gem_ms_packet[1] & 0x3f;
	y_value = gem_ms_y_high[gem_ms_packet[0] & 0x0c];
	y_value |= gem_ms_packet[2] & 0x3f;
	*delta_x = (GEM_VDI_COORD) x_value;
	*delta_y = (GEM_VDI_COORD) y_value;
	if (*delta_x > 127)
		*delta_x -= 256;
	if (*delta_y > 127)
		*delta_y -= 256;

	*buttons = 0;
	if (gem_ms_packet[0] & 0x20)
		*buttons |= GEM_VDI_BUTTON_LEFT;
	if (gem_ms_packet[0] & 0x10)
		*buttons |= GEM_VDI_BUTTON_RIGHT;
	return 1;
}
