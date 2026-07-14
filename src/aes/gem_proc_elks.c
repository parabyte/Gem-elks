/*
 * gem_proc_elks.c - original GEM/XM logical process channels on ELKS.
 *
 * Original process-channel logic is derived from Digital Research's
 * GEMPRLIB.C (1985-1986), released by Caldera Thin Clients, Inc. under the
 * GNU General Public License, version 2.  The pinned source identity and an
 * exact function-by-function mapping are recorded in
 * docs/ELKS_MULTIAPP.md.  The non-build reference snapshot is deliberately
 * omitted from the deployment package.
 *
 * Modified for ELKS on 2026-07-13.  GEM/XM allocated DOS physical arenas and
 * scheduled those arenas itself.  Repeating that allocator in userspace
 * would conflict with the ELKS kernel and would be both unsafe and expensive
 * on an 8088.  This adaptation therefore retains GEM/XM's original proc_*
 * names, twelve-channel PID bitmap, allocation order, overlay classification,
 * and return-to-Desktop rule while letting ELKS alone create processes,
 * allocate address spaces, deliver signals, schedule tasks, and reap children.
 *
 * All counters, flags, PIDs, wait values, and indexes are 8 or 16 bits.  The
 * historic four-byte memory fields are opaque GEM_U32_WORDS pairs.  They are
 * copied for compatibility only; no multiplication, division, or physical
 * address conversion is performed.
 */

#include "gem_proc.h"

#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#if !defined(ELKS) || !ELKS
#error gem_proc_elks.c requires the ELKS 16-bit process ABI
#endif

#define GEM_PROC_FREE		0
#define GEM_PROC_SYSTEM		1
#define GEM_PROC_RESERVED	2
#define GEM_PROC_RUNNING	3

#define GEM_PROC_NOT_ACCESSORY	0
#define GEM_PROC_ACCESSORY_OVERLAY 3
#define GEM_PROC_CONTINUOUS_OVERLAY 4

#define GEM_PROC_FLAG_ISSWAP	0x01
#define GEM_PROC_FLAG_ISGEM	0x02
#define GEM_PROC_FLAG_ISGRAF	0x04
#define GEM_PROC_FLAG_ACCESSORY	0x08
#define GEM_PROC_FLAG_CONTINUOUS	0x10
#define GEM_PROC_FLAG_EXTERNAL	0x20

#define GEM_PROC_TERM_POLLS	4

/*
 * ELKS timeval is a four-word external ABI.  Keep it isolated here rather
 * than introducing a C long: seconds are zero and microseconds are exactly
 * 10000 (ten milliseconds), with both high words zero.  No scaling,
 * rounding, saturation, multiply, or divide occurs.
 */
typedef struct __attribute__((packed)) gem_proc_timeval_words {
	UWORD seconds_lo;
	UWORD seconds_hi;
	UWORD useconds_lo;
	UWORD useconds_hi;
} GEM_PROC_TIMEVAL_WORDS;

typedef struct gem_proc_slot {
	UWORD pid;
	UBYTE state;
	UBYTE flags;
} GEM_PROC_SLOT;

/*
 * Four bytes per slot keeps the complete original twelve-channel table at
 * 48 bytes.  No DOS base or size is cached: ELKS is the sole owner of process
 * memory, and proc_info() returns zero in those obsolete compatibility fields.
 * The compile-time check also protects the assembly step below: a near pointer
 * advances by exactly four unscaled bytes, with no wrap for this static table.
 * The inline ADD prevents loop strength reduction from turning a variable
 * channel number into an expensive 8086 MUL.
 */
typedef BYTE GEM_PROC_SLOT_MUST_BE_FOUR_BYTES
	[(sizeof(GEM_PROC_SLOT) == 4) ? 1 : -1];

#define GEM_PROC_NEXT_SLOT(slot) \
	__asm__ volatile ("addw %1,%0" : "+r" (slot) \
			  : "i" (sizeof(GEM_PROC_SLOT)) : "cc")

/*
 * Static near data avoids malloc and makes exhaustion deterministic.  A slot
 * is found with pointer increments, never `index * sizeof (slot)`, so even a
 * conservative 8086 compiler has no reason to emit MUL.
 */
static GEM_PROC_SLOT gem_proc_slots[GEM_PROC_CHANNELS];
static UBYTE gem_proc_initialized;
/* FreeGEM/XM's original name is retained for its one-word PID bit vector. */
static UWORD gl_pids;
/* Volatile retains the hand-off for the active shared AES owner. */
static volatile WORD gem_proc_foreground;

/*
 * The active shared AES owner serializes launch requests.  Consequently one
 * bounded tail buffer, argv vector, and ELKS exec-stack image are sufficient
 * and save stack in the small ELKS data segment.  They are completely
 * prepared before vfork.
 */
static BYTE gem_proc_tail_buffer[GEM_PROC_TAIL_BYTES + 1];
static LPBYTE gem_proc_argv[GEM_PROC_ARGV_ENTRIES];

/*
 * ELKS's public execv() first serializes argv and environ into memory obtained
 * with sbrk().  The resident AES deliberately has only a 128-byte heap, while
 * the production seven-entry boot environment plus one absolute application
 * pathname needs 145 bytes.  Calling execv() in the vfork child therefore
 * failed before its _execve syscall and the child silently exited with 127.
 *
 * Build the kernel's exact exec-stack image here, before vfork, and pass it to
 * _execve directly.  The image is 512 unscaled bytes represented as 256
 * aligned 16-bit words.  This covers all sixteen accepted argv entries, the
 * complete 127-byte classic tail, the resident shell's bounded command path,
 * and the twelve-entry ELKS boot environment with room to spare.  Up to 32
 * inherited environment entries are accepted; either too many entries or too
 * many bytes fails with E2BIG before a process is created.  There is no heap
 * call, allocation, multiplication, division, pointer-width conversion, or
 * child-side buffer construction.
 *
 * The ELKS stack format is:
 *
 *   argc word
 *   argv byte-offset words, zero word
 *   environ byte-offset words, zero word
 *   argv strings followed by environment strings
 *
 * Every offset is relative to byte zero of this array.  The 512-byte bound is
 * below one 16-bit offset space, so all additions are checked by subtraction
 * before they occur and can neither wrap nor saturate.
 */
#define GEM_PROC_EXEC_BYTES          512U
#define GEM_PROC_EXEC_WORDS          256U
#define GEM_PROC_EXEC_ENV_ENTRIES     32U

/*
 * The resident owner already uses almost all of its first 64 KiB code
 * segment.  Keep this cold launch-only serializer in the native second 8086
 * code segment.  Calls are ordinary medium-model far CALL/RETF operations;
 * data remains near and no protected-mode or 286 mechanism is involved.
 */
#define GEM_PROC_COLD \
	__far __attribute__((far_section, noinline, \
		section(".fartext.gemproc")))

typedef BYTE GEM_PROC_EXEC_WORD_SIZE_MUST_MATCH
	[(GEM_PROC_EXEC_BYTES == GEM_PROC_EXEC_WORDS + GEM_PROC_EXEC_WORDS)
	 ? 1 : -1];

static UWORD gem_proc_exec_words[GEM_PROC_EXEC_WORDS];

/*
 * True vfork shares this one word until the child either installs its new
 * image or exits.  Zero means _execve succeeded and replaced the child data
 * segment.  A positive ELKS errno means the syscall returned in the shared
 * segment; the parent reaps that already-dead child and reports failure while
 * the GEM channel is still reserved.  Volatile prevents the compiler from
 * caching the pre-vfork zero across the kernel scheduling boundary.
 */
static volatile WORD gem_proc_exec_error;

static WORD GEM_PROC_COLD
gem_proc_exec_string_bytes(const BYTE *text, UWORD *bytes)
{
	UWORD count;

	if (!text || !bytes) {
		errno = E2BIG;
		return FALSE;
	}
	count = 1;
	while (*text++) {
		if (count >= GEM_PROC_EXEC_BYTES) {
			errno = E2BIG;
			return FALSE;
		}
		count++;
	}
	*bytes = count;
	return TRUE;
}

/* Measure one NULL-terminated pointer vector without changing the output. */
static WORD GEM_PROC_COLD
gem_proc_exec_measure(LPBYTE *entries, UWORD limit, UWORD *entry_count,
	UWORD *string_bytes)
{
	UWORD count;
	UWORD length;
	UWORD total;

	count = 0;
	total = 0;
	if (entries) {
		while (*entries) {
			if (count >= limit
			    || !gem_proc_exec_string_bytes(*entries, &length)
			    || length > GEM_PROC_EXEC_BYTES - total) {
				errno = E2BIG;
				return FALSE;
			}
			total += length;
			count++;
			entries++;
		}
	}
	*entry_count = count;
	*string_bytes = total;
	return TRUE;
}

/* Copy measured entries and publish their exact byte offsets to ELKS. */
static VOID GEM_PROC_COLD
gem_proc_exec_pack(LPBYTE *entries, UWORD **slot_pointer,
	BYTE **text_pointer, UWORD *offset)
{
	const BYTE *source;
	BYTE *destination;
	UWORD *slot;
	BYTE character;

	slot = *slot_pointer;
	destination = *text_pointer;
	while (entries && *entries) {
		*slot++ = *offset;
		source = *entries++;
		do {
			character = *source++;
			*destination++ = character;
			(*offset)++;
		} while (character);
	}
	*slot_pointer = slot;
	*text_pointer = destination;
}

static WORD GEM_PROC_COLD
gem_proc_build_exec_stack(UWORD *stack_bytes)
{
	LPBYTE *environment;
	BYTE *text;
	UWORD *slot;
	UWORD argument_count;
	UWORD argument_bytes;
	UWORD environment_count;
	UWORD environment_bytes;
	UWORD vector_words;
	UWORD vector_bytes;
	UWORD offset;

	if (!stack_bytes
	    || !gem_proc_exec_measure(gem_proc_argv,
		GEM_PROC_ARGV_ENTRIES - 1U, &argument_count,
		&argument_bytes))
		return FALSE;
	environment = (LPBYTE *) environ;
	if (!gem_proc_exec_measure(environment, GEM_PROC_EXEC_ENV_ENTRIES,
		&environment_count, &environment_bytes))
		return FALSE;

	/* argc plus the two pointer-vector terminators require three words. */
	vector_words = argument_count + environment_count;
	vector_words++;
	vector_words++;
	vector_words++;
	vector_bytes = vector_words + vector_words;
	if (vector_bytes > GEM_PROC_EXEC_BYTES
	    || argument_bytes > GEM_PROC_EXEC_BYTES - vector_bytes) {
		errno = E2BIG;
		return FALSE;
	}
	offset = vector_bytes + argument_bytes;
	if (environment_bytes > GEM_PROC_EXEC_BYTES - offset) {
		errno = E2BIG;
		return FALSE;
	}
	offset = vector_bytes;
	slot = gem_proc_exec_words;
	text = (BYTE *) gem_proc_exec_words + vector_bytes;
	*slot++ = argument_count;
	gem_proc_exec_pack(gem_proc_argv, &slot, &text, &offset);
	*slot++ = 0;
	gem_proc_exec_pack(environment, &slot, &text, &offset);
	*slot = 0;
	*stack_bytes = offset;
	return TRUE;
}

static VOID
gem_proc_term_pause(VOID)
{
	GEM_PROC_TIMEVAL_WORDS timeout;

	timeout.seconds_lo = 0;
	timeout.seconds_hi = 0;
	timeout.useconds_lo = 10000;
	timeout.useconds_hi = 0;
	(void) select(1, NULL, NULL, NULL,
		      (struct timeval *) (VOID *) &timeout);
}

/*
 * These are direct 16-bit adaptations of FreeGEM/XM GEMPRLIB.C's
 * pr_scpid()/pr_gpid() channel allocator.  Only NUM_PDS is replaced by the
 * fixed ELKS channel count and the bit vector is explicitly unsigned.  The
 * original code used one bit per logical GEM process.  Retaining that
 * representation costs one word, scans at most twelve bits, and follows
 * GEM/XM's lowest-free-channel allocation order without reproducing its DOS
 * arena manager.
 */
static VOID
pr_scpid(WORD pid, WORD isclear)
{
	WORD ii;
	UWORD bv;

	bv = 0x0001U;
	for (ii = 0; ii < GEM_PROC_CHANNELS; ii++) {
		if (ii == pid) {
			if (isclear)
				gl_pids &= (UWORD) ~bv;
			else
				gl_pids |= bv;
		} else {
			/* A one-bit shift is a native 8086 operation. */
			bv <<= 1;
		}
	}
}

static WORD
pr_gpid(VOID)
{
	WORD ii;
	UWORD bv;

	bv = 0x0001U;
	for (ii = 0; ii < GEM_PROC_CHANNELS; ii++) {
		if (!(bv & gl_pids))
			return ii;
		bv <<= 1;
	}
	return NIL;
}

static VOID
gem_proc_zero_pair(GEM_U32_WORDS *value)
{
	if (value) {
		value->lo = 0;
		value->hi = 0;
	}
}

static VOID
gem_proc_init(VOID)
{
	GEM_PROC_SLOT *slot;
	UWORD count;

	if (gem_proc_initialized)
		return;

	/*
	 * C startup already zeroed the table.  Mark only the two original
	 * resident channels.  Their address and size fields intentionally remain
	 * zero because ELKS does not expose another task's physical arena.
	 */
	slot = gem_proc_slots;
	gl_pids = 0;
	pr_scpid(GEM_PROC_DESKTOP, FALSE);
	pr_scpid(GEM_PROC_AES, FALSE);
	slot->state = GEM_PROC_SYSTEM;
	slot->flags = GEM_PROC_FLAG_ISGEM;
	GEM_PROC_NEXT_SLOT(slot);
	slot->state = GEM_PROC_SYSTEM;
	slot->flags = GEM_PROC_FLAG_ISGEM;

	/* Explicitly clear the remaining states for warm/re-entry environments. */
	GEM_PROC_NEXT_SLOT(slot);
	count = GEM_PROC_CHANNELS - 2;
	while (count--) {
		slot->state = GEM_PROC_FREE;
		GEM_PROC_NEXT_SLOT(slot);
	}

	gem_proc_foreground = GEM_PROC_DESKTOP;
	gem_proc_initialized = TRUE;
}

static GEM_PROC_SLOT *
gem_proc_slot(WORD channel)
{
	GEM_PROC_SLOT *slot;
	UWORD count;

	if (channel < 0 || channel >= GEM_PROC_CHANNELS)
		return NULL;

	slot = gem_proc_slots;
	count = (UWORD) channel;
	while (count--)
		GEM_PROC_NEXT_SLOT(slot);
	return slot;
}

static VOID
gem_proc_release(GEM_PROC_SLOT *slot, WORD channel)
{
	/*
	 * Clear fields individually.  This avoids pulling memset into the hot
	 * process path and makes it obvious that no stale PID can be signalled.
	 * GEMPRLIB.C documents the original invariant: whenever a program
	 * terminates, control returns to Desktop.  Apply it before freeing the
	 * channel so input can never remain assigned to a dead ELKS task.
	 */
	if (gem_proc_foreground == channel)
		gem_proc_foreground = GEM_PROC_DESKTOP;
	slot->pid = 0;
	slot->state = GEM_PROC_FREE;
	slot->flags = 0;
	pr_scpid(channel, TRUE);
}

static WORD
gem_proc_tail_space(UBYTE character)
{
	return character == (UBYTE) ' ' || character == (UBYTE) '\t';
}

static WORD
gem_proc_tail_end(UBYTE character)
{
	return character == 0 || character == (UBYTE) '\r'
	       || character == (UBYTE) '\n';
}

static WORD GEM_PROC_COLD
gem_proc_build_argv(LPBYTE command, LPBYTE classic_tail)
{
	LPBYTE *argument;
	const UBYTE *tail;
	UWORD length;
	UWORD input;
	UWORD output;
	UWORD arguments_left;
	UBYTE character;
	UBYTE quoted;

	if (!command || !command[0]) {
		errno = ENOENT;
		return FALSE;
	}

	argument = gem_proc_argv;
	*argument++ = command;
	arguments_left = GEM_PROC_ARGV_ENTRIES - 2;
	output = 0;

	if (!classic_tail) {
		*argument = NULL;
		return TRUE;
	}

	tail = (const UBYTE *) classic_tail;
	length = (UWORD) tail[0];
	if (length > GEM_PROC_TAIL_BYTES) {
		errno = E2BIG;
		return FALSE;
	}

	input = 0;
	while (input < length) {
		character = tail[input + 1];
		if (gem_proc_tail_end(character))
			break;
		if (gem_proc_tail_space(character)) {
			input++;
			continue;
		}
		if (!arguments_left) {
			errno = E2BIG;
			return FALSE;
		}

		*argument++ = &gem_proc_tail_buffer[output];
		arguments_left--;
		quoted = FALSE;

		while (input < length) {
			character = tail[input + 1];
			if (gem_proc_tail_end(character)) {
				input = length;
				break;
			}
			if (character == (UBYTE) '"') {
				quoted = (UBYTE) !quoted;
				input++;
				continue;
			}
			if (!quoted && gem_proc_tail_space(character))
				break;
			if (output >= GEM_PROC_TAIL_BYTES) {
				errno = E2BIG;
				return FALSE;
			}
			gem_proc_tail_buffer[output++] = (BYTE) character;
			input++;
		}

		/* Each argv string is NUL-terminated within the same fixed buffer. */
		if (output > GEM_PROC_TAIL_BYTES) {
			errno = E2BIG;
			return FALSE;
		}
		gem_proc_tail_buffer[output++] = '\0';
	}

	*argument = NULL;
	return TRUE;
}

WORD
proc_create(GEM_U32_WORDS ignored_base, GEM_U32_WORDS size_hint,
	    WORD isswap, WORD isgem, WORD *channel)
{
	GEM_PROC_SLOT *slot;
	WORD number;

	/* ELKS owns placement; preserve the original parameter only at the ABI. */
	(void) ignored_base;
	gem_proc_init();
	if (!channel) {
		errno = EINVAL;
		return FALSE;
	}
	*channel = -1;

	number = pr_gpid();
	if (number >= 2) {
		slot = gem_proc_slot(number);
		if (!slot || slot->state != GEM_PROC_FREE) {
			errno = EBUSY;
			return FALSE;
		}

		/* ELKS owns memory sizing; retain no duplicate arena state. */
		(void) size_hint;
		pr_scpid(number, FALSE);
		slot->pid = 0;
		slot->state = GEM_PROC_RESERVED;
		slot->flags = 0;
		if (isswap != FALSE)
			slot->flags |= GEM_PROC_FLAG_ISSWAP;
		if (isgem != FALSE)
			slot->flags |= GEM_PROC_FLAG_ISGEM;
		*channel = number;
		return TRUE;
	}

	errno = EAGAIN;
	return FALSE;
}

WORD
proc_run(WORD channel, WORD isgraf, WORD isover, LPBYTE command,
	 LPBYTE classic_tail)
{
	GEM_PROC_SLOT *slot;
	pid_t child;
	pid_t waited;
	UWORD exec_stack_bytes;
	WORD exec_error;
	WORD wait_status;

	gem_proc_init();
	slot = gem_proc_slot(channel);
	if (!slot) {
		errno = EINVAL;
		return FALSE;
	}
	if (slot->state != GEM_PROC_RESERVED) {
		errno = EBUSY;
		return FALSE;
	}
	if (!gem_proc_build_argv(command, classic_tail)
	    || !gem_proc_build_exec_stack(&exec_stack_bytes))
		return FALSE;
	gem_proc_exec_error = 0;

	/*
	 * Only vfork, the raw _execve syscall, and _exit occur after the child
	 * return.  These are the direct process syscalls allowed
	 * with a true shared-address-space vfork on an 8086.  The upstream ELKS
	 * base left its true-vfork path dormant; the
	 * project patch patches/elks/0001-enable-true-vfork-for-gem-multiapp.patch
	 * enables it.  This sequence remains correct with either kernel
	 * implementation.  The parent is suspended while the aligned static exec
	 * image is in use, so no lock, heap call, or wrapper process is necessary.
	 */
	child = vfork();
	if (child == (pid_t) 0) {
		if (_execve((const char *) command,
			(char *) gem_proc_exec_words, (int) exec_stack_bytes) < 0)
			gem_proc_exec_error = (WORD) (errno ? errno : EIO);
		_exit(127);
	}
	if (child == (pid_t) -1)
		return FALSE;

	/*
	 * A failed _execve has already reached _exit before true vfork wakes this
	 * parent.  Reap that exact PID now, without polling another child, and
	 * leave the logical channel in RESERVED state for proc_delete().  A
	 * successful exec installed private child data, so this shared word remains
	 * zero even if the new program later exits with status 127 itself.
	 */
	exec_error = gem_proc_exec_error;
	if (exec_error) {
		do {
			waited = waitpid(child, &wait_status, 0);
		} while (waited == (pid_t) -1 && errno == EINTR);
		(void) wait_status;
		errno = exec_error;
		return FALSE;
	}

	slot->pid = (UWORD) child;
	if (isgraf != FALSE)
		slot->flags |= GEM_PROC_FLAG_ISGRAF;
	else
		slot->flags &= (UBYTE) ~GEM_PROC_FLAG_ISGRAF;
	/*
	 * Preserve GEMPRLIB.C pr_run()'s exact overlay classification.  Value 3
	 * is a desk accessory; value 4 is a continuous GEM application.  Other
	 * values are ordinary applications.  The active shared AES window/menu
	 * owner consumes the retained continuous bit; this process module keeps
	 * only the launch and lifecycle classification.
	 */
	if (isover == GEM_PROC_CONTINUOUS_OVERLAY) {
		slot->flags |= GEM_PROC_FLAG_CONTINUOUS;
		slot->flags &= (UBYTE) ~GEM_PROC_FLAG_ACCESSORY;
	} else if (isover == GEM_PROC_ACCESSORY_OVERLAY) {
		slot->flags |= GEM_PROC_FLAG_ACCESSORY;
		slot->flags &= (UBYTE) ~GEM_PROC_FLAG_CONTINUOUS;
	} else {
		slot->flags &= (UBYTE) ~GEM_PROC_FLAG_ACCESSORY;
		slot->flags &= (UBYTE) ~GEM_PROC_FLAG_CONTINUOUS;
	}
	slot->state = GEM_PROC_RUNNING;
	return TRUE;
}

static WORD
gem_proc_delete_slot(GEM_PROC_SLOT *slot, WORD channel)
{
	pid_t child;
	pid_t waited;
	WORD status;
	UWORD polls;

	if (slot->state == GEM_PROC_RESERVED) {
		gem_proc_release(slot, channel);
		return TRUE;
	}
	if (slot->state != GEM_PROC_RUNNING) {
		errno = EINVAL;
		return FALSE;
	}

	child = (pid_t) slot->pid;
	if (kill(child, SIGTERM) < 0 && errno != ESRCH)
		return FALSE;

	/*
	 * Give a signal-aware application four bounded scheduling intervals to
	 * restore its tty/video state and exit.  The old immediate WNOHANG poll
	 * always ran before the child could be scheduled, effectively turning
	 * every delete into SIGKILL.  This cold management path waits at most
	 * forty milliseconds; ordinary completion is still reaped by the
	 * nonblocking AES event loop.
	 */
	polls = GEM_PROC_TERM_POLLS;
	for (;;) {
		do {
			waited = waitpid(child, &status, WNOHANG);
		} while (waited == (pid_t) -1 && errno == EINTR);
		if (waited == child) {
			gem_proc_release(slot, channel);
			return TRUE;
		}
		if (waited == (pid_t) -1 && errno == ECHILD) {
			gem_proc_release(slot, channel);
			return TRUE;
		}
		if (!polls)
			break;
		polls--;
		gem_proc_term_pause();
	}

	/*
	 * The AES event loop must never hang behind an application which catches
	 * or ignores SIGTERM.  ELKS has very few task slots, so escalate directly
	 * to SIGKILL and reap this exact positive PID.  No process-group or -1
	 * signal can escape the logical GEM channel.
	 */
	if (kill(child, SIGKILL) < 0 && errno != ESRCH)
		return FALSE;
	do {
		waited = waitpid(child, &status, 0);
	} while (waited == (pid_t) -1 && errno == EINTR);
	if (waited != child && !(waited == (pid_t) -1 && errno == ECHILD))
		return FALSE;

	gem_proc_release(slot, channel);
	return TRUE;
}

WORD
proc_delete(WORD channel)
{
	GEM_PROC_SLOT *slot;
	UWORD count;
	WORD result;

	gem_proc_init();
	if (channel == -1) {
		/* Original Desktop uses -1 solely to remove desk accessories. */
		result = TRUE;
		slot = gem_proc_slots;
		count = GEM_PROC_CHANNELS;
		channel = 0;
		while (count--) {
			if ((slot->flags & GEM_PROC_FLAG_ACCESSORY) != 0
			    && !gem_proc_delete_slot(slot, channel))
				result = FALSE;
			GEM_PROC_NEXT_SLOT(slot);
			channel++;
		}
		return result;
	}

	slot = gem_proc_slot(channel);
	if (!slot || slot->state == GEM_PROC_SYSTEM
	    || slot->state == GEM_PROC_FREE) {
		errno = EINVAL;
		return FALSE;
	}
	return gem_proc_delete_slot(slot, channel);
}

WORD
proc_info(WORD channel, WORD *isswap, WORD *isgem,
	  GEM_U32_WORDS *base, GEM_U32_WORDS *channel_size,
	  GEM_U32_WORDS *end_memory, GEM_U32_WORDS *swap_size,
	  GEM_U32_WORDS *interrupt_address)
{
	GEM_PROC_SLOT *slot;

	gem_proc_init();
	slot = gem_proc_slot(channel);
	if (!slot || slot->state == GEM_PROC_FREE) {
		errno = EINVAL;
		return FALSE;
	}

	if (isswap)
		*isswap = (slot->flags & GEM_PROC_FLAG_ISSWAP) != 0;
	if (isgem)
		*isgem = (slot->flags & GEM_PROC_FLAG_ISGEM) != 0;
	gem_proc_zero_pair(base);
	gem_proc_zero_pair(channel_size);
	gem_proc_zero_pair(end_memory);
	gem_proc_zero_pair(swap_size);
	gem_proc_zero_pair(interrupt_address);
	return TRUE;
}

WORD
proc_switch(WORD channel)
{
	GEM_PROC_SLOT *slot;

	gem_proc_init();
	slot = gem_proc_slot(channel);
	if (!slot || slot->state == GEM_PROC_FREE
	    || slot->state == GEM_PROC_RESERVED) {
		errno = EINVAL;
		return FALSE;
	}

	/*
	 * ELKS already schedules tasks.  This value is the logical AES foreground
	 * owner which the resident menu/window/input manager will consume; it is
	 * deliberately not implemented with process-group signals.
	 */
	if (gem_proc_foreground == channel)
		return FALSE;
	gem_proc_foreground = channel;
	return TRUE;
}

WORD
gem_proc_foreground_channel(VOID)
{
	gem_proc_init();
	return gem_proc_foreground;
}

WORD
proc_shrink(WORD channel)
{
	GEM_PROC_SLOT *slot;

	gem_proc_init();
	slot = gem_proc_slot(channel);
	if (!slot || slot->state == GEM_PROC_FREE) {
		errno = EINVAL;
		return FALSE;
	}

	/*
	 * ELKS brk/sbrk move only this task's break within its fixed data segment;
	 * they cannot resize another channel's kernel segment.  Executable a.out
	 * heap/stack sizing replaces the original DOS shrink operation.
	 */
	return TRUE;
}

WORD
proc_setblock(WORD channel)
{
	GEM_PROC_SLOT *slot;

	gem_proc_init();
	slot = gem_proc_slot(channel);
	if (!slot || slot->state == GEM_PROC_FREE) {
		errno = EINVAL;
		return FALSE;
	}

	/* DOS setblock has no ELKS analogue; the kernel owns segment allocation. */
	return TRUE;
}

WORD
gem_proc_channel_for_pid(UWORD pid)
{
	GEM_PROC_SLOT *slot;
	WORD channel;
	UWORD count;

	if (!pid)
		return NIL;
	gem_proc_init();
	slot = gem_proc_slots;
	channel = 0;
	count = GEM_PROC_CHANNELS;
	while (count--) {
		if (slot->state == GEM_PROC_RUNNING && slot->pid == pid)
			return channel;
		GEM_PROC_NEXT_SLOT(slot);
		channel++;
	}
	return NIL;
}

WORD
gem_proc_adopt_external(UWORD pid, WORD *channel)
{
	GEM_PROC_SLOT *slot;
	WORD number;

	gem_proc_init();
	if (!channel || !pid) {
		errno = EINVAL;
		return FALSE;
	}
	*channel = NIL;

	/* One ELKS PID may own exactly one original GEM logical channel. */
	if (gem_proc_channel_for_pid(pid) != NIL) {
		errno = EBUSY;
		return FALSE;
	}

	number = pr_gpid();
	if (number < 2) {
		errno = EAGAIN;
		return FALSE;
	}
	slot = gem_proc_slot(number);
	if (!slot || slot->state != GEM_PROC_FREE) {
		errno = EBUSY;
		return FALSE;
	}

	/*
	 * Only the four-byte logical record is adopted.  The task, its segments,
	 * scheduling state, parentage, and eventual destruction remain wholly in
	 * the ELKS kernel.  APPL_INIT establishes that this is a GEM graphics
	 * client, so retain the same two original classification bits as a normal
	 * proc_run() graphical child and add one private non-child tag.
	 */
	pr_scpid(number, FALSE);
	slot->pid = pid;
	slot->state = GEM_PROC_RUNNING;
	slot->flags = GEM_PROC_FLAG_ISGEM | GEM_PROC_FLAG_ISGRAF
		      | GEM_PROC_FLAG_EXTERNAL;
	*channel = number;
	return TRUE;
}

WORD
gem_proc_release_external(WORD channel, UWORD pid)
{
	GEM_PROC_SLOT *slot;

	gem_proc_init();
	if (channel < 2 || !pid) {
		errno = EINVAL;
		return FALSE;
	}
	slot = gem_proc_slot(channel);
	if (!slot || slot->state != GEM_PROC_RUNNING
	    || !(slot->flags & GEM_PROC_FLAG_EXTERNAL)
	    || slot->pid != pid) {
		errno = ESRCH;
		return FALSE;
	}

	gem_proc_release(slot, channel);
	return TRUE;
}

WORD
gem_proc_poll(WORD *channel, WORD *status)
{
	GEM_PROC_SLOT *slot;
	pid_t child;
	pid_t waited;
	WORD wait_status;
	WORD number;
	UWORD count;

	gem_proc_init();
	slot = gem_proc_slots;
	number = 0;
	count = GEM_PROC_CHANNELS;
	while (count--) {
		if (slot->state == GEM_PROC_RUNNING
		    && !(slot->flags & GEM_PROC_FLAG_EXTERNAL)) {
			child = (pid_t) slot->pid;
			do {
				waited = waitpid(child, &wait_status, WNOHANG);
			} while (waited == (pid_t) -1 && errno == EINTR);

			if (waited == child) {
				if (channel)
					*channel = number;
				if (status)
					*status = wait_status;
				gem_proc_release(slot, number);
				return TRUE;
			}
			if (waited == (pid_t) -1 && errno == ECHILD) {
				/* Keep a proc_run child usable after an outside reap. */
				if (channel)
					*channel = number;
				if (status)
					*status = GEM_PROC_STATUS_UNKNOWN;
				gem_proc_release(slot, number);
				return TRUE;
			}
		}
		GEM_PROC_NEXT_SLOT(slot);
		number++;
	}

	return FALSE;
}
