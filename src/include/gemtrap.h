/*
 * gemtrap.h - GEM request record for the direct-linked port
 *
 * The broker port delivered this record from a kernel INT EF handler;
 * the stock-ELKS port moves it over two plain kernel pipes instead.
 * The layout matches the retired broker ABI, so the resident AES/VDI
 * dispatch sources compile unchanged:
 *
 *   AES: CX = 200 or 201, ES:BX = AES parameter block
 *   VDI: CX = 0473h,      DS:DX = VDI parameter block
 */

#ifndef ELKS_GEM_GEMTRAP_H
#define ELKS_GEM_GEMTRAP_H

#define GEMCTL_REGISTER     0
#define GEMCTL_NEXT         1
#define GEMCTL_REPLY        2
#define GEMCTL_CANCEL       3
#define GEMCTL_UNREGISTER   4
#define GEMCTL_NEXT_NOWAIT  5
#define GEMCTL_ATTACH       6
#define GEMCTL_DETACH       7

/*
 * Tags zero through eleven are GEM's twelve PD slots.  EXIT is a
 * lifecycle record, so it gets a selector no real GEM trap can
 * produce.
 */
#define GEMTRAP_ATTACH_TAGS 12
#define GEMTRAP_CX_EXIT     0xffffU

struct gemtrap_request {
	unsigned short slot;	/* task tag; always zero in-process */
	unsigned short pid;	/* getpid() of the one linked client */
	unsigned short ax;	/* caller AX; reply AX on completion */
	unsigned short bx;	/* caller BX */
	unsigned short cx;	/* AES/VDI selector */
	unsigned short dx;	/* caller DX */
	unsigned short es;	/* caller ES */
	unsigned short ds;	/* caller DS */
	unsigned short data_limit;	/* exclusive byte limit of the caller DS */
	unsigned short generation_lo;	/* attachment generation, low half */
	unsigned short generation_hi;	/* attachment generation, high half */
};

typedef char gemtrap_request_must_be_22_bytes
	[(sizeof(struct gemtrap_request) == 22) ? 1 : -1];

/*
 * In-process stand-in for the retired gemctl() system call,
 * implemented by gem_main.c; no operation enters the kernel.
 */
int gemctl(unsigned int operation, struct gemtrap_request *request);

#endif				/* ELKS_GEM_GEMTRAP_H */
