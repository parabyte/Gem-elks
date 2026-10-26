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

void
gem_mouse_ms_reset(void)
{
	gem_ms_packet_count = 0;
}

GEM_VDI_WORD
gem_mouse_ms_parse(GEM_VDI_UBYTE byte, GEM_VDI_COORD *delta_x,
	GEM_VDI_COORD *delta_y, GEM_VDI_WORD *buttons)
{
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

	/* put the high bits back, then sign extend the eight-bit movement */
	*delta_x = (signed char) (((gem_ms_packet[0] & 0x03) << 6)
		| (gem_ms_packet[1] & 0x3f));
	*delta_y = (signed char) (((gem_ms_packet[0] & 0x0c) << 4)
		| (gem_ms_packet[2] & 0x3f));

	*buttons = 0;
	if (gem_ms_packet[0] & 0x20)
		*buttons |= GEM_VDI_BUTTON_LEFT;
	if (gem_ms_packet[0] & 0x10)
		*buttons |= GEM_VDI_BUTTON_RIGHT;
	return 1;
}
