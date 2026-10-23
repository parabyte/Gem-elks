/* ELKS disk formatting for Desktop. */

#include "aes.h"

#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* only A and B can reach mkfs */
WORD
gemdesk_posix_format(WORD drive)
{
	static BYTE mkfs_path[] = "/bin/mkfs";
	static BYTE floppy_a[] = "/dev/fd0";
	static BYTE floppy_b[] = "/dev/fd1";
	static BYTE blocks[] = "360";
	static BYTE *arguments[4];
	BYTE *device;
	WORD child;
	WORD waited;
	WORD status;

	if (drive == 'A')
		device = floppy_a;
	else if (drive == 'B')
		device = floppy_b;
	else {
		errno = EINVAL;
		return FALSE;
	}

	arguments[0] = mkfs_path;
	arguments[1] = device;
	arguments[2] = blocks;
	arguments[3] = (BYTE *) 0;
	child = (WORD) vfork();
	if (child < 0)
		return FALSE;
	if (!child) {
		execv((const char *) mkfs_path, (char **) arguments);
		_exit(127);
	}

	do {
		waited = (WORD) waitpid((pid_t) child, (int *) &status, 0);
	} while (waited < 0 && errno == EINTR);
	return waited == child && WIFEXITED(status)
		&& WEXITSTATUS(status) == 0;
}
