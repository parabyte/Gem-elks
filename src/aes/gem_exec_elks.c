/* direct ELKS vfork and exec */

#include "gem_exec.h"

#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define GEM_EXEC_TAIL_BYTES	127U
#define GEM_EXEC_ARGV_ENTRIES	17U
#define GEM_EXEC_BYTES		512U
#define GEM_EXEC_WORDS		256U
#define GEM_EXEC_ENV_ENTRIES	32U

static BYTE gem_exec_tail[GEM_EXEC_TAIL_BYTES + 1U];
static LPBYTE gem_exec_argv[GEM_EXEC_ARGV_ENTRIES];
static UWORD gem_exec_words[GEM_EXEC_WORDS];
static volatile WORD gem_exec_error;

/* pack argv and environ before vfork */
static WORD
gem_exec_prepare(LPBYTE command, LPBYTE classic_tail, UWORD *stack_bytes)
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
	gem_exec_argv[0] = command;
	argc = 1;
	output = 0;
	if (classic_tail) {
		length = (UBYTE) classic_tail[0];
		if (length > GEM_EXEC_TAIL_BYTES) {
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
			if (argc >= GEM_EXEC_ARGV_ENTRIES - 1U
				|| output >= GEM_EXEC_TAIL_BYTES) {
				errno = E2BIG;
				return FALSE;
			}
			gem_exec_argv[argc++] = gem_exec_tail + output;
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
				if (output >= GEM_EXEC_TAIL_BYTES) {
					errno = E2BIG;
					return FALSE;
				}
				gem_exec_tail[output++] = (BYTE) character;
				input++;
			}
			gem_exec_tail[output++] = 0;
		}
	}
	gem_exec_argv[argc] = NULL;

	environment = (LPBYTE *) environ;
	envc = 0;
	while (environment && environment[envc]) {
		if (envc >= GEM_EXEC_ENV_ENTRIES) {
			errno = E2BIG;
			return FALSE;
		}
		envc++;
	}
	vector_bytes = (UWORD) ((argc + envc + 3U) << 1);
	if (vector_bytes >= GEM_EXEC_BYTES) {
		errno = E2BIG;
		return FALSE;
	}
	slot = gem_exec_words;
	destination = (BYTE *) gem_exec_words + vector_bytes;
	offset = vector_bytes;
	*slot++ = argc;

	index = 0;
	entry = gem_exec_argv;
	for (;;) {
		if (index == argc) {
			*slot++ = 0;
			if (entry == gem_exec_argv) {
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
			if (offset >= GEM_EXEC_BYTES) {
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

WORD GEM_EXEC_COLD
gem_exec(LPBYTE command, LPBYTE classic_tail, UWORD *pid)
{
	pid_t child;
	pid_t waited;
	UWORD exec_bytes;
	WORD status;
	WORD saved_errno;

	if (pid)
		*pid = 0;
	if (!gem_exec_prepare(command, classic_tail, &exec_bytes))
		return FALSE;
	gem_exec_error = 0;
	child = vfork();
	if (child == (pid_t) 0) {
		if (_execve((const char *) command, (char *) gem_exec_words,
				(int) exec_bytes) < 0)
			gem_exec_error = (WORD) (errno ? errno : EIO);
		_exit(127);
	}
	if (child == (pid_t) -1)
		return FALSE;
	saved_errno = gem_exec_error;
	if (saved_errno) {
		do {
			waited = waitpid(child, &status, 0);
		} while (waited == (pid_t) -1 && errno == EINTR);
		errno = saved_errno;
		return FALSE;
	}
	if (pid)
		*pid = (UWORD) child;
	return TRUE;
}
