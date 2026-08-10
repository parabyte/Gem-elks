/*
 * gem_video_hercules.c - Hercules display driver
 *
 * Copyright 1999 Caldera Thin Clients, Inc.
 * Copyright 1987 Digital Research, Inc.
 *
 * no BIOS mode for Hercules graphics so the card is programmed
 * directly, drawing in the shared 1-bit engine gem_video_mono.c,
 * gem_pc_hercules_offset() handles the four-way interleaved scan lines
 */

#include <arch/io.h>

#include "gem_video_hercules.h"

/* load a CRTC table, keeping the screen blanked till done */
static void
gem_hercules_crtc(const GEM_VDI_UBYTE *table, GEM_VDI_UBYTE mode)
{
	GEM_VDI_UWORD index;

	outb(mode, 0x3b8);
	for (index = 0; index < 12; index++) {
		outb((GEM_VDI_UBYTE) index, 0x3b4);
		outb(table[index], 0x3b5);
	}
	outb((GEM_VDI_UBYTE) (mode + 8), 0x3b8);
}

void
gem_hercules_open(GEM_VDI_SCREEN *screen)
{
	static const GEM_VDI_UBYTE graphics_crtc[12] = {
		0x35, 0x2d, 0x2e, 0x07, 0x5b, 0x02,
		0x57, 0x57, 0x02, 0x03, 0x00, 0x00
	};

	/* allow graphics memory, then switch to graphics with video on */
	outb(1, 0x3bf);
	gem_hercules_crtc(graphics_crtc, 0x02);

	screen->xres = 720;
	screen->yres = 348;
	screen->planes = 1;
	screen->colors = 2;
}

void
gem_hercules_close(void)
{
	static const GEM_VDI_UBYTE text_crtc[12] = {
		0x61, 0x50, 0x52, 0x0f, 0x19, 0x06,
		0x19, 0x19, 0x02, 0x0d, 0x0b, 0x0c
	};

	outb(0, 0x3bf);
	gem_hercules_crtc(text_crtc, 0x20);
}
