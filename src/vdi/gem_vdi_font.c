/*
 * gem_vdi_font.c - GEM font chain and .FNT loading for the ELKS VDI
 *
 * loads *.FNT files off disk, one record per face/size/style, ordered face
 * then size then style with the system font always at the head
 */

#include "gem_vdi_font.h"

#include "gem_far_resource.h"
#include "gem_resident_memory.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

/* fields in the on-disk GEM font header */
#define GEM_VDI_FNT_HEADER_BYTES	88
#define GEM_VDI_FNT_ID			0
#define GEM_VDI_FNT_POINT		2
#define GEM_VDI_FNT_NAME		4
#define GEM_VDI_FNT_FIRST_ADE		36
#define GEM_VDI_FNT_LAST_ADE		38
#define GEM_VDI_FNT_TOP			40
#define GEM_VDI_FNT_ASCENT		42
#define GEM_VDI_FNT_HALF		44
#define GEM_VDI_FNT_DESCENT		46
#define GEM_VDI_FNT_BOTTOM		48
#define GEM_VDI_FNT_MAX_CHAR_WIDTH	50
#define GEM_VDI_FNT_MAX_CELL_WIDTH	52
#define GEM_VDI_FNT_LEFT_OFFSET		54
#define GEM_VDI_FNT_RIGHT_OFFSET	56
#define GEM_VDI_FNT_THICKEN		58
#define GEM_VDI_FNT_UL_SIZE		60
#define GEM_VDI_FNT_LIGHTEN		62
#define GEM_VDI_FNT_SKEW		64
#define GEM_VDI_FNT_FLAGS		66
#define GEM_VDI_FNT_HOR_TABLE		68
#define GEM_VDI_FNT_OFF_TABLE		72
#define GEM_VDI_FNT_DAT_TABLE		76
#define GEM_VDI_FNT_FORM_WIDTH		80
#define GEM_VDI_FNT_FORM_HEIGHT		82

/* one far allocation backs one .FNT file */
#define GEM_VDI_FONT_FILES	8
#define GEM_VDI_FONT_NO_FILE	0xffffU

/* file reads land here a chunk at a time before going to far memory */
#define GEM_VDI_FONT_CHUNK	256U

static GEM_VDI_FONT gem_vdi_font_table[GEM_VDI_FONTS];
static UWORD gem_vdi_font_used;

static BYTE gem_vdi_font_face_table[GEM_VDI_FACES][GEM_VDI_FONT_NAME];
static UWORD gem_vdi_font_face_id[GEM_VDI_FACES];
static UWORD gem_vdi_font_faces;

static GEM_FAR_RESOURCE gem_vdi_font_file[GEM_VDI_FONT_FILES];

static UBYTE gem_vdi_font_scratch[GEM_VDI_FONT_CHUNK];
static BYTE gem_vdi_font_directory[GEM_VDI_FONT_DIRECTORY_MAX];

/*
 * system font metrics for each cell height: 8x16 on VGA, 8x14 on EGA and
 * Hercules, 8x8 on CGA, all face 1 "System" ten point, fields in order are
 * top, ascent, half, descent, bottom, max_char_width, max_cell_width,
 * left_offset, right_offset, thicken, ul_size
 */
static const UWORD gem_vdi_font_system_16[] = {
	13, 12, 6, 2, 2, 7, 8, 1, 5, 1, 1
};

static const UWORD gem_vdi_font_system_14[] = {
	10, 8, 5, 2, 3, 7, 8, 1, 5, 1, 1
};

static const UWORD gem_vdi_font_system_8[] = {
	6, 6, 4, 1, 1, 7, 8, 0, 3, 1, 1
};

static VOID
gem_vdi_font_zero(VOID *destination, UWORD count)
{
	UBYTE *bytes;

	bytes = (UBYTE *) destination;
	while (count--)
		*bytes++ = 0;
}

static UWORD
gem_vdi_font_word(const UBYTE *bytes)
{
	UWORD value;

	value = bytes[1];
	value <<= 8;
	value |= bytes[0];
	return value;
}

/*
 * a .FNT table pointer is a 32-bit file offset, fonts that fit one far
 * segment never use the high half, one that dont is rejected by the caller
 */
static UWORD
gem_vdi_font_long_low(const UBYTE *bytes)
{
	return gem_vdi_font_word(bytes);
}

static UWORD
gem_vdi_font_long_high(const UBYTE *bytes)
{
	return gem_vdi_font_word(bytes + 2);
}

GEM_VDI_FONT *
gem_vdi_font_first(VOID)
{
	if (!gem_vdi_font_used)
		return (GEM_VDI_FONT *) 0;
	return &gem_vdi_font_table[0];
}

GEM_VDI_FONT *
gem_vdi_font_next(const GEM_VDI_FONT *font)
{
	UWORD index;

	if (!font)
		return (GEM_VDI_FONT *) 0;
	index = (UWORD) (font - &gem_vdi_font_table[0]);
	index++;
	if (index >= gem_vdi_font_used)
		return (GEM_VDI_FONT *) 0;
	return &gem_vdi_font_table[index];
}

UWORD
gem_vdi_font_face_count(VOID)
{
	return gem_vdi_font_faces;
}

/* count how many sizes face one has */
UWORD
gem_vdi_font_size_count(VOID)
{
	UWORD count;
	UWORD index;
	WORD point;

	count = 0;
	point = -1;
	for (index = 0; index < gem_vdi_font_used; index++) {
		if ((gem_vdi_font_table[index].font_id & 0x00ffU) != 1U)
			continue;
		if (gem_vdi_font_table[index].point == point)
			continue;
		point = gem_vdi_font_table[index].point;
		count++;
	}
	return count;
}

const BYTE *
gem_vdi_font_face_name(UWORD index, UWORD *face_id)
{
	if (!index || index > gem_vdi_font_faces)
		return (const BYTE *) 0;
	index--;
	if (face_id)
		*face_id = gem_vdi_font_face_id[index];
	return gem_vdi_font_face_table[index];
}

/*
 * faces are named once, a .FNT whose face id is already known keeps the first
 * name seen
 */
static UWORD
gem_vdi_font_face_intern(UWORD face, const BYTE *name)
{
	UWORD index;
	UWORD position;

	for (index = 0; index < gem_vdi_font_faces; index++)
		if (gem_vdi_font_face_id[index] == face)
			return index;
	if (gem_vdi_font_faces >= GEM_VDI_FACES)
		return 0;
	index = gem_vdi_font_faces;
	gem_vdi_font_face_id[index] = face;
	for (position = 0; position < GEM_VDI_FONT_NAME; position++) {
		gem_vdi_font_face_table[index][position] =
			name ? name[position] : 0;
		if (name && !name[position])
			name = (const BYTE *) 0;
	}
	gem_vdi_font_face_table[index][GEM_VDI_FONT_NAME - 1] = 0;
	gem_vdi_font_faces++;
	return index;
}

VOID
gem_vdi_font_reset(UWORD rom_segment, UWORD rom_offset, UWORD rom_rows)
{
	static const BYTE system_name[] = "System";
	const UWORD *metrics;
	GEM_VDI_FONT *font;

	gem_vdi_font_unload_all();
	gem_vdi_font_used = 0;
	gem_vdi_font_faces = 0;

	font = &gem_vdi_font_table[0];
	gem_vdi_font_zero(font, sizeof(*font));
	metrics = (rom_rows == 8U) ? gem_vdi_font_system_8
		: (rom_rows == 14U) ? gem_vdi_font_system_14
		: gem_vdi_font_system_16;
	font->font_id = 1;
	font->point = 10;
	font->first_ade = 0;
	font->last_ade = 255;
	font->top = metrics[0];
	font->ascent = metrics[1];
	font->half = metrics[2];
	font->descent = metrics[3];
	font->bottom = metrics[4];
	font->max_char_width = metrics[5];
	font->max_cell_width = metrics[6];
	font->left_offset = metrics[7];
	font->right_offset = metrics[8];
	font->thicken = metrics[9];
	font->ul_size = metrics[10];
	font->lighten = 0x5555U;
	font->skew = 0x5555U;
	font->flags = GEM_VDI_FONT_DEFAULT | GEM_VDI_FONT_MONOSPACE;
	font->form_width = 256;
	font->form_height = rom_rows ? rom_rows : 16U;
	font->data_segment = rom_segment;
	font->dat_offset = rom_offset;
	font->rom_rows = rom_rows ? rom_rows : 16U;
	font->file_index = GEM_VDI_FONT_NO_FILE;
	font->face_index = gem_vdi_font_face_intern(1, system_name);
	gem_vdi_font_used = 1;
}

/*
 * keep the chain sorted, an inserted font goes by face, then point size, then
 * style, so the size and style search can walk forward
 */
static UWORD
gem_vdi_font_slot_for(UWORD font_id, WORD point)
{
	UWORD index;
	UWORD face;
	UWORD style;
	UWORD other_face;
	UWORD other_style;

	face = font_id & 0x00ffU;
	style = (font_id >> 8) & 0x00ffU;
	for (index = 1; index < gem_vdi_font_used; index++) {
		other_face = gem_vdi_font_table[index].font_id & 0x00ffU;
		if (other_face < face)
			continue;
		if (other_face > face)
			return index;
		if (gem_vdi_font_table[index].point < point)
			continue;
		if (gem_vdi_font_table[index].point > point)
			return index;
		other_style = (gem_vdi_font_table[index].font_id >> 8)
			& 0x00ffU;
		if (other_style > style)
			return index;
	}
	return gem_vdi_font_used;
}

static GEM_VDI_FONT *
gem_vdi_font_insert(UWORD slot)
{
	UWORD index;

	if (gem_vdi_font_used >= GEM_VDI_FONTS)
		return (GEM_VDI_FONT *) 0;
	for (index = gem_vdi_font_used; index > slot; index--)
		gem_vdi_font_table[index] = gem_vdi_font_table[index - 1];
	gem_vdi_font_used++;
	gem_vdi_font_zero(&gem_vdi_font_table[slot],
		sizeof(gem_vdi_font_table[slot]));
	return &gem_vdi_font_table[slot];
}

static WORD
gem_vdi_font_have(UWORD font_id, WORD point)
{
	UWORD index;

	for (index = 0; index < gem_vdi_font_used; index++)
		if (gem_vdi_font_table[index].font_id == font_id
			&& gem_vdi_font_table[index].point == point)
			return TRUE;
	return FALSE;
}

static WORD
gem_vdi_font_read_exact(WORD descriptor, UBYTE *buffer, UWORD count)
{
	WORD result;

	while (count) {
		result = (WORD) read(descriptor, buffer, count);
		if (result < 0) {
			if (errno == EINTR)
				continue;
			return FALSE;
		}
		if (!result) {
			errno = EIO;
			return FALSE;
		}
		buffer += (UWORD) result;
		count -= (UWORD) result;
	}
	return TRUE;
}

/* copy count bytes from the file position into the far block */
static WORD
gem_vdi_font_copy_span(WORD descriptor, const GEM_FAR_RESOURCE *storage,
	UWORD destination, UWORD count)
{
	UWORD chunk;

	while (count) {
		chunk = count;
		if (chunk > GEM_VDI_FONT_CHUNK)
			chunk = GEM_VDI_FONT_CHUNK;
		if (!gem_vdi_font_read_exact(descriptor, gem_vdi_font_scratch,
				chunk))
			return FALSE;
		if (!gem_far_resource_copy_in(storage, destination,
				gem_vdi_font_scratch, chunk))
			return FALSE;
		destination = (UWORD) (destination + chunk);
		count = (UWORD) (count - chunk);
	}
	return TRUE;
}

static WORD
gem_vdi_font_seek(WORD descriptor, UWORD position)
{
	return lseek(descriptor, (long) (unsigned long) position, SEEK_SET)
		>= 0;
}

/* byte-swap every word of a span in place (motorola order) */
static VOID
gem_vdi_font_swap_far(UWORD segment, UWORD offset, UWORD bytes)
{
	UWORD chunk;
	UWORD index;
	UBYTE swap;

	while (bytes > 1U) {
		chunk = bytes;
		if (chunk > GEM_VDI_FONT_CHUNK)
			chunk = GEM_VDI_FONT_CHUNK;
		chunk &= (UWORD) ~1U;
		gem_resident_memory_from(segment, offset,
			gem_vdi_font_scratch, chunk);
		for (index = 0; index + 1U < chunk; index += 2U) {
			swap = gem_vdi_font_scratch[index];
			gem_vdi_font_scratch[index] =
				gem_vdi_font_scratch[index + 1U];
			gem_vdi_font_scratch[index + 1U] = swap;
		}
		gem_resident_memory_to(gem_vdi_font_scratch, segment, offset,
			chunk);
		offset = (UWORD) (offset + chunk);
		bytes = (UWORD) (bytes - chunk);
	}
}

static UWORD
gem_vdi_font_file_slot(VOID)
{
	UWORD index;

	for (index = 0; index < GEM_VDI_FONT_FILES; index++)
		if (!gem_vdi_font_file[index].base.hi)
			return index;
	return GEM_VDI_FONT_NO_FILE;
}

/*
 * load one .FNT, the three on-disk table offsets are file positions, the copy
 * keeps the offset table, the optional horizontal table and the character
 * strip, in that order, in one far block
 */
static WORD
gem_vdi_font_load_file(const BYTE *path)
{
	UBYTE header[GEM_VDI_FNT_HEADER_BYTES];
	GEM_FAR_RESOURCE *storage;
	GEM_VDI_FONT *font;
	WORD descriptor;
	WORD saved_errno;
	WORD added;
	UWORD file_slot;
	UWORD font_id;
	UWORD point;
	UWORD first_ade;
	UWORD last_ade;
	UWORD flags;
	UWORD form_width;
	UWORD form_height;
	UWORD characters;
	UWORD off_bytes;
	UWORD hor_bytes;
	UWORD dat_bytes;
	UWORD off_file;
	UWORD hor_file;
	UWORD dat_file;
	UWORD total;

	descriptor = (WORD) open((const char *) path, O_RDONLY);
	if (descriptor < 0)
		return 0;
	added = 0;
	storage = (GEM_FAR_RESOURCE *) 0;
	file_slot = GEM_VDI_FONT_NO_FILE;
	if (!gem_vdi_font_read_exact(descriptor, header, sizeof(header)))
		goto done;

	font_id = gem_vdi_font_word(header + GEM_VDI_FNT_ID);
	point = gem_vdi_font_word(header + GEM_VDI_FNT_POINT);
	first_ade = gem_vdi_font_word(header + GEM_VDI_FNT_FIRST_ADE);
	last_ade = gem_vdi_font_word(header + GEM_VDI_FNT_LAST_ADE);
	flags = gem_vdi_font_word(header + GEM_VDI_FNT_FLAGS);
	form_width = gem_vdi_font_word(header + GEM_VDI_FNT_FORM_WIDTH);
	form_height = gem_vdi_font_word(header + GEM_VDI_FNT_FORM_HEIGHT);

	/* reject anything the one-segment layout cant hold */
	if (!(font_id & 0x00ffU) || last_ade < first_ade || last_ade > 255U
		|| !form_width || !form_height
		|| form_height > GEM_VDI_FONT_MAX_ROWS
		|| gem_vdi_font_word(header + GEM_VDI_FNT_MAX_CELL_WIDTH)
		> GEM_VDI_FONT_MAX_CELL
		|| gem_vdi_font_long_high(header + GEM_VDI_FNT_OFF_TABLE)
		|| gem_vdi_font_long_high(header + GEM_VDI_FNT_DAT_TABLE))
		goto done;
	if (gem_vdi_font_have(font_id, (WORD) point))
		goto done;

	characters = (UWORD) (last_ade - first_ade + 1U);
	off_bytes = (UWORD) ((characters + 1U) << 1);
	hor_bytes = (flags & GEM_VDI_FONT_HORZ_OFF)
		? (UWORD) (characters << 1) : 0U;
	if (form_height > 65535U / form_width)
		goto done;
	dat_bytes = (UWORD) (form_width * form_height);
	if (off_bytes > (UWORD) (65535U - hor_bytes)
		|| (UWORD) (off_bytes + hor_bytes) >
		(UWORD) (65535U - dat_bytes))
		goto done;
	total = (UWORD) (off_bytes + hor_bytes + dat_bytes);

	off_file = gem_vdi_font_long_low(header + GEM_VDI_FNT_OFF_TABLE);
	hor_file = gem_vdi_font_long_low(header + GEM_VDI_FNT_HOR_TABLE);
	dat_file = gem_vdi_font_long_low(header + GEM_VDI_FNT_DAT_TABLE);

	file_slot = gem_vdi_font_file_slot();
	if (file_slot == GEM_VDI_FONT_NO_FILE)
		goto done;
	storage = &gem_vdi_font_file[file_slot];
	if (!gem_far_resource_alloc(storage, total)) {
		storage = (GEM_FAR_RESOURCE *) 0;
		goto done;
	}

	if (!gem_vdi_font_seek(descriptor, off_file)
		|| !gem_vdi_font_copy_span(descriptor, storage, 0, off_bytes))
		goto done;
	if (hor_bytes && (!gem_vdi_font_seek(descriptor, hor_file)
			|| !gem_vdi_font_copy_span(descriptor, storage,
				off_bytes, hor_bytes)))
		goto done;
	if (!gem_vdi_font_seek(descriptor, dat_file)
		|| !gem_vdi_font_copy_span(descriptor, storage,
			(UWORD) (off_bytes + hor_bytes), dat_bytes))
		goto done;

	/* STDFORM means motorola byte order, so byte-swap the offset table and
	 * strip before an 8086 reads them */
	if (flags & GEM_VDI_FONT_STDFORM) {
		gem_vdi_font_swap_far(storage->base.hi, 0, off_bytes);
		gem_vdi_font_swap_far(storage->base.hi,
			(UWORD) (off_bytes + hor_bytes), dat_bytes);
	}

	font = gem_vdi_font_insert(gem_vdi_font_slot_for(font_id,
			(WORD) point));
	if (!font)
		goto done;

	font->font_id = font_id;
	font->point = (WORD) point;
	font->first_ade = first_ade;
	font->last_ade = last_ade;
	font->top = gem_vdi_font_word(header + GEM_VDI_FNT_TOP);
	font->ascent = gem_vdi_font_word(header + GEM_VDI_FNT_ASCENT);
	font->half = gem_vdi_font_word(header + GEM_VDI_FNT_HALF);
	font->descent = gem_vdi_font_word(header + GEM_VDI_FNT_DESCENT);
	font->bottom = gem_vdi_font_word(header + GEM_VDI_FNT_BOTTOM);
	font->max_char_width =
		gem_vdi_font_word(header + GEM_VDI_FNT_MAX_CHAR_WIDTH);
	font->max_cell_width =
		gem_vdi_font_word(header + GEM_VDI_FNT_MAX_CELL_WIDTH);
	font->left_offset = gem_vdi_font_word(header + GEM_VDI_FNT_LEFT_OFFSET);
	font->right_offset =
		gem_vdi_font_word(header + GEM_VDI_FNT_RIGHT_OFFSET);
	font->thicken = gem_vdi_font_word(header + GEM_VDI_FNT_THICKEN);
	font->ul_size = gem_vdi_font_word(header + GEM_VDI_FNT_UL_SIZE);
	font->lighten = gem_vdi_font_word(header + GEM_VDI_FNT_LIGHTEN);
	font->skew = gem_vdi_font_word(header + GEM_VDI_FNT_SKEW);
	font->flags = flags;
	font->form_width = form_width;
	font->form_height = form_height;
	font->data_segment = storage->base.hi;
	font->off_offset = 0;
	font->hor_offset = hor_bytes ? off_bytes : 0U;
	font->dat_offset = (UWORD) (off_bytes + hor_bytes);
	font->rom_rows = 0;
	font->file_index = file_slot;
	header[GEM_VDI_FNT_NAME + GEM_VDI_FONT_NAME - 1] = 0;
	font->face_index = gem_vdi_font_face_intern(font_id & 0x00ffU,
		(const BYTE *) (header + GEM_VDI_FNT_NAME));
	added = 1;
	storage = (GEM_FAR_RESOURCE *) 0;

done:
	saved_errno = (WORD) errno;
	if (storage)
		(void) gem_far_resource_free(storage);
	(void) close(descriptor);
	errno = saved_errno;
	return added;
}

static WORD
gem_vdi_font_name_is_fnt(const BYTE *name)
{
	UWORD length;

	length = 0;
	while (name[length])
		length++;
	if (length < 5U)
		return FALSE;
	length -= 4U;
	if (name[length] != '.')
		return FALSE;
	if (name[length + 1U] != 'F' && name[length + 1U] != 'f')
		return FALSE;
	if (name[length + 2U] != 'N' && name[length + 2U] != 'n')
		return FALSE;
	return name[length + 3U] == 'T' || name[length + 3U] == 't';
}

VOID
gem_vdi_font_set_directory(const BYTE *path)
{
	UWORD index;

	gem_vdi_font_directory[0] = 0;
	if (!path)
		return;
	for (index = 0; index + 1U < GEM_VDI_FONT_DIRECTORY_MAX
		&& path[index]; index++)
		gem_vdi_font_directory[index] = path[index];
	/* the trailing separator is added when a name is appended */
	while (index > 1U && path[index - 1U] == '/')
		index--;
	gem_vdi_font_directory[index] = 0;
}

WORD
gem_vdi_font_load_all(VOID)
{
	struct dirent entry;
	BYTE path[GEM_VDI_FONT_DIRECTORY_MAX + 1 + sizeof(entry.d_name)];
	UWORD faces_before;
	UWORD position;
	UWORD index;
	WORD descriptor;

	if (!gem_vdi_font_used || !gem_vdi_font_directory[0])
		return 0;
	faces_before = gem_vdi_font_faces;
	descriptor = (WORD) open((const char *) gem_vdi_font_directory,
		O_RDONLY);
	if (descriptor < 0)
		return 0;
	while (_readdir(descriptor, &entry, 1) > 0) {
		if (!gem_vdi_font_name_is_fnt((const BYTE *) entry.d_name))
			continue;
		for (position = 0; gem_vdi_font_directory[position]; position++)
			path[position] = gem_vdi_font_directory[position];
		path[position++] = '/';
		for (index = 0; entry.d_name[index]; index++)
			path[position++] = entry.d_name[index];
		path[position] = 0;
		(void) gem_vdi_font_load_file(path);
		if (gem_vdi_font_used >= GEM_VDI_FONTS)
			break;
	}
	(void) close(descriptor);
	return (WORD) (gem_vdi_font_faces - faces_before);
}

VOID
gem_vdi_font_unload_all(VOID)
{
	UWORD index;

	if (gem_vdi_font_used > 1U)
		gem_vdi_font_used = 1;
	for (index = 0; index < GEM_VDI_FONT_FILES; index++)
		if (gem_vdi_font_file[index].base.hi)
			(void) gem_far_resource_free(&gem_vdi_font_file[index]);
	/* faces beyond the system font go with the fonts that named them */
	if (gem_vdi_font_faces > 1U)
		gem_vdi_font_faces = 1;
}

/* one entry of the font's bit-offset table */
static UWORD
gem_vdi_font_offset_entry(const GEM_VDI_FONT *font, UWORD index)
{
	UBYTE bytes[2];

	gem_resident_memory_from(font->data_segment,
		(UWORD) (font->off_offset + (index << 1)), bytes, 2U);
	return gem_vdi_font_word(bytes);
}

UWORD
gem_vdi_font_char_width(const GEM_VDI_FONT *font, UWORD character)
{
	UWORD index;

	if (!font)
		return 0;
	if (font->rom_rows)
		return 8U;
	if (character < font->first_ade || character > font->last_ade)
		return 0;
	index = (UWORD) (character - font->first_ade);
	return (UWORD) (gem_vdi_font_offset_entry(font, index + 1U)
		- gem_vdi_font_offset_entry(font, index));
}

VOID
gem_vdi_font_char_offsets(const GEM_VDI_FONT *font, UWORD character,
	WORD *left, WORD *right)
{
	UBYTE bytes[2];
	UWORD index;

	if (left)
		*left = 0;
	if (right)
		*right = 0;
	if (!font || font->rom_rows || !(font->flags & GEM_VDI_FONT_HORZ_OFF))
		return;
	if (character < font->first_ade || character > font->last_ade)
		return;
	index = (UWORD) (character - font->first_ade);
	gem_resident_memory_from(font->data_segment,
		(UWORD) (font->hor_offset + (index << 1)), bytes, 2U);
	if (left)
		*left = (WORD) bytes[0];
	if (right)
		*right = (WORD) bytes[1];
}

/*
 * pull one glyph out of the font strip, left aligned in each output row, the
 * strip is one wide bitmap: row r starts form_width bytes after row r-1, and
 * the offset table gives the first and last bit column of each character
 */
UWORD
gem_vdi_font_glyph(const GEM_VDI_FONT *font, UWORD character,
	GEM_VDI_UBYTE *rows, UWORD stride)
{
	UBYTE source[(GEM_VDI_FONT_MAX_CELL / 8) + 2];
	UWORD width;
	UWORD start;
	UWORD shift;
	UWORD first;
	UWORD span;
	UWORD row;
	UWORD index;
	UWORD offset;
	UWORD value;

	if (!font || !rows || !stride || !font->data_segment)
		return 0;

	if (font->rom_rows) {
		/* the system font keeps one byte per row per glyph not a strip
		 * so the copy is one run, every code including the furniture
		 * codes below 32 is a glyph in the font */
		if (character > 255U)
			return 0;
		offset = (UWORD) (font->dat_offset
			+ (UWORD) (character * font->rom_rows));
		for (row = 0; row < font->rom_rows; row++) {
			gem_resident_memory_from(font->data_segment,
				(UWORD) (offset + row), source, 1U);
			rows[row * stride] = source[0];
			for (index = 1; index < stride; index++)
				rows[row * stride + index] = 0;
		}
		for (; row < font->form_height; row++)
			for (index = 0; index < stride; index++)
				rows[row * stride + index] = 0;
		return 8U;
	}

	if (character < font->first_ade || character > font->last_ade)
		return 0;
	index = (UWORD) (character - font->first_ade);
	first = gem_vdi_font_offset_entry(font, index);
	width = (UWORD) (gem_vdi_font_offset_entry(font, index + 1U) - first);
	if (!width || width > GEM_VDI_FONT_MAX_CELL)
		return 0;

	start = (UWORD) (first >> 3);
	shift = (UWORD) (first & 7U);
	span = (UWORD) ((shift + width + 7U) >> 3);
	if (span > sizeof(source))
		return 0;

	for (row = 0; row < font->form_height; row++) {
		gem_resident_memory_from(font->data_segment,
			(UWORD) (font->dat_offset
				+ (UWORD) (row * font->form_width) + start),
			source, span);
		/* shift the run left so bit 7 of the first byte is the glyph's
		 * first column, trailing bits past the width are cleared by the
		 * caller's width bound */
		for (index = 0; index < stride; index++) {
			if (index >= span) {
				rows[row * stride + index] = 0;
				continue;
			}
			value = source[index];
			value <<= shift;
			/* a zero shift shifts the carry-in out entirely */
			if (index + 1U < span)
				value |= (UWORD) source[index + 1U]
					>> (8U - shift);
			rows[row * stride + index] = (GEM_VDI_UBYTE) value;
		}
	}
	return width;
}
