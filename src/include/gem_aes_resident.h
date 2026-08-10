/*
 * gem_aes_resident.h - resident AES dispatcher core
 *
 * the resident AES core: app start/stop, message passing, the process calls
 * and copying the client's param block in. ELKS owns tasks, scheduling and
 * memory
 */

#ifndef ELKS_GEM_AES_RESIDENT_H
#define ELKS_GEM_AES_RESIDENT_H

#include "gemtrap.h"

#include "aes.h"

/*
 * internal result, park this call and serve another client. never goes back
 * to the client, the core writes the real result and replies later
 */
#define GEM_AES_RESIDENT_DEFERRED (-32768)

/* handle one request. unsupported calls return -1, a synthetic EXIT tears the client down and returns TRUE */
WORD gem_aes_resident_request(struct gemtrap_request *request);

/* map a trap to its PD/channel tag, -1 before APPL_INIT finishes */
WORD gem_aes_resident_application(const struct gemtrap_request *request);

/* hand back one done request, reply already in AX. FALSE means nothing done */
WORD gem_aes_resident_ready(struct gemtrap_request *request);
WORD gem_aes_resident_active(VOID);

/* run one bounded event/input pass. elapsed_milliseconds is zero after a normal request, twenty after one ELKS timer signal */
VOID gem_aes_resident_poll(UWORD elapsed_milliseconds);

#endif				/* ELKS_GEM_AES_RESIDENT_H */
