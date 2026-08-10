/*
 * gem_app_vdi.c - VDI parameter arrays for standalone GEM apps
 *
 * the VDI bindings (-DUSER_INTIN) want these arrays as externs, the
 * desktop has its own in deskgraf.c, sizes match the desktop's
 */

#include "aes.h"

WORD contrl[12];
WORD intin[128];
WORD ptsin[256];
WORD intout[45];
WORD ptsout[12];
