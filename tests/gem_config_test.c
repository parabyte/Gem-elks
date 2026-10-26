#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "gem_config.h"

static void
config(const char *text)
{
	FILE *file = fopen(GEM_CONFIG_FILE, "w");
	assert(file != NULL);
	assert(fputs(text, file) >= 0);
	assert(fclose(file) == 0);
}

int
main(void)
{
	char large[1025];

	unsetenv("GEM_VIDEO");
	unsetenv("CONSOLE");
	unsetenv("MOUSE_PROTOCOL");
	unsetenv("MOUSE_PORT");
	unlink(GEM_CONFIG_FILE);
	assert(gem_config_load() == 0);
	assert(gem_config_get("MOUSE_PROTOCOL") == NULL);
	config("# settings\r\n\tGEM_VIDEO = ega \r\n"
		"MOUSE_PROTOCOL=ms # serial\nMOUSE_PORT=/dev/ttyS0\n"
		"MOUSE_PORT = /dev/ttyS1\nCONSOLE=\n");
	assert(gem_config_load() == 0);
	assert(!strcmp(gem_config_get("GEM_VIDEO"), "ega"));
	assert(!strcmp(gem_config_get("MOUSE_PROTOCOL"), "ms"));
	assert(!strcmp(gem_config_get("MOUSE_PORT"), "/dev/ttyS1"));
	assert(gem_config_get("CONSOLE") == NULL);
	assert(setenv("MOUSE_PORT", "none", 1) == 0);
	assert(!strcmp(gem_config_get("MOUSE_PORT"), "none"));
	unsetenv("MOUSE_PORT");
	config("MOUSE_PORT=none");
	assert(gem_config_load() == 0);
	assert(!strcmp(gem_config_get("MOUSE_PORT"), "none"));
	assert(gem_config_get("GEM_VIDEO") == NULL);
	config("MOUSE_POT=/dev/ttyS0\n");
	assert(gem_config_load() == -1 && errno == EINVAL);
	config("MOUSE_PROTOCOL ms\n");
	assert(gem_config_load() == -1 && errno == EINVAL);
	memset(large, '#', sizeof(large) - 1);
	large[sizeof(large) - 1] = 0;
	config(large);
	assert(gem_config_load() == -1 && errno == E2BIG);
	large[sizeof(large) - 2] = 0;
	config(large);
	assert(gem_config_load() == 0);
	unlink(GEM_CONFIG_FILE);
	puts("gem.conf checks passed");
	return 0;
}
