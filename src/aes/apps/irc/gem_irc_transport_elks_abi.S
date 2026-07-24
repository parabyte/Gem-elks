/*
 * Copyright (C) 2026 Goliath Keet <gatekeeper@xt-emporium.com>
 * SPDX-License-Identifier: GPL-2.0-only
 */

/*
 * gem_irc_transport_elks_abi.S - ELKS network layouts at an 8086 word seam.
 *
 * These routines are linked into a GNU ia16 medium-model executable.  A far
 * C call places its first 16-bit argument at 6(BP), every external libc call
 * is far, and each public routine returns with LRET.  Only original 8088/8086
 * instructions and addressing modes are used.
 *
 * ELKS in_gethostbyname() unavoidably returns a four-byte IPv4 value in DX:AX.
 * The resolver stores AX as the low memory word and DX as the high memory word
 * without arithmetic or byte conversion.  sockaddr_in, fd_set, and timeval
 * are constructed here from independent words so target C never declares a
 * long, 32-bit scalar, four-byte descriptor set, or four-byte time field.
 */

	.code16
	.section .fartext.gem_irc_transport,"ax",@progbits

/* int resolve(host, address_lo, address_hi); */
	.global gem_irc_elks_resolve_ipv4
gem_irc_elks_resolve_ipv4:
	push	%bp
	mov	%sp, %bp
	push	%bx
	push	%cx
	push	%si
	push	%di
	mov	8(%bp), %si
	mov	10(%bp), %di
	test	%si, %si
	jz	2f
	test	%di, %di
	jz	2f
	push	6(%bp)
	lcall	$"in_gethostbyname!"@SEG, $in_gethostbyname
	add	$2, %sp
	mov	%ax, %bx
	mov	%dx, %cx
	/* Zero and ff:ff are both unusable resolver results. */
	mov	%bx, %ax
	or	%cx, %ax
	jz	2f
	cmp	$0xffff, %bx
	jne	1f
	cmp	$0xffff, %cx
	je	2f
1:
	mov	%bx, (%si)
	mov	%cx, (%di)
	mov	$1, %ax
	jmp	3f
2:
	xor	%ax, %ax
3:
	pop	%di
	pop	%si
	pop	%cx
	pop	%bx
	pop	%bp
	lret

/*
 * int connect_ipv4(fd, address_lo, address_hi, host_order_port);
 *
 * ELKS sockaddr_in is four adjacent words: family, network-order port,
 * address low word, address high word.  AF_INET is zero.
 * XCHG AL,AH performs the sole 16-bit port byte swap; it has no overflow or
 * rounding behavior.
 */
	.global gem_irc_elks_connect_ipv4
gem_irc_elks_connect_ipv4:
	push	%bp
	mov	%sp, %bp
	/*
	 * Reserve the eight-byte sockaddr before saving callee-owned registers.
	 * The local words are addressed at -8..-2(BP).  Saving BX/SI/DI first
	 * would put those registers in the same three words and the sockaddr
	 * stores would corrupt them before returning to C.
	 */
	sub	$8, %sp
	push	%bx
	push	%si
	push	%di
	/*
	 * ELKS/KTCP requires an explicit local bind (an ephemeral port from
	 * INADDR_ANY:0) before connect(); without it connect() is rejected.
	 * Build a zeroed sockaddr and bind first; its result is not needed.
	 */
	xor	%ax, %ax
	mov	%ax, -8(%bp)		/* AF_INET = 0 */
	mov	%ax, -6(%bp)		/* port = 0 (any) */
	mov	%ax, -4(%bp)		/* address low = 0 */
	mov	%ax, -2(%bp)		/* address high = 0 */
	lea	-8(%bp), %bx
	mov	$8, %ax
	push	%ax
	push	%bx
	push	6(%bp)
	lcall	$"bind!"@SEG, $bind
	add	$6, %sp
	xor	%ax, %ax
	mov	%ax, -8(%bp)		/* AF_INET */
	mov	12(%bp), %ax
	xchg	%al, %ah
	mov	%ax, -6(%bp)		/* network-order port */
	mov	8(%bp), %ax
	mov	%ax, -4(%bp)
	mov	10(%bp), %ax
	mov	%ax, -2(%bp)
	lea	-8(%bp), %bx
	mov	$8, %ax
	push	%ax
	push	%bx
	push	6(%bp)
	lcall	$"connect!"@SEG, $connect
	add	$6, %sp
	pop	%di
	pop	%si
	pop	%bx
	add	$8, %sp
	pop	%bp
	lret

/*
 * int select_ready(fd, want_write);
 *
 * Locals are three four-byte fd_set values followed by an eight-byte zero
 * timeval.  ELKS supports descriptors 0..19.  The one requested bit is made
 * with an 8086 variable-count shift through CL and stored as low/high words.
 * select(2) edits the sets in place; C receives only a 1/2/4 readiness mask.
 */
	.global gem_irc_elks_select_ready
gem_irc_elks_select_ready:
	push	%bp
	mov	%sp, %bp
	/*
	 * As above, reserve all twenty local bytes before the three saved words.
	 * This keeps fd_set/timeval at -20..-2(BP) and preserves the GNU ia16
	 * near-call contract for BX, SI, and DI across every readiness poll.
	 */
	sub	$20, %sp
	push	%bx
	push	%si
	push	%di
	mov	6(%bp), %cx
	test	%cx, %cx
	jns	14f
	jmp	8f
14:
	cmp	$20, %cx
	jb	15f
	jmp	8f
15:
	xor	%bx, %bx
	xor	%dx, %dx
	mov	$1, %ax
	cmp	$16, %cx
	jb	5f
	sub	$16, %cx
	shl	%cl, %ax
	mov	%ax, %dx
	jmp	6f
5:
	shl	%cl, %ax
	mov	%ax, %bx
6:
	mov	%bx, -20(%bp)		/* read fd_set low */
	mov	%dx, -18(%bp)		/* read fd_set high */
	xor	%ax, %ax
	mov	%ax, -16(%bp)
	mov	%ax, -14(%bp)
	cmpw	$0, 8(%bp)
	je	7f
	mov	%bx, -16(%bp)		/* write fd_set low */
	mov	%dx, -14(%bp)		/* write fd_set high */
7:
	mov	%bx, -12(%bp)		/* error fd_set low */
	mov	%dx, -10(%bp)		/* error fd_set high */
	xor	%ax, %ax
	mov	%ax, -8(%bp)		/* timeval seconds low */
	mov	%ax, -6(%bp)		/* timeval seconds high */
	mov	%ax, -4(%bp)		/* timeval useconds low */
	mov	%ax, -2(%bp)		/* timeval useconds high */
	lea	-8(%bp), %ax
	push	%ax
	lea	-12(%bp), %ax
	push	%ax
	cmpw	$0, 8(%bp)
	je	9f
	lea	-16(%bp), %ax
	jmp	10f
9:
	xor	%ax, %ax
10:
	push	%ax
	lea	-20(%bp), %ax
	push	%ax
	mov	6(%bp), %ax
	inc	%ax
	push	%ax
	lcall	$"select!"@SEG, $select
	add	$10, %sp
	test	%ax, %ax
	jle	11f
	xor	%ax, %ax
	mov	-20(%bp), %bx
	or	-18(%bp), %bx
	jz	12f
	or	$1, %ax
12:
	mov	-16(%bp), %bx
	or	-14(%bp), %bx
	jz	13f
	or	$2, %ax
13:
	mov	-12(%bp), %bx
	or	-10(%bp), %bx
	jz	11f
	or	$4, %ax
	jmp	11f
8:
	mov	$-1, %ax
11:
	pop	%di
	pop	%si
	pop	%bx
	add	$20, %sp
	pop	%bp
	lret
