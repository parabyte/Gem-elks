/*
 * POSIX entry point for the OpenGEM desktop source.
 */

#include "ppdaes.h"

#include <errno.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

WORD GEMAIN(WORD argc, BYTE *argv[]);

/*
 * ELKS select(2) retains a timeval made from two historical four-byte
 * fields.  Keep that wire layout isolated here as low/high word pairs so the
 * Desktop never imports libc sleep(), time_t arithmetic, or 32-bit helper
 * calls.  The delay scale is one microsecond per unit.  This startup poll
 * requests exactly 10000 microseconds, so both high words are zero and no
 * rounding, saturation, carry, division, or multiplication is needed.
 */
typedef struct __attribute__((packed)) gemdesk_timeval_words {
	UWORD seconds_lo;
	UWORD seconds_hi;
	UWORD useconds_lo;
	UWORD useconds_hi;
} GEMDESK_TIMEVAL_WORDS;

static VOID
gemdesk_owner_pause(VOID)
{
	GEMDESK_TIMEVAL_WORDS timeout;

	timeout.seconds_lo = 0;
	timeout.seconds_hi = 0;
	timeout.useconds_lo = 10000U;
	timeout.useconds_hi = 0;
	(void) select(1, NULL, NULL, NULL,
		(struct timeval *) (VOID *) &timeout);
}

int
main(int argc, char **argv)
{
	/*
	 * Original GEM started Desktop with GEMSYS as its DOS current directory.
	 * ELKS init has no per-entry working-directory field, so establish that
	 * same process-local directory here before the unmodified GEMAIN path
	 * opens DESKTOP.RSC and the original ICN files.  This is one startup-only
	 * kernel call and leaves every asset byte in its original on-disk format.
	 */
	if (chdir("/GEMAPPS/GEMSYS") != 0)
		return 1;
	/*
	 * Init starts the owner and Desktop as separate ELKS tasks.  Scheduling
	 * order is intentionally a kernel decision, so wait for the actual GEM
	 * vector signature before entering unchanged GEMAIN.  This is readiness
	 * synchronization inside the native client, not a launcher or protocol
	 * wrapper; after the one bounded startup wait every call follows the
	 * original INT EFh arrays directly.
	 */
	while (!aescheck())
		gemdesk_owner_pause();
	return GEMAIN((WORD) argc, (BYTE **) argv);
}

/*
 * Create a native MINIX file system on one real floppy device and wait for
 * completion.  Classic Desktop used FORMAT.COM plus DOS/BIOS ownership
 * hand-offs here.  ELKS already owns task memory, the block driver, keyboard,
 * and video, so the correct boundary is one direct vfork/execv/waitpid
 * transaction.  No command shell, wrapper process, converted command record,
 * dynamic allocation, or wide arithmetic is involved.
 *
 * The XT default is 360 unscaled 1 KiB blocks.  This matches a 360 KiB PC/XT
 * floppy.  mkfs validates the block device and returns a normal POSIX exit
 * status.  Only A and B are accepted: the system/root disk can therefore
 * never be formatted through an accidental Desktop selection.
 */
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
