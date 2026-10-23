/*
 * gem_resource_resident.h - per-PD GEM resource ownership on ELKS
 *
 * each attached GEM process owns at most one classic RSC image. the bytes stay
 * in one ELKS paragraph segment and pointer fields stay offset/segment pairs
 */

#ifndef ELKS_GEM_RESOURCE_RESIDENT_H
#define ELKS_GEM_RESOURCE_RESIDENT_H

#include "gem_far_resource.h"

/* one resource coordinate-fix context */
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

/* load and relocate one checked RSC file */
WORD gem_resource_resident_load(GEM_RESOURCE_RESIDENT *resident,
	const BYTE *filename, const GEM_RESOURCE_METRICS *metrics);

/* RSRC_FREE for a loaded per-PD image */
WORD gem_resource_resident_free(GEM_RESOURCE_RESIDENT *resident);

/* release one loaded RSC image */
WORD gem_resource_resident_cleanup(GEM_RESOURCE_RESIDENT *resident);

/* RSRC_GADDR and RSRC_SADDR, using explicit far-address words */
WORD gem_resource_resident_gaddr(const GEM_RESOURCE_RESIDENT *resident,
	UWORD type, UWORD index, GEM_FAR_ADDRESS *address);
WORD gem_resource_resident_saddr(GEM_RESOURCE_RESIDENT *resident,
	UWORD type, UWORD index, GEM_FAR_ADDRESS address);

/* RSRC_OBFIX for one object in a tree from RSRC_GADDR */
WORD gem_resource_resident_obfix(GEM_RESOURCE_RESIDENT *resident,
	GEM_FAR_ADDRESS tree, UWORD object);

/* return the relocated tree-index address */
WORD gem_resource_resident_tree_table(const GEM_RESOURCE_RESIDENT *resident,
	GEM_FAR_ADDRESS *tree_table);

#endif				/* ELKS_GEM_RESOURCE_RESIDENT_H */
