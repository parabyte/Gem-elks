/*
 * gemdesk_main.c - GEM Desktop client entry point
 *
 * /bin/gem starts the Desktop with transport pipes on descriptors 3 and
 * 4. point the bindings at the pipe transport, run the original GEMAIN,
 * exit; the server sees the closed pipe and takes over
 */

#include <unistd.h>

#include "aes.h"

WORD GEMAIN(WORD argc, BYTE *argv[]);
WORD gem_client_install(VOID);

int
main(int argc, char **argv)
{
	if (chdir("/lib/gemsys") != 0)
		return 1;
	if (!gem_client_install())
		return 1;
	(void) GEMAIN((WORD) argc, (BYTE **) argv);
	return 0;
}
