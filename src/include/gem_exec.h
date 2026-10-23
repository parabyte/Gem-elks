/* start one ELKS process */

#ifndef ELKS_GEM_EXEC_H
#define ELKS_GEM_EXEC_H

#include "aes.h"

#ifdef ELKS
#define GEM_EXEC_COLD __far __attribute__((far_section, noinline, \
	section(".fartext.gemexec")))
#else
#define GEM_EXEC_COLD
#endif

WORD GEM_EXEC_COLD gem_exec(LPBYTE command, LPBYTE classic_tail, UWORD *pid);

#endif /* ELKS_GEM_EXEC_H */
