/*
 * gem_proc.h - original GEM logical PD-slot constants.
 *
 * These numbers size and name the resident AES PD table, following GEM's
 * original twelve-entry layout with channel zero reserved for Desktop and
 * channel one for the AES itself.  They are bookkeeping tags only: process
 * creation, scheduling, and memory belong entirely to the ELKS kernel, and
 * program launch goes through the original single-tasking SHEL_WRITE record
 * consumed by gem_main.c with plain vfork/execv/waitpid.
 */

#ifndef ELKS_GEM_PROC_H
#define ELKS_GEM_PROC_H

#define GEM_PROC_CHANNELS	12
#define GEM_PROC_DESKTOP	0
#define GEM_PROC_AES		1

#endif /* ELKS_GEM_PROC_H */
