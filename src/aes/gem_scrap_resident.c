/*
 * gem_scrap_resident.c - AES Scrap Manager for resident ELKS GEM
 *
 * the clipboard on disk. checks for a scrap file of each of the six
 * types with stat(), deletes them with unlink().
 *
 * the scrap dir and file name both come from GEM.RSC so neither is
 * written here. theyre DOS paths so we drop the drive letter and flip
 * the separators, same path just spelled the ELKS way
 */

#include "gem_scrap_resident.h"

#include "gem_system_resource.h"

#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>

static BYTE gem_scrap_path[GEM_SCRAP_PATH_MAX];
static BYTE gem_scrap_name[GEM_SCRAP_PATH_MAX];

static const BYTE *const gem_scrap_type[GEM_SCRAP_TYPES] = {
	"CSV", "TXT", "GEM", "IMG", "DCA", "USR"
};

static const UWORD gem_scrap_bit[GEM_SCRAP_TYPES] = {
	GEM_SCRAP_CSV, GEM_SCRAP_TXT, GEM_SCRAP_GEM,
	GEM_SCRAP_IMG, GEM_SCRAP_DCA, GEM_SCRAP_USR
};

static UWORD
gem_scrap_length(const BYTE *text)
{
	UWORD length;

	length = 0;
	while (text[length])
		length++;
	return length;
}

/* build "<scrap dir>/SCRAP.<type>" into the caller's buffer; a null
 * type stops after the directory itself */
static WORD
gem_scrap_build(BYTE *buffer, UWORD size, const BYTE *type)
{
	UWORD position;
	UWORD index;

	position = 0;
	while (gem_scrap_path[position]) {
		if (position + 1U >= size)
			return FALSE;
		buffer[position] = gem_scrap_path[position];
		position++;
	}
	if (!type) {
		buffer[position] = 0;
		return TRUE;
	}
	for (index = 0; gem_scrap_name[index]; index++) {
		if (position + 1U >= size)
			return FALSE;
		buffer[position++] = gem_scrap_name[index];
	}
	for (index = 0; type[index]; index++) {
		if (position + 1U >= size)
			return FALSE;
		buffer[position++] = type[index];
	}
	buffer[position] = 0;
	return TRUE;
}

/* GEM roots its tree at GEMAPPS, ELKS keeps it under /lib, so a leading
 * /GEMAPPS (any case) becomes /lib in place */
static VOID
gem_scrap_remap_root(BYTE *path)
{
	UWORD i;
	static const BYTE root[] = "gemapps";

	if (path[0] != '/')
		return;
	for (i = 0; i < 7U; i++)
		if ((UBYTE) (path[1 + i] | 0x20) != (UBYTE) root[i])
			return;
	if (path[8] != '/' && path[8] != 0)
		return;
	i = 0;
	while (path[8 + i]) {
		path[4 + i] = path[8 + i];
		i++;
	}
	path[4 + i] = 0;
	path[0] = '/';
	path[1] = 'l';
	path[2] = 'i';
	path[3] = 'b';
}

/* turn a GEM.RSC DOS path into ELKS spelling: drop the drive letter,
 * flip the separators and move the GEMAPPS root under /lib */
static VOID
gem_scrap_unix_path(BYTE *text)
{
	UWORD index;
	UWORD out;

	index = 0;
	if (text[0] && text[1] == ':')
		index = 2;
	out = 0;
	while (text[index]) {
		text[out++] = (text[index] == '\\') ? '/' : text[index];
		index++;
	}
	text[out] = 0;
	gem_scrap_remap_root(text);
}

VOID
gem_scrap_resident_init(VOID)
{
	/* STSCDIR is the scrap dir, STSCRAP the file name stem */
	if (gem_system_resource_string(GEM_SYSTEM_STSCDIR, gem_scrap_path,
			GEM_SCRAP_PATH_MAX) < 0)
		gem_scrap_path[0] = 0;
	gem_scrap_unix_path(gem_scrap_path);
	if (gem_system_resource_string(GEM_SYSTEM_STSCRAP, gem_scrap_name,
			GEM_SCRAP_PATH_MAX) < 0)
		gem_scrap_name[0] = 0;
	gem_scrap_unix_path(gem_scrap_name);
}

/* one pass over the six scrap types; read collects which ones exist,
 * clear deletes what it finds and always reports success */
static WORD
gem_scrap_walk(WORD is_read)
{
	BYTE name[GEM_SCRAP_PATH_MAX + 16U];
	struct stat status;
	UWORD vector;
	UWORD index;

	vector = 0;
	if (!gem_scrap_path[0] || !gem_scrap_name[0])
		return is_read ? 0 : FALSE;
	for (index = 0; index < GEM_SCRAP_TYPES; index++) {
		if (!gem_scrap_build(name, sizeof(name), gem_scrap_type[index]))
			continue;
		if (stat((const char *) name, &status) != 0)
			continue;
		if (is_read)
			vector |= gem_scrap_bit[index];
		else
			(void) unlink((const char *) name);
	}
	return is_read ? (WORD) vector : TRUE;
}

WORD
gem_scrap_resident_read(BYTE *path, UWORD size)
{
	UWORD length;

	if (!path || !size || !gem_scrap_path[0])
		return 0;
	length = gem_scrap_length(gem_scrap_path);
	if (length + 2U > size)
		return 0;
	for (size = 0; size < length; size++)
		path[size] = gem_scrap_path[size];
	/* add a trailing separator so callers can tack a name on */
	path[length] = '/';
	path[length + 1U] = 0;
	return gem_scrap_walk(TRUE);
}

WORD
gem_scrap_resident_write(const BYTE *path)
{
	struct stat status;
	BYTE candidate[GEM_SCRAP_PATH_MAX];
	UWORD length;

	if (!path || !path[0])
		return FALSE;
	length = 0;
	while (path[length]) {
		if (length + 1U >= GEM_SCRAP_PATH_MAX)
			return FALSE;
		candidate[length] = path[length];
		length++;
	}
	/* drop the trailing separator */
	while (length > 1U && candidate[length - 1U] == '/')
		length--;
	candidate[length] = 0;
	if (stat((const char *) candidate, &status) != 0)
		return FALSE;
	if (!S_ISDIR(status.st_mode)) {
		errno = ENOTDIR;
		return FALSE;
	}
	for (length = 0; length < GEM_SCRAP_PATH_MAX; length++) {
		gem_scrap_path[length] = candidate[length];
		if (!candidate[length])
			break;
	}
	gem_scrap_path[GEM_SCRAP_PATH_MAX - 1U] = 0;
	return TRUE;
}

WORD
gem_scrap_resident_clear(VOID)
{
	return gem_scrap_walk(FALSE);
}
