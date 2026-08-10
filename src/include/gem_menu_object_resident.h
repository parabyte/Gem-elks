/*
 * gem_menu_object_resident.h - GEM menu/object bar for the resident AES
 *
 * a menu is just an OBJECT tree left in the relocated RSC segment. this draws
 * the menu bar with its boxes and titles, what the OpenGEM Desktop menu needs
 */

#ifndef ELKS_GEM_MENU_OBJECT_RESIDENT_H
#define ELKS_GEM_MENU_OBJECT_RESIDENT_H

#include "gem_resource_resident.h"

#define GEM_MENU_OBJECT_RESIDENT_HIDDEN  0U
#define GEM_MENU_OBJECT_RESIDENT_VISIBLE 1U

/* one active menu for the whole AES. channel and generation words stand in for the PD pointer, so a reused channel cant pull down a newer app's menu */
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

/* clear the active-menu descriptor without touching video memory */
VOID gem_menu_object_resident_init(GEM_MENU_OBJECT_RESIDENT *menu);

/* the same local LASTOB check MENU_BAR does for selectors 31 through 34 */
WORD gem_menu_object_resident_tree_count(const GEM_RESOURCE_RESIDENT *resource,
	GEM_FAR_ADDRESS tree, UWORD *object_count);

/* show or hide the menu bar for one resident RSC tree. on show, checks the object extent, sets the bar to the screen width, draws through the resident VDI owner. on hide, only the matching owner/generation may clear the active menu */
WORD gem_menu_object_resident_bar(GEM_MENU_OBJECT_RESIDENT *menu,
	const GEM_RESOURCE_RESIDENT *resource, GEM_FAR_ADDRESS tree,
	UWORD show, WORD owner, UWORD generation_lo, UWORD generation_hi);

/* redraw the one-pixel black rule under the menu bar at row line_y. the object renderer paints the bar white, so every bar repaint must call this after */
WORD gem_menu_object_resident_bar_rule(WORD line_y);

/* generation-safe active-menu cleanup, used by synthetic EXIT and APPL_EXIT */
VOID gem_menu_object_resident_detach(GEM_MENU_OBJECT_RESIDENT *menu,
	WORD owner, UWORD generation_lo, UWORD generation_hi);

#endif				/* ELKS_GEM_MENU_OBJECT_RESIDENT_H */
