/* GEM AES and VDI declarations */

#ifndef ELKS_GEM_AES_H
#define ELKS_GEM_AES_H

#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>

#include "gem_types.h"
#include "gsxdefs.h"
#include "obdefs.h"
#include "gemdefs.h"
#include "aesbind.h"
#include "vdibind.h"

/* Pacific C helper names */
#undef ADDR
#define ADDR(p)        ((void *)(p))
#define LBCOPY(d,s,n)  memcpy((d),(s),(n))
#define LWCOPY(d,s,n)  memcpy((d),(s), (n) * sizeof(WORD))
#define LWGET(p)       (*(WORD *)(p))
#define LBGET(p)       (*(BYTE *)(p))
#define LWSET(p,v)     (*(WORD *)(p) = (v))
#define LBSET(p,v)     (*(BYTE *)(p) = (v))

/*
 * With GEM_TRAP_FAR_DATA pointers carry a real offset:segment pair;
 * otherwise a near offset with a zero segment.
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

#endif				/* ELKS_GEM_AES_H */
