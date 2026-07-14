/*
 * gem_resource_resident.h - per-PD original GEM resource ownership on ELKS.
 *
 * Each attached GEM process owns at most one classic RSC image.  The bytes
 * remain in one ELKS paragraph segment and pointer fields remain their
 * original offset/segment word pairs.  This interface never flattens a far
 * address, creates a near mirror of the resource, or stores a C wide scalar.
 */

#ifndef ELKS_GEM_RESOURCE_RESIDENT_H
#define ELKS_GEM_RESOURCE_RESIDENT_H

#include "gem_far_resource.h"

/*
 * Original GEM encodes object positions as a signed character offset in the
 * high byte and a row/column count in the low byte.  These unscaled pixel and
 * cell dimensions are retained per PD so RSRC_OBFIX needs no global state.
 * options bit zero is GEMRSLIB's outlined-root compatibility selection.
 */
typedef struct gem_resource_metrics {
	UWORD screen_width;
	UWORD screen_height;
	UWORD character_width;
	UWORD character_height;
	UWORD options;
} GEM_RESOURCE_METRICS;

#define GEM_RESOURCE_OPTION_OUTLINED_ROOT 0x0001U
#define GEM_RESOURCE_RESIDENT_LOADED      0x0001U

/*
 * This descriptor is intended to be embedded directly in one resident PD.
 * `storage` is the sole far allocation.  No dynamic near arena, table copy,
 * filename copy, or converted object representation is retained.
 */
typedef struct gem_resource_resident {
	GEM_FAR_RESOURCE storage;
	GEM_RESOURCE_METRICS metrics;
	UWORD flags;
} GEM_RESOURCE_RESIDENT;

typedef BYTE GEM_RESOURCE_METRICS_MUST_BE_10_BYTES
	[(sizeof(GEM_RESOURCE_METRICS) == 10) ? 1 : -1];
typedef BYTE GEM_RESOURCE_RESIDENT_MUST_BE_18_BYTES
	[(sizeof(GEM_RESOURCE_RESIDENT) == 18) ? 1 : -1];

/* Initialize an unused per-PD descriptor without allocating memory. */
VOID gem_resource_resident_init(GEM_RESOURCE_RESIDENT *resident);

/*
 * Load one original little-endian PC GEM RSC file, validate every table and
 * nested offset, relocate its pointer pairs in place, and apply GEMRSLIB's
 * object coordinate fix.  A loaded descriptor rejects a second load with
 * EBUSY.  Byte counts are exact 16-bit file offsets; no truncation occurs.
 */
WORD gem_resource_resident_load(GEM_RESOURCE_RESIDENT *resident,
				const BYTE *filename,
				const GEM_RESOURCE_METRICS *metrics);

/* Original RSRC_FREE behavior for a successfully loaded per-PD image. */
WORD gem_resource_resident_free(GEM_RESOURCE_RESIDENT *resident);

/*
 * Synthetic EXIT and APPL_EXIT use this exact idempotent cleanup entry point.
 * It succeeds for an already-empty PD and otherwise releases its sole ELKS
 * far segment before clearing the descriptor.
 */
WORD gem_resource_resident_cleanup(GEM_RESOURCE_RESIDENT *resident);

/* Original RSRC_GADDR and RSRC_SADDR using explicit far-address words. */
WORD gem_resource_resident_gaddr(const GEM_RESOURCE_RESIDENT *resident,
				 UWORD type, UWORD index,
				 GEM_FAR_ADDRESS *address);
WORD gem_resource_resident_saddr(GEM_RESOURCE_RESIDENT *resident,
				 UWORD type, UWORD index,
				 GEM_FAR_ADDRESS address);

/* Original RSRC_OBFIX for one object in a tree returned by RSRC_GADDR. */
WORD gem_resource_resident_obfix(GEM_RESOURCE_RESIDENT *resident,
				 GEM_FAR_ADDRESS tree, UWORD object);

/*
 * Return GEMRSLIB's APP_LOPNAME value: the far address of the relocated tree
 * index table.  The resident AES copies this plus `storage.base` and
 * `storage.bytes` into original AES global words 5 through 9.  No resource
 * header or pointer representation is duplicated in the PD.
 */
WORD gem_resource_resident_tree_table(
	const GEM_RESOURCE_RESIDENT *resident,
	GEM_FAR_ADDRESS *tree_table);

#endif /* ELKS_GEM_RESOURCE_RESIDENT_H */
