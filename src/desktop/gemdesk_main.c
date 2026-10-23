/* Desktop client entry point */

#include <unistd.h>

#include "aes.h"

WORD GEMAIN(WORD argc, BYTE *argv[]);

int
main(int argc, char **argv)
{
	if (chdir("/lib/gemsys") != 0)
		return 1;
	if (!aescheck())
		return 1;
	(void) GEMAIN((WORD) argc, (BYTE **) argv);
	return 0;
}
