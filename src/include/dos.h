/*
 * dos.h - GEMDOS source compatibility backed by POSIX in libgem.
 *
 * File sizes and offsets are unscaled byte counts.  Values which cross the
 * historical 32-bit GEM ABI are represented internally as two little-endian
 * 16-bit words.  Arithmetic on those values must use explicit carry or borrow;
 * the XT build must never make the compiler emit a 32-bit arithmetic helper.
 *
 * DOS date and time values use the original packed integer layouts:
 *   date: bits 15..9 year since 1980, 8..5 month, 4..0 day
 *   time: bits 15..11 hour, 10..5 minute, 4..0 second/2
 */

#ifndef ELKS_GEM_DOS_H
#define ELKS_GEM_DOS_H

#include "aes.h"

#define STDIN  0x0000
#define STDOUT 0x0001
#define STDERR 0x0002
#define STDAUX 0x0003
#define STDPRN 0x0004

#define E_BADFUNC    1
#define E_FILENOTFND 2
#define E_PATHNOTFND 3
#define E_NOHANDLES  4
#define E_NOACCESS   5
#define E_BADHANDLE  6
#define E_MEMBLKERR  7
#define E_NOMEMORY   8
#define E_BADMEMBLK  9
#define E_BADENVIR   10
#define E_BADFORMAT  11
#define E_BADACCESS  12
#define E_BADDATA    13
#define E_BADDRIVE   15
#define E_NODELDIR   16
#define E_NOTDEVICE  17
#define E_NOFILES    18

#define F_RDONLY  0x01
#define F_HIDDEN  0x02
#define F_SYSTEM  0x04
#define F_VOLUME  0x08
#define F_SUBDIR  0x10
#define F_ARCHIVE 0x20
#define F_FAKE    0x40
#define F_DESKTOP 0x80

#define F_GETMOD 0x0
#define F_SETMOD 0x1

/*
 * Preserve the original GEM DTA/FNODE contract exactly: twelve filename
 * bytes followed by one NUL byte.  ELKS can expose longer components, but
 * this Desktop cannot represent them losslessly in its original FNODE.
 * gemdos_posix therefore skips an overlength entry instead of truncating it
 * and later operating on the wrong file.  Supporting longer names requires
 * a coordinated change to both structures, not a larger private DTA alone.
 */
#define GEM_DTA_NAME_MAX 12

typedef struct __attribute__((packed)) gem_fcb {
    UBYTE fcb_reserved[21];
    UBYTE fcb_attr;
    UWORD fcb_time;
    UWORD fcb_date;
    /* Unscaled file size, low word first; overflow saturates both words. */
    GEM_U32_WORDS fcb_size;
    BYTE fcb_name[GEM_DTA_NAME_MAX + 1];
} FCB;
#define GEM_FCB_DEFINED 1

typedef struct exec_blk {
    WORD   eb_segenv;
    LPBYTE eb_pcmdln;
    LPVOID eb_pfcb1;
    LPVOID eb_pfcb2;
} EXEC_BLK;

typedef struct over_blk {
    WORD ob_seglod;
    WORD ob_relfac;
} OVER_BLK;

union REGS {
    struct {
        UWORD ax;
        UWORD bx;
        UWORD cx;
        UWORD dx;
        UWORD si;
        UWORD di;
        UWORD cflag;
    } x;
    struct {
        UBYTE al, ah;
        UBYTE bl, bh;
        UBYTE cl, ch;
        UBYTE dl, dh;
    } h;
};

extern union REGS DR;

WORD dos_chdir(LPBYTE pdrvpath);
WORD dos_gdir(WORD drive, LPBYTE pdrvpath);
WORD dos_gdrv(VOID);
WORD dos_sdrv(WORD newdrv);
WORD dos_sdta(LPVOID ldta);
WORD dos_vlabel(LPVOID fcb);
WORD dos_sfirst(LPBYTE pspec, WORD attr);
WORD dos_snext(VOID);
WORD dos_open(LPBYTE pname, WORD access);
WORD dos_close(WORD handle);

/*
 * These declarations retain each original four-byte GEM field while exposing
 * its two 16-bit halves directly.  Read and write counts require hi == 0
 * because an ELKS near
 * buffer cannot cross 64 KiB; a nonzero high word is rejected with E_BADDATA.
 * Individual syscalls are capped at 0x7fff bytes and combined without loss.
 *
 * lseek offsets and results are signed, unscaled byte positions.  The ELKS
 * four-byte syscall value is copied word-for-word.  A wider host-audit result
 * saturates to 0xffff:0xffff; syscall failure returns the same bits with
 * DOS_ERR set, preserving normal GEM failure testing.
 *
 * The allocator accepts only nonnegative hi == 0 byte counts, rounds a
 * successful request upward to two-byte alignment, and rejects overflow.
 * Storage comes from the ELKS process heap through libc malloc/free and the
 * kernel sbrk/brk boundary.  dos_avail reports the remaining portion of the
 * Desktop's bounded 12 KiB payload budget.
 */
GEM_U32_WORDS dos_read(WORD handle, GEM_U32_WORDS cnt, LPBYTE pbuffer);
#define dos_read_word(handle, cnt, buffer) \
	gem_u32_to_u16_sat(dos_read((handle), gem_u32_words((cnt), 0), \
				     (buffer)))
GEM_U32_WORDS dos_lseek(WORD handle, WORD smode, GEM_U32_WORDS sofst);
WORD dos_wait(VOID);
LPVOID dos_alloc(GEM_U32_WORDS nbytes);
#define dos_alloc_word(nbytes) dos_alloc(gem_u32_words((nbytes), 0))
GEM_U32_WORDS dos_avail(VOID);
#define dos_avail_word() gem_u32_to_u16_sat(dos_avail())
WORD dos_free(LPVOID maddr);
WORD dos_space(WORD drv, GEM_U32_WORDS *ptotal, GEM_U32_WORDS *pavail);
WORD dos_rmdir(LPBYTE ppath);
WORD dos_create(LPBYTE pname, WORD attr);
WORD dos_mkdir(LPBYTE ppath);
WORD dos_delete(LPBYTE pname);
WORD dos_rename(LPBYTE poname, LPBYTE pnname);
GEM_U32_WORDS dos_write(WORD handle, GEM_U32_WORDS cnt, LPBYTE pbuffer);
#define dos_write_word(handle, cnt, buffer) \
	gem_u32_to_u16_sat(dos_write((handle), gem_u32_words((cnt), 0), \
				      (buffer)))
WORD dos_chmod(LPBYTE pname, WORD func, WORD attr);
WORD dos_setdt(WORD handle, WORD time, WORD date);
WORD dos_dtype(WORD drive);
int int86(int vec, union REGS *inregs, union REGS *outregs);
int intdos(union REGS *inregs, union REGS *outregs);

#endif
