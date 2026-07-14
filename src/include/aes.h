/*
 * aes.h - GEM-compatible AES/VDI API for GEM VDI
 *
 * This header exposes the GEM source API used by the Linux and ELKS
 * GEM desktop ports.  Applications include this header, or one of the
 * legacy aliases such as PPDGEM.H or VDI.H, and link against libgem.
 *
 * The implementation is backed directly by the local GEM VDI engine.
 */

#ifndef ELKS_GEM_AES_H
#define ELKS_GEM_AES_H

#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>

/* Legacy helper macros (Pacific C style) */
#undef ADDR
#define ADDR(p)        ((void *)(p))
#define LBCOPY(d,s,n)  memcpy((d),(s),(n))
#define LWCOPY(d,s,n)  memcpy((d),(s), (n) * sizeof(WORD))
#define LWGET(p)       (*(WORD *)(p))
#define LBGET(p)       (*(BYTE *)(p))

/*
 * Compact ELKS clients keep the old near-pointer convention and export a
 * zero segment to their process-local AES.  The direct original Desktop is
 * different: its RSC records live in resident far segments, and its GEMDOS
 * buffers remain near pointers promoted with the caller's real DS.  GNU ia16
 * already represents both cases as the original adjacent offset/segment
 * words.  Extract those words through gem_near_pointer_words() in that one
 * profile; no normalization, wide scalar, shift, or pointer flattening is
 * performed.
 */
#if GEM_TRAP_FAR_DATA
#define FP_OFF(p)      ((WORD) gem_near_pointer_words(\
				(const void FAR *)(p)).lo)
#define FP_SEG(p)      ((WORD) gem_near_pointer_words(\
				(const void FAR *)(p)).hi)
#else
#define FP_OFF(p)      ((WORD) (UWORD) (p))
#define FP_SEG(p)      ((WORD) 0)
#endif
#define MK_FP(seg,off) ((LPVOID) ((UWORD) (seg) == 0 \
			 ? (UWORD) (off) : (UWORD) 0))
#define FPOFF(p)       FP_OFF(p)
#define FPSEG(p)       FP_SEG(p)
#define MKFP(seg,off)  MK_FP(seg, off)

#ifndef min
#define min(a,b)       (( (a) < (b) ) ? (a) : (b))
#endif
#ifndef max
#define max(a,b)       (( (a) > (b) ) ? (a) : (b))
#endif

/***************************************************************************
 * Basic Types (from aes_types.h / vdi_types.h)
 ***************************************************************************/

#ifndef BYTE
#define BYTE    char                /* Signed byte */
#endif

#ifndef BOOLEAN
#define BOOLEAN int                 /* 2 valued (true/false) */
#endif

#ifndef WORD
#define WORD    int                 /* Signed word (16 bits) */
#endif

#ifndef UWORD
#define UWORD   unsigned int        /* Unsigned word */
#endif

/*
 * Historic GEM files and call interfaces contain four-byte integer fields.
 * They are unscaled little-endian bit patterns, represented as two explicit
 * 16-bit halves so ia16-gcc can never introduce a wider scalar temporary.
 * Arithmetic, when an interface requires it, must update lo first and carry
 * or borrow explicitly into hi.  Conversion to a near pointer is valid only
 * when hi is zero.  The packed attribute fixes the wire/structure size at
 * exactly four bytes without relying on the host compiler's alignment rules.
 */
typedef struct __attribute__((packed)) gem_u32_words {
    UWORD lo;
    UWORD hi;
} GEM_U32_WORDS;

/*
 * Four-byte GEM quantities are manipulated only through these word-pair
 * helpers.  Values are unsigned, unscaled, and little-endian at an external
 * interface.  Addition and multiplication wrap modulo 2^32, matching the
 * historical bit pattern without creating a C scalar wider than 16 bits.
 * The saturating conversion clamps any value above 65535 to 65535.
 */
#ifndef UBYTE
#define UBYTE   unsigned char       /* Unsigned byte */
#endif

/* Modifier keywords */
#ifndef REG
#define REG     register
#endif

#ifndef LOCAL
#define LOCAL   auto
#endif

#ifndef EXTERN
#define EXTERN  extern
#endif

#ifndef MLOCAL
#define MLOCAL  static
#endif

#ifndef GLOBAL
#define GLOBAL  /**/
#endif

#ifndef VOID
#define VOID    void
#endif

/*
 * Most ELKS GEM code keeps data in the executable's primary segment, where a
 * near pointer is the cheapest representation on an 8088.  The trap-linked
 * original Desktop is the one intentional exception: classic AES resource
 * blocks live in resident-owned paragraph segments and RSRC_GADDR returns the
 * original offset:segment address.  Restore the historical far LP* types for
 * that client only.  Function code remains in the selected small/medium code
 * model; this switch changes data pointers, not the CPU or operating mode.
 */
#undef FAR
#undef NEAR
#undef far
#undef near
#if GEM_TRAP_FAR_DATA
#define FAR     __far
#define NEAR    /* primary-data pointer */
#define far     __far
#define near    /* primary-data pointer */
#else
#define FAR     /* empty */
#define NEAR    /* empty */
#define far     /* empty */
#define near    /* empty */
#endif

/*
 * Ordinary application data remains near even in the original-client
 * profile.  Only pointers decoded from a four-byte GEM slot, and the typed
 * resource records reached through them, carry a segment word.  This keeps
 * directory, window, and event hot paths on cheap near addressing.
 */
typedef WORD *LPWORD;
typedef BYTE *LPBYTE;
typedef void *LPVOID;
typedef UWORD *LPUWORD;
typedef LPBYTE *LPLSTR;
typedef LPVOID *LPLPTR;

typedef void FAR *GEM_SLOT_POINTER;
typedef BYTE FAR *GEM_SLOT_BYTE_POINTER;
typedef WORD FAR *GEM_SLOT_WORD_POINTER;

GEM_U32_WORDS gem_u32_words(UWORD lo, UWORD hi);
VOID gem_u32_add_to(GEM_U32_WORDS *value, GEM_U32_WORDS amount);
GEM_U32_WORDS gem_u32_mul_u16(UWORD left, UWORD right);
UWORD gem_u32_to_u16_sat(GEM_U32_WORDS value);
GEM_U32_WORDS gem_u32_div10(GEM_U32_WORDS value, UWORD *remainder);

/*
 * Cross the historical four-byte GEM pointer slot without a C long.  Packing
 * writes the 16-bit near offset to lo and always clears hi.  Unpacking rejects
 * a nonzero high half because it cannot name data in the current ELKS small-
 * model data segment.  Values are unscaled byte offsets; no rounding occurs.
 */
GEM_U32_WORDS gem_near_pointer_words(const void FAR *pointer);
LPVOID gem_near_words_pointer(GEM_U32_WORDS field);

typedef struct gemblkstr {
    LPUWORD gb_pcontrol;
    LPUWORD gb_pglobal;
    LPUWORD gb_pintin;
    LPUWORD gb_pintout;
    LPLPTR  gb_padrin;
    LPLPTR  gb_padrout;
} GEMBLK;

typedef GEMBLK *LPGEMBLK;

typedef struct gsx_parameters {
    LPWORD contrl;
    LPWORD intin;
    LPWORD ptsin;
    LPWORD intout;
    LPWORD ptsout;
} GSXPAR;

typedef GSXPAR *LPGSXPAR;

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
 * Object Constants
 ***************************************************************************/

#define ROOT        0
#define NIL         -1

/* Keyboard states */
#define K_RSHIFT    0x0001
#define K_LSHIFT    0x0002
#define K_CTRL      0x0004
#define K_ALT       0x0008

/* Max string length */
#define MAX_LEN     81
#define MAX_DEPTH   8

/* Inside patterns */
#define IP_HOLLOW   0
#define IP_1PATT    1
#define IP_2PATT    2
#define IP_3PATT    3
#define IP_4PATT    4
#define IP_5PATT    5
#define IP_6PATT    6
#define IP_SOLID    7

/* System colors */
#define SYS_FG      0x1100
#define WTS_FG      0x11a1
#define WTN_FG      0x1100

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

/* Object types */
#define G_BOX       20
#define G_TEXT      21
#define G_BOXTEXT   22
#define G_IMAGE     23
#define G_USERDEF   24
#define G_PROGDEF   24
#define G_IBOX      25
#define G_BUTTON    26
#define G_BOXCHAR   27
#define G_STRING    28
#define G_FTEXT     29
#define G_FBOXTEXT  30
#define G_ICON      31
#define G_TITLE     32
#define G_CICON     33
#define G_CLRICN    33
#define G_DTMFDB    34

/* Desktop MFDB placement for xgrf_dtimage(). */
#define DT_CENTER   1
#define DT_TILE     2

/* Wind_set / Wind_get constants */
#define WF_KIND         1
#define WF_NAME         2
#define WF_INFO         3
#define WF_WXYWH        4
#define WF_CURRXYWH     5
#define WF_PREVXYWH     6
#define WF_FULLXYWH     7
#define WF_CXYWH        WF_CURRXYWH
#define WF_PXYWH        WF_PREVXYWH
#define WF_FXYWH        WF_FULLXYWH
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

/* Wind_calc modes */
#define WC_BORDER       0
#define WC_WORK         1
#define WA_UPPAGE       0
#define WA_DNPAGE       1
#define WA_UPLINE       2
#define WA_DNLINE       3
#define WA_LFPAGE       4
#define WA_RTPAGE       5
#define WA_LFLINE       6
#define WA_RTLINE       7
/* Object flags */
#define NONE        0x0000
#define SELECTABLE  0x0001
#define DEFAULT     0x0002
#define EXIT        0x0004
#define EDITABLE    0x0008
#define RBUTTON     0x0010
#define LASTOB      0x0020
#define TOUCHEXIT   0x0040
#define HIDETREE    0x0080
#define INDIRECT    0x0100
#define ESCCANCEL   0x0200
#define BITBUTTON   0x0400
#define SCROLLER    0x0800
#define FLAG3D      0x1000
#define USECOLOURCAT 0x2000
#define USECOLORCAT  0x2000
#define FL3DIND     0x1000
#define FL3DBAK     0x4000
#define FL3DACT     0x5000
#define SUBMENU     0x8000

/* Object states */
#define NORMAL      0x0000
#define SELECTED    0x0001
#define CROSSED     0x0002
#define CHECKED     0x0004
#define DISABLED    0x0008
#define OUTLINED    0x0010
#define SHADOWED    0x0020
#define WHITEBAK    0x0040
#define DRAW3D      0x0080
#define HIGHLIGHTED 0x0100
#define UNHIGHLIGHTED 0x0200

/* Object colors */
#define WHITE       0
#define BLACK       1
#define RED         2
#define GREEN       3
#define BLUE        4
#define CYAN        5
#define YELLOW      6
#define MAGENTA     7
#define DWHITE      8
#define DBLACK      9
#define DRED        10
#define DGREEN      11
#define DBLUE       12
#define DCYAN       13
#define DYELLOW     14
#define DMAGENTA    15

/* Edit modes */
#define EDSTART     0
#define EDINIT      1
#define EDCHAR      2
#define EDEND       3

/* Text justification */
#define TE_LEFT     0
#define TE_RIGHT    1
#define TE_CNTR     2

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
#define WM_ONTOP    31
#define WM_OFFTOP   32
#define PR_FINISH   33
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

/***************************************************************************
 * GEM Structures
 ***************************************************************************/

/* GRECT - Graphics Rectangle */
typedef struct grect {
    WORD g_x;
    WORD g_y;
    WORD g_w;
    WORD g_h;
} GRECT;

/* ORECT - Object Rectangle (linked list) */
typedef struct orect {
    struct orect *o_link;
    WORD o_x;
    WORD o_y;
    WORD o_w;
    WORD o_h;
} ORECT;

/*
 * Resource structures are also the exact little-endian records stored in a
 * classic PC GEM .RSC file.  Pointer-bearing fields occupy four bytes in that
 * file even though an ELKS small-model near pointer occupies only one 16-bit
 * word.  Keep those fields as explicit low/high words.  The resource loader
 * relocates the low word in place and writes zero to the high word; callers
 * must cross the field through the small-model helpers in AES.
 *
 * Packing is an ABI statement, not a request for unaligned accesses.  Every
 * member starts at an even byte offset and the resource arenas have word-
 * aligned bases, so the 8088/8086 still performs ordinary aligned byte/word
 * loads.  The size checks below make an accidental host-layout change a
 * compile-time error.
 */

/* OBJECT - GEM Object, 24 bytes on disk and in memory. */
typedef struct __attribute__((packed)) object {
    WORD    ob_next;
    WORD    ob_head;
    WORD    ob_tail;
    UWORD   ob_type;
    UWORD   ob_flags;
    UWORD   ob_state;
    GEM_U32_WORDS ob_spec;
    UWORD   ob_x;
    UWORD   ob_y;
    UWORD   ob_width;
    UWORD   ob_height;
} OBJECT;

/* TEDINFO - Text Edit Info, 28 bytes on disk and in memory. */
typedef struct __attribute__((packed)) text_edinfo {
    GEM_U32_WORDS te_ptext;
    GEM_U32_WORDS te_ptmplt;
    GEM_U32_WORDS te_pvalid;
    WORD    te_font;
    WORD    te_junk1;
    WORD    te_just;
    WORD    te_color;
    WORD    te_junk2;
    WORD    te_thickness;
    WORD    te_txtlen;
    WORD    te_tmplen;
} TEDINFO;

/* ICONBLK - Icon Block, 34 bytes on disk and in memory. */
typedef struct __attribute__((packed)) icon_block {
    GEM_U32_WORDS ib_pmask;
    GEM_U32_WORDS ib_pdata;
    GEM_U32_WORDS ib_ptext;
    WORD    ib_char;
    WORD    ib_xchar;
    WORD    ib_ychar;
    WORD    ib_xicon;
    WORD    ib_yicon;
    WORD    ib_wicon;
    WORD    ib_hicon;
    WORD    ib_xtext;
    WORD    ib_ytext;
    WORD    ib_wtext;
    WORD    ib_htext;
} ICONBLK;

/* BITBLK - Bit Block for monochrome images, 14 bytes on disk and in memory. */
typedef struct __attribute__((packed)) bit_block {
    GEM_U32_WORDS bi_pdata;
    WORD    bi_wb;
    WORD    bi_hl;
    WORD    bi_x;
    WORD    bi_y;
    WORD    bi_color;
} BITBLK;

/* USERBLK - User-defined object block */
typedef struct user_blk {
    GEM_U32_WORDS ub_code;
    GEM_U32_WORDS ub_parm;
} USERBLK;

/* APPLBLK - Application block */
typedef struct appl_blk {
    LPVOID  ab_code;
    LPVOID  ab_parm;
} APPLBLK;

/*
 * PARMBLK - Parameter block for user-defined objects.
 *
 * The resource tree address is the historical four-byte far pointer in the
 * original-client profile.  pb_parm deliberately remains a two-byte near
 * primary-data pointer: resident AES never executes a client callback, and
 * the Settings client reconstructs this block locally before calling its own
 * function.  Thus the adapted ELKS far-data record is exactly 28 bytes, not
 * the 30-byte record produced by a historical four-byte LONG pb_parm.
 */
typedef struct parm_blk {
    OBJECT FAR *pb_tree;
    WORD    pb_obj;
    WORD    pb_prevstate;
    WORD    pb_currstate;
    WORD    pb_x, pb_y, pb_w, pb_h;
    WORD    pb_xc, pb_yc, pb_wc, pb_hc;
    LPVOID  pb_parm;
} PARMBLK;

/*
 * MFDB - Memory Form Definition Block.
 *
 * Digital Research FDB.fd_addr is a classic four-byte far address even when
 * the C caller keeps the MFDB record itself in near data.  Retain the two
 * original 16-bit words explicitly: `lo` is the byte offset and `hi` is the
 * segment.  0:0 selects the physical screen.  No pointer normalization,
 * rounding, or arithmetic occurs at this interface, and the packed record is
 * the historical 20 bytes on an ia16 target.
 */
typedef struct __attribute__((packed)) memform {
    GEM_U32_WORDS mp;    /* Original offset:segment memory address */
    WORD    fwp;        /* Form width in pixels */
    WORD    fh;         /* Form height in pixels */
    WORD    fww;        /* Form width in words */
    WORD    ff;         /* Form format (0=device, 1=standard) */
    WORD    np;         /* Number of planes */
    WORD    r1;         /* Reserved */
    WORD    r2;         /* Reserved */
    WORD    r3;         /* Reserved */
} MFDB;

/* FILLPAT - Fill pattern array */
typedef struct patarray {
    WORD    patword[16];
} FILLPAT;

/* CICON - Color icon data */
typedef struct cicon {
    WORD    num_planes;
    WORD    *col_data;
    WORD    *col_mask;
    WORD    *sel_data;
    WORD    *sel_mask;
    struct cicon *next_res;
} CICON;

/* CICONBLK - Color Icon Block */
typedef struct ciconblk {
    ICONBLK monoblk;
    CICON   *mainlist;
} CICONBLK;

/* RSHDR - Resource file header, 36 bytes on disk and in memory. */
typedef struct __attribute__((packed)) rshdr {
    UWORD   rsh_vrsn;       /* Version */
    UWORD   rsh_object;     /* Offset to OBJECT array */
    UWORD   rsh_tedinfo;    /* Offset to TEDINFO array */
    UWORD   rsh_iconblk;    /* Offset to ICONBLK array */
    UWORD   rsh_bitblk;     /* Offset to BITBLK array */
    UWORD   rsh_frstr;      /* Offset to free strings */
    UWORD   rsh_string;     /* Offset to string data */
    UWORD   rsh_imdata;     /* Offset to image data */
    UWORD   rsh_frimg;      /* Offset to free images */
    UWORD   rsh_trindex;    /* Offset to tree index */
    UWORD   rsh_nobs;       /* Number of objects */
    UWORD   rsh_ntree;      /* Number of trees */
    UWORD   rsh_nted;       /* Number of TEDINFOs */
    UWORD   rsh_nib;        /* Number of ICONBLKs */
    UWORD   rsh_nbb;        /* Number of BITBLKs */
    UWORD   rsh_nstring;    /* Number of strings */
    UWORD   rsh_nimages;    /* Number of images */
    UWORD   rsh_rssize;     /* Total resource size */
} RSHDR;

/*
 * C89-compatible static assertions for the PC GEM resource ABI.  A negative
 * array bound is deliberate: it stops the build before a differently aligned
 * structure could make the zero-copy loader walk the wrong record size.
 */
typedef char GEM_OBJECT_SIZE_IS_24[(sizeof(OBJECT) == 24) ? 1 : -1];
typedef char GEM_TEDINFO_SIZE_IS_28[(sizeof(TEDINFO) == 28) ? 1 : -1];
typedef char GEM_ICONBLK_SIZE_IS_34[(sizeof(ICONBLK) == 34) ? 1 : -1];
typedef char GEM_BITBLK_SIZE_IS_14[(sizeof(BITBLK) == 14) ? 1 : -1];
typedef char GEM_RSHDR_SIZE_IS_36[(sizeof(RSHDR) == 36) ? 1 : -1];
#if defined(__IA16__) || (defined(ELKS) && ELKS)
typedef char GEM_MFDB_SIZE_IS_20[(sizeof(MFDB) == 20) ? 1 : -1];
#if GEM_TRAP_FAR_DATA
typedef char GEM_PARMBLK_SIZE_IS_28[(sizeof(PARMBLK) == 28) ? 1 : -1];
#else
typedef char GEM_PARMBLK_SIZE_IS_26[(sizeof(PARMBLK) == 26) ? 1 : -1];
#endif
#endif

/* MEVENT - Mouse event structure */
typedef struct mevent {
    UWORD   e_flags;
    UWORD   e_bclk;
    UWORD   e_bmsk;
    UWORD   e_bst;
    UWORD   e_m1flags;
    GRECT   e_m1;
    UWORD   e_m2flags;
    GRECT   e_m2;
    WORD    *e_mepbuf;
    GEM_U32_WORDS e_time;
    WORD    e_mx;
    WORD    e_my;
    UWORD   e_mb;
    UWORD   e_ks;
    UWORD   e_kr;
    UWORD   e_br;
    UWORD   e_m3flags;
    GRECT   e_m3;
    WORD    e_xtra0;
    WORD    *e_smepbuf;
    GEM_U32_WORDS e_xtra1;
    GEM_U32_WORDS e_xtra2;
} MEVENT;

/* Pointer typedefs for structures */
typedef OBJECT FAR *LPOBJ;
typedef OBJECT FAR *LPTREE;
typedef ORECT *LPORECT;
typedef GRECT *LPGRECT;
typedef TEDINFO FAR *LPTEDI;
typedef ICONBLK FAR *LPICON;
typedef BITBLK FAR *LPBIT;
typedef USERBLK *LPUSER;
typedef APPLBLK *LPAPPL;
typedef PARMBLK *LPPARM;
typedef MFDB *LPMFDB;
typedef FILLPAT *LPFILL;
typedef RSHDR FAR *LPRSHDR;
typedef MEVENT *LPMEV;
typedef LPBIT *LPLBIT;
typedef LPTREE *LPLTREE;

/*
 * Typed overlay for one classic four-byte data address.  GNU ia16 keeps far
 * pointer address spaces type-specific, so callers select the matching union
 * member instead of casting a generic far pointer.  Reading or writing the
 * `words` member performs an exact two-word copy and no pointer arithmetic.
 */
typedef union gem_typed_slot_pointer {
    GEM_U32_WORDS words;
    GEM_SLOT_POINTER pointer;
    GEM_SLOT_BYTE_POINTER bytes;
    GEM_SLOT_WORD_POINTER word_pointer;
    LPOBJ object;
    LPTREE tree;
    LPTEDI tedinfo;
    LPICON icon;
    LPBIT bitblk;
    LPRSHDR rshdr;
} GEM_TYPED_SLOT_POINTER;

/* Compatibility macro */
#define NOTREE ((LPTREE)-1)

#ifndef G_SIZE
#define G_SIZE 64
#endif

/* Prototype macro (for function declarations) */
#define _(x) x

/* Legacy helper prototypes implemented in DESKPPD.C */
WORD LSTCPY(GEM_SLOT_BYTE_POINTER d, GEM_SLOT_BYTE_POINTER s);

#ifndef STNOFRMT
#define STNOFRMT 0
#endif

/* User-defined object draw function type */
typedef WORD (*USERFUNC)(LPPARM pb);

/* Legacy PPD hook structures used by desktop */
typedef struct {
    USERFUNC ub_code;
    GEM_U32_WORDS ub_parm;
} PPDUBLK;

typedef struct x_buf_v2 {
    WORD    buf_len;
    WORD    arch;
    LPVOID  cc;
    /* AES-owned system-window data remains in the client's primary DS. */
    OBJECT  *w_active;
    LPBYTE  info;
    GEM_U32_WORDS abilities;
} X_BUF_V2;

#define ABLE_GETINFO  1
#define ABLE_PROP     2
#define ABLE_WTREE    4
#define ABLE_X3D      8
#define ABLE_XSHL     16
#define ABLE_PROP2    32
#define ABLE_EMSDESK  64
#define ABLE_XBVSET   128
typedef X_BUF_V2 *LPXBUF;

#ifndef G_SIZE
#define G_SIZE 64
#endif

extern WORD global[G_SIZE];
extern WORD DOS_ERR;

/***************************************************************************
 * Utility Functions
 ***************************************************************************/

/* Integer multiply/divide helpers.
 * No fixed-point binary scale is implied.  The implementation forms the
 * product as explicit 16-bit high/low halves, divides one bit at a time, and
 * truncates toward zero.  A zero divisor or out-of-range quotient saturates
 * to the signed or unsigned 16-bit result boundary.
 */
WORD  MUL_DIV(WORD m1, UWORD m2, WORD d1);
UWORD UMUL_DIV(UWORD m1, UWORD m2, UWORD d1);
WORD  x_mul_div(WORD m1, WORD m2, WORD d1);

/* Rectangle functions */
WORD  rc_equal(GRECT *p1, GRECT *p2);
VOID  rc_copy(GRECT *psbox, GRECT *pdbox);
WORD  rc_intersect(GRECT *p1, GRECT *p2);
WORD  rc_inside(WORD x, WORD y, GRECT *pt);
VOID  rc_grect_to_array(GRECT *area, WORD *array);

/***************************************************************************
 * AES Application Functions
 ***************************************************************************/

WORD appl_init(LPXBUF xbuf);
WORD appl_exit(void);
WORD appl_write(WORD rwid, WORD length, LPVOID pbuff);
WORD appl_read(WORD rwid, WORD length, LPVOID pbuff);
WORD appl_find(LPVOID pname);
WORD appl_tplay(LPVOID tbuffer, WORD tlength, WORD tscale);
WORD appl_trecord(LPVOID tbuffer, WORD tlength);
WORD appl_bvset(UWORD bvdisk, UWORD bvhard);
WORD appl_xbvget(GEM_U32_WORDS *bvdisk, GEM_U32_WORDS *bvhard);
WORD appl_xbvset(GEM_U32_WORDS bvdisk, GEM_U32_WORDS bvhard);
VOID appl_yield(void);
WORD appl_getinfo(WORD type, WORD *out1, WORD *out2, WORD *out3, WORD *out4);
WORD appl_search(WORD mode, BYTE *fname, WORD *type, WORD *ap_id);
#define xapp_getinfo appl_getinfo

/***************************************************************************
 * AES Event Functions
 ***************************************************************************/

UWORD evnt_keybd(void);
WORD  evnt_button(WORD clicks, UWORD mask, UWORD state,
                  WORD *pmx, WORD *pmy, WORD *pmb, WORD *pks);
WORD  evnt_mouse(WORD flags, WORD x, WORD y, WORD width, WORD height,
                 WORD *pmx, WORD *pmy, WORD *pmb, WORD *pks);
WORD  evnt_mesag(LPVOID pbuff);
WORD  evnt_timer(UWORD locnt, UWORD hicnt);
WORD  evnt_multi(UWORD flags, UWORD bclk, UWORD bmsk, UWORD bst,
                 UWORD m1flags, UWORD m1x, UWORD m1y, UWORD m1w, UWORD m1h,
                 UWORD m2flags, UWORD m2x, UWORD m2y, UWORD m2w, UWORD m2h,
                 LPVOID mepbuff, UWORD tlc, UWORD thc,
                 UWORD *pmx, UWORD *pmy, UWORD *pmb, UWORD *pks,
                 UWORD *pkr, UWORD *pbr);
WORD  evnt_dclick(WORD rate, WORD setit);

/***************************************************************************
 * AES Menu Functions
 ***************************************************************************/

WORD menu_bar(LPTREE tree, WORD showit);
WORD menu_icheck(LPTREE tree, WORD itemnum, WORD checkit);
WORD menu_ienable(LPTREE tree, WORD itemnum, WORD enableit);
WORD menu_tnormal(LPTREE tree, WORD titlenum, WORD normalit);
WORD menu_text(LPTREE tree, WORD inum, LPBYTE ptext);
/* Set the eight-character process name used by original menu_fixup(). */
WORD gem_menu_set_app_name(WORD pid, LPBYTE pstr);
WORD menu_register(WORD pid, LPVOID pstr);
WORD menu_unregister(WORD mid);
WORD menu_click(WORD click, WORD setit);
WORD menu_popup(LPTREE tree, WORD x, WORD y, WORD *item);
WORD menu_attach(WORD flag, LPTREE tree, WORD item, LPVOID menu);
WORD menu_istart(WORD flag, LPTREE tree, WORD imenu, WORD item);
WORD menu_settings(WORD flag, LPVOID values);

/***************************************************************************
 * AES Object Functions
 ***************************************************************************/

WORD objc_add(LPTREE tree, WORD parent, WORD child);
WORD objc_delete(LPTREE tree, WORD delob);
WORD objc_draw(LPTREE tree, WORD drawob, WORD depth,
               WORD xc, WORD yc, WORD wc, WORD hc);
WORD objc_find(LPTREE tree, WORD startob, WORD depth, WORD mx, WORD my);
WORD objc_order(LPTREE tree, WORD mov_obj, WORD newpos);
WORD objc_offset(LPTREE tree, WORD obj, WORD *poffx, WORD *poffy);
WORD objc_edit(LPTREE tree, WORD obj, WORD inchar, WORD *idx, WORD kind);
WORD objc_change(LPTREE tree, WORD drawob, WORD depth,
                 WORD xc, WORD yc, WORD wc, WORD hc,
                 WORD newstate, WORD redraw);
WORD objc_sysvar(WORD mode, WORD which, WORD in1, WORD in2,
                 WORD *out1, WORD *out2);

/***************************************************************************
 * AES Form Functions
 ***************************************************************************/

WORD form_do(LPTREE form, WORD start);
WORD form_dial(WORD dtype, WORD ix, WORD iy, WORD iw, WORD ih,
               WORD x, WORD y, WORD w, WORD h);
WORD form_alert(WORD defbut, LPBYTE astring);
WORD form_error(WORD errnum);
WORD form_center(LPTREE tree, WORD *pcx, WORD *pcy, WORD *pcw, WORD *pch);
WORD form_keybd(LPTREE form, WORD obj, WORD nxt_obj, WORD thechar,
                WORD *pnxt_obj, WORD *pchar);
WORD form_button(LPTREE form, WORD obj, WORD clks, WORD *pnxt_obj);

/***************************************************************************
 * AES Graphics Functions
 ***************************************************************************/

WORD graf_rubbox(WORD xorigin, WORD yorigin, WORD wmin, WORD hmin,
                 WORD *pwend, WORD *phend);
WORD graf_dragbox(WORD w, WORD h, WORD sx, WORD sy,
                  WORD xc, WORD yc, WORD wc, WORD hc,
                  WORD *pdx, WORD *pdy);
WORD graf_mbox(WORD w, WORD h, WORD srcx, WORD srcy, WORD dstx, WORD dsty);
WORD graf_movebox(WORD w, WORD h, WORD srcx, WORD srcy, WORD dstx, WORD dsty);
WORD graf_watchbox(LPTREE tree, WORD obj, UWORD instate, UWORD outstate);
WORD graf_slidebox(LPTREE tree, WORD parent, WORD obj, WORD isvert);
WORD graf_handle(WORD *pwchar, WORD *phchar, WORD *pwbox, WORD *phbox);
WORD graf_mouse(WORD m_number, LPVOID m_addr);
VOID graf_mkstate(WORD *pmx, WORD *pmy, WORD *pmstate, WORD *pkstate);
WORD graf_growbox(WORD sx, WORD sy, WORD sw, WORD sh,
                  WORD dx, WORD dy, WORD dw, WORD dh);
WORD graf_shrinkbox(WORD sx, WORD sy, WORD sw, WORD sh,
                    WORD dx, WORD dy, WORD dw, WORD dh);

/***************************************************************************
 * AES Scrap Functions
 ***************************************************************************/

WORD scrp_read(LPVOID pscrap);
WORD scrp_write(LPVOID pscrap);
WORD scrp_clear(void);

/***************************************************************************
 * AES File Selector Functions
 ***************************************************************************/

WORD fsel_input(LPVOID pipath, LPVOID pisel, WORD *pbutton);
WORD fsel_exinput(LPVOID pipath, LPVOID pisel, WORD *pbutton, LPBYTE ptitle);

/***************************************************************************
 * AES Window Functions
 ***************************************************************************/

WORD wind_create(UWORD kind, WORD wx, WORD wy, WORD ww, WORD wh);
WORD wind_open(WORD handle, WORD wx, WORD wy, WORD ww, WORD wh);
WORD wind_close(WORD handle);
WORD wind_delete(WORD handle);
WORD wind_get(WORD w_handle, WORD w_field,
              WORD *pw1, WORD *pw2, WORD *pw3, WORD *pw4);
WORD wind_set(WORD w_handle, WORD w_field,
              WORD w2, WORD w3, WORD w4, WORD w5);
WORD wind_find(WORD mx, WORD my);
WORD wind_update(WORD beg_update);
WORD wind_calc(WORD wctype, UWORD kind,
               WORD x, WORD y, WORD w, WORD h,
               WORD *px, WORD *py, WORD *pw, WORD *ph);
WORD wind_new(void);

/***************************************************************************
 * AES Resource Functions
 ***************************************************************************/

/*
 * Shared raw-asset boundary used by the RSC and original desktop-icon
 * loaders.  All lengths are unscaled 16-bit byte counts.  No allocation,
 * conversion, wide arithmetic, or hidden buffering occurs in either helper.
 */
WORD gem_asset_find_path(const BYTE *name, BYTE *found, UWORD found_size);
WORD gem_asset_read_exact(WORD fd, UBYTE *buffer, UWORD length);
VOID gem_object_clear(OBJECT *object);

WORD rsrc_load(LPBYTE rsname);
WORD rsrc_free(void);
WORD rsrc_gaddr(WORD rstype, WORD rsid, LPVOID *paddr);
WORD rsrc_saddr(WORD rstype, WORD rsid, LPVOID lngval);
WORD rsrc_obfix(LPTREE tree, WORD obj);

/***************************************************************************
 * AES Shell Functions
 ***************************************************************************/

WORD shel_read(LPVOID pcmd, LPVOID ptail);
WORD shel_write(WORD doex, WORD isgr, WORD iscr, LPVOID pcmd, LPVOID ptail);
WORD shel_find(LPVOID ppath);
WORD shel_envrn(LPVOID ppath, LPVOID psrch);
WORD shel_get(LPBYTE pbuffer, WORD len);
WORD shel_put(LPBYTE pbuffer, WORD len);
WORD shel_rdef(LPVOID lpcmd, LPVOID lpdir);
WORD shel_wdef(LPVOID lpcmd, LPVOID lpdir);

/***************************************************************************
 * GEM stdout diagnostics
 ***************************************************************************/

VOID gem_stdout_msg(const char *msg);
VOID gem_stdout_kv(const char *tag, const char *value);
VOID gem_stdout_word(const char *tag, WORD value);

/***************************************************************************
 * VDI Functions
 ***************************************************************************/

/* Workstation functions */
WORD v_opnwk(WORD work_in[], WORD *handle, WORD work_out[]);
WORD v_clswk(WORD handle);
WORD v_clrwk(WORD handle);
WORD v_updwk(WORD handle);
WORD v_opnvwk(WORD work_in[], WORD *handle, WORD work_out[]);
WORD v_clsvwk(WORD handle);
WORD vq_extnd(WORD handle, WORD owflag, WORD work_out[]);

/* Output primitives */
WORD v_pline(WORD handle, WORD count, WORD xy[]);
WORD v_pmarker(WORD handle, WORD count, WORD xy[]);
WORD v_gtext(WORD handle, WORD x, WORD y, BYTE *string);
WORD v_fillarea(WORD handle, WORD count, WORD xy[]);
WORD v_cellarray(WORD handle, WORD xy[], WORD row_length,
                 WORD el_per_row, WORD num_rows, WORD wr_mode, WORD *colors);
WORD vr_recfl(WORD handle, WORD *xy);

/* GDP (Graphics Device Primitives) */
WORD v_bar(WORD handle, WORD xy[]);
WORD v_arc(WORD handle, WORD xc, WORD yc, WORD rad, WORD sang, WORD eang);
WORD v_pieslice(WORD handle, WORD xc, WORD yc, WORD rad, WORD sang, WORD eang);
WORD v_circle(WORD handle, WORD xc, WORD yc, WORD rad);
WORD v_ellipse(WORD handle, WORD xc, WORD yc, WORD xrad, WORD yrad);
WORD v_ellarc(WORD handle, WORD xc, WORD yc, WORD xrad, WORD yrad,
              WORD sang, WORD eang);
WORD v_ellpie(WORD handle, WORD xc, WORD yc, WORD xrad, WORD yrad,
              WORD sang, WORD eang);
WORD v_rbox(WORD handle, WORD xy[]);
WORD v_rfbox(WORD handle, WORD xy[]);
WORD v_justified(WORD handle, WORD x, WORD y, BYTE string[],
                 WORD length, WORD word_space, WORD char_space);
WORD vqt_justified(WORD handle, WORD x, WORD y, BYTE string[],
                   WORD length, WORD word_space, WORD char_space,
                   WORD offsets[]);

/* Attribute functions */
WORD vsl_type(WORD handle, WORD style);
WORD vsl_width(WORD handle, WORD width);
WORD vsl_color(WORD handle, WORD index);
WORD vsl_udsty(WORD handle, WORD pattern);
WORD vsl_ends(WORD handle, WORD beg_style, WORD end_style);
WORD vsm_type(WORD handle, WORD symbol);
WORD vsm_height(WORD handle, WORD height);
WORD vsm_color(WORD handle, WORD index);
WORD vst_height(WORD handle, WORD height,
                WORD *char_width, WORD *char_height,
                WORD *cell_width, WORD *cell_height);
WORD vst_rotation(WORD handle, WORD angle);
WORD vst_font(WORD handle, WORD font);
WORD vst_color(WORD handle, WORD index);
WORD vst_alignment(WORD handle, WORD hor_in, WORD vert_in,
                   WORD *hor_out, WORD *vert_out);
WORD vst_effects(WORD handle, WORD effect);
WORD vst_point(WORD handle, WORD point,
               WORD *char_width, WORD *char_height,
               WORD *cell_width, WORD *cell_height);
WORD vsf_interior(WORD handle, WORD style);
WORD vsf_style(WORD handle, WORD index);
WORD vsf_color(WORD handle, WORD index);
WORD vsf_perimeter(WORD handle, WORD per_vis);
WORD vsf_udpat(WORD handle, WORD fill_pat[], WORD planes);
WORD vswr_mode(WORD handle, WORD mode);
WORD vs_color(WORD handle, WORD index, WORD *rgb);

/* Raster operations */
WORD vro_cpyfm(WORD handle, WORD wr_mode, WORD xy[], LPMFDB srcMFDB, LPMFDB desMFDB);
WORD vrt_cpyfm(WORD handle, WORD wr_mode, WORD xy[],
               LPMFDB srcMFDB, LPMFDB desMFDB, WORD *index);
WORD vr_trnfm(WORD handle, LPMFDB srcMFDB, LPMFDB desMFDB);

/* Input functions */
WORD v_show_c(WORD handle, WORD reset);
WORD v_hide_c(WORD handle);
WORD vq_mouse(WORD handle, WORD *status, WORD *px, WORD *py);
WORD vq_key_s(WORD handle, WORD *status);
WORD vex_butv(WORD handle, LPVOID usercode, LPVOID *savecode);
WORD vex_motv(WORD handle, LPVOID usercode, LPVOID *savecode);
WORD vex_curv(WORD handle, LPVOID usercode, LPVOID *savecode);
WORD vex_timv(WORD handle, LPVOID tim_addr, LPVOID *old_addr, WORD *scale);
WORD vsc_form(WORD handle, WORD *cur_form);

/* Inquiry functions */
WORD vq_color(WORD handle, WORD index, WORD set_flag, WORD rgb[]);
WORD vq_cellarray(WORD handle, WORD xy[], WORD row_len, WORD num_rows,
                  WORD *el_used, WORD *rows_used, WORD *stat, WORD colors[]);
WORD vq_chcells(WORD handle, WORD *rows, WORD *columns);
WORD vqin_mode(WORD handle, WORD dev_type, WORD *mode);
WORD vqt_width(WORD handle, BYTE character,
               WORD *cell_width, WORD *left_delta, WORD *right_delta);
WORD vqt_extent(WORD handle, BYTE string[], WORD extent[]);
WORD vqt_attributes(WORD handle, WORD attributes[]);
WORD vqt_fontinfo(WORD handle, WORD *minade, WORD *maxade, WORD distances[], WORD *maxwidth, WORD effects[]);
WORD vqt_name(WORD handle, WORD element_num, BYTE name[]);
WORD vqt_font_info(WORD handle, WORD *minADE, WORD *maxADE,
                   WORD distances[], WORD *maxwidth, WORD effects[]);
WORD vql_attributes(WORD handle, WORD attributes[]);
WORD vqm_attributes(WORD handle, WORD attributes[]);
WORD vqf_attributes(WORD handle, WORD attributes[]);
WORD v_get_pixel(WORD handle, WORD x, WORD y, WORD *pel, WORD *index);

/* Control functions */
WORD vs_clip(WORD handle, WORD clip_flag, WORD xy[]);
WORD vsin_mode(WORD handle, WORD dev_type, WORD mode);

/* Escape functions */
WORD v_sound(WORD handle, WORD frequency, WORD duration);
WORD vs_mute(WORD handle, WORD action);

/* Font functions */
WORD vst_load_fonts(WORD handle, WORD select);
WORD vst_unload_fonts(WORD handle, WORD select);

VOID v_copies(WORD handle, WORD count);
VOID v_etext(WORD handle, WORD x, WORD y, UBYTE string[], WORD offsets[]);
VOID v_orient(WORD handle, WORD orientation);
VOID v_tray(WORD handle, WORD tray);
WORD v_xbit_image(WORD handle, BYTE *filename, WORD aspect,
                  WORD x_scale, WORD y_scale, WORD h_align, WORD v_align,
                  WORD rotate, WORD background, WORD foreground, WORD xy[]);
WORD vst_ex_load_fonts(WORD handle, WORD select, WORD font_max,
                       WORD font_free);
VOID v_ps_halftone(WORD handle, WORD index, WORD angle, WORD frequency);
VOID v_setrgbi(WORD handle, WORD primtype, WORD r, WORD g, WORD b, WORD i);
VOID v_topbot(WORD handle, WORD height, WORD *char_width, WORD *char_height,
              WORD *cell_width, WORD *cell_height);
VOID vs_bkcolor(WORD handle, WORD color);
VOID v_set_app_buff(LPVOID address, WORD nparagraphs);
WORD v_bez_on(WORD handle);
WORD v_bez_off(WORD handle);
WORD v_bez_qual(WORD handle, WORD prcnt);
VOID v_pat_rotate(WORD handle, WORD angle);
VOID vs_grayoverride(WORD handle, WORD grayval);
VOID v_bez(WORD handle, WORD count, LPWORD xyarr, UBYTE *bezarr,
           LPWORD minmax, LPWORD npts, LPWORD nmove);
VOID v_bezfill(WORD handle, WORD count, LPWORD xyarr, UBYTE *bezarr,
               LPWORD minmax, LPWORD npts, LPWORD nmove);

WORD xgrf_stepcalc(WORD orgw, WORD orgh, WORD xc, WORD yc,
                   WORD w, WORD h, WORD *pcx, WORD *pcy,
                   WORD *pcnt, WORD *pxstep, WORD *pystep);
WORD xgrf_2box(WORD xc, WORD yc, WORD w, WORD h,
               WORD corners, WORD cnt, WORD xstep, WORD ystep,
               WORD doubled);
WORD xgrf_colour(WORD type, WORD fg, WORD bg, WORD style, WORD index);
#define xgrf_color xgrf_colour
WORD xgrf_dtimage(LPMFDB deskMFDB);

WORD prop_get(LPBYTE program, LPBYTE section, LPBYTE buf, WORD buflen,
              WORD options);
WORD prop_put(LPBYTE program, LPBYTE section, LPBYTE buf, WORD options);
WORD prop_del(LPBYTE program, LPBYTE section, WORD options);
WORD prop_gui_get(WORD propnum);
WORD prop_gui_set(WORD propnum, WORD value);
WORD xshl_getshell(LPBYTE program);
WORD xshl_setshell(LPBYTE program);

/***************************************************************************
 * Icon Helpers
 ***************************************************************************/

VOID fix_icon(WORD vdi_handle, LPTREE tree);
VOID trans_gimage(WORD vdi_handle, LPTREE tree, WORD obj);

#endif /* ELKS_GEM_AES_H */
