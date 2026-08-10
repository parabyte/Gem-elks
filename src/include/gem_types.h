/*
 * gem_types.h - the scalar types and four-byte ABI fields
 *
 * Split out of aes.h, which had grown to carry what DRI shipped
 * as five separate headers.  Include "aes.h" to get them all;
 * this header stands alone for code that wants only this part.
 */

#ifndef ELKS_GEM_GEM_TYPES_H
#define ELKS_GEM_GEM_TYPES_H

/***************************************************************************
 * Basic Types (from aes_types.h / vdi_types.h)
 ***************************************************************************/

#ifndef BYTE
#define BYTE    char		/* Signed byte */
#endif

#ifndef BOOLEAN
#define BOOLEAN int		/* 2 valued (true/false) */
#endif

#ifndef WORD
#define WORD    int		/* Signed word (16 bits) */
#endif

#ifndef UWORD
#define UWORD   unsigned int	/* Unsigned word */
#endif

/*
 * Four-byte GEM ABI fields, stored as two explicit little-endian
 * 16-bit words so ia16-gcc never emits a 32-bit helper.
 */
typedef struct __attribute__((packed)) gem_u32_words {
	UWORD lo;
	UWORD hi;
} GEM_U32_WORDS;

#ifndef UBYTE
#define UBYTE   unsigned char	/* Unsigned byte */
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
#define GLOBAL /**/
#endif
#ifndef VOID
#define VOID    void
#endif
/*
 * GEM_TRAP_FAR_DATA brings back far data pointers for the trap-linked
 * original Desktop, whose resource blocks live in resident far
 * segments.  Everything else stays near.
 */
#undef FAR
#undef NEAR
#undef far
#undef near
#if GEM_TRAP_FAR_DATA
#define FAR     __far
#define NEAR			/* primary-data pointer */
#define far     __far
#define near			/* primary-data pointer */
#else
#define FAR			/* empty */
#define NEAR			/* empty */
#define far			/* empty */
#define near			/* empty */
#endif
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

/* Packing clears hi; unpacking rejects a nonzero high half. */
GEM_U32_WORDS gem_near_pointer_words(const void FAR * pointer);
LPVOID gem_near_words_pointer(GEM_U32_WORDS field);

typedef struct gemblkstr {
	LPUWORD gb_pcontrol;
	LPUWORD gb_pglobal;
	LPUWORD gb_pintin;
	LPUWORD gb_pintout;
	LPLPTR gb_padrin;
	LPLPTR gb_padrout;
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


#endif				/* gem_types.h */
