/*
 * gem_vdi_sysfont.c - the GEM system font, paged in from disk
 *
 * a GEM driver carries its own system face, it dont borrow the adapter ROM
 * font (only EGA and later BIOSes have one, and none matches the GEM shapes
 * at the window-furniture codes below 32),I did have a conversation with ghaerr 
  and for size reasons i did mention about using ega glyths, 
  In the end i just decided to port over the authentic gem font support.

 the face is the DRI system font
 * sized to the adapter - 8x8 on CGA, 8x14 on EGA and Hercules, 8x16 on VGA
 *
 */

#include "gem_vdi_font.h"

#include "gem_far_resource.h"
#include "gem_resident_memory.h"

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#define GEM_VDI_SYSFONT_GLYPHS	256U
#define GEM_VDI_SYSFONT_CHUNK	512U

/* the far segment the running adapter's face is read into */
static GEM_FAR_RESOURCE gem_vdi_sysfont_storage;

/* file reads land here a chunk at a time before going to far memory */
static UBYTE gem_vdi_sysfont_scratch[GEM_VDI_SYSFONT_CHUNK];

/* the file for a cell height, alongside GEM.RSC in the system directory */
static const char *
gem_vdi_sysfont_path(UWORD rows)
{
	if (rows <= 8U)
		return "/lib/gemsys/sysfont.08";
	if (rows <= 14U)
		return "/lib/gemsys/sysfont.14";
	return "/lib/gemsys/sysfont.16";
}

static WORD
gem_vdi_sysfont_read_exact(WORD descriptor, UBYTE *buffer, UWORD count)
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


WORD
gem_vdi_sysfont_load(UWORD rows, UWORD *segment, UWORD *offset)
{
	const char *path;
	WORD descriptor;
	WORD saved_errno;
	UWORD total;
	UWORD done;
	UWORD chunk;

	total = (UWORD) (GEM_VDI_SYSFONT_GLYPHS * rows);
	path = gem_vdi_sysfont_path(rows);

	/* a repeat call reloads instead of leaking the earlier segment */
	if (gem_vdi_sysfont_storage.base.hi)
		(void) gem_far_resource_free(&gem_vdi_sysfont_storage);
	if (!gem_far_resource_alloc(&gem_vdi_sysfont_storage, total))
		return FALSE;

	descriptor = (WORD) open(path, O_RDONLY);
	if (descriptor < 0) {
		(void) gem_far_resource_free(&gem_vdi_sysfont_storage);
		return FALSE;
	}
	done = 0;
	while (done < total) {
		chunk = (UWORD) (total - done);
		if (chunk > GEM_VDI_SYSFONT_CHUNK)
			chunk = GEM_VDI_SYSFONT_CHUNK;
		if (!gem_vdi_sysfont_read_exact(descriptor,
				gem_vdi_sysfont_scratch, chunk)
			|| !gem_far_resource_copy_in(&gem_vdi_sysfont_storage,
				done, gem_vdi_sysfont_scratch, chunk)) {
			saved_errno = (WORD) errno;
			(void) close(descriptor);
			(void) gem_far_resource_free(&gem_vdi_sysfont_storage);
			errno = saved_errno;
			return FALSE;
		}
		done = (UWORD) (done + chunk);
	}
	(void) close(descriptor);

	*segment = gem_vdi_sysfont_storage.base.hi;
	*offset = gem_vdi_sysfont_storage.base.lo;
	return TRUE;
}
