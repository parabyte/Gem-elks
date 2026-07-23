/*
 * gem_app_vdi.c - VDI parameter arrays for standalone GEM application clients.
 *
 * The imported VDI bindings (built with -DUSER_INTIN) reference these five
 * arrays as externs so a client can place them in its own data segment.  The
 * Desktop supplies its copies in deskgraf.c; this file supplies them for the
 * standalone applications, which do not link the Desktop sources.
 *
 * Sizes match the Desktop's so every classic VDI call has room for its
 * control, input, and output words.
 */

#include "portab.h"

WORD contrl[12];
WORD intin[128];
WORD ptsin[256];
WORD intout[45];
WORD ptsout[12];
