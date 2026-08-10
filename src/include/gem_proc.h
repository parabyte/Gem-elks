/*
 * gem_proc.h - GEM logical PD-slot constants
 *
 * GEM's slot layout: channel zero is the Desktop, channel one the AES itself.
 * ELKS owns process creation and scheduling
 */

#ifndef ELKS_GEM_PROC_H
#define ELKS_GEM_PROC_H

#define GEM_PROC_CHANNELS	6
#define GEM_PROC_DESKTOP	0
#define GEM_PROC_AES		1

#endif				/* ELKS_GEM_PROC_H */
