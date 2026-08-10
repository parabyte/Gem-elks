/*
 * gem_fsel_resident.c - AES file selector
 *
 * shows the file box and runs it one step at a time - gem_form_begin_fsel()
 * arms the first pass and gem_form_fsel_progress() runs one step each time the
 * form settles, till OK or Cancel
 *
 * tree is FSELECTR out of the AES's own GEM.RSC
 *
 * dragging the elevator cant nest in a held form, so a press anywhere on the
 * slider pages by a windowful, and a scroll redraws the whole file box, the
 * list is 9 lines
 */

#include "gem_form_internal.h"

#include "gem_far_resource.h"
#include "gem_resident_memory.h"
#include "gem_system_resource.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#define GEM_FORM_FSEL_MAX_FILES  100U
#define GEM_FORM_FSEL_NAME_BYTES 16U
#define GEM_FORM_FSEL_PATH_MAX   80U
#define GEM_FORM_FSEL_SPEC_MAX   16U

/* one AES-wide selector */
static UBYTE gem_form_fsel_active;

static WORD gem_form_fsel_last_y;

static GEM_FORM_CONTEXT gem_form_fsel_context;

static GEM_FAR_RESOURCE gem_form_fsel_storage;

static UWORD gem_form_fsel_offset[GEM_FORM_FSEL_MAX_FILES];

static WORD gem_form_fsel_count;

static WORD gem_form_fsel_top;

static WORD gem_form_fsel_selection;

static WORD gem_form_fsel_reread;

static BYTE gem_form_fsel_path[GEM_FORM_FSEL_PATH_MAX];

static BYTE gem_form_fsel_spec[GEM_FORM_FSEL_SPEC_MAX];

static BYTE gem_form_fsel_title[GEM_FORM_FSEL_SPEC_MAX + 4U];

static BYTE gem_form_fsel_scratch[GEM_FORM_FSEL_NAME_BYTES + 4U];

static GEM_BINDINGS_POINTER_SLOT gem_form_fsel_client_path;

static GEM_BINDINGS_POINTER_SLOT gem_form_fsel_client_name;

static UWORD
gem_form_fsel_length(const BYTE *text)
{
	UWORD length;

	length = 0;
	while (text[length])
		length++;
	return length;
}

static VOID
gem_form_fsel_copy(BYTE *destination, const BYTE *source, UWORD size)
{
	UWORD index;

	if (!size)
		return;
	for (index = 0; index + 1U < size && source[index]; index++)
		destination[index] = source[index];
	destination[index] = 0;
}

/* write one string into an editable field's TEDINFO text */
static WORD
gem_form_fsel_set_text(UWORD object_index, const BYTE *text)
{
	TEDINFO tedinfo;
	UBYTE GEM_FORM_FAR *target;
	UWORD limit;
	UWORD index;

	if (!gem_form_tedinfo(&gem_form_fsel_context, object_index, &tedinfo)
		|| tedinfo.te_txtlen <= 0)
		return FALSE;
	limit = (UWORD) tedinfo.te_txtlen;
	target = (UBYTE GEM_FORM_FAR *) gem_form_pointer(&gem_form_fsel_context,
		tedinfo.te_ptext, limit);
	if (!target)
		return FALSE;
	for (index = 0; index + 1U < limit && text[index]; index++)
		target[index] = (UBYTE) text[index];
	target[index] = 0;
	return TRUE;
}

/* read one field's TEDINFO text back out */
static WORD
gem_form_fsel_get_text(UWORD object_index, BYTE *text, UWORD size)
{
	TEDINFO tedinfo;
	UBYTE GEM_FORM_FAR *source;
	UWORD limit;
	UWORD index;

	if (!size)
		return FALSE;
	text[0] = 0;
	if (!gem_form_tedinfo(&gem_form_fsel_context, object_index, &tedinfo)
		|| tedinfo.te_txtlen <= 0)
		return FALSE;
	limit = (UWORD) tedinfo.te_txtlen;
	source = (UBYTE GEM_FORM_FAR *) gem_form_pointer(&gem_form_fsel_context,
		tedinfo.te_ptext, limit);
	if (!source)
		return FALSE;
	for (index = 0; index + 1U < size && index < limit
		&& source[index]; index++)
		text[index] = (BYTE) source[index];
	text[index] = 0;
	return TRUE;
}

/* the file spec after the last separator */
static UWORD
gem_form_fsel_spec_offset(const BYTE *path)
{
	UWORD length;

	length = gem_form_fsel_length(path);
	while (length && path[length - 1U] != '/')
		length--;
	return length;
}

/*
 * the list shows names in a fixed 8 plus 3 layout, so the stem is padded to 8
 * before the extension, padding comes off on the way back to the caller
 */
static VOID
gem_form_fsel_format_name(const BYTE *name, BYTE *out, UWORD size)
{
	UWORD in_index;
	UWORD out_index;
	UWORD stop;

	out_index = 0;
	for (in_index = 0; name[in_index] && name[in_index] != '.'
		&& out_index + 1U < size; in_index++)
		out[out_index++] = name[in_index];
	if (name[in_index] == '.') {
		stop = in_index;
		while (stop < 8U && out_index + 1U < size) {
			out[out_index++] = ' ';
			stop++;
		}
		in_index++;
		while (name[in_index] && out_index + 1U < size)
			out[out_index++] = name[in_index++];
	}
	out[out_index] = 0;
}

static VOID
gem_form_fsel_unformat_name(const BYTE *text, BYTE *out, UWORD size)
{
	UWORD index;
	UWORD out_index;

	out_index = 0;
	for (index = 0; index < 8U && text[index]
		&& out_index + 1U < size; index++)
		if (text[index] != ' ')
			out[out_index++] = text[index];
	if (text[index] && out_index + 1U < size) {
		out[out_index++] = '.';
		while (text[index] && out_index + 1U < size)
			out[out_index++] = text[index++];
	}
	out[out_index] = 0;
}

/* DOS '*' and '?' matching */
static WORD
gem_form_fsel_wildcard(const BYTE *pattern, const BYTE *name)
{
	while (*pattern) {
		if (*pattern == '*') {
			pattern++;
			if (!*pattern)
				return TRUE;
			while (*name) {
				if (gem_form_fsel_wildcard(pattern, name))
					return TRUE;
				name++;
			}
			return gem_form_fsel_wildcard(pattern, name);
		}
		if (!*name)
			return FALSE;
		if (*pattern != '?' && *pattern != *name)
			return FALSE;
		pattern++;
		name++;
	}
	return !*name;
}

/* read one stored entry: a marker byte then the plain name */
static VOID
gem_form_fsel_entry(WORD index, BYTE *out, UWORD size)
{
	UWORD position;

	out[0] = 0;
	if (index < 0 || index >= gem_form_fsel_count
		|| !gem_form_fsel_storage.base.hi)
		return;
	if (size > GEM_FORM_FSEL_NAME_BYTES)
		size = GEM_FORM_FSEL_NAME_BYTES;
	gem_resident_memory_from(gem_form_fsel_storage.base.hi,
		gem_form_fsel_offset[index], (UBYTE *) out, size);
	out[size - 1U] = 0;
	for (position = 0; position < size; position++)
		if (!out[position])
			return;
}

/* folders sort before files, folder marker is 0x07 and a file's is a space,
 * within a class its a plain compare */
static WORD
gem_form_fsel_compare(WORD left, WORD right)
{
	BYTE first[GEM_FORM_FSEL_NAME_BYTES];
	BYTE second[GEM_FORM_FSEL_NAME_BYTES];
	UWORD index;

	gem_form_fsel_entry(left, first, sizeof(first));
	gem_form_fsel_entry(right, second, sizeof(second));
	for (index = 0; index < GEM_FORM_FSEL_NAME_BYTES; index++) {
		if (first[index] != second[index])
			return (WORD) ((UBYTE) first[index])
				- (WORD) ((UBYTE) second[index]);
		if (!first[index])
			break;
	}
	return 0;
}

/* read the directory, keep folders and files matching the spec, then shell
 * sort the list */
static WORD
gem_form_fsel_read(void)
{
	struct dirent entry;
	struct stat status;
	BYTE full[GEM_FORM_FSEL_PATH_MAX + GEM_FORM_FSEL_NAME_BYTES];
	BYTE record[GEM_FORM_FSEL_NAME_BYTES];
	UWORD directory_length;
	UWORD used;
	WORD descriptor;
	WORD gap;
	WORD outer;
	WORD inner;
	WORD swap;

	gem_form_fsel_count = 0;
	gem_form_fsel_top = 0;
	gem_form_fsel_selection = 0;
	if (!gem_form_fsel_storage.base.hi)
		return FALSE;

	/* working path is "<directory>/<spec>", cut back to the directory, keep a
	 * bare "/" whole */
	gem_form_fsel_copy(full, gem_form_fsel_path, sizeof(full));
	directory_length = gem_form_fsel_spec_offset(full);
	while (directory_length > 1U && full[directory_length - 1U] == '/')
		directory_length--;
	full[directory_length] = 0;
	if (!directory_length) {
		full[0] = '/';
		full[1] = 0;
		directory_length = 1U;
	}
	descriptor = (WORD) open((const char *) full, O_RDONLY);
	if (descriptor < 0)
		return FALSE;

	used = 0;
	while (_readdir(descriptor, &entry, 1) > 0) {
		if (entry.d_name[0] == '.')
			continue;
		if (used + GEM_FORM_FSEL_NAME_BYTES
			> gem_form_fsel_storage.bytes
			|| (UWORD) gem_form_fsel_count >=
			GEM_FORM_FSEL_MAX_FILES)
			break;
		gem_form_fsel_copy(full + directory_length + 1U,
			(const BYTE *) entry.d_name,
			(UWORD) (sizeof(full) - directory_length - 1U));
		full[directory_length] = '/';
		record[0] = ' ';
		if (stat((const char *) full, &status) == 0
			&& S_ISDIR(status.st_mode))
			record[0] = 0x07;
		if (record[0] == ' '
			&& !gem_form_fsel_wildcard(gem_form_fsel_spec,
				(const BYTE *) entry.d_name))
			continue;
		gem_form_fsel_copy(record + 1, (const BYTE *) entry.d_name,
			GEM_FORM_FSEL_NAME_BYTES - 1U);
		if (!gem_far_resource_copy_in(&gem_form_fsel_storage, used,
				(const UBYTE *) record,
				GEM_FORM_FSEL_NAME_BYTES))
			break;
		gem_form_fsel_offset[gem_form_fsel_count] = used;
		used = (UWORD) (used + GEM_FORM_FSEL_NAME_BYTES);
		gem_form_fsel_count++;
	}
	(void) close(descriptor);

	for (gap = gem_form_fsel_count / 2; gap > 0; gap /= 2)
		for (outer = gap; outer < gem_form_fsel_count; outer++)
			for (inner = outer - gap; inner >= 0; inner -= gap) {
				if (gem_form_fsel_compare(inner, inner + gap)
					<= 0)
					break;
				swap = (WORD) gem_form_fsel_offset[inner];
				gem_form_fsel_offset[inner] =
					gem_form_fsel_offset[inner + gap];
				gem_form_fsel_offset[inner + gap] =
					(UWORD) swap;
			}
	return TRUE;
}

/* point the 9 G_FBOXTEXT lines at the names visible from the current scroll
 * position, then size and place the elevator */
static VOID
gem_form_fsel_format(WORD top)
{
	BYTE entry[GEM_FORM_FSEL_NAME_BYTES];
	BYTE shown[GEM_FORM_FSEL_NAME_BYTES + 2U];
	OBJECT GEM_FORM_FAR *object;
	unsigned long product;
	UWORD index;
	WORD visible;
	WORD height;
	WORD track;
	WORD y;

	if (top < 0)
		top = 0;
	gem_form_fsel_top = top;
	visible = gem_form_fsel_count - top;
	if (visible > GEM_SYSTEM_FSEL_NAMES)
		visible = GEM_SYSTEM_FSEL_NAMES;
	for (index = 0; index < GEM_SYSTEM_FSEL_NAMES; index++) {
		if ((WORD) index < visible) {
			gem_form_fsel_entry((WORD) (top + (WORD) index), entry,
				sizeof(entry));
			shown[0] = entry[0];
			gem_form_fsel_format_name(entry + 1, shown + 1,
				(UWORD) (sizeof(shown) - 1U));
		} else {
			shown[0] = ' ';
			shown[1] = 0;
		}
		(void) gem_form_fsel_set_text(
			(UWORD) (GEM_SYSTEM_F1NAME + (WORD) index), shown);
		object = gem_form_object_at(&gem_form_fsel_context,
			(UWORD) (GEM_SYSTEM_F1NAME + (WORD) index));
		object->ob_type = (object->ob_type & 0xff00U)
			| GEM_FORM_G_FBOXTEXT;
		object->ob_state = GEM_FORM_NORMAL;
	}

	object = gem_form_object_at(&gem_form_fsel_context, GEM_SYSTEM_FSVSLID);
	track = object->ob_height;
	height = track;
	y = 0;
	if (gem_form_fsel_count > GEM_SYSTEM_FSEL_NAMES) {
		product = (unsigned long) (UWORD) GEM_SYSTEM_FSEL_NAMES
			* (unsigned long) (UWORD) track;
		height = (WORD) (product
			/ (unsigned long) (UWORD) gem_form_fsel_count);
		if (height < gem_form_fsel_context.character_height / 2)
			height = (WORD)
				(gem_form_fsel_context.character_height / 2);
		product = (unsigned long) (UWORD) top
			* (unsigned long) (UWORD) (track - height);
		y = (WORD) (product / (unsigned long) (UWORD)
			(gem_form_fsel_count - GEM_SYSTEM_FSEL_NAMES));
	}
	object = gem_form_object_at(&gem_form_fsel_context, GEM_SYSTEM_FSVELEV);
	object->ob_y = y;
	object->ob_height = height;
}

/* scroll one line either way, clamped at both ends */
static WORD
gem_form_fsel_scroll_one(WORD current, WORD touched)
{
	WORD next;

	next = (touched == GEM_SYSTEM_FUPAROW) ? (WORD) (current - 1)
		: (WORD) (current + 1);
	if (next < 0)
		next++;
	if (gem_form_fsel_count - next < GEM_SYSTEM_FSEL_NAMES)
		next--;
	return (gem_form_fsel_count > GEM_SYSTEM_FSEL_NAMES) ? next : current;
}

/* mark or unmark one visible line */
static VOID
gem_form_fsel_select(WORD line, UWORD state)
{
	OBJECT GEM_FORM_FAR *object;

	if (line < 1 || line > GEM_SYSTEM_FSEL_NAMES)
		return;
	object = gem_form_object_at(&gem_form_fsel_context,
		(UWORD) (GEM_SYSTEM_F1NAME + line - 1));
	object->ob_state = state;
}

/* activate a path, format it, retitle the box */
static VOID
gem_form_fsel_newdir(void)
{
	UWORD offset;
	UWORD length;

	offset = gem_form_fsel_spec_offset(gem_form_fsel_path);
	gem_form_fsel_copy(gem_form_fsel_spec, gem_form_fsel_path + offset,
		sizeof(gem_form_fsel_spec));
	if (!gem_form_fsel_spec[0]) {
		gem_form_fsel_copy(gem_form_fsel_spec, (const BYTE *) "*.*",
			sizeof(gem_form_fsel_spec));
		gem_form_fsel_copy(gem_form_fsel_path + offset,
			gem_form_fsel_spec,
			(UWORD) (sizeof(gem_form_fsel_path) - offset));
	}
	(void) gem_form_fsel_read();

	/* box is titled with the spec */
	length = gem_form_fsel_length(gem_form_fsel_spec);
	if (length > GEM_FORM_FSEL_SPEC_MAX - 3U)
		length = GEM_FORM_FSEL_SPEC_MAX - 3U;
	gem_form_fsel_title[0] = ' ';
	gem_form_fsel_copy(gem_form_fsel_title + 1, gem_form_fsel_spec,
		(UWORD) (length + 1U));
	gem_form_fsel_title[length + 1U] = ' ';
	gem_form_fsel_title[length + 2U] = 0;
	(void) gem_form_fsel_set_text(GEM_SYSTEM_FTITLE, gem_form_fsel_title);
	(void) gem_form_fsel_set_text(GEM_SYSTEM_FSDIRECT, gem_form_fsel_path);
	gem_form_fsel_format(0);
}

/*
 * one step of the selector, TOUCHED is the object the form came back with, its
 * high bit still carries the double-click flag, returns TRUE to run the form
 * again, FALSE when OK or Cancel ended it
 */
static WORD
gem_form_fsel_step(WORD touched)
{
	BYTE entry[GEM_FORM_FSEL_NAME_BYTES + 2U];
	BYTE plain[GEM_FORM_FSEL_NAME_BYTES];
	OBJECT GEM_FORM_FAR *object;
	UWORD offset;
	WORD double_click;
	WORD line;
	WORD step;

	double_click = (touched & 0x4000) != 0;
	touched &= 0x3fff;
	step = 0;

	switch (touched) {
	case GEM_SYSTEM_FSOK:
	case GEM_SYSTEM_FSCANCEL:
		return FALSE;
	case GEM_SYSTEM_FUPAROW:
	case GEM_SYSTEM_FDNAROW:
		step = 1;
		break;
	case GEM_SYSTEM_FSVSLID:
	case GEM_SYSTEM_FSVELEV:
		/*
		 * dragging the slider cant nest in this held form, so a press on
		 * the slider just pages by a windowful either way
		 */
		object = gem_form_object_at(&gem_form_fsel_context,
			GEM_SYSTEM_FSVELEV);
		touched = (gem_form_fsel_last_y <= object->ob_y)
			? GEM_SYSTEM_FUPAROW : GEM_SYSTEM_FDNAROW;
		step = GEM_SYSTEM_FSEL_NAMES;
		break;
	case GEM_SYSTEM_FCLSBOX:
		/* drop one directory off the path */
		gem_form_fsel_path[gem_form_fsel_spec_offset
			(gem_form_fsel_path)] = 0;
		offset = gem_form_fsel_length(gem_form_fsel_path);
		while (offset > 1U && gem_form_fsel_path[offset - 1U] == '/')
			offset--;
		while (offset && gem_form_fsel_path[offset - 1U] != '/')
			offset--;
		gem_form_fsel_path[offset] = 0;
		if (!offset) {
			gem_form_fsel_path[0] = '/';
			gem_form_fsel_path[1] = 0;
		}
		gem_form_fsel_copy(gem_form_fsel_path
			+ gem_form_fsel_length(gem_form_fsel_path),
			gem_form_fsel_spec, (UWORD) (sizeof(gem_form_fsel_path)
				- gem_form_fsel_length(gem_form_fsel_path)));
		gem_form_fsel_reread = TRUE;
		break;
	case GEM_SYSTEM_FTITLE:
		gem_form_fsel_reread = TRUE;
		break;
	default:
		if (touched >= GEM_SYSTEM_F1NAME
			&& touched <= GEM_SYSTEM_F9NAME) {
			line = (WORD) (touched - GEM_SYSTEM_F1NAME + 1);
			if (gem_form_fsel_top + line > gem_form_fsel_count)
				break;
			if (gem_form_fsel_selection
				&& gem_form_fsel_selection != line)
				gem_form_fsel_select(gem_form_fsel_selection,
					GEM_FORM_NORMAL);
			gem_form_fsel_selection = line;
			gem_form_fsel_select(line, GEM_FORM_SELECTED);
			(void) gem_form_fsel_get_text((UWORD) touched, entry,
				sizeof(entry));
			gem_form_fsel_unformat_name(entry + 1, plain,
				sizeof(plain));
			if (entry[0] == ' ') {
				/* a file: it becomes the selection */
				(void) gem_form_fsel_set_text
					(GEM_SYSTEM_FSSELECT, plain);
				if (double_click)
					return FALSE;
			} else {
				/* a folder: descend into it */
				offset = gem_form_fsel_spec_offset
					(gem_form_fsel_path);
				gem_form_fsel_copy(gem_form_fsel_path + offset,
					plain, (UWORD)
					(sizeof(gem_form_fsel_path) - offset));
				offset = gem_form_fsel_length
					(gem_form_fsel_path);
				if (offset + 1U < sizeof(gem_form_fsel_path)) {
					gem_form_fsel_path[offset] = '/';
					gem_form_fsel_path[offset + 1U] = 0;
				}
				gem_form_fsel_copy(gem_form_fsel_path
					+
					gem_form_fsel_length
					(gem_form_fsel_path),
					gem_form_fsel_spec, (UWORD)
					(sizeof(gem_form_fsel_path)
						-
						gem_form_fsel_length
						(gem_form_fsel_path)));
				gem_form_fsel_reread = TRUE;
			}
		}
		break;
	}

	if (gem_form_fsel_reread) {
		gem_form_fsel_reread = FALSE;
		gem_form_fsel_newdir();
		return TRUE;
	}
	if (step) {
		line = gem_form_fsel_top;
		while (step--)
			line = gem_form_fsel_scroll_one(line, touched);
		if (line != gem_form_fsel_top) {
			gem_form_fsel_select(gem_form_fsel_selection,
				GEM_FORM_NORMAL);
			gem_form_fsel_selection = 0;
			gem_form_fsel_format(line);
		}
	}
	return TRUE;
}

/*
 * hand the caller's path and name back, the working path and the selection with
 * its display padding taken off
 */
VOID
gem_form_fsel_result(BYTE *path, UWORD path_size, BYTE *name, UWORD name_size)
{
	if (path && path_size)
		gem_form_fsel_copy(path, gem_form_fsel_path, path_size);
	if (name && name_size) {
		(void) gem_form_fsel_get_text(GEM_SYSTEM_FSSELECT,
			gem_form_fsel_scratch,
			(UWORD) sizeof(gem_form_fsel_scratch));
		gem_form_fsel_copy(name, gem_form_fsel_scratch, name_size);
	}
}

static VOID
gem_form_fsel_close(VOID)
{
	if (gem_form_fsel_storage.base.hi)
		(void) gem_far_resource_free(&gem_form_fsel_storage);
	gem_form_fsel_active = FALSE;
	gem_form_fsel_count = 0;
	gem_form_fsel_top = 0;
	gem_form_fsel_selection = 0;
}

/*
 * take the FSELECTR tree out of the AES's own resource, seed it from the
 * caller's path and name, read the directory, arm the first pass
 */
WORD
gem_form_begin_fsel(const GEM_FORM_CALL *call, GEM_FORM_EFFECTS *effects)
{
	GEM_FORM_CALL system_call;
	GEM_FAR_ADDRESS tree;
	GEM_FORM_PD *pd;
	OBJECT GEM_FORM_FAR *root;
	BYTE incoming[GEM_FORM_FSEL_NAME_BYTES];

	pd = gem_form_pd_at(call->owner);
	if (!pd || pd->state != GEM_FORM_PD_FREE || gem_form_fsel_active)
		return FALSE;
	if (!gem_system_resource_gaddr(R_TREE, GEM_SYSTEM_TREE_FSELECTOR,
			&tree) || !tree.hi)
		return FALSE;

	/* tree lives in the AES's own resource, so open the context against that
	 * not the caller's */
	system_call = *call;
	system_call.resource = gem_system_resource();
	if (!gem_form_open_tree(&gem_form_fsel_context, &system_call, tree))
		return FALSE;
	if (gem_form_fsel_context.object_count <= (UWORD) GEM_SYSTEM_F9NAME)
		return FALSE;

	/* both caller strings must be readable and writable in its DS */
	if (!call->addr_in[0].hi || call->addr_in[0].hi != call->client_segment
		|| !gem_resident_memory_range(call->addr_in[0].lo,
			GEM_FORM_FSEL_PATH_BYTES, call->client_limit)
		|| !call->addr_in[1].hi
		|| call->addr_in[1].hi != call->client_segment
		|| !gem_resident_memory_range(call->addr_in[1].lo,
			GEM_FORM_FSEL_NAME_BYTES_OUT, call->client_limit))
		return FALSE;
	gem_form_fsel_client_path = call->addr_in[0];
	gem_form_fsel_client_name = call->addr_in[1];

	gem_resident_memory_from(call->client_segment, call->addr_in[0].lo,
		(UBYTE *) gem_form_fsel_path,
		(UWORD) sizeof(gem_form_fsel_path));
	gem_form_fsel_path[sizeof(gem_form_fsel_path) - 1U] = 0;
	if (!gem_form_fsel_path[0])
		return FALSE;
	gem_resident_memory_from(call->client_segment, call->addr_in[1].lo,
		(UBYTE *) incoming, (UWORD) sizeof(incoming));
	incoming[sizeof(incoming) - 1U] = 0;

	if (!gem_far_resource_alloc(&gem_form_fsel_storage,
			(UWORD) (GEM_FORM_FSEL_MAX_FILES
				* GEM_FORM_FSEL_NAME_BYTES)))
		return FALSE;

	(void) gem_form_fsel_set_text(GEM_SYSTEM_FSSELECT, incoming);
	gem_form_fsel_reread = FALSE;
	gem_form_fsel_newdir();

	gem_form_clear_pd(pd);
	if (!gem_form_copy_context(&pd->context, &gem_form_fsel_context)) {
		gem_form_fsel_close();
		return FALSE;
	}
	pd->owner = (UBYTE) call->owner;
	pd->generation_lo = call->generation_lo;
	pd->generation_hi = call->generation_hi;
	pd->kind = GEM_FORM_KIND_FSEL;
	pd->tree_kind = GEM_FORM_TREE_SYSTEM;
	pd->state = GEM_FORM_PD_WAITING;
	pd->edit_object = GEM_FORM_ROOT;
	pd->next_object = GEM_FORM_ROOT;
	pd->saved_default = GEM_FORM_ROOT;
	(void) gem_form_switch_field(pd, GEM_SYSTEM_FSSELECT);
	gem_form_fsel_active = TRUE;

	root = gem_form_object_at(&gem_form_fsel_context, GEM_FORM_ROOT);
	gem_form_clear_effects(effects);
	effects->begin_update = TRUE;
	effects->draw_tree = TRUE;
	effects->tree_kind = GEM_FORM_TREE_SYSTEM;
	effects->tree.lo = tree.lo;
	effects->tree.hi = tree.hi;
	effects->rectangle.x = root->ob_x;
	effects->rectangle.y = root->ob_y;
	effects->rectangle.width = root->ob_width;
	effects->rectangle.height = root->ob_height;
	return TRUE;
}

/*
 * one completed pass, either it asks for another pass so the tree is redrawn
 * and the form re-armed, or OK/Cancel ends it and the parked call is left ready
 */
VOID
gem_form_fsel_progress(GEM_FORM_PD *pd, GEM_FORM_EFFECTS *effects)
{
	OBJECT GEM_FORM_FAR *root;

	if (!gem_form_fsel_step((WORD) pd->result))
		return;
	pd->state = GEM_FORM_PD_WAITING;
	pd->result = 0;
	pd->pressed_object = GEM_FORM_NIL;
	pd->edit_object = GEM_FORM_ROOT;
	pd->next_object = GEM_FORM_ROOT;
	(void) gem_form_switch_field(pd, GEM_SYSTEM_FSSELECT);

	root = gem_form_object_at(&pd->context, GEM_FORM_ROOT);
	gem_form_clear_effects(effects);
	effects->draw_tree = TRUE;
	effects->tree_kind = GEM_FORM_TREE_SYSTEM;
	effects->tree = pd->context.tree;
	effects->rectangle.x = root->ob_x;
	effects->rectangle.y = root->ob_y;
	effects->rectangle.width = root->ob_width;
	effects->rectangle.height = root->ob_height;
}

/* --- the seams gem_form_resident.c reaches the selector through --- */

VOID
gem_form_fsel_pointer(WORD y)
{
	gem_form_fsel_last_y = y;
}

VOID
gem_form_fsel_release(GEM_BINDINGS_POINTER_SLOT *path,
	GEM_BINDINGS_POINTER_SLOT *name)
{
	if (path)
		*path = gem_form_fsel_client_path;
	if (name)
		*name = gem_form_fsel_client_name;
	gem_form_fsel_active = FALSE;
}

VOID
gem_form_fsel_reset(VOID)
{
	gem_form_fsel_close();
}
