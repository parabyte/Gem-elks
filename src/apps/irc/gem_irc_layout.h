/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

/*
 * gem_irc_layout.h - shared pixel constants for the GEM IRC UI
 *
 * the target build and the host layout smoke test use these exact values
 */

#ifndef ELKS_GEM_IRC_LAYOUT_H
#define ELKS_GEM_IRC_LAYOUT_H

#define GEM_IRC_LAYOUT_MIN_WIDTH       400
/* fits the topic strip, a few transcript rows, the editor, and the toolbar */
#define GEM_IRC_LAYOUT_MIN_HEIGHT      176
#define GEM_IRC_LAYOUT_MARGIN          16
#define GEM_IRC_LAYOUT_INPUT_PADDING   6
#define GEM_IRC_LAYOUT_TOPIC_PADDING   4
#define GEM_IRC_LAYOUT_SIDE_WIDTH      104
#define GEM_IRC_LAYOUT_PANE_PADDING    4
#define GEM_IRC_LAYOUT_TEXT_SIZE       80U

#endif				/* ELKS_GEM_IRC_LAYOUT_H */
