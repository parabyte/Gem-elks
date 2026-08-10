/*
 * gem_system_resource.h - the AES's own GEM.RSC for resident ELKS GEM
 *
 * classic GEM keeps one system resource of its own, separate from any app's.
 * it holds the file selector and alert trees, the free strings the Scrap and
 * Shell managers build paths from, and the eight built-in mouse forms.
 *
 * the tree, string and image numbers below come from DRI's own GEM.H generated
 * with the resource, so theyre the file's contract not anything chosen here
 */

#ifndef ELKS_GEM_SYSTEM_RESOURCE_H
#define ELKS_GEM_SYSTEM_RESOURCE_H

#include "gem_resource_resident.h"

#define GEM_SYSTEM_RESOURCE_FILE	"/lib/gemsys/gem.rsc"

/* GEM.H trees */
#define GEM_SYSTEM_TREE_FSELECTOR	0U
#define GEM_SYSTEM_TREE_ALERT		1U
#define GEM_SYSTEM_TREE_DESKTOP		2U

/* GEM.H objects inside the file selector tree */
#define GEM_SYSTEM_FSTITLE		1
#define GEM_SYSTEM_FSDIRECT		2
#define GEM_SYSTEM_FSSELECT		3
#define GEM_SYSTEM_FSOK			4
#define GEM_SYSTEM_FSCANCEL		5
#define GEM_SYSTEM_FCLSBOX		7
#define GEM_SYSTEM_FTITLE		8
#define GEM_SYSTEM_SCRLBAR		9
#define GEM_SYSTEM_FUPAROW		10
#define GEM_SYSTEM_FDNAROW		11
#define GEM_SYSTEM_FSVSLID		12
#define GEM_SYSTEM_FSVELEV		13
#define GEM_SYSTEM_FILEBOX		14
#define GEM_SYSTEM_F1NAME		15
#define GEM_SYSTEM_F9NAME		23
#define GEM_SYSTEM_FSEL_NAMES \
	(GEM_SYSTEM_F9NAME - GEM_SYSTEM_F1NAME + 1)

/* GEM.H free strings the resident uses */
#define GEM_SYSTEM_STPATH		0U
#define GEM_SYSTEM_AL18ERR		27U
#define GEM_SYSTEM_ALNOFIT		28U
#define GEM_SYSTEM_AL04ERR		29U
#define GEM_SYSTEM_AL05ERR		30U
#define GEM_SYSTEM_AL15ERR		31U
#define GEM_SYSTEM_AL16ERR		32U
#define GEM_SYSTEM_AL08ERR		33U
#define GEM_SYSTEM_ALXXERR		34U
#define GEM_SYSTEM_ALNOFUNC		35U
#define GEM_SYSTEM_STSCDIR		15U
#define GEM_SYSTEM_STINPATH		14U
#define GEM_SYSTEM_STSCRAP		20U

/* GEM.H free images: three alert icons then the eight mouse forms */
#define GEM_SYSTEM_NOTEBB		0U
#define GEM_SYSTEM_QUESTBB		1U
#define GEM_SYSTEM_STOPBB		2U
#define GEM_SYSTEM_MICE00		3U

/* load GEM.RSC once. the AES cant run without it: the alert and file selector trees, the alert and error text, the mouse forms and the built-in paths all live there and arent duplicated anywhere in the resident */
WORD gem_system_resource_load(VOID);

/* release it at shutdown */
VOID gem_system_resource_free(VOID);

/* the loaded descriptor, or null when GEM.RSC isnt available */
const GEM_RESOURCE_RESIDENT *gem_system_resource(VOID);

/* resolve a tree/object address in the system resource */
WORD gem_system_resource_gaddr(UWORD type, UWORD index,
	GEM_FAR_ADDRESS *address);

/* copy one free string into a resident buffer. returns the length without the terminator, or -1 when the string isnt there */
WORD gem_system_resource_string(UWORD index, BYTE *buffer, UWORD size);

#endif				/* ELKS_GEM_SYSTEM_RESOURCE_H */
