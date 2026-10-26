#ifndef GEM_TEST_IO_H
#define GEM_TEST_IO_H

unsigned char gem_test_inb(unsigned int port);
void gem_test_outb(unsigned char value, unsigned int port);
#define inb(port) gem_test_inb(port)
#define outb(value, port) gem_test_outb(value, port)

#endif
