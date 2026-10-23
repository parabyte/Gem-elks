/* original Desktop file API over ELKS */

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
 * Original DTA/FNODE name field: twelve bytes plus one NUL.
 * gemdos_posix skips a longer ELKS name rather than truncating it and
 * later touching the wrong file.
 */
#define GEM_DTA_NAME_MAX 12

typedef struct __attribute__((packed)) gem_fcb {
	UBYTE fcb_reserved[21];
	UBYTE fcb_attr;
	UWORD fcb_time;
	UWORD fcb_date;
	/* Plain file size, low word first; overflow pins both words at max. */
	GEM_U32_WORDS fcb_size;
	BYTE fcb_name[GEM_DTA_NAME_MAX + 1];
} FCB;

#define GEM_FCB_DEFINED 1

extern UWORD DOS_AX;

WORD dos_chdir(LPBYTE pdrvpath);
WORD dos_gdir(WORD drive, LPBYTE pdrvpath);
WORD dos_gdrv(VOID);
WORD dos_sdrv(WORD newdrv);
WORD dos_sdta(LPVOID ldta);
WORD dos_vlabel(LPVOID fcb);
WORD dos_executable(LPBYTE path);
WORD dos_sfirst(LPBYTE pspec, WORD attr);
WORD dos_snext(VOID);
WORD dos_open(LPBYTE pname, WORD access);
WORD dos_close(WORD handle);

/*
 * Read and write counts need hi == 0 (a near buffer cannot cross
 * 64 KiB); a nonzero high word is refused with E_BADDATA.  A failed
 * lseek returns 0xffff:0xffff with DOS_ERR set.
 */
GEM_U32_WORDS dos_read(WORD handle, GEM_U32_WORDS cnt, LPBYTE pbuffer);
#define dos_read_word(handle, cnt, buffer) \
	gem_u32_to_u16_sat(dos_read((handle), gem_u32_words((cnt), 0), \
				     (buffer)))
GEM_U32_WORDS dos_lseek(WORD handle, WORD smode, GEM_U32_WORDS sofst);
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
#endif
