#ifndef GEM_MOUSE_SERIAL_H
#define GEM_MOUSE_SERIAL_H

#ifdef __ia16__
#define GEM_SERIAL_NEAR __far __attribute__((near_section, noinline, \
	section(".text.gemserial")))
#else
#define GEM_SERIAL_NEAR
#endif

void GEM_SERIAL_NEAR gem_mouse_serial_save(int fd);
void GEM_SERIAL_NEAR gem_mouse_serial_setup(void);
void gem_mouse_serial_restore(void);

#endif
