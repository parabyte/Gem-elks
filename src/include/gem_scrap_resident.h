/*
 * gem_scrap_resident.h - AES Scrap Manager for resident ELKS GEM
 *
 * the AES Scrap Manager. it owns one scrap directory path. SCRP_READ hands it
 * back with a bit vector of which SCRAP.* files are in it, SCRP_WRITE moves it,
 * SCRP_CLEAR deletes them
 */

#ifndef ELKS_GEM_SCRAP_RESIDENT_H
#define ELKS_GEM_SCRAP_RESIDENT_H

#include "aes.h"

#define GEM_SCRAP_READ		80U
#define GEM_SCRAP_WRITE		81U
#define GEM_SCRAP_CLEAR		82U

/* scrap file type bits, in the order theyre tried */
#define GEM_SCRAP_CSV		0x0001U
#define GEM_SCRAP_TXT		0x0002U
#define GEM_SCRAP_GEM		0x0004U
#define GEM_SCRAP_IMG		0x0008U
#define GEM_SCRAP_DCA		0x0010U
#define GEM_SCRAP_USR		0x8000U

#define GEM_SCRAP_TYPES		6U

/* largest stored scrap path */
#define GEM_SCRAP_PATH_MAX	64U

/* reset the scrap directory to the built-in default */
VOID gem_scrap_resident_init(VOID);

/* read the scrap path and file mask */
WORD gem_scrap_resident_read(BYTE *path, UWORD size);

/* adopt PATH as the scrap directory, false when it isnt a directory */
WORD gem_scrap_resident_write(const BYTE *path);

/* delete every SCRAP.* file in the scrap directory */
WORD gem_scrap_resident_clear(VOID);

#endif				/* ELKS_GEM_SCRAP_RESIDENT_H */
