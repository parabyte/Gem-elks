/*
 * gemdefs.h - the event, window and resource constants
 *
 * Split out of aes.h, which had grown to carry what DRI shipped
 * as five separate headers.  Include "aes.h" to get them all;
 * this header stands alone for code that wants only this part.
 */

#ifndef ELKS_GEM_GEMDEFS_H
#define ELKS_GEM_GEMDEFS_H

#include "gem_types.h"

/***************************************************************************
 * Miscellaneous Constants
 ***************************************************************************/

#ifndef FAILURE
#define FAILURE (-1)
#endif

#ifndef SUCCESS
#define SUCCESS (0)
#endif

#ifndef YES
#define YES     1
#endif

#ifndef NO
#define NO      0
#endif

#ifndef TRUE
#define TRUE    (1)
#endif

#ifndef FALSE
#define FALSE   (0)
#endif

#ifndef NULL
#define NULL    0
#endif

#ifndef NULLPTR
#define NULLPTR ((void *)0)
#endif

#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif

/***************************************************************************
 * Event Constants
 ***************************************************************************/

/* Event mask bits */
#define MU_KEYBD    0x0001
#define MU_BUTTON   0x0002
#define MU_M1       0x0004
#define MU_M2       0x0008
#define MU_MESAG    0x0010
#define MU_TIMER    0x0020
#define MU_M3       0x0040

/* Message types */
#define MN_SELECTED 10
#define WM_REDRAW   20
#define WM_TOPPED   21
#define WM_CLOSED   22
#define WM_FULLED   23
#define WM_ARROWED  24
#define WM_HSLID    25
#define WM_VSLID    26
#define WM_SIZED    27
#define WM_MOVED    28
#define WM_NEWTOP   29
#define WM_UNTOPPED 30
#define WM_ONTOP    31
#define WM_OFFTOP   32
#define PR_FINISH   33
#define CT_UPDATE   50
#define CT_MOVE     51
#define CT_NEWTOP   52

/* wind_update() flags */
#define END_UPDATE  0
#define BEG_UPDATE  1
#define END_MCTRL   2
#define BEG_MCTRL   3
#define AC_OPEN     40
#define AC_CLOSE    41

/***************************************************************************
 * Window Constants
 ***************************************************************************/

/* Window components */
#define NAME        0x0001
#define CLOSER      0x0002
#define FULLER      0x0004
#define MOVER       0x0008
#define INFO        0x0010
#define SIZER       0x0020
#define UPARROW     0x0040
#define DNARROW     0x0080
#define VSLIDE      0x0100
#define LFARROW     0x0200
#define RTARROW     0x0400
#define HSLIDE      0x0800
#define SMALLER     0x4000

#define WKIND_SHOW  (NAME | CLOSER | FULLER | MOVER | INFO | SIZER \
                     | UPARROW | DNARROW | VSLIDE)

/* Wind_get/set modes */
#define WF_KIND         1
#define WF_NAME         2
#define WF_INFO         3
#define WF_WORKXYWH     4
#define WF_CURRXYWH     5
#define WF_PREVXYWH     6
#define WF_FULLXYWH     7
#define WF_HSLIDE       8
#define WF_VSLIDE       9
#define WF_TOP          10
#define WF_FIRSTXYWH    11
#define WF_NEXTXYWH     12
#define WF_NEWDESK      14
#define WF_HSLSIZ       15
#define WF_VSLSIZ       16
#define WF_SCREEN       17
#define WF_SIZTOP       18
#define WF_COLOR        18
#define WF_TATTRB       19
#define WF_BEVAL        32
#define WF_BOTTOM       33
#define WF_DCOLOR       71

/* Form_dial modes */
#define FMD_START       0
#define FMD_GROW        1
#define FMD_SHRINK      2
#define FMD_FINISH      3

/* Graf_mouse forms */
#define ARROW           0
#define TEXT_CRSR       1
#define BUSYBEE         2
#define HOURGLASS       2
#define HGLASS          2
#define POINT_HAND      3
#define FLAT_HAND       4
#define THIN_CROSS      5
#define THICK_CROSS     6
#define OUTLN_CROSS     7
#define USER_DEF        255
#define M_OFF           256
#define M_ON            257

/* Resource types */
#define R_TREE          0
#define R_OBJECT        1
#define R_TEDINFO       2
#define R_ICONBLK       3
#define R_BITBLK        4
#define R_STRING        5
#define R_IMAGEDATA     6
#define R_OBSPEC        7
#define R_TEPTEXT       8
#define R_TEPTMPLT      9
#define R_TEPVALID      10
#define R_IBPMASK       11
#define R_IBPDATA       12
#define R_IBPTEXT       13
#define R_BIPDATA       14
#define R_FRSTR         15
#define R_FRIMG         16


#endif				/* gemdefs.h */
