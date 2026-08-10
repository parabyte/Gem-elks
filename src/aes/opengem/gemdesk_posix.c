/*
 * gemdesk_posix.c - POSIX entry point for the OpenGEM desktop source.
 */

#include "aes.h"

#include <errno.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

WORD GEMAIN(WORD argc, BYTE *argv[]);

/*
 * The stock-ELKS direct-linked build has its own single-executable
 * entry point in gem_main.c; the broker-era client main() below
 * waited for a kernel INT EF signature that no longer exists.  That
 * build only uses the mkfs format helper at the end of this file.
 */
#ifndef GEM_DIRECT

/*
 * ELKS select(2) still takes a timeval made of two old four-byte
 * fields.  Spell that layout out here as low/high word pairs so the
 * Desktop never pulls in libc sleep(), time_t maths, or 32-bit helper
 * calls.  Units are microseconds; this startup poll asks for exactly
 * 10000 of them, so both high words stay zero and no rounding, carry,
 * divide, or multiply is ever needed.
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
	 * Original GEM started the Desktop with GEMSYS as its DOS
	 * current directory.  ELKS init has no per-entry working
	 * directory, so set the same directory here before the
	 * unmodified GEMAIN path opens DESKTOP.RSC and the original ICN
	 * files.  One kernel call at startup, and every asset byte stays
	 * in its original on-disk format.
	 */
	if (chdir("/lib/gemsys") != 0)
		return 1;
	/*
	 * Init starts the owner and the Desktop as separate ELKS tasks,
	 * and which runs first is the kernel's call.  So wait until the
	 * real GEM vector signature shows up before entering unchanged
	 * GEMAIN.  This is just the client waiting for the owner to be
	 * ready, not a launcher or protocol wrapper; after this one
	 * startup wait every call goes through the original INT EFh
	 * arrays directly.
	 */
	while (!aescheck())
		gemdesk_owner_pause();
	return GEMAIN((WORD) argc, (BYTE **) argv);
}

#endif /* !GEM_DIRECT */

/*
 * Make a native MINIX file system on a real floppy and wait for it to
 * finish.  The classic Desktop used FORMAT.COM plus DOS/BIOS
 * ownership hand-offs here; ELKS already owns task memory, the block
 * driver, keyboard, and video, so one plain vfork/execv/waitpid of
 * mkfs is the right way to do it.  No shell, wrapper process,
 * converted command record, allocation, or wide maths involved.
 *
 * The XT default is 360 blocks of 1 KiB, matching a 360 KiB PC/XT
 * floppy.  mkfs checks the block device itself and returns a normal
 * POSIX exit status.  Only A and B are accepted, so a stray Desktop
 * selection can never format the system/root disk.
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
