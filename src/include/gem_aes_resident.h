/* resident AES dispatcher */

#ifndef ELKS_GEM_AES_RESIDENT_H
#define ELKS_GEM_AES_RESIDENT_H

#include "gem_request.h"

#include "aes.h"

/* park this request and serve another client */
#define GEM_AES_RESIDENT_DEFERRED (-32768)

/* handle one AES request */
WORD gem_aes_resident_request(struct gem_request *request);

/* return the attached channel */
WORD gem_aes_resident_application(const struct gem_request *request);

/* take one finished request */
WORD gem_aes_resident_ready(struct gem_request *request);
/* consume one socket exit record */
WORD gem_aes_resident_exit(struct gem_request *request);
WORD gem_aes_resident_active(VOID);

/* run one bounded input and timer pass */
VOID gem_aes_resident_poll(UWORD elapsed_milliseconds);

#endif				/* ELKS_GEM_AES_RESIDENT_H */
