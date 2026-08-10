/*
 * gem_system_resource.c - the AES's own GEM.RSC for resident ELKS GEM
 *
 * loads the AES's own GEM.RSC once and keeps it, so the AES can pull its
 * trees, strings and images out of it.
 *
 * GEM.RSC is the authority for the file selector and alert boxes, the
 * message text, mouse shapes and built-in paths; nothing here copies it
 * so the AES wont start without the file
 */

#include "gem_system_resource.h"

#include "gem_rsc.h"
#include "gem_vdi_resident.h"

#include <errno.h>

static GEM_RESOURCE_RESIDENT gem_system_resource_image;
static UBYTE gem_system_resource_tried;

WORD
gem_system_resource_load(VOID)
{
	GEM_RESOURCE_METRICS metrics;

	if (gem_system_resource_image.flags & GEM_RESOURCE_RESIDENT_LOADED)
		return TRUE;
	if (gem_system_resource_tried)
		return FALSE;
	gem_system_resource_tried = TRUE;
	gem_resource_resident_init(&gem_system_resource_image);
	if (!gem_vdi_resident_get_metrics(&metrics.screen_width,
			&metrics.screen_height, &metrics.character_width,
			&metrics.character_height))
		return FALSE;
	/* fix up the resource the same way the desktop's resource is fixed */
	metrics.options = GEM_RESOURCE_OPTION_OUTLINED_ROOT;
	return gem_resource_resident_load(&gem_system_resource_image,
		(const BYTE *) GEM_SYSTEM_RESOURCE_FILE, &metrics);
}

VOID
gem_system_resource_free(VOID)
{
	(void) gem_resource_resident_cleanup(&gem_system_resource_image);
	gem_system_resource_tried = FALSE;
}

const GEM_RESOURCE_RESIDENT *
gem_system_resource(VOID)
{
	if (!(gem_system_resource_image.flags & GEM_RESOURCE_RESIDENT_LOADED))
		return (const GEM_RESOURCE_RESIDENT *) 0;
	return &gem_system_resource_image;
}

WORD
gem_system_resource_gaddr(UWORD type, UWORD index, GEM_FAR_ADDRESS *address)
{
	if (!gem_system_resource()) {
		errno = EINVAL;
		return FALSE;
	}
	return gem_resource_resident_gaddr(&gem_system_resource_image, type,
		index, address);
}

WORD
gem_system_resource_string(UWORD index, BYTE *buffer, UWORD size)
{
	GEM_RSC_IMAGE image;
	GEM_FAR_ADDRESS address;

	if (!buffer || !size)
		return -1;
	buffer[0] = 0;
	/* R_STRING gives back the text itself */
	if (!gem_system_resource_gaddr(R_STRING, index, &address)
		|| !address.hi)
		return -1;
	image.segment = address.hi;
	image.bytes = 0xffffU;
	return gem_rsc_string(&image, address.lo, buffer, size);
}
