/*
 * gem_video_cga.h - CGA driver interface
 *
 * Copyright 1999 Caldera Thin Clients, Inc.
 * Copyright 1987 Digital Research, Inc.
 */

#ifndef ELKS_GEM_VIDEO_CGA_H
#define ELKS_GEM_VIDEO_CGA_H

#include "gem_video_common.h"

/* switch to 640x200 graphics and fill in screen geometry */
void gem_cga_open(GEM_VDI_SCREEN *screen);

#endif				/* ELKS_GEM_VIDEO_CGA_H */
