/* ELKS types for the imported Pacific C bindings */

#ifndef ELKS_GEM_BINDINGS_PPDGEM_H
#define ELKS_GEM_BINDINGS_PPDGEM_H

#include "gem_bindings_elks.h"
#include "ppdgbind.h"

WORD gem_bindings_connect(VOID);
WORD gem_bindings_call(UWORD selector, UWORD parameter_offset);

#endif /* ELKS_GEM_BINDINGS_PPDGEM_H */
