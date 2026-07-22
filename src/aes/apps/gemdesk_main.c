/*
 * gemdesk_main.c - GEM Desktop client entry point for stock ELKS.
 *
 * The Desktop is started by the AES server (/bin/gem) with the two
 * transport pipes already on file descriptors 3 and 4 and GEMSYS as the
 * inherited working directory.  Startup is sequential: divert the original
 * bindings to the pipe transport and run the unchanged original GEMAIN.
 * When GEMAIN returns - QUIT, or a SHEL_WRITE launch - this process simply
 * exits; the server observes the closed pipe and takes over exactly like
 * the original single-tasking AES shell.
 */

#include <unistd.h>

#include "aes.h"

WORD GEMAIN(WORD argc, BYTE *argv[]);
WORD gem_client_install(VOID);

int
main(int argc, char **argv)
{
	if (chdir("/GEMAPPS/GEMSYS") != 0)
		return 1;
	if (!gem_client_install())
		return 1;
	(void) GEMAIN((WORD) argc, (BYTE **) argv);
	return 0;
}
