/*
 * gem_video_cga.c - CGA display driver
 *
 * Copyright 1999 Caldera Thin Clients, Inc.
 * Copyright 1987 Digital Research, Inc.
 *
 * 640x200 mono, drawing lives in the shared 1-bit engine in
 * gem_video_mono.c, even rows at offset 0 odd rows at 0x2000,
 * gem_pc_cga_offset() handles that layout
 */

#include "gem_video_cga.h"

void
gem_cga_open(GEM_VDI_SCREEN *screen)
{
	gem_bios_video_set_mode(0x06);
	screen->xres = 640;
	screen->yres = 200;
	screen->planes = 1;
	screen->colors = 2;
}
