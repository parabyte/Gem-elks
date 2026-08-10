/*
 * gem_menu_seam.c - the resident AES's menu half
 *
 * the menu half of the AES, MENU_BAR, MENU_TEXT and the pull-down
 * calls, plus the drawing and restore the menu manager needs from the
 * core, the bar rule, the desk snapshot and putting back what a dropped
 * menu covered
 */

#include "gem_aes_internal.h"

#include "gem_resident_memory.h"
#include "gem_vdi_resident.h"

/* find the generation that owns the AES-wide menu tree */
GEM_RESIDENT_PD *
gem_resident_menu_owner(VOID)
{
	GEM_RESIDENT_PD *pd;

	if (gem_resident_menu.visible != GEM_MENU_OBJECT_RESIDENT_VISIBLE)
		return (GEM_RESIDENT_PD *) 0;
	pd = gem_resident_pd_for_channel(gem_resident_menu.owner);
	if (!pd || pd->state != GEM_PD_ATTACHED
		|| pd->generation_lo != gem_resident_menu.generation_lo
		|| pd->generation_hi != gem_resident_menu.generation_hi)
		return (GEM_RESIDENT_PD *) 0;
	return pd;
}

VOID
gem_resident_menu_rectangle(const GEM_MENU_PULL_RECTANGLE *source,
	GRECT *destination)
{
	destination->g_x = source->x;
	destination->g_y = source->y;
	destination->g_w = source->width;
	destination->g_h = source->height;
}

/*
 * the menu panel box draws its 1 pixel border outward so the clip must
 * grow a pixel every side, held to the screen, or the outer black ring
 * gets cut off
 */
VOID
gem_resident_menu_grow_clip(GRECT *rectangle)
{
	WORD edge;

	if (rectangle->g_x > 0) {
		rectangle->g_x--;
		rectangle->g_w++;
	}
	if (rectangle->g_y > 0) {
		rectangle->g_y--;
		rectangle->g_h++;
	}
	rectangle->g_w++;
	edge = (WORD) (gem_resident_windows.screen_width - rectangle->g_x);
	if (rectangle->g_w > edge)
		rectangle->g_w = edge;
	rectangle->g_h++;
	edge = (WORD) (gem_resident_windows.screen_height - rectangle->g_y);
	if (rectangle->g_h > edge)
		rectangle->g_h = edge;
}

/*
 * the menu bar ends with a black rule on its last row, the object
 * renderer never draws it so every bar repaint puts it back
 */
WORD
gem_resident_menu_bar_rule(VOID)
{
	return gem_menu_object_resident_bar_rule(
		(WORD) (gem_resident_windows.box_height - 1));
}

/* draw one object or subtree straight from the active relocated menu RSC */
WORD
gem_resident_menu_draw_main(WORD object, UWORD depth, const GRECT *clip)
{
	GEM_RESIDENT_PD *pd;

	pd = gem_resident_menu_owner();
	if (!pd)
		return FALSE;
	return gem_resident_draw_object_tree(pd, gem_resident_menu.tree,
		object, depth, clip, 0, FALSE);
}

/*
 * build one member of the fixed M_DESK tree just to draw it, the real
 * state stays in gem_menu_pull_resident
 */
WORD
gem_resident_menu_draw_desk_snapshot(GEM_RESIDENT_PD *pd,
	const GEM_MENU_PULL_DESK_OBJECT *snapshot, const GRECT *rectangle)
{
	GEM_BINDINGS_POINTER_SLOT tree;
	GRECT clip;
	UWORD segment;

	if (!pd || !snapshot || !rectangle)
		return FALSE;
	clip = *rectangle;
	gem_resident_menu_desk_object.ob_next = NIL;
	gem_resident_menu_desk_object.ob_head = NIL;
	gem_resident_menu_desk_object.ob_tail = NIL;
	gem_resident_menu_desk_object.ob_type = snapshot->type;
	gem_resident_menu_desk_object.ob_flags = snapshot->flags | LASTOB;
	gem_resident_menu_desk_object.ob_state = snapshot->state;
	gem_resident_menu_desk_object.ob_x = (UWORD) rectangle->g_x;
	gem_resident_menu_desk_object.ob_y = (UWORD) rectangle->g_y;
	gem_resident_menu_desk_object.ob_width = (UWORD) rectangle->g_w;
	gem_resident_menu_desk_object.ob_height = (UWORD) rectangle->g_h;
	segment = gem_resident_data_segment();
	if ((snapshot->type & 0x00ffU) == G_BOX) {
		/* the M_DESK root box spec */
		gem_resident_menu_desk_object.ob_spec.lo = 0x1100U;
		gem_resident_menu_desk_object.ob_spec.hi = 0x00ffU;
		/* thickness -1 draws outward, let the ring past the clip */
		gem_resident_menu_grow_clip(&clip);
	} else {
		gem_resident_menu_desk_object.ob_spec.lo =
			(UWORD) snapshot->text;
		gem_resident_menu_desk_object.ob_spec.hi = segment;
	}
	tree.lo = (UWORD) &gem_resident_menu_desk_object;
	tree.hi = segment;
	return gem_resident_draw_object_tree(pd, tree, ROOT, 0,
		&clip, segment, FALSE);
}

/* draw one M_DESK row or the whole fixed root/row list */
WORD
gem_resident_menu_draw_desk(WORD object, UWORD complete,
	const GRECT *effect_rectangle)
{
	GEM_RESIDENT_PD *pd;
	GRECT rectangle;
	WORD root_x;
	WORD root_y;
	UWORD index;
	UWORD last;

	pd = gem_resident_menu_owner();
	if (!pd)
		return FALSE;
	if (!complete) {
		if (object < ROOT
			|| !gem_menu_pull_resident_desk_object((UWORD) object,
				&gem_resident_menu_desk_snapshot))
			return FALSE;
		return gem_resident_menu_draw_desk_snapshot(pd,
			&gem_resident_menu_desk_snapshot, effect_rectangle);
	}
	last = gem_menu_pull_resident_desk_count();
	if (!last || !gem_menu_pull_resident_desk_object(ROOT,
			&gem_resident_menu_desk_snapshot))
		return FALSE;
	gem_resident_menu_rectangle(&gem_resident_menu_desk_snapshot.rectangle,
		&rectangle);
	root_x = rectangle.g_x;
	root_y = rectangle.g_y;
	if (!gem_resident_menu_draw_desk_snapshot(pd,
			&gem_resident_menu_desk_snapshot, &rectangle))
		return FALSE;
	index = 1U;
	while (index <= last) {
		if (!gem_menu_pull_resident_desk_object(index,
				&gem_resident_menu_desk_snapshot))
			return FALSE;
		gem_resident_menu_rectangle
			(&gem_resident_menu_desk_snapshot.rectangle,
			&rectangle);
		rectangle.g_x = (WORD) ((UWORD) rectangle.g_x + (UWORD) root_x);
		rectangle.g_y = (WORD) ((UWORD) rectangle.g_y + (UWORD) root_y);
		if (!gem_resident_menu_draw_desk_snapshot(pd,
				&gem_resident_menu_desk_snapshot, &rectangle))
			return FALSE;
		index++;
	}
	return TRUE;
}

/*
 * restore a dismissed pull-down without a bitmap save, repaint the
 * Desktop in the exposed rectangle, then the W_ACTIVE frames, the
 * WM_REDRAW delivery, and the menu bar
 */
WORD
gem_resident_menu_restore(const GRECT *rectangle, WORD extra_owner)
{
	GRECT bar;

	if (!rectangle || rectangle->g_w <= 0 || rectangle->g_h <= 0)
		return FALSE;
	/*
	 * reserve one extra queue record when the menu manager will
	 * append MN_SELECTED after this restore, so the all-or-nothing
	 * queue check still holds
	 */
	if (!gem_window_resident_damage(&gem_resident_windows, rectangle,
			&gem_resident_window_effects)
		||
		!gem_resident_window_apply_effects_reserved
		(&gem_resident_window_effects, extra_owner))
		return FALSE;
	bar.g_x = 0;
	bar.g_y = 0;
	bar.g_w = gem_resident_windows.screen_width;
	bar.g_h = gem_resident_windows.box_height;
	/* the tree paints the rule row white, put the black rule back */
	return gem_resident_menu_draw_main(1, 8U, &bar)
		&& gem_resident_menu_bar_rule();
}

/*
 * the pull-down root is a hollow box and the text glyphs stay
 * transparent, so a single-object redraw of a title or row needs this
 * white backing fill or old pixels show between the glyphs
 */
WORD
gem_resident_menu_opaque_backing(const GRECT *rectangle)
{
	GEM_VDI_SCREEN *screen;

	if (!rectangle || rectangle->g_w <= 0 || rectangle->g_h <= 0)
		return FALSE;
	screen = gem_vdi_resident_screen();
	if (!screen)
		return FALSE;
	gem_vdi_set_clip(screen, 0, NULL);
	gem_vdi_hide_cursor(screen);
	gem_vdi_set_mode(GEM_VDI_REPLACE);
	gem_vdi_set_foreground(screen, GEM_RESIDENT_NATIVE_WHITE);
	gem_vdi_fill_rect(screen, rectangle->g_x, rectangle->g_y,
		rectangle->g_w, rectangle->g_h);
	gem_vdi_show_cursor(screen);
	return TRUE;
}

/* run the menu drawing steps and one optional AES message */
WORD
gem_resident_menu_apply_effects(const GEM_MENU_PULL_EFFECTS *effects)
{
	const GEM_MENU_PULL_DRAW_EFFECT *draw;
	GEM_RESIDENT_PD *target;
	GRECT rectangle;
	UWORD index;

	if (!effects || effects->draw_count > GEM_MENU_PULL_DRAW_EFFECTS)
		return FALSE;
	target = (GEM_RESIDENT_PD *) 0;
	if (effects->message_ready) {
		if (effects->target_owner >= GEM_PROC_CHANNELS)
			return FALSE;
		target = gem_resident_pd_for_channel(
			(WORD) effects->target_owner);
		if (!target || target->state != GEM_PD_ATTACHED
			|| target->generation_lo !=
			effects->target_generation_lo
			|| target->generation_hi !=
			effects->target_generation_hi)
			return FALSE;
		gem_resident_queue_progress(target);
		if (target->queue_index > GEM_RESIDENT_QUEUE_BYTES - 16U)
			return FALSE;
	}
	draw = effects->draw;
	index = effects->draw_count;
	while (index--) {
		gem_resident_menu_rectangle(&draw->rectangle, &rectangle);
		switch (draw->action) {
		case GEM_MENU_PULL_SAVE_AREA:
			/* restore is an exact clipped repaint, theres no backing bitmap */
			break;
		case GEM_MENU_PULL_DRAW_OBJECT:
			/*
			 * this draws an explicit selected/normal state, not
			 * an XOR toggle, so clear the object rectangle first
			 * or the old black fill survives
			 */
			if (!gem_resident_menu_opaque_backing(&rectangle))
				return FALSE;
			if (draw->tree_kind == GEM_MENU_PULL_TREE_DESK) {
				if (!gem_resident_menu_draw_desk(draw->object,
						FALSE, &rectangle))
					return FALSE;
			} else if (!gem_resident_menu_draw_main(draw->object,
					0, &rectangle))
				return FALSE;
			break;
		case GEM_MENU_PULL_DRAW_MENU:
			if (!gem_resident_menu_opaque_backing(&rectangle))
				return FALSE;
			if (draw->tree_kind == GEM_MENU_PULL_TREE_DESK) {
				if (!gem_resident_menu_draw_desk(draw->object,
						TRUE, &rectangle))
					return FALSE;
			} else {
				/* the panel border draws outward, grow its clip */
				gem_resident_menu_grow_clip(&rectangle);
				if (!gem_resident_menu_draw_main(draw->object,
						8U, &rectangle))
					return FALSE;
			}
			break;
		case GEM_MENU_PULL_RESTORE_AREA:
			if (!gem_resident_menu_restore(&rectangle,
					target ? (WORD) effects->target_owner :
					NIL))
				return FALSE;
			break;
		case GEM_MENU_PULL_REDRAW_BAR:
			if (!gem_resident_menu_draw_main(draw->object,
					8U, &rectangle)
				|| !gem_resident_menu_bar_rule())
				return FALSE;
			break;
		default:
			return FALSE;
		}
		draw++;
	}
	if (effects->redraw_all) {
		rectangle.g_x = 0;
		rectangle.g_y = 0;
		rectangle.g_w = gem_resident_windows.screen_width;
		rectangle.g_h = gem_resident_windows.box_height;
		/*
		 * a new owner may install fewer G_TITLE objects, clear the
		 * whole strip so retired glyphs cant survive the switch,
		 * then put the black rule back last
		 */
		if (!gem_resident_menu_opaque_backing(&rectangle)
			|| !gem_resident_menu_draw_main(1, 8U, &rectangle)
			|| !gem_resident_menu_bar_rule())
			return FALSE;
	}
	if (target) {
		gem_resident_enqueue_window_message(target, effects->message);
		gem_resident_queue_progress(target);
	}
	return TRUE;
}

VOID
gem_resident_menu_detach_owner(GEM_RESIDENT_PD *pd, WORD channel)
{
	gem_menu_pull_resident_detach((UWORD) channel,
		pd->generation_lo, pd->generation_hi,
		&gem_resident_menu_effects);
	/* EXIT cleanup is final for this generation, a failed draw cant stop it */
	(void) gem_resident_menu_apply_effects(&gem_resident_menu_effects);
	gem_menu_object_resident_detach(&gem_resident_menu, channel,
		pd->generation_lo, pd->generation_hi);
}

/* MENU_BAR */
WORD
gem_resident_menu_bar(const struct gemtrap_request *request)
{
	GEM_MENU_PULL_TREE view;
	GEM_RESIDENT_PD *pd;
	WORD old_owner;
	UWORD old_generation_lo;
	UWORD old_generation_hi;
	WORD channel;
	WORD result;

	pd = gem_resident_pd_for_request(request, &channel);
	if (!pd)
		return -1;
	if (!int_in[0]) {
		if (gem_menu_pull_resident_deactivate((UWORD) channel,
				pd->generation_lo, pd->generation_hi,
				&gem_resident_menu_effects)
			&&
			!gem_resident_menu_apply_effects
			(&gem_resident_menu_effects))
			return -1;
		return gem_menu_object_resident_bar(&gem_resident_menu,
			&pd->resource, addr_in[0], FALSE, channel,
			pd->generation_lo, pd->generation_hi);
	}

	/* close an earlier generations tracking while its tree is still live */
	if (gem_resident_menu.visible == GEM_MENU_OBJECT_RESIDENT_VISIBLE) {
		old_owner = gem_resident_menu.owner;
		old_generation_lo = gem_resident_menu.generation_lo;
		old_generation_hi = gem_resident_menu.generation_hi;
		if (gem_menu_pull_resident_deactivate((UWORD) old_owner,
				old_generation_lo, old_generation_hi,
				&gem_resident_menu_effects)
			&&
			!gem_resident_menu_apply_effects
			(&gem_resident_menu_effects))
			return -1;
	}
	result = gem_menu_object_resident_bar(&gem_resident_menu,
		&pd->resource, addr_in[0], TRUE, channel,
		pd->generation_lo, pd->generation_hi);
	if (!result)
		return FALSE;
	if (!gem_menu_pull_resident_tree_from_resource(&view, &pd->resource,
			addr_in[0], gem_resident_menu.object_count)
		|| !gem_menu_pull_resident_activate(&view, (UWORD) channel,
			pd->generation_lo, pd->generation_hi,
			&gem_resident_menu_effects)
		|| !gem_resident_menu_apply_effects(&gem_resident_menu_effects)) {
		(void) gem_menu_object_resident_bar(&gem_resident_menu,
			&pd->resource, addr_in[0], FALSE, channel,
			pd->generation_lo, pd->generation_hi);
		return FALSE;
	}
	return TRUE;
}

/* check a menu text pointer and remember how many bytes are left after it */
WORD
gem_resident_menu_text(const struct gemtrap_request *request,
	const GEM_RESIDENT_PD *pd, GEM_BINDINGS_POINTER_SLOT address,
	GEM_RESIDENT_MENU_TEXT_POINTER *pointer, UWORD *limit)
{
	if (!request || !pd || !pointer || !limit || !address.hi)
		return FALSE;
	if (address.hi == request->ds) {
		if (!gem_resident_memory_range(address.lo, 1U,
				request->data_limit))
			return FALSE;
		*limit = (UWORD) (request->data_limit - address.lo);
	} else if ((pd->resource.flags & GEM_RESOURCE_RESIDENT_LOADED)
		&& address.hi == pd->resource.storage.base.hi
		&& gem_resident_memory_range(address.lo, 1U,
			pd->resource.storage.bytes)) {
		*limit = (UWORD) (pd->resource.storage.bytes - address.lo);
	} else
		return FALSE;
	pointer->address.lo = address.lo;
	pointer->address.hi = address.hi;
	return TRUE;
}

/* the MENU_ICHECK through MENU_CLICK calls */
WORD
gem_resident_menu_pull(const struct gemtrap_request *request)
{
	GEM_MENU_PULL_CALL call;
	GEM_RESIDENT_MENU_TEXT_POINTER text;
	GEM_RESIDENT_PD *pd;
	GEM_MENU_PULL_TREE tree;
	GEM_BINDINGS_POINTER_SLOT text_address;
	UWORD object_count;
	UWORD owner_segment;
	UWORD text_limit;
	UWORD opcode;
	WORD channel;
	WORD handled;
	WORD result;

	pd = gem_resident_pd_for_request(request, &channel);
	if (!pd)
		return -1;
	opcode = control[0];
	owner_segment = gem_resident_data_segment();
	tree.resource = (GEM_MENU_PULL_BYTE_POINTER) 0;
	tree.bytes = 0;
	tree.segment = 0;
	tree.tree_offset = 0;
	tree.object_count = 0;
	if (opcode >= GEM_MENU_PULL_ICHECK && opcode <= GEM_MENU_PULL_TEXT) {
		result = gem_menu_object_resident_tree_count(&pd->resource,
			addr_in[0], &object_count);
		__asm__ volatile ("movw %0,%%ds"::"r" (owner_segment)
			:"memory");
		if (!result)
			return FALSE;
		result = gem_menu_pull_resident_tree_from_resource(&tree,
			&pd->resource, addr_in[0], object_count);
		__asm__ volatile ("movw %0,%%ds"::"r" (owner_segment)
			:"memory");
		if (!result)
			return FALSE;
	}
	text.pointer = (GEM_MENU_PULL_TEXT_POINTER) 0;
	text_limit = 0;
	text_address.lo = 0;
	text_address.hi = 0;
	if (opcode == GEM_MENU_PULL_TEXT)
		text_address = addr_in[1];
	else if (opcode == GEM_MENU_PULL_REGISTER)
		text_address = addr_in[0];
	if (text_address.hi
		&& !gem_resident_menu_text(request, pd, text_address,
			&text, &text_limit))
		return FALSE;

	call.owner = (UWORD) channel;
	call.generation_lo = pd->generation_lo;
	call.generation_hi = pd->generation_hi;
	call.control = control;
	call.int_in = int_in;
	call.int_out = int_out;
	call.tree = tree;
	call.text = text.pointer;
	call.text_limit = text_limit;
	handled = FALSE;
	result = gem_menu_pull_resident_dispatch(&call,
		&gem_resident_menu_effects, &handled);
	__asm__ volatile ("movw %0,%%ds"::"r" (owner_segment)
		:"memory");
	if (!handled)
		return -1;
	if (!gem_resident_menu_apply_effects(&gem_resident_menu_effects))
		return -1;
	return result;
}
