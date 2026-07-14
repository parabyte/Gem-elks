/*
 * IBM PC video BIOS helpers for the native ELKS GEM display drivers.
 *
 * This file contains only 8088/8086 instructions.  The project links these
 * public C entry points with the GNU ia16 medium code model, so callers use a
 * far CALL and each routine returns with LRET.  After PUSH BP / MOV SP,BP the
 * saved return offset is at 2(BP), the saved return segment is at 4(BP), and
 * the first 16-bit C argument is therefore at 6(BP).  Every routine preserves
 * the C callee-saved registers.
 */

	.code16
	.text

/*
 * Return the active adapter class.
 *
 * 0 = VGA, 1 = EGA, 2 = CGA-compatible color, 3 = Hercules-compatible mono.
 * The probe order and BIOS calls follow FreeGEM's SDUNI9 universal driver.
 */
	.global	gem_bios_video_detect
gem_bios_video_detect:
	push	%bp
	mov	%sp, %bp
	push	%bx
	push	%cx
	push	%dx
	push	%ds
	push	%es
	push	%si
	push	%di

	mov	$0x1a00, %ax
	int	$0x10
	cmp	$0x1a, %al
	jne	1f
	xor	%ax, %ax		/* VGA or MCGA BIOS is present. */
	jmp	4f

1:
	/* EGA BIOS call 12h changes BL from the sentinel value 10h. */
	mov	$0x12, %ah
	mov	$0x10, %bl
	int	$0x10
	cmp	$0x10, %bl
	je	2f
	mov	$1, %ax
	jmp	4f

2:
	/*
	 * With no EGA/VGA BIOS, use the equipment word display bits.  A mono
	 * startup display selects Hercules; either color text setting selects
	 * CGA.  Segment zero is accessed only while interrupts are unchanged,
	 * and DS is restored before returning to C.
	 */
	xor	%ax, %ax
	mov	%ax, %ds
	mov	0x0410, %ax
	and	$0x0030, %ax
	cmp	$0x0030, %ax
	jne	3f
	mov	$3, %ax
	jmp	4f
3:
	mov	$2, %ax

4:
	pop	%di
	pop	%si
	pop	%es
	pop	%ds
	pop	%dx
	pop	%cx
	pop	%bx
	pop	%bp
	lret

/* Return the low-byte BIOS video mode as an unsigned 16-bit value. */
	.global	gem_bios_video_get_mode
gem_bios_video_get_mode:
	push	%bp
	mov	%sp, %bp
	push	%bx
	mov	$0x0f, %ah
	int	$0x10
	xor	%ah, %ah
	pop	%bx
	pop	%bp
	lret

/* Set BIOS video mode from the low byte of the sole C argument. */
	.global	gem_bios_video_set_mode
gem_bios_video_set_mode:
	push	%bp
	mov	%sp, %bp
	push	%bx
	push	%cx
	push	%dx
	push	%es
	push	%si
	push	%di
	mov	6(%bp), %ax
	xor	%ah, %ah
	int	$0x10
	pop	%di
	pop	%si
	pop	%es
	pop	%dx
	pop	%cx
	pop	%bx
	pop	%bp
	lret

/*
 * Return the ROM 8 by 16 font pointer from VGA BIOS function 1130h, BH=6.
 * INT 10h returns ES:BP, so the ordinary C frame pointer is saved across the
 * interrupt.  DS is restored from its saved stack word before writing the
 * two near caller outputs.  This routine uses only original 8088/8086
 * instructions and keeps the far address as two independent 16-bit words.
 */
	.global	gem_bios_video_font_8x16
gem_bios_video_font_8x16:
	push	%bp
	mov	%sp, %bp
	push	%bx
	push	%cx
	push	%dx
	push	%ds
	push	%es
	push	%si
	push	%di
	mov	6(%bp), %si
	mov	8(%bp), %di
	push	%bp
	mov	$0x1130, %ax
	mov	$0x0600, %bx
	int	$0x10
	mov	%bp, %cx
	mov	%es, %dx
	pop	%bp
	mov	-8(%bp), %ax
	mov	%ax, %ds
	test	%si, %si
	jz	1f
	mov	%dx, (%si)
1:
	test	%di, %di
	jz	2f
	mov	%cx, (%di)
2:
	pop	%di
	pop	%si
	pop	%es
	pop	%ds
	pop	%dx
	pop	%cx
	pop	%bx
	pop	%bp
	lret

/*
 * Return the low 16 bits of the BIOS tick count maintained at 0040:006c.
 * INT 1ah function zero is present on the original IBM PC/XT BIOS.  The
 * counter advances about every 55 ms and wraps safely under 16-bit unsigned
 * subtraction, which is sufficient for GEM click timing.
 */
	.global	gem_vdi_clock_ticks
gem_vdi_clock_ticks:
	xor	%ah, %ah
	int	$0x1a
	mov	%dx, %ax
	lret

/*
 * Read the complete BIOS tick count into two caller-owned 16-bit words.
 * INT 1Ah function zero returns CX:DX.  SI and DI retain the near pointers
 * across the interrupt and are restored for the ia16 cdecl caller.  This is
 * an ABI boundary only: no 32-bit register, scalar, instruction, or helper is
 * introduced into the PC/XT runtime.
 */
	.global	gem_vdi_clock_words
gem_vdi_clock_words:
	push	%bp
	mov	%sp, %bp
	push	%si
	push	%di
	mov	6(%bp), %si
	mov	8(%bp), %di
	xor	%ah, %ah
	int	$0x1a
	test	%si, %si
	jz	1f
	mov	%cx, (%si)
1:
	test	%di, %di
	jz	2f
	mov	%dx, (%di)
2:
	pop	%di
	pop	%si
	pop	%bp
	lret

/*
 * Return y * 80 + x / 8 for the 640-pixel EGA/VGA modes.
 *
 * Every shift below is the one-bit 8086 form.  The two partial products are
 * y * 64 and y * 16, so no MUL instruction or compiler helper is involved.
 * The largest VGA offset is 479 * 80 + 79 = 38399 and therefore fits in AX.
 */
	.global	gem_pc_planar_offset
gem_pc_planar_offset:
	push	%bp
	mov	%sp, %bp
	mov	8(%bp), %ax		/* y */
	mov	%ax, %dx
	shl	%ax
	shl	%ax
	shl	%ax
	shl	%ax			/* AX = y * 16 */
	shl	%dx
	shl	%dx
	shl	%dx
	shl	%dx
	shl	%dx
	shl	%dx			/* DX = y * 64 */
	add	%dx, %ax		/* AX = y * 80 */
	mov	6(%bp), %dx		/* x */
	shr	%dx
	shr	%dx
	shr	%dx			/* DX = x / 8, rounded down */
	add	%dx, %ax
	pop	%bp
	lret
/*
 * Return x / 8 for a nonnegative, already-clipped screen coordinate.
 * Three explicit one-bit shifts avoid the slower 8088 CL-count shift used
 * by the compiler for even a constant count greater than one.
 */
	.global	gem_pc_byte_column
gem_pc_byte_column:
	push	%bp
	mov	%sp, %bp
	mov	6(%bp), %ax
	shr	%ax
	shr	%ax
	shr	%ax
	pop	%bp
	lret

/*
 * Return the IBM CGA mode-6 byte offset.
 *
 * Even scan lines start at B800:0000 and odd scan lines at B800:2000.
 * Each bank contains 100 rows of 80 bytes.  The maximum used offset is
 * 0x2000 + 99 * 80 + 79 = 16191, safely inside the 16-bit segment offset.
 */
	.global	gem_pc_cga_offset
gem_pc_cga_offset:
	push	%bp
	mov	%sp, %bp
	mov	8(%bp), %ax		/* y */
	mov	%ax, %cx
	and	$1, %cx
	jz	1f
	mov	$0x2000, %cx
	jmp	2f
1:
	xor	%cx, %cx
2:
	shr	%ax			/* AX = y / 2 */
	mov	%ax, %dx
	shl	%ax
	shl	%ax
	shl	%ax
	shl	%ax			/* AX = row * 16 */
	shl	%dx
	shl	%dx
	shl	%dx
	shl	%dx
	shl	%dx
	shl	%dx			/* DX = row * 64 */
	add	%dx, %ax
	add	%cx, %ax		/* Add the odd-line 0x2000 bank. */
	mov	6(%bp), %dx
	shr	%dx
	shr	%dx
	shr	%dx
	add	%dx, %ax
	pop	%bp
	lret

/*
 * Return the Hercules 720x348 byte offset.
 *
 * Hercules interleaves four scan-line banks at 0x0000, 0x2000, 0x4000,
 * and 0x6000.  A logical row is 90 bytes.  The shift/add sequence forms
 * row * (64 + 16 + 8 + 2) without multiplication.  BX is callee-saved by
 * the ia16 C ABI, so it is explicitly preserved.
 */
	.global	gem_pc_hercules_offset
gem_pc_hercules_offset:
	push	%bp
	mov	%sp, %bp
	push	%bx
	mov	8(%bp), %ax		/* y */
	mov	%ax, %cx
	and	$3, %cx
	jz	3f
	cmp	$1, %cx
	jne	1f
	mov	$0x2000, %cx
	jmp	3f
1:
	cmp	$2, %cx
	jne	2f
	mov	$0x4000, %cx
	jmp	3f
2:
	mov	$0x6000, %cx
3:
	shr	%ax
	shr	%ax			/* AX = y / 4 */
	mov	%ax, %dx
	shl	%dx
	shl	%dx
	shl	%dx
	shl	%dx
	shl	%dx
	shl	%dx			/* DX = row * 64 */
	mov	%ax, %bx
	shl	%bx
	shl	%bx
	shl	%bx
	shl	%bx			/* BX = row * 16 */
	add	%bx, %dx		/* DX = row * 80 */
	mov	%ax, %bx
	shl	%bx
	shl	%bx
	shl	%bx			/* BX = row * 8 */
	add	%bx, %dx		/* DX = row * 88 */
	shl	%ax			/* AX = row * 2 */
	add	%dx, %ax		/* AX = row * 90 */
	add	%cx, %ax		/* Add the selected 0x2000-byte bank. */
	mov	6(%bp), %dx
	shr	%dx
	shr	%dx
	shr	%dx
	add	%dx, %ax
	pop	%bx
	pop	%bp
	lret

/*
 * Copy one bounded byte run inside a physical video segment.
 *
 * The VGA caller selects write mode one first.  Each MOVSB source read then
 * loads all four VGA latches, and its destination write commits those latches
 * to the new byte.  CGA and Hercules use the same routine as an ordinary
 * packed-byte memmove.  Forward copies use REP MOVSB.  Reverse copies use an
 * explicit load/store loop instead of STD so an asynchronous ELKS signal can
 * never enter C code with the 8086 direction flag set.
 *
 * Arguments: segment, source offset, destination offset, count, reverse.
 * C clips count to at most one scan line (80 or 90 bytes), and adjusts neither
 * endpoint.  All additions below therefore remain inside the 16-bit segment.
 */
	.global	gem_pc_video_copy_bytes
gem_pc_video_copy_bytes:
	push	%bp
	mov	%sp, %bp
	push	%ds
	push	%es
	push	%si
	push	%di

	mov	6(%bp), %ax
	mov	%ax, %ds
	mov	%ax, %es
	mov	8(%bp), %si
	mov	10(%bp), %di
	mov	12(%bp), %cx
	jcxz	3f
	cmpw	$0, 14(%bp)
	je	2f

	/* Point both indexes at the final byte without multiplication. */
	mov	%cx, %ax
	dec	%ax
	add	%ax, %si
	add	%ax, %di
1:
	movb	(%si), %al
	movb	%al, %es:(%di)
	dec	%si
	dec	%di
	loop	1b
	jmp	3f
2:
	cld
	rep movsb
3:
	pop	%di
	pop	%si
	pop	%es
	pop	%ds
	pop	%bp
	lret

/*
 * Fill complete bytes in consecutive 80-byte planar scan lines.
 *
 * The C caller has already selected set/reset color, logical operation, and
 * an ff bit mask.  Replace and clear therefore need only STOSB: every bit is
 * overwritten and no VGA latch value participates.  XOR, OR, and AND first
 * read the identical byte with LODSB, then STOSB applies the selected logical
 * operation against the freshly loaded latches.  AX's byte value is ignored
 * by enabled set/reset hardware, but setting it to one documents the same
 * value used by the former C read/OR/write loop.
 *
 * Arguments: first offset, bytes per row, row count, read-latches Boolean.
 * A row is at most 80 bytes and the final VGA byte offset is at most 38399.
 */
	.global	gem_pc_planar_fill_rows
gem_pc_planar_fill_rows:
	push	%bp
	mov	%sp, %bp
	push	%bx
	push	%ds
	push	%es
	push	%si
	push	%di

	mov	$0xa000, %ax
	mov	%ax, %es
	mov	6(%bp), %di
	mov	8(%bp), %bx
	mov	10(%bp), %dx
	or	%dx, %dx
	jz	7f
	cld
	cmpw	$0, 12(%bp)
	jne	5f

	mov	$1, %al
4:
	mov	%bx, %cx
	rep stosb
	add	$80, %di
	sub	%bx, %di
	dec	%dx
	jne	4b
	jmp	7f

5:
	mov	%es, %ax
	mov	%ax, %ds
	mov	%di, %si
6:
	mov	%bx, %cx
8:
	lodsb
	stosb
	loop	8b
	add	$80, %si
	sub	%bx, %si
	add	$80, %di
	sub	%bx, %di
	dec	%dx
	jne	6b
7:
	pop	%di
	pop	%si
	pop	%es
	pop	%ds
	pop	%bx
	pop	%bp
	lret

/*
 * Overlay one aligned native BIOS-font byte per planar scan line.
 *
 * C medium-model arguments:
 *   6(BP)  first A000 video offset, scale one byte
 *   8(BP)  near pointer to unchanged glyph row bytes
 *  10(BP)  row count, normally sixteen
 *
 * The caller has already enabled set/reset and selected the foreground
 * color.  OUTW DX,AX is an original 8086 instruction: AL selects graphics
 * controller register eight and AH supplies its exact eight-pixel mask.
 * Blank source rows need neither port nor video access.  ES alone addresses
 * video, so DS continues to address the resident glyph buffer throughout.
 */
	.section .fartext.gem_pc_planar_glyph_rows,"ax",@progbits
	.global	gem_pc_planar_glyph_rows
gem_pc_planar_glyph_rows:
	push	%bp
	mov	%sp, %bp
	push	%si
	push	%di
	push	%ds
	push	%es
	/* GNU ia16 medium data and near pointers use the stack segment. */
	push	%ss
	pop	%ds

	mov	6(%bp), %di
	mov	8(%bp), %si
	mov	10(%bp), %cx
	mov	$0xa000, %ax
	mov	%ax, %es
	mov	$0x03ce, %dx
	or	%cx, %cx
	jz	3f
1:
	lodsb
	or	%al, %al
	jz	2f
	mov	%al, %ah
	mov	$8, %al
	outw	%ax, %dx
	orb	$1, %es:(%di)
2:
	add	$80, %di
	loop	1b
3:
	pop	%es
	pop	%ds
	pop	%di
	pop	%si
	pop	%bp
	lret

/*
 * Overlay a complete aligned text run directly from the BIOS 8-by-16 font.
 *
 * C medium-model arguments:
 *   6(BP)  first A000 video offset, scale one byte
 *   8(BP)  near resident character pointer
 *  10(BP)  character count, 1..80
 *  12(BP)  character stride, exactly one or two bytes
 *  14(BP)  BIOS font segment
 *  16(BP)  BIOS font base offset
 *
 * The outer loop reads each low-byte character through SS, because GNU ia16
 * medium near data shares the stack segment.  Four original 8086 single-bit
 * shifts form character*16.  If adding the BIOS base offset carries, adding
 * 1000h to the explicit segment preserves the same real-mode physical byte
 * address without a wide pointer.  DS then addresses the native sixteen font
 * bytes while ES remains A000 for the entire string.  After sixteen additions
 * of eighty, subtracting 1279 advances the next glyph by exactly one screen
 * byte.  At count eighty the largest horizontal advance is 79 bytes and every
 * already-clipped video offset remains at or below 38399.
 */
	.global	gem_pc_planar_text_run
gem_pc_planar_text_run:
	push	%bp
	mov	%sp, %bp
	sub	$2, %sp		/* Saved next-character near offset. */
	push	%bx
	push	%si
	push	%di
	push	%ds
	push	%es

	push	%ss
	pop	%ds
	mov	6(%bp), %di
	mov	8(%bp), %si
	mov	10(%bp), %bx
	mov	$0xa000, %ax
	mov	%ax, %es
	or	%bx, %bx
	jz	5f
1:
	movb	(%si), %al
	inc	%si
	cmpw	$2, 12(%bp)
	jne	2f
	inc	%si
2:
	mov	%si, -2(%bp)
	xor	%ah, %ah
	shl	%ax
	shl	%ax
	shl	%ax
	shl	%ax
	add	16(%bp), %ax
	mov	14(%bp), %dx
	jnc	3f
	add	$0x1000, %dx
3:
	mov	%ax, %si
	mov	%dx, %ds
	mov	$16, %cx
4:
	lodsb
	or	%al, %al
	jz	6f
	mov	%al, %ah
	mov	$8, %al
	mov	$0x03ce, %dx
	outw	%ax, %dx
	orb	$1, %es:(%di)
6:
	add	$80, %di
	loop	4b
	/* Sixteen rows moved DI by 1280; net one byte reaches the next cell. */
	sub	$1279, %di
	push	%ss
	pop	%ds
	mov	-2(%bp), %si
	dec	%bx
	jne	1b
5:
	pop	%es
	pop	%ds
	pop	%di
	pop	%si
	pop	%bx
	mov	%bp, %sp
	pop	%bp
	lret

	.section .note.GNU-stack,"",@progbits
