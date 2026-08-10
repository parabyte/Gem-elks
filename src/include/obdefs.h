/*
 * obdefs.h - the GEM object records and their constants
 *
 * Split out of aes.h, which had grown to carry what DRI shipped
 * as five separate headers.  Include "aes.h" to get them all;
 * this header stands alone for code that wants only this part.
 */

#ifndef ELKS_GEM_OBDEFS_H
#define ELKS_GEM_OBDEFS_H

#include "gem_types.h"
#include "gsxdefs.h"

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
 * These structures are the exact little-endian records in a classic
 * PC GEM .RSC file.  Four-byte pointer fields stay explicit low/high
 * words; the loader relocates the low word in place and zeroes the
 * high word.
 */

/* OBJECT - GEM Object, 24 bytes on disk and in memory. */
typedef struct __attribute__((packed)) object {
	WORD ob_next;
	WORD ob_head;
	WORD ob_tail;
	UWORD ob_type;
	UWORD ob_flags;
	UWORD ob_state;
	GEM_U32_WORDS ob_spec;
	UWORD ob_x;
	UWORD ob_y;
	UWORD ob_width;
	UWORD ob_height;
} OBJECT;

/* TEDINFO - Text Edit Info, 28 bytes on disk and in memory. */
typedef struct __attribute__((packed)) text_edinfo {
	GEM_U32_WORDS te_ptext;
	GEM_U32_WORDS te_ptmplt;
	GEM_U32_WORDS te_pvalid;
	WORD te_font;
	WORD te_junk1;
	WORD te_just;
	WORD te_color;
	WORD te_junk2;
	WORD te_thickness;
	WORD te_txtlen;
	WORD te_tmplen;
} TEDINFO;

/* ICONBLK - Icon Block, 34 bytes on disk and in memory. */
typedef struct __attribute__((packed)) icon_block {
	GEM_U32_WORDS ib_pmask;
	GEM_U32_WORDS ib_pdata;
	GEM_U32_WORDS ib_ptext;
	WORD ib_char;
	WORD ib_xchar;
	WORD ib_ychar;
	WORD ib_xicon;
	WORD ib_yicon;
	WORD ib_wicon;
	WORD ib_hicon;
	WORD ib_xtext;
	WORD ib_ytext;
	WORD ib_wtext;
	WORD ib_htext;
} ICONBLK;

/* BITBLK - Bit Block for monochrome images, 14 bytes on disk and in memory. */
typedef struct __attribute__((packed)) bit_block {
	GEM_U32_WORDS bi_pdata;
	WORD bi_wb;
	WORD bi_hl;
	WORD bi_x;
	WORD bi_y;
	WORD bi_color;
} BITBLK;

/* USERBLK - User-defined object block */
typedef struct user_blk {
	GEM_U32_WORDS ub_code;
	GEM_U32_WORDS ub_parm;
} USERBLK;

/* APPLBLK - Application block */
typedef struct appl_blk {
	LPVOID ab_code;
	LPVOID ab_parm;
} APPLBLK;

/*
 * PARMBLK - Parameter block for user-defined objects.
 *
 * pb_parm stays a two-byte near pointer, so the far-data record is
 * 28 bytes, not the classic 30.
 */
typedef struct parm_blk {
	OBJECT FAR *pb_tree;
	WORD pb_obj;
	WORD pb_prevstate;
	WORD pb_currstate;
	WORD pb_x, pb_y, pb_w, pb_h;
	WORD pb_xc, pb_yc, pb_wc, pb_hc;
	LPVOID pb_parm;
} PARMBLK;

/*
 * MFDB - Memory Form Definition Block.
 *
 * The address keeps the classic offset:segment words, lo the offset
 * and hi the segment; 0:0 means the physical screen.
 */
typedef struct __attribute__((packed)) memform {
	GEM_U32_WORDS mp;	/* Original offset:segment memory address */
	WORD fwp;		/* Form width in pixels */
	WORD fh;		/* Form height in pixels */
	WORD fww;		/* Form width in words */
	WORD ff;		/* Form format (0=device, 1=standard) */
	WORD np;		/* Number of planes */
	WORD r1;		/* Reserved */
	WORD r2;		/* Reserved */
	WORD r3;		/* Reserved */
} MFDB;

/* DRI PC GEM sources call the same record an FDB. */
typedef MFDB FDB;

/* FILLPAT - Fill pattern array */
typedef struct patarray {
	WORD patword[16];
} FILLPAT;

/* CICON - Color icon data */
typedef struct cicon {
	WORD num_planes;
	WORD *col_data;
	WORD *col_mask;
	WORD *sel_data;
	WORD *sel_mask;
	struct cicon *next_res;
} CICON;

/* CICONBLK - Color Icon Block */
typedef struct ciconblk {
	ICONBLK monoblk;
	CICON *mainlist;
} CICONBLK;

/* RSHDR - Resource file header, 36 bytes on disk and in memory. */
typedef struct __attribute__((packed)) rshdr {
	UWORD rsh_vrsn;		/* Version */
	UWORD rsh_object;	/* Offset to OBJECT array */
	UWORD rsh_tedinfo;	/* Offset to TEDINFO array */
	UWORD rsh_iconblk;	/* Offset to ICONBLK array */
	UWORD rsh_bitblk;	/* Offset to BITBLK array */
	UWORD rsh_frstr;	/* Offset to free strings */
	UWORD rsh_string;	/* Offset to string data */
	UWORD rsh_imdata;	/* Offset to image data */
	UWORD rsh_frimg;	/* Offset to free images */
	UWORD rsh_trindex;	/* Offset to tree index */
	UWORD rsh_nobs;		/* Number of objects */
	UWORD rsh_ntree;	/* Number of trees */
	UWORD rsh_nted;		/* Number of TEDINFOs */
	UWORD rsh_nib;		/* Number of ICONBLKs */
	UWORD rsh_nbb;		/* Number of BITBLKs */
	UWORD rsh_nstring;	/* Number of strings */
	UWORD rsh_nimages;	/* Number of images */
	UWORD rsh_rssize;	/* Total resource size */
} RSHDR;

/* C89 static assertions: a layout drift becomes a negative array bound. */
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
	UWORD e_flags;
	UWORD e_bclk;
	UWORD e_bmsk;
	UWORD e_bst;
	UWORD e_m1flags;
	GRECT e_m1;
	UWORD e_m2flags;
	GRECT e_m2;
	WORD *e_mepbuf;
	GEM_U32_WORDS e_time;
	WORD e_mx;
	WORD e_my;
	UWORD e_mb;
	UWORD e_ks;
	UWORD e_kr;
	UWORD e_br;
	UWORD e_m3flags;
	GRECT e_m3;
	WORD e_xtra0;
	WORD *e_smepbuf;
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
 * Typed overlay for one four-byte data address.  GNU ia16 far pointer
 * address spaces are type-specific, so callers pick the matching
 * member instead of casting.
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
	WORD buf_len;
	WORD arch;
	LPVOID cc;
	/* AES-owned system-window data stays in the client's primary DS. */
	OBJECT *w_active;
	LPBYTE info;
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


#endif				/* obdefs.h */
