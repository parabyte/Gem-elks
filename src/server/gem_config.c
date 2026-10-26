/* gem.conf settings, kept in one buffer so there is no heap allocation */
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "gem_config.h"

static char gem_config_text[1024];
static const char *gem_config_names[] = {
	"GEM_VIDEO", "CONSOLE", "MOUSE_PROTOCOL", "MOUSE_PORT"
};
static char *gem_config_values[4];

static char *
gem_config_trim(char *text)
{
	char *end;

	while (*text == ' ' || *text == '\t' || *text == '\r')
		text++;
	end = text + strlen(text);
	while (end > text && (end[-1] == ' ' || end[-1] == '\t'
		|| end[-1] == '\r'))
		*--end = 0;
	return text;
}

int GEM_CONFIG_COLD
gem_config_load(void)
{
	int fd, count, used, index, saved_errno;
	char *line, *next, *value, *comment;

	memset(gem_config_values, 0, sizeof(gem_config_values));
	fd = open(GEM_CONFIG_FILE, O_RDONLY);
	if (fd < 0)
		return errno == ENOENT ? 0 : -1;
	used = 0;
	do {
		count = read(fd, gem_config_text + used,
			sizeof(gem_config_text) - used);
		if (count > 0)
			used += count;
	} while ((count > 0 && used < (int) sizeof(gem_config_text))
		|| (count < 0 && errno == EINTR));
	saved_errno = errno;
	close(fd);
	if (count < 0) {
		errno = saved_errno;
		return -1;
	}
	if (used == (int) sizeof(gem_config_text)) {
		errno = E2BIG;
		return -1;
	}
	gem_config_text[used] = 0;
	for (line = gem_config_text; line; line = next) {
		next = strchr(line, '\n');
		if (next)
			*next++ = 0;
		comment = strchr(line, '#');
		if (comment)
			*comment = 0;
		line = gem_config_trim(line);
		if (!*line)
			continue;
		value = strchr(line, '=');
		if (!value)
			goto invalid;
		*value++ = 0;
		line = gem_config_trim(line);
		for (index = 0; index < 4; index++)
			if (!strcmp(line, gem_config_names[index]))
				break;
		if (index == 4)
			goto invalid;
		gem_config_values[index] = gem_config_trim(value);
	}
	return 0;
invalid:
	errno = EINVAL;
	return -1;
}

const char * GEM_CONFIG_NEAR
gem_config_get(const char *name)
{
	const char *value;
	int index;

	value = getenv(name);
	if (value && *value)
		return value;
	for (index = 0; index < 4; index++)
		if (!strcmp(name, gem_config_names[index])) {
			value = gem_config_values[index];
			return value && *value ? value : NULL;
		}
	return NULL;
}
