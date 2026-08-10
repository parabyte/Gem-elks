/*
 * gem_tape_resident.h - AES event tape for resident ELKS GEM
 *
 * records and plays back input events. recording appends every physical change
 * to the caller's buffer, playback feeds those records back as input.
 *
 * the resident has one place where physical samples enter the AES, so thats
 * where the tape is spliced. both calls are parked like FORM_DO is - recording
 * waits until the buffer fills, playback until the tape runs out, and blocking
 * inside the trap would stop every other client - so each completes when its
 * tape does
 */

#ifndef ELKS_GEM_TAPE_RESIDENT_H
#define ELKS_GEM_TAPE_RESIDENT_H

#include "gemtrap.h"

#include "aes.h"
#include "gem_vdi_resident.h"

#define GEM_TAPE_APPL_TPLAY	14U
#define GEM_TAPE_APPL_TRECORD	15U

/* the event-change codes and the six-byte on-tape record */
#define GEM_TAPE_TCHNG		0U
#define GEM_TAPE_BCHNG		1U
#define GEM_TAPE_MCHNG		2U
#define GEM_TAPE_KCHNG		3U
#define GEM_TAPE_RECORD_BYTES	6U

/* drop any tape in progress */
VOID gem_tape_resident_reset(VOID);

/* arm recording into the caller's buffer. returns FALSE when the range isnt the caller's or another tape is running */
WORD gem_tape_resident_record(UWORD owner, UWORD segment, UWORD offset,
	UWORD records);

/* arm playback of the caller's buffer, scale is the playback speed divisor */
WORD gem_tape_resident_play(UWORD owner, UWORD segment, UWORD offset,
	UWORD records, WORD scale);

/* TRUE while OWNER's tape is still running */
WORD gem_tape_resident_busy(UWORD owner);

/* how many records the finished tape wrote, for the record call's reply */
UWORD gem_tape_resident_written(VOID);

/* give up a channel's tape when its generation goes away */
VOID gem_tape_resident_detach(UWORD owner);

/* append one physical sample to a running recording. ELAPSED is the milliseconds since the previous tick. returns TRUE when the tape has just filled, so the caller can complete the parked call */
WORD gem_tape_resident_sample(const GEM_VDI_RESIDENT_INPUT *input,
	UWORD elapsed_milliseconds);

/* take the next sample off a running playback instead of the hardware. returns FALSE when nothing is due yet, sets FINISHED when the tape has just run out */
WORD gem_tape_resident_replay(GEM_VDI_RESIDENT_INPUT *input,
	UWORD elapsed_milliseconds, WORD *finished);

#endif				/* ELKS_GEM_TAPE_RESIDENT_H */
