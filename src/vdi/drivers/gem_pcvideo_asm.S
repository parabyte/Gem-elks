/*
 * gem_pcvideo_asm.S - PC video BIOS helpers
 *
 * GNU ia16 medium code model, far CALL in, LRET out, after PUSH BP /
 * MOV SP,BP the first 16-bit C arg sits at 6(BP)
 */

	.code16
	.text

/* adapter probe via the BIOS, 0=VGA, 1=EGA, 2=CGA color, 3=Hercules mono */
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
	xor	%ax, %ax		/* a VGA or MCGA BIOS answered */
	jmp	4f

1:
	/* an EGA BIOS answers call 12h by changing BL from the 10h marker */
	mov	$0x12, %ah
	mov	$0x10, %bl
	int	$0x10
	cmp	$0x10, %bl
	je	2f
	mov	$1, %ax
	jmp	4f

2:
	/* no EGA/VGA BIOS, use the equipment word display bits, mono startup means Hercules, color text means CGA */
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

/* current BIOS video mode, low byte */
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

/* set the BIOS video mode from the low byte of 6(BP) */
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

/* low 16 bits of the BIOS tick count, a tick is about 55 ms and the count wraps cleanly under 16-bit unsigned subtraction */
	.global	gem_vdi_clock_ticks
gem_vdi_clock_ticks:
	xor	%ah, %ah
	int	$0x1a
	mov	%dx, %ax
	lret

/* whole BIOS tick count (INT 1Ah returns CX:DX) into two caller words, 6(BP) high out pointer, 8(BP) low out pointer */
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

/* y * 80 + x / 8 for the 640-pixel EGA/VGA modes, args 6(BP) x, 8(BP) y, largest offset 38399 fits in AX */
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
/* x / 8 for an already-clipped screen coordinate */
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

/* CGA mode-6 byte offset, even scan lines at B800:0000, odd at B800:2000, each bank holds 100 rows of 80 bytes */
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
	add	%cx, %ax		/* 0x2000 bank for odd lines */
	mov	6(%bp), %dx
	shr	%dx
	shr	%dx
	shr	%dx
	add	%dx, %ax
	pop	%bp
	lret

/* Hercules 720x348 byte offset, four scan-line banks interleave at 0x0000/0x2000/0x4000/0x6000, a logical row is 90 bytes */
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
	add	%cx, %ax		/* chosen bank */
	mov	6(%bp), %dx
	shr	%dx
	shr	%dx
	shr	%dx
	add	%dx, %ax
	pop	%bx
	pop	%bp
	lret

/* copy one byte run inside a video segment, reverse copies use a load/store loop not STD so an ELKS signal mid-copy never lands in C with the direction flag set, args 6(BP) segment, 8(BP) source offset, 10(BP) dest offset, 12(BP) count, 14(BP) reverse */
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

	/* point both indexes at the last byte */
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

/* fill whole bytes across consecutive 80-byte planar scan lines, C caller set color, op and an ff bit mask, nonzero read-latches does LODSB before each STOSB so XOR/OR/AND modes see fresh latches, args 6(BP) first offset, 8(BP) bytes per row, 10(BP) rows, 12(BP) read-latches flag */
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

/* draw one aligned glyph byte onto each planar scan line, set/reset and foreground already set, args 6(BP) first A000 offset, 8(BP) near glyph row pointer, 10(BP) row count */
	.section .fartext.gem_pc_planar_glyph_rows,"ax",@progbits
	.global	gem_pc_planar_glyph_rows
gem_pc_planar_glyph_rows:
	push	%bp
	mov	%sp, %bp
	push	%si
	push	%di
	push	%ds
	push	%es
	/* ia16 medium near data lives in the stack segment */
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

/* draw an aligned text run straight from the 8x16 system font, if adding the font base offset carries then adding 1000h to the segment keeps the same real-mode address, args 6(BP) first A000 offset, 8(BP) near character pointer, 10(BP) count 1..80, 12(BP) stride (one or two), 14(BP) font segment, 16(BP) font offset */
	.global	gem_pc_planar_text_run
gem_pc_planar_text_run:
	push	%bp
	mov	%sp, %bp
	sub	$2, %sp		/* stash for the next-character offset */
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
	/* sixteen rows moved DI by 1280, net one byte lands on the next cell */
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
