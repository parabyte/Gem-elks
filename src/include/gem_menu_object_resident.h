/*
 * gem_menu_object_resident.h - original GEM menu/object bar for resident AES.
 *
 * Digital Research AES represents a menu as an ordinary OBJECT tree.  This
 * resident interface keeps that representation in the relocated RSC segment;
 * it does not copy the tree into near memory or build a second widget model.
 * The first bounded closure implements MENU_BAR's original mn_bar(),
 * ob_draw(), everyobj(), ob_offset(), and the G_BOX/G_IBOX/G_TITLE portion of
 * just_draw()/ystate() needed by the OpenGEM Desktop ADMENU tree.
 */

#ifndef ELKS_GEM_MENU_OBJECT_RESIDENT_H
#define ELKS_GEM_MENU_OBJECT_RESIDENT_H

#include "gem_resource_resident.h"

#define GEM_MENU_OBJECT_RESIDENT_HIDDEN  0U
#define GEM_MENU_OBJECT_RESIDENT_VISIBLE 1U

/*
 * The active menu is AES-wide, just as original gl_mntree was AES-wide.
 * ELKS channel and generation words replace the original in-process PD
 * pointer so a reused channel cannot detach a newer application's menu.
 * All addresses remain exact unscaled offset:segment pairs.
 */
typedef struct gem_menu_object_resident {
	GEM_FAR_ADDRESS tree;
	UWORD object_count;
	WORD owner;
	UWORD generation_lo;
	UWORD generation_hi;
	UWORD visible;
} GEM_MENU_OBJECT_RESIDENT;

typedef BYTE GEM_MENU_OBJECT_RESIDENT_MUST_BE_14_BYTES
	[(sizeof(GEM_MENU_OBJECT_RESIDENT) == 14) ? 1 : -1];

/* Clear the AES-wide active-menu descriptor without touching video memory. */
VOID gem_menu_object_resident_init(GEM_MENU_OBJECT_RESIDENT *menu);

/* Reuse MENU_BAR's exact local LASTOB validation for selectors 31 through 34. */
WORD gem_menu_object_resident_tree_count(
	const GEM_RESOURCE_RESIDENT *resource, GEM_FAR_ADDRESS tree,
	UWORD *object_count);

/*
 * Implement original MENU_BAR / mn_bar for one resident RSC tree.
 *
 * show is a Boolean word.  On show, resource and tree must name one loaded,
 * relocated original RSC tree.  The function validates the local LASTOB
 * extent, writes THEBAR's screen width exactly as mn_bar() did, and draws the
 * bar through the resident VDI owner.  On hide, resource and tree are ignored
 * and only the matching owner/generation may clear the active menu.
 */
WORD gem_menu_object_resident_bar(GEM_MENU_OBJECT_RESIDENT *menu,
	const GEM_RESOURCE_RESIDENT *resource, GEM_FAR_ADDRESS tree,
	UWORD show, WORD owner, UWORD generation_lo, UWORD generation_hi);

/* Synthetic EXIT and APPL_EXIT use this generation-safe gl_mntree cleanup. */
VOID gem_menu_object_resident_detach(GEM_MENU_OBJECT_RESIDENT *menu,
	WORD owner, UWORD generation_lo, UWORD generation_hi);

#endif /* ELKS_GEM_MENU_OBJECT_RESIDENT_H */
