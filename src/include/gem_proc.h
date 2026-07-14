/*
 * gem_proc.h - original GEM process calls backed by ELKS processes.
 *
 * Multi-application GEM used logical channel numbers and four-byte physical
 * DOS memory addresses.  ELKS already owns process address spaces, so this
 * interface keeps the original call names and channel control flow but makes
 * every four-byte compatibility value an explicit pair of 16-bit words.
 * There is no C `long`, no hidden 32-bit arithmetic, and no physical-address
 * arithmetic at this boundary.
 */

#ifndef ELKS_GEM_PROC_H
#define ELKS_GEM_PROC_H

#include "aes.h"

/*
 * Original multi-application GEM reserved channel zero for Desktop and
 * channel one for AES.  Ten further channels give the original twelve-entry
 * process table without exceeding the 16-entry ELKS kernel task table.
 */
#define GEM_PROC_CHANNELS	12
#define GEM_PROC_DESKTOP	0
#define GEM_PROC_AES		1

/*
 * A classic GEM command tail contains one unsigned length byte followed by
 * at most 127 command bytes.  ELKS receives an argv vector, so the seam
 * tokenizes that tail before vfork.  Fifteen tail arguments plus argv[0] are
 * accepted; exceeding either fixed bound fails with E2BIG rather than
 * truncating a program name or argument.
 */
#define GEM_PROC_TAIL_BYTES	127
#define GEM_PROC_ARGV_ENTRIES	17

/* Returned by gem_proc_poll when ELKS reports that somebody else reaped it. */
#define GEM_PROC_STATUS_UNKNOWN	(-1)

WORD proc_create(GEM_U32_WORDS ignored_base, GEM_U32_WORDS size_hint,
		 WORD isswap, WORD isgem, WORD *channel);
WORD proc_run(WORD channel, WORD isgraf, WORD isover, LPBYTE command,
	      LPBYTE classic_tail);
WORD proc_delete(WORD channel);
WORD proc_info(WORD channel, WORD *isswap, WORD *isgem,
	       GEM_U32_WORDS *base, GEM_U32_WORDS *channel_size,
	       GEM_U32_WORDS *end_memory, GEM_U32_WORDS *swap_size,
	       GEM_U32_WORDS *interrupt_address);
WORD proc_switch(WORD channel);
WORD proc_shrink(WORD channel);
WORD proc_setblock(WORD channel);

/*
 * Return the current original GEM logical foreground channel.  ELKS remains
 * the real scheduler; this one-word tag is consumed only by resident AES
 * mouse and keyboard routing.
 */
WORD gem_proc_foreground_channel(VOID);

/*
 * Return the original logical channel which owns one exec-created ELKS PID.
 * The resident AES uses this after a child's APPL_INIT trap so its persistent
 * GEM PD tag matches the channel reserved by proc_create().  NIL means that
 * the PID is not a live GEM process.  The scan is bounded to twelve four-byte
 * records and performs no allocation or process-memory lookup.
 */
WORD gem_proc_channel_for_pid(UWORD pid);

/*
 * Adopt an already-running ELKS process which reached APPL_INIT without
 * being created by proc_run().  This is the direct POSIX equivalent of GEM's
 * preloaded accessory entries: ELKS keeps sole ownership of the task and its
 * memory, while GEM allocates only one logical channel number.  Dynamic
 * channels start at two.  No process is created, copied, reparented, or
 * waited for by this call.
 *
 * An adopted process is not a child of the resident AES.  Its kernel-issued
 * synthetic GEMTRAP_CX_EXIT is therefore the authoritative lifetime event.
 * gem_proc_release_external() releases only a matching adopted PID/channel;
 * it cannot release a proc_run() child or a reused channel accidentally.
 */
WORD gem_proc_adopt_external(UWORD pid, WORD *channel);
WORD gem_proc_release_external(WORD channel, UWORD pid);

/*
 * Poll one completed child without blocking.  TRUE means that *channel and
 * *status contain a completion to turn into GEM's PR_FINISH message.  The
 * status is the unscaled 16-bit ELKS wait status; WIFEXITED and related ELKS
 * macros may inspect it.  FALSE means no child changed state.
 */
WORD gem_proc_poll(WORD *channel, WORD *status);

#endif /* ELKS_GEM_PROC_H */
