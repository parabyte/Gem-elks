/*
 * gemdos_posix.c - the Desktop's dos_* file calls over ELKS syscalls
 *
 * every dos_* file call goes straight to its ELKS syscall, was just too much work
 * to make userfacing apps posix, happy for someone to help! its very low on my agenda, as i wish to try and stay
 * source compatible with gem
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

#undef PATH_MAX
#define PATH_MAX 128

/* how deep directory searches can nest */
#define GEM_MAX_SEARCHES 8
#define GEM_PATTERN_MAX 67
#define GEM_GDIR_MAX 67
#define GEM_MOUNT_NAME_MAX 32
#define GEM_VOLUME_LABEL_MAX 11
#define DOS_HEAP_BYTES 0x3000U

typedef struct gem_search {
	WORD used;
	LPVOID dta;
	DIR *dir;
	char dirpath[PATH_MAX];
	char pattern[GEM_PATTERN_MAX];
	WORD attr;
} GEM_SEARCH;

/* only fields copied out of native stat; time and date are the packed
 * DOS fields documented in dos.h */
typedef struct dos_stat_view {
	UWORD mode;
	GEM_U32_WORDS size;
	UWORD time;
	UWORD date;
} DOS_STAT_VIEW;

/* ELKS reports f_blocks and f_bavail in plain 1 KiB blocks */
typedef struct dos_filesystem_view {
	GEM_U32_WORDS total_blocks;
	GEM_U32_WORDS available_blocks;
	BYTE mount_name[GEM_MOUNT_NAME_MAX];
} DOS_FILESYSTEM_VIEW;

/* exact ELKS <arch/statfs.h> wire layout, four-byte fields as word
 * pairs. dev_t is one 16-bit word on ELKS/8086 */
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

extern int ustatfs(dev_t device, DOS_ELKS_STATFS * information, int flags);

/* exact ELKS utime wire image: access time then modification time */
typedef struct dos_elks_utimbuf {
	GEM_U32_WORDS access_time;
	GEM_U32_WORDS modification_time;
} DOS_ELKS_UTIMBUF;

extern int utime(const char *path, DOS_ELKS_UTIMBUF * times);

/* pre-scaled packed date/time field tables */
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

/* second spans for months 1 through 12, entry zero unused */
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

/* DOS_AX holds the last error/status word the Desktop reads back. every
 * other emulated DOS register is gone */
#define DOS_AX DR.x.ax

static LPVOID current_dta;
static FCB fallback_dta;
static GEM_SEARCH searches[GEM_MAX_SEARCHES];
static UWORD dos_current_drive;

/* setting a file's date/time works by handle, but ELKS utime() wants the
 * name. the Desktop only sets it on the file it just opened, so one
 * pathname slot is enough; any other handle gets E_BADHANDLE */
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
	/* the Desktop checks for E_NOACCESS before merging a copied folder,
	 * so map an existing dest to that */
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

/* syscall result to GEM register convention: on failure AX gets a GEM
 * error number and DOS_ERR goes true, handles and counts pass through */
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

/* copy a NUL-terminated component without silent truncation */
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
dos_split_search(const BYTE *specification, char *directory, char *pattern)
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

/* case-sensitive '*' and '?' matching; retry_pattern/retry_name remember
 * the last star position instead of recursing */
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
	for (used = 0; destination[used]; used++);
	if (used && destination[used - 1U] != '/') {
		if (used + 1U >= PATH_MAX)
			return FALSE;
		destination[used++] = '/';
		destination[used] = '\0';
	}
	return dos_copy_text(destination + used, PATH_MAX - used, name);
}

/* last pathname component without touching the path */
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

/* copy an unsigned native ABI field into a low/high word pair. host
 * build fields can be wider than four bytes; nonzero upper bytes saturate
 * instead of wrapping */

/* add one word pair, saturating at 0xffff:0xffff on overflow */
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

/* double a word pair COUNT times, overflow saturates. disk counts use
 * count 10: ELKS statfs reports 1 KiB blocks and Desktop shows bytes */
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

/* query the filesystem holding the current POSIX directory */
static WORD
dos_filesystem_query(DOS_FILESYSTEM_VIEW *view)
{
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
	memcpy(view->mount_name, native.mount_name, sizeof(view->mount_name));
	view->mount_name[sizeof(view->mount_name) - 1U] = '\0';
	return TRUE;
}

/* plain byte totals in the GEM four-byte fields */
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
	/* f_blocks/f_bavail are in 1 KiB blocks */
	(void) dos_words_shift_left(total, 10U);
	(void) dos_words_shift_left(available, 10U);
	return TRUE;
}

/* POSIX mount identity onto the eleven-char label field. the root mount
 * has no basename so ROOT stands in; names too long fail rather than
 * being cut short */
static WORD
dos_mount_label(const DOS_FILESYSTEM_VIEW *view, BYTE *label)
{
	const BYTE *source;
	const BYTE *scan;
	UWORD used;

	if (!view || !label || !view->mount_name[0])
		return FALSE;
	if (view->mount_name[0] == '/' && view->mount_name[1] == '\0')
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

/* the one FCB op Desktop still uses: read the volume identity */
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

/* subtract SPAN from VALUE if VALUE is big enough, plain seconds */
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

static UWORD
dos_word_shift_right(UWORD value, UWORD count)
{
	while (count--)
		value >>= 1;
	return value;
}

/* packed DOS date/time to unsigned epoch seconds. bad calendar fields
 * fail with no partial timestamp; values past 0xffff:0xffff saturate at
 * the ELKS unsigned time_t boundary */
static WORD
dos_unpack_clock(UWORD packed_time, UWORD packed_date, GEM_U32_WORDS *seconds)
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

/* apply a packed GEM time to the one dest the Desktop copy created. fstat
 * keeps the access time, utime changes only the requested mod time */
static WORD
dos_set_handle_clock(WORD handle, UWORD packed_time, UWORD packed_date)
{
	struct stat native;
	DOS_ELKS_UTIMBUF times;
	GEM_U32_WORDS modified;
	GEM_U32_WORDS accessed;
	const UBYTE *bytes;

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
	memset(&times, 0, sizeof(times));
	times.access_time = accessed;
	times.modification_time = modified;
	return (WORD) utime((const char *) dos_handle_path, &times);
}

/* pack already-checked calendar fields, odd seconds round down by one */
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

/* native epoch seconds to packed DOS UTC time. a wider host build value
 * saturates at an endpoint instead of being cut short */
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
		span.lo = (year & 3U) == 0 && year != 2100U ? 0x8500U : 0x3380U;
		span.hi = (year & 3U) == 0 && year != 2100U ? 0x01e2U : 0x01e1U;
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
	for (day = 1U; dos_take_span(&remaining, &span); day++);
	span.lo = 3600U;
	span.hi = 0;
	for (hour = 0; dos_take_span(&remaining, &span); hour++);
	span.lo = 60U;
	for (minute = 0; dos_take_span(&remaining, &span); minute++);
	dos_pack_clock(view, year, month, day, hour, minute, remaining.lo);
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

/* every pathname syscall from one translated buffer. RESULT stays the
 * native zero/handle/-1 value, dos_finish_syscall() turns it into GEM
 * error state once */
static WORD
dos_path_syscall(UWORD function, const char *source, const char *newname,
	UWORD *attr)
{
	char path[PATH_MAX];
	char newpath[PATH_MAX];
	struct stat native;
	UBYTE current_attr;
	UWORD mode;
	WORD flags;
	WORD result;

	if (!source || !*source
		|| !dos_copy_text(path, (UWORD) sizeof(path), source)) {
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
		if (*attr & (F_SYSTEM | F_VOLUME | F_SUBDIR)) {
			errno = EACCES;
			return FAILURE;
		}
		if ((*attr & F_HIDDEN)
			&& dos_path_basename(path)[0] != '.') {
			errno = EACCES;
			return FAILURE;
		}
		mode = (*attr & F_RDONLY) ? 0444U : 0666U;
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
			*attr = dos_file_attr(path, mode);
			return 0;
		}
		current_attr = dos_file_attr(path, mode);
		if (((UBYTE) *attr & (F_HIDDEN | F_SYSTEM | F_SUBDIR
					| F_ARCHIVE))
			!= (current_attr & (F_HIDDEN | F_SYSTEM | F_SUBDIR
					| F_ARCHIVE))) {
			errno = EACCES;
			return FAILURE;
		}
		if (*attr & F_RDONLY)
			mode &= (UWORD) ~(S_IWUSR | S_IWGRP | S_IWOTH);
		else
			mode |= S_IWUSR;
		return (WORD) chmod(path, (mode_t) mode);
	case 0x5600:
		if (!newname || !*newname
			|| !dos_copy_text(newpath, (UWORD) sizeof(newpath),
				newname)) {
			errno = ENOENT;
			return FAILURE;
		}
		result = (WORD) rename(path, newpath);
		if (!result && dos_path_handle != FAILURE
			&& !strcmp(path, dos_handle_path))
			(void) dos_copy_text(dos_handle_path,
				(UWORD) sizeof(dos_handle_path), newpath);
		return result;
	default:
		errno = ENOENT;
		return FAILURE;
	}
}

/* ELKS ssize_t is signed 16-bit, so one read/write is capped at 0x7fff.
 * an error after some bytes moved returns the completed prefix as success */
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

/* off_t is the four-byte ELKS lseek ABI, the union keeps it at the
 * syscall boundary. output above the GEM four-byte field saturates */
typedef union dos_seek_boundary {
	off_t native;
	GEM_U32_WORDS words;
} DOS_SEEK_BOUNDARY;

static GEM_U32_WORDS
dos_seek(WORD handle, UWORD smode, GEM_U32_WORDS offset)
{
	DOS_SEEK_BOUNDARY input;
	DOS_SEEK_BOUNDARY output;
	GEM_U32_WORDS result;
#if !ELKS
	const UBYTE *bytes;
	UWORD index;
#endif

	memset(&input, (offset.hi & 0x8000U) ? 0xff : 0, sizeof(input));
	input.words = offset;
	errno = 0;
	output.native = lseek(handle, input.native, (WORD) (smode & 0x00ffU));
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

/* the dos_* wrappers start here */
WORD
dos_chdir(LPBYTE pdrvpath)
{
	dos_finish_syscall(dos_path_syscall(0x3b00U,
			(const char *) pdrvpath, (const char *) 0,
			(UWORD *) 0));
	return !DOS_ERR;
}

WORD
dos_gdir(WORD drive, LPBYTE pdrvpath)
{
	WORD result;

	if ((UWORD) drive > 26U) {
		errno = ENODEV;
		result = FAILURE;
	} else if (!pdrvpath) {
		errno = EFAULT;
		result = FAILURE;
	} else
		result = getcwd((char *) pdrvpath, GEM_GDIR_MAX)
			? 0 : FAILURE;
	dos_finish_syscall(result);
	return !DOS_ERR;
}

WORD
dos_gdrv(VOID)
{
	dos_set_error(0);
	return (WORD) (dos_current_drive & 0x00ffU);
}

WORD
dos_sdrv(WORD newdrv)
{
	/* Desktop presentation state, not a mount operation */
	if ((UWORD) newdrv < 26U) {
		dos_current_drive = (UWORD) newdrv;
		dos_set_error(0);
	} else
		dos_set_error(E_BADDRIVE);
	return !DOS_ERR;
}

WORD
dos_vlabel(LPVOID fcb)
{
	(void) dos_fcb_volume_first((const UBYTE *) fcb);
	return !DOS_ERR;
}

WORD
dos_sdta(LPVOID ldta)
{
	current_dta = ldta;
	dos_set_error(0);
	return TRUE;
}

WORD
dos_sfirst(LPBYTE pspec, WORD attr)
{
	(void) dos_search_first(pspec, attr);
	return !DOS_ERR;
}

WORD
dos_snext(VOID)
{
	(void) dos_search_next(dos_search_slot(current_dta, FALSE));
	return !DOS_ERR;
}

WORD
dos_create(LPBYTE pname, WORD attr)
{
	UWORD attr_word;

	attr_word = (UWORD) attr;
	dos_finish_syscall(dos_path_syscall(0x3c00U,
			(const char *) pname, (const char *) 0, &attr_word));
	return (WORD) DOS_AX;
}

WORD
dos_open(LPBYTE pname, WORD access)
{
	dos_finish_syscall(dos_path_syscall((UWORD) (0x3d00 + access),
			(const char *) pname, (const char *) 0, (UWORD *) 0));
	return (WORD) DOS_AX;
}

WORD
dos_close(WORD handle)
{
	WORD result;

	result = (WORD) close(handle);
	if (!result && handle == dos_path_handle) {
		dos_path_handle = FAILURE;
		dos_handle_path[0] = '\0';
	}
	dos_finish_syscall(result);
	return !DOS_ERR;
}

WORD
dos_delete(LPBYTE ppath)
{
	dos_finish_syscall(dos_path_syscall(0x4100U,
			(const char *) ppath, (const char *) 0, (UWORD *) 0));
	return !DOS_ERR;
}

GEM_U32_WORDS
dos_read(WORD handle, GEM_U32_WORDS cnt, LPBYTE pbuffer)
{
	GEM_U32_WORDS moved;

	moved = dos_zero_words;
	if (cnt.hi) {
		dos_set_error(E_BADDATA);
		return moved;
	}
	moved.lo = dos_io(handle, cnt.lo, pbuffer, FALSE);
	return moved;
}

GEM_U32_WORDS
dos_write(WORD handle, GEM_U32_WORDS cnt, LPBYTE pbuffer)
{
	GEM_U32_WORDS moved;

	moved = dos_zero_words;
	if (cnt.hi) {
		dos_set_error(E_BADDATA);
		return moved;
	}
	moved.lo = dos_io(handle, cnt.lo, pbuffer, TRUE);
	return moved;
}

GEM_U32_WORDS
dos_lseek(WORD handle, WORD smode, GEM_U32_WORDS sofst)
{
	return dos_seek(handle, (UWORD) smode, sofst);
}

WORD
dos_wait(VOID)
{
	WORD result;
	int status;

	do {
		result = (WORD) wait(&status);
	} while (result < 0 && errno == EINTR);
	if (result < 0) {
		dos_finish_syscall(result);
		return (WORD) DOS_AX;
	}
	dos_set_error(0);
	return (WORD) status;
}

/* dos_alloc/dos_free are plain malloc/free */
LPVOID
dos_alloc(GEM_U32_WORDS nbytes)
{
	LPVOID maddr;

	if (nbytes.hi || nbytes.lo == 0xffffU) {
		dos_set_error(E_NOMEMORY);
		return NULL;
	}
	maddr = malloc(nbytes.lo ? nbytes.lo : 1U);
	if (!maddr) {
		dos_set_error(E_NOMEMORY);
		return NULL;
	}
	dos_set_error(0);
	return maddr;
}

GEM_U32_WORDS
dos_avail(VOID)
{
	GEM_U32_WORDS available;

	/* ELKS malloc has no remaining-space query, so report a fixed figure
	 * the Desktop can size its scratch requests against */
	available.lo = DOS_HEAP_BYTES;
	available.hi = 0;
	dos_set_error(0);
	return available;
}

WORD
dos_free(LPVOID maddr)
{
	if (maddr)
		free(maddr);
	dos_set_error(0);
	return TRUE;
}

WORD
dos_space(WORD drv, GEM_U32_WORDS *ptotal, GEM_U32_WORDS *pavail)
{
	GEM_U32_WORDS total;
	GEM_U32_WORDS available;

	total = dos_zero_words;
	available = dos_zero_words;
	if ((UWORD) drv > 26U)
		dos_set_error(E_BADDRIVE);
	else if (!dos_filesystem_space(&total, &available))
		dos_set_error(dos_error_from_errno(errno));
	else
		dos_set_error(0);
	if (ptotal)
		*ptotal = total;
	if (pavail)
		*pavail = available;
	return !DOS_ERR;
}

WORD
dos_rmdir(LPBYTE ppath)
{
	dos_finish_syscall(dos_path_syscall(0x3a00U,
			(const char *) ppath, (const char *) 0, (UWORD *) 0));
	return !DOS_ERR;
}

WORD
dos_mkdir(LPBYTE ppath)
{
	dos_finish_syscall(dos_path_syscall(0x3900U,
			(const char *) ppath, (const char *) 0, (UWORD *) 0));
	return !DOS_ERR;
}

WORD
dos_rename(LPBYTE poname, LPBYTE pnname)
{
	dos_finish_syscall(dos_path_syscall(0x5600U,
			(const char *) poname, (const char *) pnname,
			(UWORD *) 0));
	return !DOS_ERR;
}

WORD
dos_chmod(LPBYTE pname, WORD func, WORD attr)
{
	UWORD attr_word;

	attr_word = (UWORD) attr;
	dos_finish_syscall(dos_path_syscall((UWORD) (0x4300 + func),
			(const char *) pname, (const char *) 0, &attr_word));
	if (DOS_ERR)
		return FAILURE;
	return (WORD) attr_word;
}

WORD
dos_setdt(WORD handle, WORD time, WORD date)
{
	dos_finish_syscall(dos_set_handle_clock(handle,
			(UWORD) time, (UWORD) date));
	return !DOS_ERR;
}

WORD
dos_dtype(WORD drive)
{
	DOS_FILESYSTEM_VIEW filesystem;

	/* one letter names the current POSIX filesystem, a local hard fs
	 * (type 1); every other valid letter is absent so autodetection cant
	 * invent twenty-six copies of the same mount */
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

/* ELKS extension: true when an absolute path names a regular file with
 * an execute bit, so double-clicking an executable launches it. we test
 * the mode bits not access(X_OK) since Desktop runs as root and ELKS
 * grants root execute access to every file */
WORD
dos_executable(LPBYTE path)
{
	struct stat native;
	UWORD mode;

	if (!path || !*path)
		return FALSE;
	if (stat((const char *) path, &native) != 0)
		return FALSE;
	if (!S_ISREG(native.st_mode))
		return FALSE;
	mode = (UWORD) native.st_mode;
	return (mode & (UWORD) (S_IXUSR | S_IXGRP | S_IXOTH)) != 0;
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
		outregs->x.ax = E_BADFUNC;
		outregs->x.cflag = 1;
	}
	return E_BADFUNC;
}
