/*
 * gem_mouse_ps2.c - PS/2 mouse driver
 *
 * PS/2 packets are three bytes, header with bit 3 always set (the
 * resync check) then signed X and Y, the header also carries buttons,
 * the two sign bits and two overflow bits, on overflow movement is
 * pinned to the largest step, PS/2 Y grows up so its flipped to screen
 */

#include "gem_mouse_ps2.h"

static GEM_VDI_UBYTE gem_ps2_packet[3];
static GEM_VDI_UBYTE gem_ps2_packet_count;

static GEM_VDI_WORD
gem_ps2_signed_byte(GEM_VDI_UBYTE byte)
{
	if (byte & 0x80)
		return (GEM_VDI_WORD) byte - 256;
	return byte;
}

void
gem_mouse_ps2_reset(void)
{
	gem_ps2_packet_count = 0;
}

GEM_VDI_WORD
gem_mouse_ps2_parse(GEM_VDI_UBYTE byte, GEM_VDI_COORD *delta_x,
	GEM_VDI_COORD *delta_y, GEM_VDI_WORD *buttons)
{
	if (!gem_ps2_packet_count) {
		/* a valid first PS/2 byte always has its sync bit set */
		if (!(byte & 0x08))
			return 0;
		gem_ps2_packet[0] = byte;
		gem_ps2_packet_count = 1;
		return 0;
	}
	gem_ps2_packet[gem_ps2_packet_count++] = byte;
	if (gem_ps2_packet_count < 3)
		return 0;
	gem_ps2_packet_count = 0;

	if (gem_ps2_packet[0] & 0x40)
		*delta_x = (gem_ps2_packet[0] & 0x10) ? -255 : 255;
	else
		*delta_x = gem_ps2_signed_byte(gem_ps2_packet[1]);
	if (gem_ps2_packet[0] & 0x80)
		*delta_y = (gem_ps2_packet[0] & 0x20) ? 255 : -255;
	else
		*delta_y = -gem_ps2_signed_byte(gem_ps2_packet[2]);

	*buttons = 0;
	if (gem_ps2_packet[0] & 0x01)
		*buttons |= GEM_VDI_BUTTON_LEFT;
	if (gem_ps2_packet[0] & 0x02)
		*buttons |= GEM_VDI_BUTTON_RIGHT;
	if (gem_ps2_packet[0] & 0x04)
		*buttons |= GEM_VDI_BUTTON_MIDDLE;
	return 1;
}
