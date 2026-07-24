/*
 * Copyright (C) 2026 Goliath Keet <gatekeeper@xt-emporium.com>
 * SPDX-License-Identifier: GPL-2.0-only
 */

/*
 * gem_irc_layout.h - shared scale-one pixel constants for the GEM IRC UI.
 *
 * The target and host layout smoke use these exact values.  Coordinates are
 * signed 16-bit GEM pixels; no scaling, rounding, multiplication, division,
 * or value wider than one 8086 word crosses this interface.
 */

#ifndef ELKS_GEM_IRC_LAYOUT_H
#define ELKS_GEM_IRC_LAYOUT_H

#define GEM_IRC_LAYOUT_MIN_WIDTH       400
#define GEM_IRC_LAYOUT_MIN_HEIGHT      140
#define GEM_IRC_LAYOUT_MARGIN          16
#define GEM_IRC_LAYOUT_INPUT_PADDING   6
#define GEM_IRC_LAYOUT_TOPIC_PADDING   4
#define GEM_IRC_LAYOUT_SIDE_WIDTH      104
#define GEM_IRC_LAYOUT_PANE_PADDING    4
#define GEM_IRC_LAYOUT_TEXT_SIZE       80U

#endif /* ELKS_GEM_IRC_LAYOUT_H */
