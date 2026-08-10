/*
 * gsxdefs.h - the GSX attribute constants shared by AES and VDI
 *
 * Split out of aes.h, which had grown to carry what DRI shipped
 * as five separate headers.  Include "aes.h" to get them all;
 * this header stands alone for code that wants only this part.
 */

#ifndef ELKS_GEM_GSXDEFS_H
#define ELKS_GEM_GSXDEFS_H

#include "gem_types.h"

/* GSX modes */
#define MD_REPLACE  1
#define MD_TRANS    2
#define MD_XOR      3
#define MD_ERASE    4

/* GSX fill styles */
#define FIS_HOLLOW  0
#define FIS_SOLID   1
#define FIS_PATTERN 2
#define FIS_HATCH   3
#define FIS_USER    4

/* GSX line styles */
#define SOLID       1
#define LDASHED     2
#define DOTTED      3
#define DASHDOT     4
#define DASHED      5
#define DASHDOTDOT  6
#define USERLINE    7

/* Bit blt rules */
#define ALL_WHITE   0
#define S_AND_D     1
#define S_ONLY      3
#define NOTS_AND_D  4
#define S_XOR_D     6
#define S_OR_D      7
#define D_INVERT    10
#define NOTS_OR_D   13
#define ALL_BLACK   15

/* Font types */
#define IBM         3
#define SMALL       5


#endif				/* gsxdefs.h */
