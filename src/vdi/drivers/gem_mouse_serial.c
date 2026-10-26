/* ELKS sets the baud rate but older 8250 drivers leave the UART at 8N1,
 * set 7N1 here after tcsetattr, the kernel still owns receive and IRQs
 */
#include <arch/io.h>
#include <sys/stat.h>

#include "gem_mouse_serial.h"

static const unsigned int gem_serial_ports[4] = {
	0x3f8, 0x2f8, 0x3e8, 0x2e8
};
static unsigned int gem_serial_lcr_port;
static unsigned char gem_serial_old_lcr;

void GEM_SERIAL_NEAR
gem_mouse_serial_save(int fd)
{
	struct stat status;
	unsigned int port;

	/* use the device number so /dev/mouse symlinks work too */
	if (fstat(fd, &status) < 0 || !S_ISCHR(status.st_mode))
		return;
	port = status.st_rdev - 0x0440U;
	if (port >= 4)
		return;
	gem_serial_lcr_port = gem_serial_ports[port] + 3;
	gem_serial_old_lcr = inb(gem_serial_lcr_port);
}

void GEM_SERIAL_NEAR
gem_mouse_serial_setup(void)
{
	if (gem_serial_lcr_port)
		outb(0x02, gem_serial_lcr_port);
}

void
gem_mouse_serial_restore(void)
{
	if (gem_serial_lcr_port)
		outb(gem_serial_old_lcr, gem_serial_lcr_port);
	gem_serial_lcr_port = 0;
}
