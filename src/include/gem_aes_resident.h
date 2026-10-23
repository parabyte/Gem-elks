/* resident AES dispatcher */

#ifndef ELKS_GEM_AES_RESIDENT_H
#define ELKS_GEM_AES_RESIDENT_H

#include "gemtrap.h"

#include "aes.h"

/* park this request and serve another client */
#define GEM_AES_RESIDENT_DEFERRED (-32768)

/* handle one AES request */
WORD gem_aes_resident_request(struct gemtrap_request *request);

/* map a trap to its PD/channel tag, -1 before APPL_INIT finishes */
WORD gem_aes_resident_application(const struct gemtrap_request *request);

/* take one finished request */
WORD gem_aes_resident_ready(struct gemtrap_request *request);
/* consume one kernel exit record */
WORD gem_aes_resident_exit(struct gemtrap_request *request);
WORD gem_aes_resident_active(VOID);

/* run one bounded input and timer pass */
VOID gem_aes_resident_poll(UWORD elapsed_milliseconds);

#endif				/* ELKS_GEM_AES_RESIDENT_H */
