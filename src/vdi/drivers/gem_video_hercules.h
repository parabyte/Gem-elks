/*
 * gem_video_hercules.h - Hercules driver interface
 *
 * Copyright 1999 Caldera Thin Clients, Inc.
 * Copyright 1987 Digital Research, Inc.
 */

#ifndef ELKS_GEM_VIDEO_HERCULES_H
#define ELKS_GEM_VIDEO_HERCULES_H

#include "gem_video_common.h"

/* switch to 720x348 graphics and fill in screen geometry */
void gem_hercules_open(GEM_VDI_SCREEN *screen);

/* put the card back in text mode */
void gem_hercules_close(void);

#endif				/* ELKS_GEM_VIDEO_HERCULES_H */
