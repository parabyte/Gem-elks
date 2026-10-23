/* GEM process calls backed by ELKS tasks. */

#ifndef ELKS_GEM_PROC_H
#define ELKS_GEM_PROC_H

#include "aes.h"

/* Desktop is zero, AES is one, apps use two through five. */
#define GEM_PROC_CHANNELS	6
#define GEM_PROC_DESKTOP	0
#define GEM_PROC_AES		1

/* one command plus fifteen tail arguments */
#define GEM_PROC_TAIL_BYTES	127
#define GEM_PROC_ARGV_ENTRIES	17

/* the child was already reaped */
#define GEM_PROC_STATUS_UNKNOWN	(-1)

/* cold code goes in the far bank */
#ifdef ELKS
#define GEM_PROC_COLD __far __attribute__((far_section, noinline, \
	section(".fartext.gemproc")))
#else
#define GEM_PROC_COLD
#endif

WORD proc_create(GEM_U32_WORDS ignored_base, GEM_U32_WORDS size_hint,
		 WORD isswap, WORD isgem, WORD *channel);
WORD proc_run(WORD channel, WORD isgraf, WORD isover, LPBYTE command,
	      LPBYTE classic_tail);
WORD GEM_PROC_COLD gem_proc_spawn_external(LPBYTE command, LPBYTE classic_tail,
	UWORD *pid);
WORD proc_delete(WORD channel);
WORD proc_switch(WORD channel);

/* input owner, not a scheduler */
WORD gem_proc_foreground_channel(VOID);

/* find the channel reserved for one pid */
WORD gem_proc_channel_for_pid(UWORD pid);

/* give a shell-started task a free channel */
WORD GEM_PROC_COLD gem_proc_adopt_external(UWORD pid, WORD *channel);
WORD gem_proc_release_external(WORD channel, UWORD pid);

/* reap one child, channel and status are optional */
WORD gem_proc_poll(WORD *channel, WORD *status);

#endif /* ELKS_GEM_PROC_H */
