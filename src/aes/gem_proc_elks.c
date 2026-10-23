/* GEM channels mapped to ELKS pids. */

#include "gem_proc.h"

#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define GEM_PROC_FREE		0
#define GEM_PROC_SYSTEM		1
#define GEM_PROC_RESERVED	2
#define GEM_PROC_RUNNING	3

#define GEM_PROC_FLAG_ACCESSORY	0x01
#define GEM_PROC_FLAG_EXTERNAL	0x02

#define GEM_PROC_ACCESSORY_OVERLAY 3
#define GEM_PROC_EXEC_BYTES	512U
#define GEM_PROC_EXEC_WORDS	256U
#define GEM_PROC_ENV_ENTRIES	32U

typedef struct gem_proc_slot {
	UWORD pid;
	UBYTE state;
	UBYTE flags;
} GEM_PROC_SLOT;

static GEM_PROC_SLOT gem_proc_slots[GEM_PROC_CHANNELS];
static UBYTE gem_proc_initialized;
static WORD gem_proc_foreground;
static BYTE gem_proc_tail[GEM_PROC_TAIL_BYTES + 1U];
static LPBYTE gem_proc_argv[GEM_PROC_ARGV_ENTRIES];
static UWORD gem_proc_exec_words[GEM_PROC_EXEC_WORDS];
static volatile WORD gem_proc_exec_error;

static VOID
gem_proc_init(VOID)
{
	if (gem_proc_initialized)
		return;
	gem_proc_slots[GEM_PROC_DESKTOP].state = GEM_PROC_SYSTEM;
	gem_proc_slots[GEM_PROC_AES].state = GEM_PROC_SYSTEM;
	gem_proc_foreground = GEM_PROC_DESKTOP;
	gem_proc_initialized = TRUE;
}

static GEM_PROC_SLOT *
gem_proc_slot(WORD channel)
{
	if (channel < 0 || channel >= GEM_PROC_CHANNELS)
		return NULL;
	return gem_proc_slots + channel;
}

static VOID
gem_proc_release(GEM_PROC_SLOT *slot, WORD channel)
{
	if (gem_proc_foreground == channel)
		gem_proc_foreground = GEM_PROC_DESKTOP;
	slot->pid = 0;
	slot->flags = 0;
	slot->state = GEM_PROC_FREE;
}

/* build the exec stack before vfork */
static WORD
gem_proc_prepare_exec(LPBYTE command, LPBYTE classic_tail, UWORD *stack_bytes)
{
	LPBYTE *environment;
	LPBYTE *entry;
	BYTE *destination;
	const BYTE *source;
	UWORD *slot;
	UWORD argc;
	UWORD envc;
	UWORD index;
	UWORD input;
	UWORD length;
	UWORD offset;
	UWORD output;
	UWORD vector_bytes;
	UBYTE character;
	UBYTE quoted;

	if (!command || !command[0] || !stack_bytes) {
		errno = EINVAL;
		return FALSE;
	}
	gem_proc_argv[0] = command;
	argc = 1;
	output = 0;
	if (classic_tail) {
		length = (UBYTE) classic_tail[0];
		if (length > GEM_PROC_TAIL_BYTES) {
			errno = E2BIG;
			return FALSE;
		}
		input = 0;
		while (input < length) {
			while (input < length) {
				character = (UBYTE) classic_tail[input + 1U];
				if (character != ' ' && character != '\t')
					break;
				input++;
			}
			if (input >= length)
				break;
			character = (UBYTE) classic_tail[input + 1U];
			if (character == '\r' || character == '\n' || !character)
				break;
			if (argc >= GEM_PROC_ARGV_ENTRIES - 1U
				|| output >= GEM_PROC_TAIL_BYTES) {
				errno = E2BIG;
				return FALSE;
			}
			gem_proc_argv[argc++] = gem_proc_tail + output;
			quoted = FALSE;
			while (input < length) {
				character = (UBYTE) classic_tail[input + 1U];
				if (!character || character == '\r'
					|| character == '\n') {
					input = length;
					break;
				}
				if (character == '"') {
					quoted = (UBYTE) !quoted;
					input++;
					continue;
				}
				if (!quoted
					&& (character == ' ' || character == '\t'))
					break;
				if (output >= GEM_PROC_TAIL_BYTES) {
					errno = E2BIG;
					return FALSE;
				}
				gem_proc_tail[output++] = (BYTE) character;
				input++;
			}
			gem_proc_tail[output++] = 0;
		}
	}
	gem_proc_argv[argc] = NULL;

	environment = (LPBYTE *) environ;
	envc = 0;
	while (environment && environment[envc]) {
		if (envc >= GEM_PROC_ENV_ENTRIES) {
			errno = E2BIG;
			return FALSE;
		}
		envc++;
	}
	vector_bytes = (UWORD) ((argc + envc + 3U) << 1);
	if (vector_bytes >= GEM_PROC_EXEC_BYTES) {
		errno = E2BIG;
		return FALSE;
	}
	slot = gem_proc_exec_words;
	destination = (BYTE *) gem_proc_exec_words + vector_bytes;
	offset = vector_bytes;
	*slot++ = argc;

	index = 0;
	entry = gem_proc_argv;
	for (;;) {
		if (index == argc) {
			*slot++ = 0;
			if (entry == gem_proc_argv) {
				entry = environment;
				index = 0;
				argc = envc;
				continue;
			}
			break;
		}
		*slot++ = offset;
		source = entry[index++];
		do {
			if (offset >= GEM_PROC_EXEC_BYTES) {
				errno = E2BIG;
				return FALSE;
			}
			character = (UBYTE) *source++;
			*destination++ = (BYTE) character;
			offset++;
		} while (character);
	}
	*stack_bytes = offset;
	return TRUE;
}

WORD GEM_PROC_COLD
gem_proc_spawn_external(LPBYTE command, LPBYTE classic_tail, UWORD *pid)
{
	pid_t child;
	pid_t waited;
	UWORD exec_bytes;
	WORD status;
	WORD saved_errno;

	if (!pid) {
		errno = EINVAL;
		return FALSE;
	}
	*pid = 0;
	if (!gem_proc_prepare_exec(command, classic_tail, &exec_bytes))
		return FALSE;
	gem_proc_exec_error = 0;
	child = vfork();
	if (child == (pid_t) 0) {
		if (_execve((const char *) command, (char *) gem_proc_exec_words,
				(int) exec_bytes) < 0)
			gem_proc_exec_error = (WORD) (errno ? errno : EIO);
		_exit(127);
	}
	if (child == (pid_t) -1)
		return FALSE;
	saved_errno = gem_proc_exec_error;
	if (saved_errno) {
		do {
			waited = waitpid(child, &status, 0);
		} while (waited == (pid_t) -1 && errno == EINTR);
		errno = saved_errno;
		return FALSE;
	}
	*pid = (UWORD) child;
	return TRUE;
}

WORD
proc_create(GEM_U32_WORDS ignored_base, GEM_U32_WORDS size_hint,
	WORD isswap, WORD isgem, WORD *channel)
{
	GEM_PROC_SLOT *slot;
	WORD number;

	(void) ignored_base;
	(void) size_hint;
	(void) isswap;
	(void) isgem;
	gem_proc_init();
	if (!channel) {
		errno = EINVAL;
		return FALSE;
	}
	for (number = 2; number < GEM_PROC_CHANNELS; number++) {
		slot = gem_proc_slots + number;
		if (slot->state == GEM_PROC_FREE) {
			slot->state = GEM_PROC_RESERVED;
			slot->flags = 0;
			*channel = number;
			return TRUE;
		}
	}
	*channel = -1;
	errno = EAGAIN;
	return FALSE;
}

WORD
proc_run(WORD channel, WORD isgraf, WORD isover, LPBYTE command,
	LPBYTE classic_tail)
{
	GEM_PROC_SLOT *slot;
	UWORD pid;

	gem_proc_init();
	slot = gem_proc_slot(channel);
	if (!slot || slot->state != GEM_PROC_RESERVED) {
		errno = EINVAL;
		return FALSE;
	}
	if (!gem_proc_spawn_external(command, classic_tail, &pid))
		return FALSE;
	slot->pid = pid;
	(void) isgraf;
	if (isover == GEM_PROC_ACCESSORY_OVERLAY)
		slot->flags |= GEM_PROC_FLAG_ACCESSORY;
	slot->state = GEM_PROC_RUNNING;
	return TRUE;
}

static WORD
gem_proc_delete_slot(GEM_PROC_SLOT *slot, WORD channel)
{
	pid_t child;
	pid_t waited;
	WORD status;

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
	if (slot->flags & GEM_PROC_FLAG_EXTERNAL) {
		gem_proc_release(slot, channel);
		return TRUE;
	}
	do {
		waited = waitpid(child, &status, WNOHANG);
	} while (waited == (pid_t) -1 && errno == EINTR);
	if (waited == 0) {
		(void) kill(child, SIGKILL);
		do {
			waited = waitpid(child, &status, 0);
		} while (waited == (pid_t) -1 && errno == EINTR);
	}
	if (waited != child && !(waited == (pid_t) -1 && errno == ECHILD))
		return FALSE;
	gem_proc_release(slot, channel);
	return TRUE;
}

WORD
proc_delete(WORD channel)
{
	GEM_PROC_SLOT *slot;
	WORD number;
	WORD result;

	gem_proc_init();
	if (channel != -1) {
		slot = gem_proc_slot(channel);
		if (!slot || channel < 2)
			return FALSE;
		return gem_proc_delete_slot(slot, channel);
	}
	result = TRUE;
	for (number = 2; number < GEM_PROC_CHANNELS; number++) {
		slot = gem_proc_slots + number;
		if ((slot->flags & GEM_PROC_FLAG_ACCESSORY)
			&& !gem_proc_delete_slot(slot, number))
			result = FALSE;
	}
	return result;
}

WORD
proc_switch(WORD channel)
{
	GEM_PROC_SLOT *slot;

	gem_proc_init();
	slot = gem_proc_slot(channel);
	if (!slot || slot->state == GEM_PROC_FREE
		|| slot->state == GEM_PROC_RESERVED
		|| gem_proc_foreground == channel)
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
gem_proc_channel_for_pid(UWORD pid)
{
	WORD channel;

	gem_proc_init();
	if (!pid)
		return NIL;
	for (channel = 2; channel < GEM_PROC_CHANNELS; channel++)
		if (gem_proc_slots[channel].state == GEM_PROC_RUNNING
			&& gem_proc_slots[channel].pid == pid)
			return channel;
	return NIL;
}

WORD GEM_PROC_COLD
gem_proc_adopt_external(UWORD pid, WORD *channel)
{
	GEM_PROC_SLOT *slot;
	WORD number;

	if (!pid || !channel || gem_proc_channel_for_pid(pid) != NIL)
		return FALSE;
	for (number = 2; number < GEM_PROC_CHANNELS; number++) {
		slot = gem_proc_slots + number;
		if (slot->state == GEM_PROC_FREE) {
			slot->pid = pid;
			slot->state = GEM_PROC_RUNNING;
			slot->flags = GEM_PROC_FLAG_EXTERNAL;
			*channel = number;
			return TRUE;
		}
	}
	return FALSE;
}

WORD
gem_proc_release_external(WORD channel, UWORD pid)
{
	GEM_PROC_SLOT *slot;

	slot = gem_proc_slot(channel);
	if (!slot || channel < 2 || slot->state != GEM_PROC_RUNNING
		|| !(slot->flags & GEM_PROC_FLAG_EXTERNAL) || slot->pid != pid)
		return FALSE;
	gem_proc_release(slot, channel);
	return TRUE;
}

WORD
gem_proc_poll(WORD *channel, WORD *status)
{
	GEM_PROC_SLOT *slot;
	pid_t waited;
	WORD number;
	WORD wait_status;

	gem_proc_init();
	for (number = 2; number < GEM_PROC_CHANNELS; number++) {
		slot = gem_proc_slots + number;
		if (slot->state != GEM_PROC_RUNNING
			|| (slot->flags & GEM_PROC_FLAG_EXTERNAL))
			continue;
		do {
			waited = waitpid((pid_t) slot->pid, &wait_status, WNOHANG);
		} while (waited == (pid_t) -1 && errno == EINTR);
		if (waited == (pid_t) slot->pid
			|| (waited == (pid_t) -1 && errno == ECHILD)) {
			if (channel)
				*channel = number;
			if (status)
				*status = waited == (pid_t) -1
					? GEM_PROC_STATUS_UNKNOWN : wait_status;
			gem_proc_release(slot, number);
			return TRUE;
		}
	}
	return FALSE;
}
