/*	DESKOBJ.C	06/11/84 - 02/08/85		Lee Lorenzen	*/
/*	merge source	5/27/87				mdf		*/

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

#if 0
EXTERN VOID	movs();
EXTERN VOID	r_set();
EXTERN WORD	objc_add();

EXTERN WORD	gl_width;
EXTERN WORD	gl_height;
#endif

GLOBAL OBJECT	gl_sampob[2] =
{
	{ NIL, NIL, NIL, G_IBOX, NONE, NORMAL, { 0, 0 }, 0, 0, 0, 0 },
	/*
	 * The original in-process Desktop's hollow window root was paired with
	 * GEM's screen-save/clear path.  ELKS deliberately keeps no large backing
	 * bitmap.  Use the equivalent monochrome opaque work root instead: black
	 * border/text, IP_SOLID pattern seven, logical-white interior zero.  Every
	 * clipped WM_REDRAW now erases old names and icons before drawing the new
	 * directory tree, using one native fill and no conversion or allocation.
	 */
	{ NIL, NIL, NIL, G_BOX,  NONE, NORMAL, { 0x1170U, 0 },
	  0, 0, 0, 0 }
};

/* First item slot to examine; every value is an unscaled OBJECT index. */
static WORD gl_next_item = NUM_WNODES + 2;

/*
 * Direct original GEMOBLIB.C ob_add() semantics for the Desktop-owned fixed
 * g_screen tree.  The resident AES must validate arbitrary application trees,
 * but these parent and child indexes refer to this client's already-bounded
 * NUM_SOBS array.  Updating the four original links here avoids an INT EF
 * round trip and a complete resident tree validation for every visible file.
 */
static WORD
obj_add_local(WORD parent, WORD child)
{
	LPOBJ parent_object;
	LPOBJ child_object;
	WORD last_child;

	if (parent < ROOT || parent >= NUM_SOBS
	    || child < ROOT || child >= NUM_SOBS || child == parent)
		return FALSE;
	parent_object = &G.g_screen[parent];
	child_object = &G.g_screen[child];
	if (child_object->ob_next != NIL)
		return FALSE;

	child_object->ob_next = parent;
	last_child = parent_object->ob_tail;
	if (last_child == NIL)
		parent_object->ob_head = child;
	else {
		if (last_child < ROOT || last_child >= NUM_SOBS) {
			child_object->ob_next = NIL;
			return FALSE;
		}
		G.g_screen[last_child].ob_next = child;
	}
	parent_object->ob_tail = child;
	return TRUE;
}


/*
*	Initialize all objects as children of the 0th root which is
*	the parent of unused objects.
*/
VOID obj_init(VOID)
{
	WORD		ii;
	LPOBJ		object;

	G.a_screen = ADDR(&G.g_screen[0]);
	gl_next_item = NUM_WNODES + 2;
	object = &G.g_screen[0];
	for (ii = 0; ii < NUM_SOBS; ii++)
	{
	  object->ob_head = NIL;
	  object->ob_next = NIL;
	  object->ob_tail = NIL;
	  object++;
	}
	/*
	 * The original in-process AES already knew the fixed g_screen[] extent.
	 * The resident ELKS AES validates a client-built OBJECT tree before it
	 * follows any link, so retain GEM's normal LASTOB terminator explicitly on
	 * the final fixed record.  This changes no object layout or link: it merely
	 * makes the existing NUM_SOBS boundary visible across the process segment.
	 */
	G.g_screen[NUM_SOBS - 1].ob_flags |= LASTOB;
	memcpy(&G.g_screen[ROOT], &gl_sampob[0], sizeof(OBJECT));
	r_set((GRECT *)&G.g_screen[ROOT].ob_x, 0, 0, gl_width, gl_height);
	for (ii = 0; ii < (NUM_WNODES+1); ii++)
	{
	  memcpy(&G.g_screen[DROOT+ii], &gl_sampob[1], sizeof(OBJECT));
	  (void) obj_add_local(ROOT, DROOT+ii);
	} /* for */
} /* obj_init */

/*
*	Allocate a window object from the screen tree by looking for 
*	the child of the parent with no size
*/
	WORD
obj_walloc(x, y, w, h)
	WORD		x, y, w, h;
{
	WORD		ii;
	LPOBJ		object;

	object = &G.g_screen[DROOT + 1];
	for (ii = DROOT; ii < (NUM_WNODES+1); ii++)
	{
	  if ( !(object->ob_width && object->ob_height) )
	    break;
	  object++;
	}
	if ( ii < (NUM_WNODES+1) )
	{
	  r_set((GRECT *)&object->ob_x, x, y, w, h);
	  return(ii+1);
	}
	else
	  return(0);
} /* obj_walloc */

/*
*	Free a window object by changing its size to zero and
*	NILing out all its children.
*/
	VOID
obj_wfree(obj, x, y, w, h)
	WORD		obj;
{
	WORD		ii, nxtob;
	LPOBJ		object;
	LPOBJ		child;

	object = desk_object_at(G.g_screen, obj);
	r_set((GRECT *)&object->ob_x, x, y, w, h);
	if (object->ob_head >= NUM_WNODES + 2
	    && object->ob_head < NUM_SOBS)
		gl_next_item = object->ob_head;
	for (ii = object->ob_head; ii > obj; ii = nxtob)
	{
	  child = desk_object_at(G.g_screen, ii);
	  nxtob = child->ob_next;
	  child->ob_next = NIL;
	}
	object->ob_head = object->ob_tail = NIL;
} /* obj_wfree */

/*
*	Routine to find and allocate a particular item object.  The
*	next free object is found by looking for any object that
*	is available (i.e., a next pointer of NIL).
*/
	WORD
obj_ialloc(wparent, x, y, w, h)
	WORD		wparent;
	WORD		x, y, w, h;
{
	WORD		ii;
	WORD		remaining;
	LPOBJ		object;

	if (gl_next_item < NUM_WNODES + 2 || gl_next_item >= NUM_SOBS)
		gl_next_item = NUM_WNODES + 2;
	ii = gl_next_item;
	remaining = NUM_SOBS - (NUM_WNODES + 2);
	while (remaining--)
	{
	  object = &G.g_screen[ii];
	  if (object->ob_next == NIL)
	    break;
	  ii++;
	  if (ii >= NUM_SOBS)
	    ii = NUM_WNODES + 2;
	}
	if (object->ob_next == NIL)
	{
	  object->ob_head = object->ob_tail = NIL;
	  if (!obj_add_local(wparent, ii))
	    return(0);
	  r_set((GRECT *)&object->ob_x, x, y, w, h);
	  gl_next_item = ii + 1;
	  if (gl_next_item >= NUM_SOBS)
	    gl_next_item = NUM_WNODES + 2;
	  return(ii);
	}
	else
	  return(0);
} /* obj_ialloc */
