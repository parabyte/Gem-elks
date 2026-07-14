/*
 * gem_aes_resident.h - bounded resident FreeGEM AES dispatcher nucleus.
 *
 * This is not the complete graphical AES.  It exposes the first directly
 * ported GEMSUPER.C unit: application lifecycle and message rendezvous, the
 * GEM/XM process calls, per-PD original GEM resource ownership, and the
 * original XIF parameter-block copy.  ELKS retains ownership of tasks,
 * scheduling, signals, executable segments, and process memory.
 */

#ifndef ELKS_GEM_AES_RESIDENT_H
#define ELKS_GEM_AES_RESIDENT_H

#include <linuxmt/gemtrap.h>

#include "aes.h"

/*
 * Internal owner result: keep this delivered trap asleep and dequeue another
 * client.  It never crosses INT EF as an AES return value.  The resident core
 * later writes the real result into int_out[0] and queues a GEMCTL_REPLY.
 */
#define GEM_AES_RESIDENT_DEFERRED (-32768)

/*
 * Service one delivered broker record.  AES requests return their original
 * signed 16-bit result.  Unsupported AES/VDI operations return -1 without
 * waiting.  Synthetic EXIT records are acknowledged with GEMCTL_DETACH and
 * return TRUE after their retained application state has been removed.
 */
WORD gem_aes_resident_request(struct gemtrap_request *request);

/*
 * Resolve a delivered trap to its attached original PD/channel tag.  The VDI
 * owner uses this identity for per-application workstation state; a client
 * which has not completed APPL_INIT receives -1.
 */
WORD gem_aes_resident_application(
	const struct gemtrap_request *request);

/*
 * Return one message/event request made ready by a different client.  The
 * supplied request already contains its signed 16-bit reply in AX.  FALSE
 * means that the fixed completion queue is empty.
 */
WORD gem_aes_resident_ready(struct gemtrap_request *request);
WORD gem_aes_resident_exit(struct gemtrap_request *request);

/*
 * Run one bounded event/input service pass in ordinary owner context.
 * elapsed_milliseconds has scale one and is zero after a normal request or
 * twenty after one ELKS timer signal; timer subtraction saturates at zero.
 */
VOID gem_aes_resident_poll(UWORD elapsed_milliseconds);

#endif /* ELKS_GEM_AES_RESIDENT_H */
