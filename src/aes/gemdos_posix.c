/*
 * gemdos_posix.c - original GEMDOS.C clients over a compact ELKS seam.
 *
 * The public dos_* routines at the bottom retain the original register setup
 * and __DOS() control flow.  This upper half is the one target-specific seam:
 * it translates that register image directly to ELKS system calls.  There is
 * deliberately no second set of dos_native_* wrappers.  Besides reducing
 * code size, that keeps each filesystem request to one dispatcher call and
 * one kernel boundary on an 8088.
 */

#include "dos.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#if !defined(ELKS) || !ELKS
#include <sys/statvfs.h>
#include <utime.h>
#endif

#if defined(ELKS) && ELKS
#undef PATH_MAX
#define PATH_MAX 128
#else
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#endif

/* Match the original Desktop's MAX_LEVEL recursive directory depth. */
#define GEM_MAX_SEARCHES 8
#define GEM_PATTERN_MAX 67
#define GEM_GDIR_MAX 67
#define GEM_MOUNT_NAME_MAX 32
#define GEM_VOLUME_LABEL_MAX 11
#define DOS_HEAP_BYTES 0x3000U
#define DOS_HEAP_SLOTS 8

typedef struct gem_search {
	WORD used;
	LPVOID dta;
	DIR *dir;
	char dirpath[PATH_MAX];
	char pattern[GEM_PATTERN_MAX];
	WORD attr;
} GEM_SEARCH;

/*
 * This is the only representation copied out of native stat.  File size is
 * an unscaled byte count in low/high words.  The time and date are the packed
 * DOS fields documented in dos.h.
 */
typedef struct dos_stat_view {
	UWORD mode;
	GEM_U32_WORDS size;
	UWORD time;
	UWORD date;
} DOS_STAT_VIEW;

typedef struct dos_heap_entry {
	LPVOID address;
	UWORD size;
} DOS_HEAP_ENTRY;

/*
 * Filesystem quantities cross the ELKS ustatfs ABI as four-byte fields.
 * The Desktop never performs native wide arithmetic on them: each value is
 * copied into the same explicit low/high word pair used by GEM resources.
 * ELKS reports f_blocks and f_bavail in unscaled 1 KiB blocks.
 */
typedef struct dos_filesystem_view {
	GEM_U32_WORDS total_blocks;
	GEM_U32_WORDS available_blocks;
	BYTE mount_name[GEM_MOUNT_NAME_MAX];
} DOS_FILESYSTEM_VIEW;

#if defined(ELKS) && ELKS
/*
 * Exact ELKS <arch/statfs.h> wire layout.  Keeping the six four-byte fields
 * as word pairs prevents ia16-gcc from pulling in long multiply or divide
 * helpers.  dev_t is one 16-bit word on ELKS/8086.
 */
typedef struct dos_elks_statfs {
	WORD type;
	UWORD flags;
	dev_t device;
	GEM_U32_WORDS block_size;
	GEM_U32_WORDS total_blocks;
	GEM_U32_WORDS free_blocks;
	GEM_U32_WORDS available_blocks;
	GEM_U32_WORDS total_files;
	GEM_U32_WORDS free_files;
	BYTE mount_name[GEM_MOUNT_NAME_MAX];
} DOS_ELKS_STATFS;

typedef char DOS_ELKS_STATFS_LAYOUT_IS_62_BYTES[
	(sizeof(DOS_ELKS_STATFS) == 62U) ? 1 : -1];

extern int ustatfs(dev_t device, DOS_ELKS_STATFS *information, int flags);

/* Exact ELKS utime wire image: access time then modification time. */
typedef struct dos_elks_utimbuf {
	GEM_U32_WORDS access_time;
	GEM_U32_WORDS modification_time;
} DOS_ELKS_UTIMBUF;

extern int utime(const char *path, DOS_ELKS_UTIMBUF *times);
#endif

/*
 * All table entries are already scaled.  This avoids variable-count shifts,
 * multiplication, division, and their compiler helpers on an 8088.
 */
static const UBYTE dos_hour_high[24] = {
	0x00, 0x08, 0x10, 0x18, 0x20, 0x28, 0x30, 0x38,
	0x40, 0x48, 0x50, 0x58, 0x60, 0x68, 0x70, 0x78,
	0x80, 0x88, 0x90, 0x98, 0xa0, 0xa8, 0xb0, 0xb8
};

static const UBYTE dos_minute_low[8] = {
	0x00, 0x20, 0x40, 0x60, 0x80, 0xa0, 0xc0, 0xe0
};

static const UBYTE dos_minute_high[60] = {
	0, 0, 0, 0, 0, 0, 0, 0,
	1, 1, 1, 1, 1, 1, 1, 1,
	2, 2, 2, 2, 2, 2, 2, 2,
	3, 3, 3, 3, 3, 3, 3, 3,
	4, 4, 4, 4, 4, 4, 4, 4,
	5, 5, 5, 5, 5, 5, 5, 5,
	6, 6, 6, 6, 6, 6, 6, 6,
	7, 7, 7, 7
};

static const UBYTE dos_month_low[13] = {
	0x00, 0x20, 0x40, 0x60, 0x80, 0xa0, 0xc0,
	0xe0, 0x00, 0x20, 0x40, 0x60, 0x80
};

static const UBYTE dos_month_high[13] = {
	0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1
};

/* Exact second spans for months 1 through 12; entry zero is unused. */
static const GEM_U32_WORDS dos_month_seconds[13] = {
	{ 0x0000U, 0x0000U },
	{ 0xde80U, 0x0028U }, { 0xea00U, 0x0024U },
	{ 0xde80U, 0x0028U }, { 0x8d00U, 0x0027U },
	{ 0xde80U, 0x0028U }, { 0x8d00U, 0x0027U },
	{ 0xde80U, 0x0028U }, { 0xde80U, 0x0028U },
	{ 0x8d00U, 0x0027U }, { 0xde80U, 0x0028U },
	{ 0x8d00U, 0x0027U }, { 0xde80U, 0x0028U }
};

static const UBYTE dos_month_days[13] = {
	0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

static const GEM_U32_WORDS dos_zero_words = { 0, 0 };

union REGS DR;
WORD DOS_ERR;

/*
 * Register names deliberately match GEMDOS.C.  Pointer values remain near:
 * one ELKS process cannot dereference another process data segment, and this
 * executable uses SMALL_DATA.  The segment words are retained only to keep
 * the original public ABI image exact.
 */
#define DOS_AX DR.x.ax
#define DOS_BX DR.x.bx
#define DOS_CX DR.x.cx
#define DOS_DX DR.x.dx
#define DOS_SI DR.x.si
#define DOS_DI DR.x.di
#define DOS_DS dos_ds
#define DOS_ES dos_es

static LPVOID current_dta;
static FCB fallback_dta;
static GEM_SEARCH searches[GEM_MAX_SEARCHES];
static DOS_HEAP_ENTRY dos_heap_entries[DOS_HEAP_SLOTS];
static UWORD dos_heap_used;
static UWORD dos_current_drive;
static UWORD dos_ds;
static UWORD dos_es;
static LPVOID dos_ds_pointer;
static LPVOID dos_es_pointer;
static LPVOID dos_result_pointer;
static GEM_U32_WORDS dos_transfer_words;
static GEM_U32_WORDS dos_space_total;
static GEM_U32_WORDS dos_space_available;
static WORD dos_heap_query;

/*
 * GEMDOS sets a file's packed date/time by open handle, while ELKS utime()
 * names the file.  Desktop opens its copy source and destination in a strict
 * sequence and sets time only on the most recently opened destination, so
 * one bounded pathname slot is sufficient and costs 128 bytes rather than a
 * path table.  It is cleared when that exact handle closes.  A caller whose
 * handle has been displaced receives E_BADHANDLE, never fabricated success.
 */
static WORD dos_path_handle = FAILURE;
static BYTE dos_handle_path[PATH_MAX];

static void
dos_set_error(WORD error)
{
	DOS_ERR = error != 0;
	DOS_AX = (UWORD) error;
}

static WORD
dos_error_from_errno(int error)
{
	if (error == ENOENT)
		return E_FILENOTFND;
	if (error == ENOTDIR || error == ENAMETOOLONG || error == ELOOP)
		return E_PATHNOTFND;
	/*
	 * DOS mkdir reported an existing destination directory as access denied.
	 * The original Desktop deliberately tests E_NOACCESS before merging a
	 * copied folder into that directory, so EEXIST must retain that exact
	 * compatibility result.  The remaining cases likewise mean that the
	 * requested pathname cannot be changed, rather than an invalid GEMDOS
	 * function.
	 */
	if (error == EACCES || error == EPERM || error == EROFS
	    || error == EEXIST || error == EISDIR || error == ENOTEMPTY
	    || error == EBUSY)
		return E_NOACCESS;
	if (error == EMFILE || error == ENFILE)
		return E_NOHANDLES;
	if (error == ENOMEM)
		return E_NOMEMORY;
	if (error == EBADF)
		return E_BADHANDLE;
	if (error == EXDEV || error == ENODEV || error == ENXIO)
		return E_NOTDEVICE;
	if (error == EINVAL || error == EFAULT || error == EFBIG
	    || error == EIO)
		return E_BADDATA;
	if (error == ENOSPC || error == ESPIPE || error == EINTR
	    || error == ECHILD)
		return E_BADACCESS;
	if (error == ENOSYS)
		return E_BADFUNC;
	return E_BADFUNC;
}

/*
 * Convert the usual zero-or-positive syscall result to the GEM register
 * convention.  On failure AX receives a positive GEM error number and
 * DOS_ERR becomes true; successful handles and byte counts remain unchanged.
 */
static void
dos_finish_syscall(WORD result)
{
	if (result < 0)
		dos_set_error(dos_error_from_errno(errno));
	else {
		dos_set_error(0);
		DOS_AX = (UWORD) result;
	}
}

/*
 * libc malloc grows the ELKS data segment with sbrk/brk.  This eight-entry
 * ledger enforces the Desktop's historical 12 KiB payload ceiling and lets
 * dos_free reject foreign pointers.  Requests round upward to two bytes;
 * 0xffff is rejected before the addition can wrap.
 */
static LPVOID
dos_heap_alloc(UWORD requested)
{
	DOS_HEAP_ENTRY *entry;
	LPVOID address;

	if (!requested)
		requested = 1;
	if (requested == 0xffffU)
		return NULL;
	requested = (requested + 1U) & 0xfffeU;
	if (requested > DOS_HEAP_BYTES - dos_heap_used)
		return NULL;
	for (entry = &dos_heap_entries[0];
	     entry < &dos_heap_entries[DOS_HEAP_SLOTS] && entry->address;
	     entry++)
		;
	if (entry == &dos_heap_entries[DOS_HEAP_SLOTS])
		return NULL;
	address = malloc(requested);
	if (!address)
		return NULL;
	entry->address = address;
	entry->size = requested;
	dos_heap_used += requested;
	return address;
}

static WORD
dos_heap_free(LPVOID address)
{
	DOS_HEAP_ENTRY *entry;

	if (!address)
		return TRUE;
	for (entry = &dos_heap_entries[0];
	     entry < &dos_heap_entries[DOS_HEAP_SLOTS]
	     && entry->address != address; entry++)
		;
	if (entry == &dos_heap_entries[DOS_HEAP_SLOTS])
		return FALSE;
	dos_heap_used -= entry->size;
	entry->address = NULL;
	entry->size = 0;
	free(address);
	return TRUE;
}

/* Copy a NUL-terminated component without silent truncation. */
static WORD
dos_copy_text(char *destination, UWORD length, const char *source)
{
	UWORD used;

	if (!destination || !source || !length)
		return FALSE;
	for (used = 0; source[used]; used++) {
		if (source[used] == '\\' || used + 1U >= length) {
			destination[0] = '\0';
			return FALSE;
		}
		destination[used] = source[used];
	}
	destination[used] = '\0';
	return TRUE;
}

static WORD
dos_split_search(const BYTE *specification, char *directory,
		 char *pattern)
{
	char path[PATH_MAX];
	char *slash;

	if (!dos_copy_text(path, (UWORD) sizeof(path),
			   specification && *specification
			   ? (const char *) specification : "."))
		return FALSE;
	slash = strrchr(path, '/');
	if (slash) {
		*slash++ = '\0';
		if (!dos_copy_text(directory, PATH_MAX, path[0] ? path : "/")
		    || !dos_copy_text(pattern, GEM_PATTERN_MAX, slash))
			return FALSE;
	} else if (!dos_copy_text(directory, PATH_MAX, ".")
		   || !dos_copy_text(pattern, GEM_PATTERN_MAX, path))
		return FALSE;
	if (!pattern[0]) {
		pattern[0] = '*';
		pattern[1] = '\0';
	}
	return TRUE;
}

/*
 * Case-sensitive '*' and '?' matching with fixed stack use.  retry_pattern
 * and retry_name hold the most recent star position, avoiding recursion and
 * expensive frame traffic on an 8088.
 */
static WORD
dos_wild_match(const char *pattern, const char *name)
{
	const char *retry_pattern;
	const char *retry_name;

	retry_pattern = NULL;
	retry_name = NULL;
	while (*name) {
		if (*pattern == '*') {
			do
				pattern++;
			while (*pattern == '*');
			if (!*pattern)
				return TRUE;
			retry_pattern = pattern;
			retry_name = name;
		} else if (*pattern == '?' || *pattern == *name) {
			pattern++;
			name++;
		} else if (retry_pattern) {
			pattern = retry_pattern;
			name = ++retry_name;
		} else
			return FALSE;
	}
	while (*pattern == '*')
		pattern++;
	return !*pattern;
}

static WORD
dos_join_path(char *destination, const char *directory, const char *name)
{
	UWORD used;

	if (!dos_copy_text(destination, PATH_MAX, directory))
		return FALSE;
	for (used = 0; destination[used]; used++)
		;
	if (used && destination[used - 1U] != '/') {
		if (used + 1U >= PATH_MAX)
			return FALSE;
		destination[used++] = '/';
		destination[used] = '\0';
	}
	return dos_copy_text(destination + used, PATH_MAX - used, name);
}

/* Return the final POSIX pathname component without modifying the path. */
static const char *
dos_path_basename(const char *path)
{
	const char *base;

	base = path;
	if (!path)
		return "";
	while (*path) {
		if (*path++ == '/' && *path)
			base = path;
	}
	return base;
}

/*
 * Copy an unsigned native ABI field into a low/high word pair.  Host audit
 * fields can be wider than the ELKS four-byte value; nonzero upper bytes
 * saturate rather than wrap.  ELKS takes the exact four-byte branch.
 */
#if !defined(ELKS) || !ELKS
static void
dos_boundary_words(const void *source, UWORD size, GEM_U32_WORDS *value)
{
	const UBYTE *bytes;
	UWORD index;

	bytes = (const UBYTE *) source;
	value->lo = 0;
	value->hi = 0;
	if (size > 0)
		memcpy(value, bytes, size < 4U ? size : 4U);
	for (index = 4U; index < size; index++)
		if (bytes[index]) {
			value->lo = 0xffffU;
			value->hi = 0xffffU;
			break;
		}
}
#endif

/* Add one unscaled word pair, saturating at 0xffff:0xffff on overflow. */
static WORD
dos_words_add(GEM_U32_WORDS *value, const GEM_U32_WORDS *addend)
{
	UWORD old;
	UWORD carry;

	old = value->lo;
	value->lo += addend->lo;
	carry = value->lo < old;
	old = value->hi;
	value->hi += addend->hi;
	if (value->hi < old) {
		value->lo = value->hi = 0xffffU;
		return FALSE;
	}
	old = value->hi;
	value->hi += carry;
	if (value->hi < old) {
		value->lo = value->hi = 0xffffU;
		return FALSE;
	}
	return TRUE;
}

/*
 * Scale a word pair by two COUNT times.  Every generated shift has a count
 * of exactly one, which is valid on an original 8088/8086.  Overflow
 * saturates.  Disk counts use count 10 because ELKS statfs reports 1 KiB
 * blocks and GEM Desktop displays unscaled bytes.
 */
static WORD
dos_words_shift_left(GEM_U32_WORDS *value, UWORD count)
{
	UWORD carry;

	while (count--) {
		if (value->hi & 0x8000U) {
			value->lo = value->hi = 0xffffU;
			return FALSE;
		}
		carry = value->lo & 0x8000U;
		value->lo <<= 1;
		value->hi <<= 1;
		if (carry)
			value->hi |= 1U;
	}
	return TRUE;
}

#if !defined(ELKS) || !ELKS
/* Host-audit statvfs reports blocks in f_frsize units, not ELKS 1 KiB. */
static void
dos_words_multiply_u16(GEM_U32_WORDS *value, UWORD factor)
{
	GEM_U32_WORDS addend;
	GEM_U32_WORDS result;

	addend = *value;
	result = dos_zero_words;
	while (factor) {
		if (factor & 1U)
			if (!dos_words_add(&result, &addend))
				break;
		factor >>= 1;
		if (factor && !dos_words_shift_left(&addend, 1U)) {
			result.lo = result.hi = 0xffffU;
			break;
		}
	}
	*value = result;
}
#endif

/* Query the filesystem containing the Desktop's current POSIX directory. */
static WORD
dos_filesystem_query(DOS_FILESYSTEM_VIEW *view)
{
#if defined(ELKS) && ELKS
	DOS_ELKS_STATFS native;
	struct stat path_stat;

	if (!view) {
		errno = EFAULT;
		return FALSE;
	}
	memset(view, 0, sizeof(*view));
	memset(&native, 0, sizeof(native));
	if (stat(".", &path_stat) < 0
	    || ustatfs(path_stat.st_dev, &native, 0) < 0)
		return FALSE;
	view->total_blocks = native.total_blocks;
	view->available_blocks = native.available_blocks;
	memcpy(view->mount_name, native.mount_name,
	       sizeof(view->mount_name));
	view->mount_name[sizeof(view->mount_name) - 1U] = '\0';
	return TRUE;
#else
	struct statvfs native;
	GEM_U32_WORDS block_size;

	if (!view) {
		errno = EFAULT;
		return FALSE;
	}
	memset(view, 0, sizeof(*view));
	if (statvfs(".", &native) < 0)
		return FALSE;
	dos_boundary_words(&native.f_blocks,
			   (UWORD) sizeof(native.f_blocks),
			   &view->total_blocks);
	dos_boundary_words(&native.f_bavail,
			   (UWORD) sizeof(native.f_bavail),
			   &view->available_blocks);
	dos_boundary_words(&native.f_frsize,
			   (UWORD) sizeof(native.f_frsize), &block_size);
	if (block_size.hi) {
		view->total_blocks.lo = view->total_blocks.hi = 0xffffU;
		view->available_blocks.lo = 0xffffU;
		view->available_blocks.hi = 0xffffU;
	} else {
		dos_words_multiply_u16(&view->total_blocks, block_size.lo);
		dos_words_multiply_u16(&view->available_blocks,
				       block_size.lo);
	}
	return TRUE;
#endif
}

/* Return unscaled byte totals in the historical GEM four-byte fields. */
static WORD
dos_filesystem_space(GEM_U32_WORDS *total, GEM_U32_WORDS *available)
{
	DOS_FILESYSTEM_VIEW filesystem;

	if (!total || !available) {
		errno = EFAULT;
		return FALSE;
	}
	if (!dos_filesystem_query(&filesystem))
		return FALSE;
	*total = filesystem.total_blocks;
	*available = filesystem.available_blocks;
#if defined(ELKS) && ELKS
	/* ELKS f_blocks/f_bavail are explicitly measured in 1 KiB blocks. */
	(void) dos_words_shift_left(total, 10U);
	(void) dos_words_shift_left(available, 10U);
#endif
	return TRUE;
}

/*
 * Map the POSIX mount identity to the original eleven-character label field.
 * A normal mount uses its bounded final path component.  The root mount has
 * no basename, so the stable display name ROOT denotes that actual identity.
 * Overlength identities fail instead of being silently truncated.
 */
static WORD
dos_mount_label(const DOS_FILESYSTEM_VIEW *view, BYTE *label)
{
	const BYTE *source;
	const BYTE *scan;
	UWORD used;

	if (!view || !label || !view->mount_name[0])
		return FALSE;
	if (view->mount_name[0] == '/'
	    && view->mount_name[1] == '\0')
		source = (const BYTE *) "ROOT";
	else {
		source = view->mount_name;
		for (scan = view->mount_name; *scan; scan++)
			if (*scan == '/' && scan[1])
				source = scan + 1;
	}
	for (used = 0; source[used]; used++)
		if (used >= GEM_VOLUME_LABEL_MAX) {
			label[0] = '\0';
			return FALSE;
		} else
			label[used] = source[used];
	label[used] = '\0';
	return used != 0;
}

/* Service the one FCB operation retained by Desktop: read volume identity. */
static WORD
dos_fcb_volume_first(const UBYTE *request)
{
	DOS_FILESYSTEM_VIEW filesystem;
	BYTE label[GEM_VOLUME_LABEL_MAX + 1U];
	UBYTE *dta;
	UWORD used;

	if (!request || !current_dta || request[6] != F_VOLUME
	    || request[7] > 26U || !dos_filesystem_query(&filesystem)
	    || !dos_mount_label(&filesystem, label)) {
		DOS_ERR = FALSE;
		DOS_AX = 0x00ffU;
		return FALSE;
	}
	dta = (UBYTE *) current_dta;
	memset(dta, 0, 20U);
	for (used = 0; label[used]; used++)
		dta[8U + used] = (UBYTE) label[used];
	DOS_ERR = FALSE;
	DOS_AX = 0;
	return TRUE;
}

/*
 * Subtract SPAN if VALUE is large enough.  Both operands are unscaled
 * seconds in explicit low/high words.  The low-word comparison supplies the
 * only borrow; no 32-bit compiler arithmetic is generated.
 */
static WORD
dos_take_span(GEM_U32_WORDS *value, const GEM_U32_WORDS *span)
{
	UWORD borrow;

	if (value->hi < span->hi
	    || (value->hi == span->hi && value->lo < span->lo))
		return FALSE;
	borrow = value->lo < span->lo;
	value->lo -= span->lo;
	value->hi -= span->hi;
	value->hi -= borrow;
	return TRUE;
}

/*
 * Shift right with one-bit 8086 operations only.  Counts are bounded packed
 * date/time field widths, never data-size quantities.
 */
static UWORD
dos_word_shift_right(UWORD value, UWORD count)
{
	while (count--)
		value >>= 1;
	return value;
}

/*
 * Decode a packed DOS date/time into unsigned epoch seconds.  All arithmetic
 * remains in explicit low/high words.  Invalid calendar fields fail with no
 * partial timestamp.  Values beyond 0xffff:0xffff saturate, which is the
 * documented ELKS unsigned time_t boundary behaviour.
 */
static WORD
dos_unpack_clock(UWORD packed_time, UWORD packed_date,
		 GEM_U32_WORDS *seconds)
{
	GEM_U32_WORDS span;
	UWORD day;
	UWORD days;
	UWORD hour;
	UWORD minute;
	UWORD month;
	UWORD second;
	UWORD year;
	UWORD index;

	if (!seconds)
		return FALSE;
	year = 1980U + dos_word_shift_right(packed_date, 9U);
	month = dos_word_shift_right(packed_date & 0x01e0U, 5U);
	day = packed_date & 0x001fU;
	hour = dos_word_shift_right(packed_time & 0xf800U, 11U);
	minute = dos_word_shift_right(packed_time & 0x07e0U, 5U);
	second = (packed_time & 0x001fU) << 1;
	if (month < 1U || month > 12U || hour > 23U
	    || minute > 59U || second > 59U)
		return FALSE;
	days = dos_month_days[month];
	if (month == 2U && (year & 3U) == 0 && year != 2100U)
		days++;
	if (day < 1U || day > days)
		return FALSE;

	*seconds = dos_zero_words;
	for (index = 1970U; index < year; index++) {
		span.lo = (index & 3U) == 0 && index != 2100U
			? 0x8500U : 0x3380U;
		span.hi = (index & 3U) == 0 && index != 2100U
			? 0x01e2U : 0x01e1U;
		if (!dos_words_add(seconds, &span))
			return TRUE;
	}
	for (index = 1U; index < month; index++) {
		span = dos_month_seconds[index];
		if (index == 2U && (year & 3U) == 0 && year != 2100U) {
			span.lo = 0x3b80U;
			span.hi = 0x0026U;
		}
		if (!dos_words_add(seconds, &span))
			return TRUE;
	}
	span.lo = 0x5180U;
	span.hi = 0x0001U;
	for (index = 1U; index < day; index++)
		if (!dos_words_add(seconds, &span))
			return TRUE;
	span.lo = 3600U;
	span.hi = 0;
	for (index = 0; index < hour; index++)
		if (!dos_words_add(seconds, &span))
			return TRUE;
	span.lo = 60U;
	for (index = 0; index < minute; index++)
		if (!dos_words_add(seconds, &span))
			return TRUE;
	span.lo = second;
	(void) dos_words_add(seconds, &span);
	return TRUE;
}

/*
 * Apply packed GEM time to the one destination created by Desktop copy.
 * fstat preserves its access time; utime changes only the requested
 * modification time.  The target utime wire image contains only explicit
 * word pairs; native time_t is read from stat solely by four-byte memcpy, so
 * no compiler wide helper can enter the XT executable.
 */
static WORD
dos_set_handle_clock(WORD handle, UWORD packed_time, UWORD packed_date)
{
	struct stat native;
#if defined(ELKS) && ELKS
	DOS_ELKS_UTIMBUF times;
#else
	struct utimbuf times;
#endif
	GEM_U32_WORDS modified;
	GEM_U32_WORDS accessed;
	const UBYTE *bytes;
#if !defined(ELKS) || !ELKS
	UWORD index;
#endif

	if (handle != dos_path_handle || !dos_handle_path[0]) {
		errno = EBADF;
		return FAILURE;
	}
	if (!dos_unpack_clock(packed_time, packed_date, &modified)) {
		errno = EINVAL;
		return FAILURE;
	}
	if (fstat(handle, &native) < 0)
		return FAILURE;
	bytes = (const UBYTE *) &native.st_atime;
	memcpy(&accessed, bytes, sizeof(accessed));
#if !defined(ELKS) || !ELKS
	for (index = 4U; index < (UWORD) sizeof(native.st_atime); index++)
		if (bytes[index]) {
			accessed.lo = accessed.hi = 0xffffU;
			break;
		}
#endif
	memset(&times, 0, sizeof(times));
#if defined(ELKS) && ELKS
	times.access_time = accessed;
	times.modification_time = modified;
#else
	memcpy(&times.actime, &accessed, sizeof(accessed));
	memcpy(&times.modtime, &modified, sizeof(modified));
#endif
	return (WORD) utime((const char *) dos_handle_path, &times);
}

/* Pack validated calendar fields; odd seconds round downward by one. */
static void
dos_pack_clock(DOS_STAT_VIEW *view, UWORD year, UWORD month, UWORD day,
	       UWORD hour, UWORD minute, UWORD second)
{
	UBYTE *packed;

	if (year < 1980U)
		year = 1980U;
	else if (year > 2107U)
		year = 2107U;
	packed = (UBYTE *) &view->time;
	packed[0] = dos_minute_low[minute & 7U] | (UBYTE) (second >> 1);
	packed[1] = dos_hour_high[hour] | dos_minute_high[minute];
	packed = (UBYTE *) &view->date;
	packed[0] = dos_month_low[month] | (UBYTE) day;
	packed[1] = (UBYTE) (((year - 1980U) << 1)
				    | dos_month_high[month]);
}

/*
 * Convert native epoch seconds to packed DOS UTC time with word operations.
 * A target timestamp is exactly two little-endian words.  A wider host audit
 * value saturates at an endpoint instead of being truncated.  The bounded
 * subtraction loops require at most 137 years, 11 months, 30 days, 23 hours,
 * and 59 minutes; they avoid all division and modulo helpers.
 */
static void
dos_stat_clock(const struct stat *native, DOS_STAT_VIEW *view)
{
	GEM_U32_WORDS remaining;
	GEM_U32_WORDS span;
	const UBYTE *bytes;
	UWORD day;
	UWORD hour;
	UWORD minute;
	UWORD month;
	UWORD year;
#if !ELKS
	UWORD index;
#endif

	bytes = (const UBYTE *) &native->st_mtime;
	memcpy(&remaining, bytes, sizeof(remaining));
#if !ELKS
	if (bytes[sizeof(native->st_mtime) - 1U] & 0x80U) {
		dos_pack_clock(view, 1980U, 1U, 1U, 0U, 0U, 0U);
		return;
	}
	for (index = 4; index < (UWORD) sizeof(native->st_mtime); index++)
		if (bytes[index]) {
			dos_pack_clock(view, 2107U, 12U, 31U, 23U, 59U, 59U);
			return;
		}
#endif
	year = 1970U;
	for (;;) {
		span.lo = (year & 3U) == 0 && year != 2100U
			? 0x8500U : 0x3380U;
		span.hi = (year & 3U) == 0 && year != 2100U
			? 0x01e2U : 0x01e1U;
		if (!dos_take_span(&remaining, &span))
			break;
		year++;
	}
	for (month = 1U; month < 12U; month++) {
		span = dos_month_seconds[month];
		if (month == 2U && (year & 3U) == 0 && year != 2100U) {
			span.lo = 0x3b80U;
			span.hi = 0x0026U;
		}
		if (!dos_take_span(&remaining, &span))
			break;
	}
	span.lo = 0x5180U;
	span.hi = 0x0001U;
	for (day = 1U; dos_take_span(&remaining, &span); day++)
		;
	span.lo = 3600U;
	span.hi = 0;
	for (hour = 0; dos_take_span(&remaining, &span); hour++)
		;
	span.lo = 60U;
	for (minute = 0; dos_take_span(&remaining, &span); minute++)
		;
	dos_pack_clock(view, year, month, day, hour, minute, remaining.lo);
}

static WORD
dos_get_handle_clock(WORD handle, UWORD *packed_time, UWORD *packed_date)
{
	DOS_STAT_VIEW view;
	struct stat native;

	if (!packed_time || !packed_date) {
		errno = EFAULT;
		return FAILURE;
	}
	if (fstat(handle, &native) < 0)
		return FAILURE;
	dos_stat_clock(&native, &view);
	*packed_time = view.time;
	*packed_date = view.date;
	return 0;
}

static WORD
dos_stat_path(const char *path, DOS_STAT_VIEW *view)
{
	struct stat native;
	const UBYTE *bytes;
#if !ELKS
	UWORD index;
#endif

	if (stat(path, &native) < 0)
		return FALSE;
	view->mode = (UWORD) native.st_mode;
	bytes = (const UBYTE *) &native.st_size;
	memcpy(&view->size, bytes, sizeof(view->size));
#if !ELKS
	for (index = 4; index < (UWORD) sizeof(native.st_size); index++)
		if (bytes[index]) {
			view->size.lo = 0xffffU;
			view->size.hi = 0xffffU;
			break;
		}
#endif
	dos_stat_clock(&native, view);
	return TRUE;
}

static UBYTE
dos_file_attr(const char *name, UWORD mode)
{
	UBYTE attr;

	attr = 0;
	if (S_ISDIR(mode))
		attr |= F_SUBDIR;
	else
		attr |= F_ARCHIVE;
	if (!(mode & S_IWUSR))
		attr |= F_RDONLY;
	name = dos_path_basename(name);
	if (name[0] == '.' && name[1])
		attr |= F_HIDDEN;
	return attr;
}

static GEM_SEARCH *
dos_search_slot(LPVOID dta, WORD create)
{
	GEM_SEARCH *search;
	GEM_SEARCH *free_slot;

	free_slot = NULL;
	for (search = &searches[0]; search < &searches[GEM_MAX_SEARCHES];
	     search++) {
		if (search->used && search->dta == dta)
			return search;
		if (!search->used && !free_slot)
			free_slot = search;
	}
	if (!create || !free_slot)
		return NULL;
	memset(free_slot, 0, sizeof(*free_slot));
	free_slot->used = TRUE;
	free_slot->dta = dta;
	return free_slot;
}

static WORD
dos_search_next(GEM_SEARCH *search)
{
	struct dirent *entry;
	char path[PATH_MAX];
	DOS_STAT_VIEW view;
	FCB *fcb;
	UBYTE attr;

	if (!search || !search->dir) {
		dos_set_error(E_NOFILES);
		return FALSE;
	}
	while ((entry = readdir(search->dir)) != NULL) {
		if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")
		    || !dos_wild_match(search->pattern, entry->d_name)
		    || !dos_join_path(path, search->dirpath, entry->d_name)
		    || !dos_stat_path(path, &view))
			continue;
		attr = dos_file_attr(entry->d_name, view.mode);
		if ((attr & F_SUBDIR) && !(search->attr & F_SUBDIR))
			continue;
		if ((attr & F_HIDDEN) && !(search->attr & F_HIDDEN))
			continue;
		fcb = (FCB *) search->dta;
		memset(fcb, 0, sizeof(*fcb));
		fcb->fcb_attr = attr;
		fcb->fcb_time = view.time;
		fcb->fcb_date = view.date;
		fcb->fcb_size = view.size;
		if (!dos_copy_text(fcb->fcb_name, sizeof(fcb->fcb_name),
				   entry->d_name))
			continue;
		dos_set_error(0);
		return TRUE;
	}
	closedir(search->dir);
	memset(search, 0, sizeof(*search));
	dos_set_error(E_NOFILES);
	return FALSE;
}

static WORD
dos_search_first(LPBYTE specification, WORD attr)
{
	GEM_SEARCH *search;

	if (!current_dta)
		current_dta = &fallback_dta;
	search = dos_search_slot(current_dta, TRUE);
	if (!search) {
		dos_set_error(E_NOHANDLES);
		return FALSE;
	}
	if (search->dir) {
		closedir(search->dir);
		search->dir = NULL;
	}
	if (!dos_split_search(specification, search->dirpath, search->pattern)) {
		memset(search, 0, sizeof(*search));
		dos_set_error(E_PATHNOTFND);
		return FALSE;
	}
	search->attr = attr;
	search->dir = opendir(search->dirpath);
	if (!search->dir) {
		dos_set_error(dos_error_from_errno(errno));
		memset(search, 0, sizeof(*search));
		return FALSE;
	}
	return dos_search_next(search);
}

/*
 * Perform every pathname syscall from one translated buffer.  RESULT remains
 * the native zero/handle/-1 value and is converted to GEM error state once by
 * dos_finish_syscall().  chmod uses a 16-bit mode word internally; mode_t is
 * touched only at the libc boundary.
 */
static WORD
dos_path_syscall(UWORD function)
{
	char path[PATH_MAX];
	char newpath[PATH_MAX];
	struct stat native;
	UBYTE current_attr;
	UWORD mode;
	WORD flags;
	WORD result;

	if (!dos_ds_pointer || !*(LPBYTE) dos_ds_pointer
	    || !dos_copy_text(path, (UWORD) sizeof(path),
			      (const char *) dos_ds_pointer)) {
		errno = ENOENT;
		return FAILURE;
	}
	switch (function & 0xff00U) {
	case 0x3900:
		return (WORD) mkdir(path, 0777);
	case 0x3a00:
		return (WORD) rmdir(path);
	case 0x3b00:
		return (WORD) chdir(path);
	case 0x3c00:
		if (DOS_CX & (F_SYSTEM | F_VOLUME | F_SUBDIR)) {
			errno = EACCES;
			return FAILURE;
		}
		if ((DOS_CX & F_HIDDEN)
		    && dos_path_basename(path)[0] != '.') {
			errno = EACCES;
			return FAILURE;
		}
		mode = (DOS_CX & F_RDONLY) ? 0444U : 0666U;
		result = (WORD) open(path, O_CREAT | O_TRUNC | O_RDWR,
				     (mode_t) mode);
		if (result >= 0) {
			dos_path_handle = result;
			(void) dos_copy_text(dos_handle_path,
					     (UWORD) sizeof(dos_handle_path), path);
		}
		return result;
	case 0x3d00:
		if ((function & 0x00ffU) > 2U) {
			errno = EINVAL;
			return FAILURE;
		}
		flags = O_RDONLY;
		if ((function & 3U) == 1U)
			flags = O_WRONLY;
		else if ((function & 3U) == 2U)
			flags = O_RDWR;
		result = (WORD) open(path, flags);
		if (result >= 0) {
			dos_path_handle = result;
			(void) dos_copy_text(dos_handle_path,
					     (UWORD) sizeof(dos_handle_path), path);
		}
		return result;
	case 0x4100:
		result = (WORD) unlink(path);
		if (!result && dos_path_handle != FAILURE
		    && !strcmp(path, dos_handle_path)) {
			dos_path_handle = FAILURE;
			dos_handle_path[0] = '\0';
		}
		return result;
	case 0x4300:
		if ((function & 0x00ffU) > F_SETMOD) {
			errno = ENOSYS;
			return FAILURE;
		}
		if (stat(path, &native) < 0)
			return FAILURE;
		mode = (UWORD) native.st_mode;
		if ((function & 0x00ffU) == F_GETMOD) {
			DOS_CX = dos_file_attr(path, mode);
			return 0;
		}
		current_attr = dos_file_attr(path, mode);
		if (((UBYTE) DOS_CX & (F_HIDDEN | F_SYSTEM | F_SUBDIR
					 | F_ARCHIVE))
		    != (current_attr & (F_HIDDEN | F_SYSTEM | F_SUBDIR
					   | F_ARCHIVE))) {
			errno = EACCES;
			return FAILURE;
		}
		if (DOS_CX & F_RDONLY)
			mode &= (UWORD) ~(S_IWUSR | S_IWGRP | S_IWOTH);
		else
			mode |= S_IWUSR;
		return (WORD) chmod(path, (mode_t) mode);
	case 0x5600:
		if (!dos_es_pointer || !*(LPBYTE) dos_es_pointer
		    || !dos_copy_text(newpath, (UWORD) sizeof(newpath),
				   (const char *) dos_es_pointer)) {
			errno = ENOENT;
			return FAILURE;
		}
		result = (WORD) rename(path, newpath);
		if (!result && dos_path_handle != FAILURE
		    && !strcmp(path, dos_handle_path))
			(void) dos_copy_text(dos_handle_path,
					     (UWORD) sizeof(dos_handle_path),
					     newpath);
		return result;
	default:
		errno = ENOENT;
		return FAILURE;
	}
}

/*
 * ELKS ssize_t is signed 16-bit, so a single read/write is capped at 0x7fff.
 * Counts are unscaled bytes.  A later error returns an already-completed
 * prefix as success; an error before any byte preserves the mapped GEM error.
 */
static UWORD
dos_io(WORD handle, UWORD count, LPVOID buffer, WORD writing)
{
	LPBYTE next;
	UWORD done;
	UWORD piece;
	WORD result;

	next = (LPBYTE) buffer;
	done = 0;
	while (count) {
		piece = count > 0x7fffU ? 0x7fffU : count;
		if (writing)
			result = (WORD) write(handle, next, piece);
		else
			result = (WORD) read(handle, next, piece);
		if (result < 0) {
			dos_set_error(done ? 0 : dos_error_from_errno(errno));
			return done;
		}
		if (!result)
			break;
		done += (UWORD) result;
		count -= (UWORD) result;
		next += (UWORD) result;
		if ((UWORD) result < piece)
			break;
	}
	dos_set_error(0);
	return done;
}

/*
 * off_t is the unavoidable four-byte ELKS lseek ABI.  The unions isolate it
 * at the syscall boundary.  Input is sign-extended on wider host audits;
 * output above the GEM four-byte field saturates to 0xffff:0xffff.
 */
typedef union dos_seek_boundary {
	off_t native;
	GEM_U32_WORDS words;
} DOS_SEEK_BOUNDARY;

static GEM_U32_WORDS
dos_seek(WORD handle, UWORD function)
{
	DOS_SEEK_BOUNDARY input;
	DOS_SEEK_BOUNDARY output;
	GEM_U32_WORDS result;
#if !ELKS
	const UBYTE *bytes;
	UWORD index;
#endif

	memset(&input, (DOS_CX & 0x8000U) ? 0xff : 0, sizeof(input));
	input.words.lo = DOS_DX;
	input.words.hi = DOS_CX;
	errno = 0;
	output.native = lseek(handle, input.native,
			      (WORD) (function & 0x00ffU));
	if (errno) {
		dos_set_error(dos_error_from_errno(errno));
		result.lo = result.hi = 0xffffU;
		return result;
	}
	result = output.words;
#if !ELKS
	bytes = (const UBYTE *) &output.native;
	for (index = 4; index < (UWORD) sizeof(output.native); index++)
		if (bytes[index]) {
			result.lo = result.hi = 0xffffU;
			break;
		}
#endif
	dos_set_error(0);
	return result;
}

/*
 * One compact replacement for GEMDOSIF.A86 __DOS.  The public routines below
 * still prepare the exact classic registers.  This switch performs the ELKS
 * operation directly; there is no duplicate native wrapper layer.
 */
static VOID
__DOS(VOID)
{
	UWORD function;
	WORD result;
	int status;

	function = DOS_AX;
	switch (function & 0xff00U) {
	case 0x0e00:
		/*
		 * Retain a valid A..Z compatibility selector, but every selector
		 * addresses the same POSIX namespace.  No drive prefix is ever
		 * inserted into a native pathname.
		 */
		if (DOS_DX < 26U) {
			dos_current_drive = DOS_DX;
			dos_set_error(0);
			DOS_AX = TRUE;
		} else
			dos_set_error(E_BADDRIVE);
		break;
	case 0x1100:
		(void) dos_fcb_volume_first((const UBYTE *) dos_ds_pointer);
		break;
	case 0x1900:
		dos_set_error(0);
		DOS_AX = dos_current_drive;
		break;
	case 0x1a00:
		current_dta = dos_ds_pointer;
		dos_set_error(0);
		break;
	case 0x3600:
		dos_space_total = dos_zero_words;
		dos_space_available = dos_zero_words;
		if (DOS_DX > 26U)
			dos_set_error(E_BADDRIVE);
		else if (!dos_filesystem_space(&dos_space_total,
					       &dos_space_available))
			dos_set_error(dos_error_from_errno(errno));
		else
			dos_set_error(0);
		break;
	case 0x3900:
	case 0x3a00:
	case 0x3b00:
	case 0x3c00:
	case 0x3d00:
	case 0x4100:
	case 0x4300:
	case 0x5600:
		dos_finish_syscall(dos_path_syscall(function));
		break;
	case 0x3e00:
		result = (WORD) close((WORD) DOS_BX);
		if (!result && (WORD) DOS_BX == dos_path_handle) {
			dos_path_handle = FAILURE;
			dos_handle_path[0] = '\0';
		}
		dos_finish_syscall(result);
		break;
	case 0x3f00:
	case 0x4000:
		dos_transfer_words = dos_zero_words;
		dos_transfer_words.lo = dos_io((WORD) DOS_BX, DOS_CX,
					   dos_ds_pointer,
					   function == 0x4000U);
		if (!DOS_ERR)
			DOS_AX = dos_transfer_words.lo;
		break;
	case 0x4200:
		dos_transfer_words = dos_seek((WORD) DOS_BX, function);
		if (!DOS_ERR) {
			DOS_AX = dos_transfer_words.lo;
			DOS_DX = dos_transfer_words.hi;
		}
		break;
	case 0x4700:
		if (DOS_DX > 26U) {
			errno = ENODEV;
			result = FAILURE;
		} else if (!dos_ds_pointer) {
			errno = EFAULT;
			result = FAILURE;
		} else
			result = getcwd((char *) dos_ds_pointer, GEM_GDIR_MAX)
				? 0 : FAILURE;
		dos_finish_syscall(result);
		break;
	case 0x4800:
		if (dos_heap_query) {
			dos_transfer_words = dos_zero_words;
			dos_transfer_words.lo = DOS_HEAP_BYTES - dos_heap_used;
			DOS_BX = dos_transfer_words.lo;
			dos_set_error(0);
			break;
		}
		dos_result_pointer = dos_heap_alloc(dos_transfer_words.lo);
		if (!dos_result_pointer)
			dos_set_error(E_NOMEMORY);
		else {
			dos_set_error(0);
			DOS_AX = (UWORD) FP_OFF(dos_result_pointer);
		}
		break;
	case 0x4900:
		if (!dos_heap_free(dos_es_pointer))
			dos_set_error(E_BADMEMBLK);
		else
			dos_set_error(0);
		break;
	case 0x4d00:
		do {
			result = (WORD) wait(&status);
		} while (result < 0 && errno == EINTR);
		if (result < 0)
			dos_finish_syscall(result);
		else {
			dos_set_error(0);
			DOS_AX = (UWORD) status;
		}
		break;
	case 0x4e00:
		dos_search_first((LPBYTE) dos_ds_pointer, (WORD) DOS_CX);
		break;
	case 0x4f00:
		dos_search_next(dos_search_slot(current_dta, FALSE));
		break;
	case 0x5700:
		if ((function & 0x00ffU) == 0U) {
			result = dos_get_handle_clock((WORD) DOS_BX,
						      &DOS_CX, &DOS_DX);
			if (result < 0)
				dos_finish_syscall(result);
			else
				dos_set_error(0);
		} else if ((function & 0x00ffU) == 1U)
			dos_finish_syscall(dos_set_handle_clock((WORD) DOS_BX,
							  DOS_CX, DOS_DX));
		else
			dos_set_error(E_BADFUNC);
		break;
	default:
		dos_set_error(E_BADFUNC);
		break;
	}
}

/*
 * Direct FreeGEM GEMDOS.C wrapper logic begins here.  Pointer arguments keep
 * the original low/high register image, while dos_ds_pointer and
 * dos_es_pointer retain the usable near pointer for the ELKS syscall seam.
 */
VOID
dos_func(UWORD ax, UWORD lodsdx, UWORD hidsdx)
{
	DOS_AX = ax;
	DOS_DX = lodsdx;
	DOS_DS = hidsdx;
	dos_ds_pointer = gem_near_words_pointer(
		gem_u32_words(lodsdx, hidsdx));

	__DOS();
}

/* Exported because the direct-derived Desktop volume-label code also uses it. */
VOID
dos_lpvoid(UWORD ax, LPVOID ptr)
{
	dos_func(ax, (UWORD) FP_OFF(ptr), (UWORD) FP_SEG(ptr));
}

WORD
dos_chdir(LPBYTE pdrvpath)
{
	dos_lpvoid(0x3b00, pdrvpath);
	return !DOS_ERR;
}

WORD
dos_gdir(WORD drive, LPBYTE pdrvpath)
{
	DOS_AX = 0x4700;
	DOS_DX = (UWORD) drive;
	DOS_SI = (UWORD) FP_OFF(pdrvpath);
	DOS_DS = (UWORD) FP_SEG(pdrvpath);
	dos_ds_pointer = pdrvpath;

	__DOS();

	return !DOS_ERR;
}

WORD
dos_gdrv(VOID)
{
	DOS_AX = 0x1900;

	__DOS();
	return DOS_AX & 0x00ffU;
}

WORD
dos_sdrv(WORD newdrv)
{
	DOS_AX = 0x0e00;
	DOS_DX = (UWORD) newdrv;

	__DOS();

	return DOS_AX & 0x00ffU;
}

WORD
dos_sdta(LPVOID ldta)
{
	dos_lpvoid(0x1a00, ldta);
	return !DOS_ERR;
}

WORD
dos_sfirst(LPBYTE pspec, WORD attr)
{
	DOS_CX = (UWORD) attr;

	dos_lpvoid(0x4e00, pspec);
	return !DOS_ERR;
}

WORD
dos_snext(VOID)
{
	DOS_AX = 0x4f00;

	__DOS();

	return !DOS_ERR;
}

WORD
dos_create(LPBYTE pname, WORD attr)
{
	DOS_CX = (UWORD) attr;
	dos_lpvoid(0x3c00, pname);

	return (WORD) DOS_AX;
}

WORD
dos_open(LPBYTE pname, WORD access)
{
	dos_lpvoid((UWORD) (0x3d00 + access), pname);

	return (WORD) DOS_AX;
}

WORD
dos_close(WORD handle)
{
	DOS_AX = 0x3e00;
	DOS_BX = (UWORD) handle;

	__DOS();

	return !DOS_ERR;
}

WORD
dos_delete(LPBYTE ppath)
{
	dos_lpvoid(0x4100, ppath);
	return !DOS_ERR;
}

GEM_U32_WORDS
dos_read(WORD handle, GEM_U32_WORDS cnt, LPBYTE pbuffer)
{
	if (cnt.hi) {
		dos_set_error(E_BADDATA);
		return dos_zero_words;
	}
	DOS_CX = cnt.lo;
	DOS_BX = (UWORD) handle;
	dos_lpvoid(0x3f00, pbuffer);
	return dos_transfer_words;
}

GEM_U32_WORDS
dos_write(WORD handle, GEM_U32_WORDS cnt, LPBYTE pbuffer)
{
	if (cnt.hi) {
		dos_set_error(E_BADDATA);
		return dos_zero_words;
	}
	DOS_CX = cnt.lo;
	DOS_BX = (UWORD) handle;
	dos_lpvoid(0x4000, pbuffer);
	return dos_transfer_words;
}

GEM_U32_WORDS
dos_lseek(WORD handle, WORD smode, GEM_U32_WORDS sofst)
{
	DOS_AX = 0x4200;
	DOS_AX += (UWORD) smode;
	DOS_BX = (UWORD) handle;
	DOS_CX = sofst.hi;
	DOS_DX = sofst.lo;

	__DOS();

	return dos_transfer_words;
}

WORD
dos_wait(VOID)
{
	DOS_AX = 0x4d00;
	__DOS();

	return (WORD) DOS_AX;
}

LPVOID
dos_alloc(GEM_U32_WORDS nbytes)
{
	LPVOID maddr;

	if (nbytes.hi) {
		dos_set_error(E_NOMEMORY);
		return NULL;
	}
	DOS_AX = 0x4800;
	DOS_BX = nbytes.lo;
	dos_transfer_words = nbytes;
	dos_heap_query = FALSE;

	__DOS();

	if (DOS_ERR)
		maddr = NULL;
	else
		maddr = dos_result_pointer;
	return maddr;
}

GEM_U32_WORDS
dos_avail(VOID)
{
	DOS_AX = 0x4800;
	DOS_BX = 0xffffU;
	dos_heap_query = TRUE;

	__DOS();

	dos_heap_query = FALSE;
	return dos_transfer_words;
}

WORD
dos_free(LPVOID maddr)
{
	DOS_AX = 0x4900;
	DOS_ES = (UWORD) FP_SEG(maddr);
	dos_es_pointer = maddr;

	__DOS();

	return !DOS_ERR;
}

WORD
dos_space(WORD drv, GEM_U32_WORDS *ptotal, GEM_U32_WORDS *pavail)
{
	DOS_AX = 0x3600;
	DOS_DX = (UWORD) drv;
	__DOS();

	if (ptotal)
		*ptotal = dos_space_total;
	if (pavail)
		*pavail = dos_space_available;
	return !DOS_ERR;
}

WORD
dos_rmdir(LPBYTE ppath)
{
	dos_lpvoid(0x3a00, ppath);
	return !DOS_ERR;
}

WORD
dos_mkdir(LPBYTE ppath)
{
	dos_lpvoid(0x3900, ppath);
	return !DOS_ERR;
}

WORD
dos_rename(LPBYTE poname, LPBYTE pnname)
{
	DOS_DI = (UWORD) FP_OFF(pnname);
	DOS_ES = (UWORD) FP_SEG(pnname);
	dos_es_pointer = pnname;
	dos_lpvoid(0x5600, poname);
	return !DOS_ERR;
}

WORD
dos_chmod(LPBYTE pname, WORD func, WORD attr)
{
	DOS_CX = (UWORD) attr;
	dos_lpvoid((UWORD) (0x4300 + func), pname);
	if (DOS_ERR)
		return FAILURE;
	return (WORD) DOS_CX;
}

WORD
dos_setdt(WORD handle, WORD time, WORD date)
{
	DOS_AX = 0x5701;
	DOS_BX = (UWORD) handle;
	DOS_CX = (UWORD) time;
	DOS_DX = (UWORD) date;

	__DOS();
	return !DOS_ERR;
}

WORD
dos_dtype(WORD drive)
{
	DOS_FILESYSTEM_VIEW filesystem;

	/*
	 * Drive probing is retained as a view of the selected compatibility
	 * letter.  Exactly one letter names the current POSIX filesystem, and it
	 * is a local hard filesystem (type 1 in original Desktop); every other
	 * valid letter is absent.  This prevents autodetection from fabricating
	 * twenty-six copies of the same mount.
	 */
	if (drive < 0 || drive >= 26) {
		dos_set_error(E_BADDRIVE);
		return 0x0f;
	}
	if ((UWORD) drive != dos_current_drive) {
		dos_set_error(0);
		return 0x0f;
	}
	if (!dos_filesystem_query(&filesystem)) {
		dos_set_error(dos_error_from_errno(errno));
		return 0x0f;
	}
	dos_set_error(0);
	return 1;
}

int
int86(int vec, union REGS *inregs, union REGS *outregs)
{
	(void) vec;
	(void) inregs;
	if (outregs) {
		memset(outregs, 0, sizeof(*outregs));
		outregs->x.ax = E_BADFUNC;
		outregs->x.cflag = 1;
	}
	return E_BADFUNC;
}

int
intdos(union REGS *inregs, union REGS *outregs)
{
	(void) inregs;
	if (outregs) {
		memset(outregs, 0, sizeof(*outregs));
		/* No native ELKS caller can execute an arbitrary DOS interrupt. */
		outregs->x.ax = E_BADFUNC;
		outregs->x.cflag = 1;
	}
	return E_BADFUNC;
}
