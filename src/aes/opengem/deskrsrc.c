/*	DESKRSRC.C	05/04/84 - 06/10/85	Lee Lorenzen		*/
/*	for 3.0		4/25/86			MDF			*/
/*	merge source	5/27/87			mdf			*/

/*
*       Copyright 1999, Caldera Thin Clients, Inc.                      
*       This software is licenced under the GNU Public License.         
*       Please see LICENSE.TXT for further information.                 
*                                                                       
*                  Historical Copyright                                 
*	-------------------------------------------------------------
*	GEM Desktop					  Version 2.3
*	Serial No.  XXXX-0000-654321		  All Rights Reserved
*	Copyright (C) 1985			Digital Research Inc.
*	-------------------------------------------------------------
*/

#include "ppddesk.h"

#define R_STRING 5

/* The adaptive resource beautifier (which makes checkboxes look right on
 * all GEM versions) is largely nicked from the FreeGEM RCS */
typedef WORD (*MAPROUTINE)(LPTREE tr, WORD obj);

MLOCAL WORD ini_tree(WORD which);
MLOCAL VOID map_tree(LPTREE tree, WORD first, WORD last, MAPROUTINE routine);
MLOCAL WORD menu_cleanup(LPTREE tr, WORD obj);

#if defined(ELKS) && ELKS
/*
 * Keep DESKTOP.RSC byte-for-byte original and change only the live ELKS
 * presentation after the original GEM resource loader has relocated it.
 * The replacement text lives in a cold far-text section so it does not
 * consume the Desktop's scarce 64 KiB near-data segment.  At startup it is
 * copied into the existing, large-enough RSC string slots.  Every live OBJECT
 * and TEDINFO therefore retains its original resident-resource pointer; the
 * resident AES never has to accept a pointer into the client's code segment.
 * The overlay runs after the original beautifier, and its labels intentionally
 * contain no mutable underscore markers.
 */
#define ELKS_FAR_UI __attribute__((section(".fartext.s.elks_ui")))
MLOCAL BYTE FAR elks_menu_root_icon[] ELKS_FAR_UI =
	"  Edit POSIX Root...    ";
MLOCAL BYTE FAR elks_menu_shell[] ELKS_FAR_UI =
	"  Open POSIX Shell           ";
MLOCAL BYTE FAR elks_fs_info_title[] ELKS_FAR_UI =
	" Filesystem Info ";
MLOCAL BYTE FAR elks_mount_template[] ELKS_FAR_UI = "Mount Point:  _";
MLOCAL BYTE FAR elks_fs_template[] ELKS_FAR_UI =
	"Filesystem:  ___________";
MLOCAL BYTE FAR elks_root_icon_title[] ELKS_FAR_UI =
	" POSIX Root Icon ";
MLOCAL BYTE FAR elks_apply_button[] ELKS_FAR_UI = "   Apply    ";
MLOCAL BYTE FAR elks_defaults_button[] ELKS_FAR_UI = "  Reset   ";
/*
 * These five captions describe cosmetic uses of the original root-icon
 * artwork.  The selected bitmap never changes the mounted POSIX namespace.
 * Each replacement is exactly ten visible bytes, matching the original RSC
 * button slot and leaving its eleventh byte for the terminating NUL.
 */
MLOCAL BYTE FAR elks_root_archive[] ELKS_FAR_UI = "  Archive ";
MLOCAL BYTE FAR elks_root_package[] ELKS_FAR_UI = "  Package ";
MLOCAL BYTE FAR elks_root_remote[] ELKS_FAR_UI = "  Remote  ";
MLOCAL BYTE FAR elks_root_generic[] ELKS_FAR_UI = "  Generic ";
MLOCAL BYTE FAR elks_root_system[] ELKS_FAR_UI = "  System  ";
MLOCAL BYTE FAR elks_shell_type[] ELKS_FAR_UI = " Shell ";
MLOCAL BYTE FAR elks_shell_args_type[] ELKS_FAR_UI =
	"  Shell with arguments  ";
MLOCAL BYTE FAR elks_exclusive_mode[] ELKS_FAR_UI = "Exclusive Mode:   ";
MLOCAL BYTE FAR elks_accessory_title[] ELKS_FAR_UI =
	" Manage Accessories ";
MLOCAL BYTE FAR elks_accessory_start[] ELKS_FAR_UI = "  Start  ";
MLOCAL BYTE FAR elks_accessory_info[] ELKS_FAR_UI =
	"Available accessories are native ELKS programs";
MLOCAL BYTE FAR elks_accessory_owner[] ELKS_FAR_UI = "ELKS-managed.";
MLOCAL BYTE FAR elks_posix_options[] ELKS_FAR_UI = "POSIX options:";
MLOCAL BYTE FAR elks_kernel_mounts[] ELKS_FAR_UI = "Kernel mount list";
MLOCAL BYTE FAR elks_kernel_removable[] ELKS_FAR_UI =
	"Kernel removable-media probes";
MLOCAL BYTE FAR elks_trash_line_1[] ELKS_FAR_UI =
	"Trash permanently";
MLOCAL BYTE FAR elks_trash_line_2[] ELKS_FAR_UI =
	"deletes folders, documents,";
MLOCAL BYTE FAR elks_trash_line_3[] ELKS_FAR_UI =
	"and applications dragged here";
MLOCAL BYTE FAR elks_trash_line_4[] ELKS_FAR_UI =
	"from the POSIX filesystem.";
MLOCAL BYTE FAR elks_trash_line_5[] ELKS_FAR_UI =
	"This cannot be undone.";

#define ELKS_DISK_INFO_TITLE       2
#define ELKS_ROOT_ICON_TITLE       4
#define ELKS_ACCESSORY_TITLE       1
#define ELKS_ACCESSORY_MEMORY_TEXT 21
#define ELKS_ACCESSORY_FROM_TEXT  22
#define ELKS_ACCESSORY_REMOVE_TEXT 23
#define ELKS_ACCESSORY_INFO       27
#define ELKS_ACCESSORY_OWNER      28
#define ELKS_PREFS_HEADING        35
#define ELKS_PREFS_MOUNT_LABEL    37
#define ELKS_PREFS_REMOVE_LABEL   39
#define ELKS_TRASH_LINE_1          4
#define ELKS_TRASH_LINE_2          5
#define ELKS_TRASH_LINE_3          6
#define ELKS_TRASH_LINE_4          7
#define ELKS_TRASH_LINE_5          8

/*
 * Fail compilation if a future wording edit exceeds the byte-exact original
 * RSC allocation.  sizeof includes the NUL in both operands, so equality is
 * safe and no runtime length pass is added to the 8088 startup path.
 */
#define ELKS_RSC_TEXT_FITS(symbol, original_text) \
	typedef BYTE symbol##_must_fit_original_rsc_slot \
		[(sizeof(symbol) <= sizeof(original_text)) ? 1 : -1]

ELKS_RSC_TEXT_FITS(elks_menu_root_icon, "  Install _Disk Drive...");
ELKS_RSC_TEXT_FITS(elks_menu_shell,
	"  Enter DOS _Commands      \007" "C");
ELKS_RSC_TEXT_FITS(elks_fs_info_title, " Disk Information ");
ELKS_RSC_TEXT_FITS(elks_mount_template, "Drive Identifier:  _:");
ELKS_RSC_TEXT_FITS(elks_fs_template, "Disk Label:  ___________");
ELKS_RSC_TEXT_FITS(elks_root_icon_title, " Install Disk Drive ");
ELKS_RSC_TEXT_FITS(elks_apply_button, "  _Install  ");
ELKS_RSC_TEXT_FITS(elks_defaults_button, "  _Remove ");
ELKS_RSC_TEXT_FITS(elks_root_archive, "  Floppy  ");
ELKS_RSC_TEXT_FITS(elks_root_package, "  CDROM   ");
ELKS_RSC_TEXT_FITS(elks_root_remote, "  Network ");
ELKS_RSC_TEXT_FITS(elks_root_generic, "  Other   ");
ELKS_RSC_TEXT_FITS(elks_root_system, "  Hard    ");
ELKS_RSC_TEXT_FITS(elks_shell_type, "  DOS  ");
ELKS_RSC_TEXT_FITS(elks_shell_args_type, "  DOS-takes parameters  ");
ELKS_RSC_TEXT_FITS(elks_exclusive_mode, "Needs Full Memory:");
ELKS_RSC_TEXT_FITS(elks_accessory_title, " Install Accessories ");
ELKS_RSC_TEXT_FITS(elks_accessory_start, " Install ");
ELKS_RSC_TEXT_FITS(elks_accessory_info,
	"Available accessories are Acc-type files in your");
ELKS_RSC_TEXT_FITS(elks_accessory_owner, "GEMBOOT folder.");
ELKS_RSC_TEXT_FITS(elks_posix_options, "Compatibility:");
ELKS_RSC_TEXT_FITS(elks_kernel_mounts, "Don't scan drives");
ELKS_RSC_TEXT_FITS(elks_kernel_removable,
	"Don't try to detect CDROM drives");
ELKS_RSC_TEXT_FITS(elks_trash_line_1, "The trash can is the ");
ELKS_RSC_TEXT_FITS(elks_trash_line_2,
	"the disks, folders, documents,");
ELKS_RSC_TEXT_FITS(elks_trash_line_3, "destination to which you drag");
ELKS_RSC_TEXT_FITS(elks_trash_line_4, "or applications that you want");
ELKS_RSC_TEXT_FITS(elks_trash_line_5, "to delete PERMANENTLY.");

#undef ELKS_RSC_TEXT_FITS

MLOCAL VOID
elks_copy_object_text(LPTREE tree, WORD object_id,
		      GEM_SLOT_BYTE_POINTER text, UWORD size)
{
	GEM_TYPED_SLOT_POINTER slot;
	LPOBJ object;

	object = desk_object_at(tree, object_id);
	if (object)
	{
		slot.words = object->ob_spec;
		if (slot.bytes)
			lstlcpy(slot.bytes, text, size);
	}
}

MLOCAL VOID
elks_copy_ted_text(LPTREE tree, WORD object_id,
		   GEM_SLOT_BYTE_POINTER text, UWORD size)
{
	GEM_TYPED_SLOT_POINTER slot;
	GEM_SLOT_BYTE_POINTER destination;
	LPOBJ object;
	LPTEDI tedinfo;

	object = desk_object_at(tree, object_id);
	if (!object)
		return;
	slot.words = object->ob_spec;
	tedinfo = slot.tedinfo;
	if (tedinfo)
	{
		slot.words = tedinfo->te_ptext;
		destination = slot.bytes;
		if (destination)
			lstlcpy(destination, text, size);
	}
}

MLOCAL VOID
elks_copy_ted_template(LPTREE tree, WORD object_id,
		       GEM_SLOT_BYTE_POINTER text, UWORD size)
{
	GEM_TYPED_SLOT_POINTER slot;
	GEM_SLOT_BYTE_POINTER destination;
	LPOBJ object;
	LPTEDI tedinfo;

	object = desk_object_at(tree, object_id);
	if (!object)
		return;
	slot.words = object->ob_spec;
	tedinfo = slot.tedinfo;
	if (tedinfo)
	{
		slot.words = tedinfo->te_ptmplt;
		destination = slot.bytes;
		if (destination)
			lstlcpy(destination, text, size);
	}
}

MLOCAL VOID
elks_resource_overlay(VOID)
{
	LPTREE tree;

	/* Options menu: expose the real POSIX root and /bin/sh operations. */
	if (rsrc_gaddr(R_TREE, ADMENU, (LPVOID *)&tree) && tree)
	{
		elks_copy_object_text(tree, IDSKITEM, elks_menu_root_icon,
				      sizeof(elks_menu_root_icon));
		elks_copy_object_text(tree, DOSITEM, elks_menu_shell,
				      sizeof(elks_menu_shell));
	}

	/* The former disk-information form now describes the mounted root. */
	if (rsrc_gaddr(R_TREE, ADDISKIN, (LPVOID *)&tree) && tree)
	{
		elks_copy_ted_text(tree, ELKS_DISK_INFO_TITLE,
				   elks_fs_info_title,
				   sizeof(elks_fs_info_title));
		elks_copy_ted_template(tree, DIDRIVE, elks_mount_template,
				       sizeof(elks_mount_template));
		elks_copy_ted_template(tree, DIVOLUME, elks_fs_template,
				       sizeof(elks_fs_template));
	}

	/* A one-letter drive editor is not meaningful in one POSIX namespace. */
	if (rsrc_gaddr(R_TREE, ADINSDIS, (LPVOID *)&tree) && tree)
	{
		elks_copy_ted_text(tree, ELKS_ROOT_ICON_TITLE,
				   elks_root_icon_title,
				   sizeof(elks_root_icon_title));
		elks_copy_ted_template(tree, DRID, elks_mount_template,
				       sizeof(elks_mount_template));
		desk_object_at(tree, DRID)->ob_flags &= (UWORD) ~EDITABLE;
		elks_copy_object_text(tree, DRINST, elks_apply_button,
				      sizeof(elks_apply_button));
		elks_copy_object_text(tree, DRREM, elks_defaults_button,
				      sizeof(elks_defaults_button));
		elks_copy_object_text(tree, DRFLOPPY, elks_root_archive,
				      sizeof(elks_root_archive));
		elks_copy_object_text(tree, DRCD, elks_root_package,
				      sizeof(elks_root_package));
		elks_copy_object_text(tree, DRNET, elks_root_remote,
				      sizeof(elks_root_remote));
		elks_copy_object_text(tree, DR5QRTR, elks_root_generic,
				      sizeof(elks_root_generic));
		elks_copy_object_text(tree, DRHARD, elks_root_system,
				      sizeof(elks_root_system));
	}

	/* Native command applications execute directly through ELKS exec. */
	if (rsrc_gaddr(R_TREE, ADINSAPP, (LPVOID *)&tree) && tree)
	{
		elks_copy_object_text(tree, APDOS, elks_shell_type,
				      sizeof(elks_shell_type));
		elks_copy_object_text(tree, APPARMS, elks_shell_args_type,
				      sizeof(elks_shell_args_type));
		elks_copy_object_text(tree, APMEM, elks_exclusive_mode,
				      sizeof(elks_exclusive_mode));
		/* ELKS owns address-space sizing; do not expose a dead K-byte box. */
		desk_object_at(tree, APMEMSZ)->ob_flags |= HIDETREE;
	}

	/*
	 * Keep the original Multiapp accessory manager, but describe its native
	 * ELKS process role.  GEM/XM's three per-accessory memory checkboxes chose
	 * DOS arena residency; ELKS owns every process segment, so those controls
	 * and their three-column heading are deliberately absent from the form.
	 */
	if (rsrc_gaddr(R_TREE, ADINSACC, (LPVOID *)&tree) && tree)
	{
		WORD object_id;

		elks_copy_ted_text(tree, ELKS_ACCESSORY_TITLE,
				   elks_accessory_title,
				   sizeof(elks_accessory_title));
		elks_copy_object_text(tree, ACINST, elks_accessory_start,
				      sizeof(elks_accessory_start));
		elks_copy_object_text(tree, ELKS_ACCESSORY_INFO,
				      elks_accessory_info,
				      sizeof(elks_accessory_info));
		elks_copy_object_text(tree, ELKS_ACCESSORY_OWNER,
				      elks_accessory_owner,
				      sizeof(elks_accessory_owner));
		for (object_id = ACC1FMEM;
		     object_id <= ELKS_ACCESSORY_REMOVE_TEXT; object_id++)
			desk_object_at(tree, object_id)->ob_flags |= HIDETREE;
	}

	/* Describe the real irreversible POSIX unlink operation. */
	if (rsrc_gaddr(R_TREE, ADTRINFO, (LPVOID *)&tree) && tree)
	{
		elks_copy_object_text(tree, ELKS_TRASH_LINE_1,
				      elks_trash_line_1,
				      sizeof(elks_trash_line_1));
		elks_copy_object_text(tree, ELKS_TRASH_LINE_2,
				      elks_trash_line_2,
				      sizeof(elks_trash_line_2));
		elks_copy_object_text(tree, ELKS_TRASH_LINE_3,
				      elks_trash_line_3,
				      sizeof(elks_trash_line_3));
		elks_copy_object_text(tree, ELKS_TRASH_LINE_4,
				      elks_trash_line_4,
				      sizeof(elks_trash_line_4));
		elks_copy_object_text(tree, ELKS_TRASH_LINE_5,
				      elks_trash_line_5,
				      sizeof(elks_trash_line_5));
	}

	/*
	 * The two old scan-drive toggles were never consumers of the ELKS mount
	 * namespace.  State that the kernel owns those jobs and disable the dead
	 * checkboxes instead of presenting controls which silently do nothing.
	 */
	if (rsrc_gaddr(R_TREE, ADSETPRE, (LPVOID *)&tree) && tree)
	{
		elks_copy_object_text(tree, ELKS_PREFS_HEADING,
				      elks_posix_options,
				      sizeof(elks_posix_options));
		elks_copy_object_text(tree, ELKS_PREFS_MOUNT_LABEL,
				      elks_kernel_mounts,
				      sizeof(elks_kernel_mounts));
		elks_copy_object_text(tree, ELKS_PREFS_REMOVE_LABEL,
				      elks_kernel_removable,
				      sizeof(elks_kernel_removable));
		desk_object_at(tree, SPNOSCAN)->ob_state |= DISABLED;
		desk_object_at(tree, SPNONET)->ob_state |= DISABLED;
		desk_object_at(tree, ELKS_PREFS_MOUNT_LABEL)->ob_state |= DISABLED;
		desk_object_at(tree, ELKS_PREFS_REMOVE_LABEL)->ob_state |= DISABLED;
	}
}
#endif

VOID rsrc_init(VOID)
{
	WORD n;
	LPTREE tree;

	rsrc_load(ADDR("DESKTOP.RSC"));

	for (n = ADFILEIN; n <= ADINSACC; n++) ini_tree(n);

	if (!(gl_xbuf.abilities.lo & ABLE_X3D))
	{
		rsrc_gaddr(R_TREE, ADMENU, (LPVOID *)&tree) ;
		if (tree) map_tree(tree, ROOT, NIL, menu_cleanup);
	}

#if defined(ELKS) && ELKS
	/* Install immutable ELKS labels after all original in-place cleanup. */
	elks_resource_overlay();
#endif
}


BYTE *ini_str(WORD stnum)
{
	GEM_SLOT_BYTE_POINTER lstr;

	rsrc_gaddr(R_STRING, stnum, (LPVOID *)&lstr);
	lstlcpy(&gl_lngstr[0], lstr, sizeof(gl_lngstr));
	return(&gl_lngstr[0]);
}


/*  If the AES supports checkboxes and radio buttons, implement them. 
 * 
 *  This is done simply by changing buttons with the DRAW3D and WHITEBAK
 *  flags to strings. That way, the controls look like buttons in older
 *  GEMs and like checkboxes in new ones. 
 * 
 */
	
#define EXT3D (DRAW3D | WHITEBAK)

/* The FreeGEM checkboxes and radio buttons are based on those in GEM/5.
 * This program will detect GEM/5 or FreeGEM and act accordingly.
 */

	
MLOCAL WORD gem5 = -1;
	
WORD make_cbox(LPTREE tr, WORD obj)
{
	GEM_TYPED_SLOT_POINTER slot;
	LPTEDI tedi;
	LPOBJ object;

	object = desk_object_at(tr, obj);
	if ((object->ob_state & EXT3D) == EXT3D)
	{
		if (object->ob_type == G_BUTTON)
		{
			object->ob_type = G_STRING;
		}

		/* The little pseudo-buttons on the "file info" page are not 
		 * buttons, but boxes. So rather than make them strings, just
		 * take their borders and 3D away. */
		if (object->ob_flags & FLAG3D)
		{
			object->ob_flags &= ~FLAG3D;
			object->ob_flags &= ~USECOLORCAT;
			/*
			 * The historical mask clears the character byte in bits
			 * 16..23.  ob_spec stores that byte in the low half of hi,
			 * so this word operation is exactly the old 0xff00ffff mask
			 * without creating a 32-bit C value on the 8086.
			 */
			object->ob_spec.hi &= 0xff00u;
			if (gem5)
			{
				object->ob_state |= CROSSED;
				object->ob_type = G_STRING;
				object->ob_spec = gem_near_pointer_words(ADDR(" "));
			}
		}
	}
	else if (object->ob_type == G_BUTTON)
	{
		object->ob_state |= DRAW3D;
	}


	/* Do the GEM/5 3D bits */
	if (gem5) 
	{
		if (object->ob_flags & FLAG3D)
		{
			object->ob_state |= CROSSED;

			if (object->ob_type == G_BOX || object->ob_type == G_BOXCHAR)
			{
				object->ob_spec.lo =
					(object->ob_spec.lo & 0xff80u) | 0x007eu;
				object->ob_flags |= OUTLINED;
			}
		}
		if (object->ob_state & EXT3D)
		{
			object->ob_state |= CROSSED;
		}

		if (object->ob_type == G_BOX && object->ob_state & SHADOWED)
		{
			object->ob_state |= CROSSED | CHECKED;
			object->ob_state &= ~SHADOWED;
			object->ob_spec.lo =
				(object->ob_spec.lo & 0xff80u) | 0x007eu;
		}

		if (object->ob_type == G_FTEXT || object->ob_type == G_FBOXTEXT)
		{
			object->ob_type = G_FBOXTEXT;
			object->ob_state |= CROSSED | OUTLINED | CHECKED;
		}
/* Title bars */
		if (object->ob_type == G_BOXTEXT)
		{
			slot.words = object->ob_spec;
			tedi = slot.tedinfo;
			if (tedi && tedi->te_color == 0x1191)
				tedi->te_color = 0x1a7b;
		}
/* Entry fields */
		if (object->ob_type == G_TEXT || object->ob_type == G_BOXTEXT)
		{
			object->ob_type = G_BOXTEXT;
			object->ob_state |= CROSSED | OUTLINED | CHECKED;
		}
	}
	return(TRUE);

}


/* Remove underlines from a string, moving all the other characters up
 * and putting spaces at the end */
MLOCAL VOID chop_underline(GEM_SLOT_BYTE_POINTER txt)
{
	GEM_SLOT_BYTE_POINTER txt2;
	while (*txt)
	{
		if (*txt == '_')
		{
			txt2 = txt;
			while (txt2[1])
			{
				txt2[0] = txt2[1];
				++txt2;	
			}
			txt2[0] = ' ';
		}
		++txt;
	}
}



MLOCAL WORD gem3_cleanup(LPTREE tr, WORD obj)
{
	GEM_TYPED_SLOT_POINTER slot;
	LPOBJ object;

	object = desk_object_at(tr, obj);
	if (object->ob_type == G_BUTTON)
	{
		slot.words = object->ob_spec;
		chop_underline(slot.bytes);
	}

	/* Shrink "extended 3D" buttons (checkboxes / radio buttons) a bit */
	if ((object->ob_state & EXT3D) == EXT3D)
	{
		object->ob_y += 2;
		object->ob_x += 2;
		object->ob_height -= 4;
		object->ob_width -= 4;
	}
	return(TRUE);
}


MLOCAL WORD menu_cleanup(LPTREE tr, WORD obj)
{
	GEM_TYPED_SLOT_POINTER slot;
	LPOBJ object;

	object = desk_object_at(tr, obj);
	if (object->ob_type == G_STRING || object->ob_type == G_TITLE)
	{
		slot.words = object->ob_spec;
		chop_underline(slot.bytes);
	}
	return(TRUE);
}


	

	
static WORD ini_tree(WORD which)		/* find tree address */
{
	WORD w;
	LPTREE tree;

	w = rsrc_gaddr(R_TREE, which, (LPVOID *)&tree) ;
	
	if (!tree) return w;

	if (gem5 == -1)
	{
		char name[40];
		WORD attrib[10];

		/* First check for the ViewMAX/2 driver, which also fails on 
		 * vqt_name() */
		if (vqt_attributes(gl_handle, attrib))
		{	
			vqt_name(gl_handle, 1, name);
			if (name[0]) gem5 = 0; else gem5 = 1;
		}
		else gem5 = 0;
	}

	/* Remove underlines from buttons if not in FreeGEM */
	if (!(gl_xbuf.abilities.lo & ABLE_X3D))
	{
		map_tree(tree, ROOT, NIL, gem3_cleanup);
	}
	if ((gl_xbuf.abilities.lo & ABLE_X3D) || gem5)
	{
		map_tree(tree, ROOT, NIL, make_cbox);
	}
	return w;
}


	

	
static VOID map_tree(LPTREE tree, WORD first, WORD last, MAPROUTINE routine)
{
	REG WORD tmp1;
	REG WORD this = first;
	LPOBJ object;
	
						/* non-recursive tree	*/
						/*   traversal		*/
child:
						/* see if we need to	*/
						/*   to stop		*/
	if ( this == last)
	  return;

						/* do this object	*/
	(*routine)(tree, this);
						/* if this guy has kids	*/
						/*   then do them	*/
	object = desk_object_at(tree, this);
	tmp1 = object->ob_head;
	
	if ( tmp1 != NIL )
	{
	  this = tmp1;
	  goto child;
	}
sibling:
						/* if this obj. has a	*/
						/*   sibling that is not*/
						/*   his parent, then	*/
						/*   move to him and do	*/
						/*   him and his kids	*/
	object = desk_object_at(tree, this);
	tmp1 = object->ob_next;
	if ( (tmp1 == last) ||
	     (tmp1 == NIL) )
	  return;

	if (desk_object_at(tree, tmp1)->ob_tail != this)
	{
	  this = tmp1;
	  goto child;
	}
						/* if this is the root	*/
						/*   which has no parent*/
						/*   then stop else 	*/
						/*   move up to the	*/
						/*   parent and finish	*/
						/*   off his siblings	*/ 
	this = tmp1;
	goto sibling;
}



