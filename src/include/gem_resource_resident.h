/*
 * gem_resource_resident.h - per-PD GEM resource ownership on ELKS
 *
 * each attached GEM process owns at most one classic RSC image. the bytes stay
 * in one ELKS paragraph segment and pointer fields stay offset/segment pairs
 */

#ifndef ELKS_GEM_RESOURCE_RESIDENT_H
#define ELKS_GEM_RESOURCE_RESIDENT_H

#include "gem_far_resource.h"

/* GEM stores an object position as a signed character offset in the high byte and a row/column count in the low byte. pixel and cell sizes are per PD so RSRC_OBFIX needs no global state. options bit zero is the outlined-root switch */
typedef struct gem_resource_metrics {
	UWORD screen_width;
	UWORD screen_height;
	UWORD character_width;
	UWORD character_height;
	UWORD options;
} GEM_RESOURCE_METRICS;

#define GEM_RESOURCE_OPTION_OUTLINED_ROOT 0x0001U
#define GEM_RESOURCE_RESIDENT_LOADED      0x0001U

/* sits inside one resident PD, storage is the only far allocation */
typedef struct gem_resource_resident {
	GEM_FAR_RESOURCE storage;
	GEM_RESOURCE_METRICS metrics;
	UWORD flags;
} GEM_RESOURCE_RESIDENT;

typedef BYTE GEM_RESOURCE_METRICS_MUST_BE_10_BYTES
	[(sizeof(GEM_RESOURCE_METRICS) == 10) ? 1 : -1];
typedef BYTE GEM_RESOURCE_RESIDENT_MUST_BE_18_BYTES
	[(sizeof(GEM_RESOURCE_RESIDENT) == 18) ? 1 : -1];

/* set up an unused per-PD descriptor without allocating anything */
VOID gem_resource_resident_init(GEM_RESOURCE_RESIDENT *resident);

/* load one PC GEM RSC file, check every table and nested offset, relocate its pointer pairs in place, apply the object coordinate fix. a loaded descriptor refuses a second load with EBUSY */
WORD gem_resource_resident_load(GEM_RESOURCE_RESIDENT *resident,
	const BYTE *filename, const GEM_RESOURCE_METRICS *metrics);

/* RSRC_FREE for a loaded per-PD image */
WORD gem_resource_resident_free(GEM_RESOURCE_RESIDENT *resident);

/* cleanup for synthetic EXIT and APPL_EXIT. safe to call twice, an empty PD succeeds, otherwise the far segment is released first */
WORD gem_resource_resident_cleanup(GEM_RESOURCE_RESIDENT *resident);

/* RSRC_GADDR and RSRC_SADDR, using explicit far-address words */
WORD gem_resource_resident_gaddr(const GEM_RESOURCE_RESIDENT *resident,
	UWORD type, UWORD index, GEM_FAR_ADDRESS *address);
WORD gem_resource_resident_saddr(GEM_RESOURCE_RESIDENT *resident,
	UWORD type, UWORD index, GEM_FAR_ADDRESS address);

/* RSRC_OBFIX for one object in a tree from RSRC_GADDR */
WORD gem_resource_resident_obfix(GEM_RESOURCE_RESIDENT *resident,
	GEM_FAR_ADDRESS tree, UWORD object);

/* return the far address of the relocated tree index table. the AES copies this plus storage.base and storage.bytes into AES global words 5 through 9 */
WORD gem_resource_resident_tree_table(const GEM_RESOURCE_RESIDENT *resident,
	GEM_FAR_ADDRESS *tree_table);

#endif				/* ELKS_GEM_RESOURCE_RESIDENT_H */
