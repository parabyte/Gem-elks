#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "drivers/gem_mouse_serial.h"

static int stat_error;
static struct stat tty;
static unsigned int wanted_port;
static unsigned char lcr;
static unsigned int writes;

int
gem_test_fstat(int fd, struct stat *status)
{
	assert(fd == 4);
	*status = tty;
	return stat_error;
}

unsigned char
gem_test_inb(unsigned int port)
{
	assert(port == wanted_port);
	return lcr;
}

void
gem_test_outb(unsigned char value, unsigned int port)
{
	assert(port == wanted_port);
	lcr = value;
	writes++;
}

int
main(void)
{
	static const unsigned int ports[] = {0x3fb, 0x2fb, 0x3eb, 0x2eb};
	unsigned int index;

	for (index = 0; index < 4; index++) {
		tty.st_mode = S_IFCHR;
		tty.st_rdev = 0x0440 + index;
		wanted_port = ports[index];
		lcr = 0x1b;
		writes = 0;
		gem_mouse_serial_save(4);
		assert(writes == 0);
		/* tcsetattr may have set 7N1 on a newer kernel */
		lcr = 0x02;
		gem_mouse_serial_setup();
		assert(lcr == 0x02 && writes == 1);
		gem_mouse_serial_restore();
		assert(lcr == 0x1b && writes == 2);
		gem_mouse_serial_restore();
		assert(writes == 2);
	}
	/* no UART writes for a failed stat, a file or another tty */
	writes = 0;
	stat_error = -1;
	gem_mouse_serial_save(4);
	gem_mouse_serial_setup();
	gem_mouse_serial_restore();
	assert(writes == 0);
	stat_error = 0;
	tty.st_mode = S_IFREG;
	gem_mouse_serial_save(4);
	gem_mouse_serial_setup();
	assert(writes == 0);
	tty.st_mode = S_IFCHR;
	tty.st_rdev = 0x0400;
	gem_mouse_serial_save(4);
	gem_mouse_serial_setup();
	assert(writes == 0);
	tty.st_rdev = 0x0444;
	gem_mouse_serial_save(4);
	gem_mouse_serial_setup();
	assert(writes == 0);
	puts("serial UART setup and restore checks passed");
	return 0;
}
