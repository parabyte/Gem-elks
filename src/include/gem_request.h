/* local GEM request */

#ifndef ELKS_GEM_REQUEST_H
#define ELKS_GEM_REQUEST_H

#define GEM_SERVER_SOCKET "/tmp/gem.aes"
#define GEM_REQUEST_EXIT  0xffffU

struct gem_request {
	unsigned short slot;
	unsigned short pid;
	unsigned short ax;
	unsigned short bx;
	unsigned short cx;
	unsigned short dx;
	unsigned short es;
	unsigned short ds;
	unsigned short data_limit;
	unsigned short generation_lo;
	unsigned short generation_hi;
};

typedef char gem_request_must_be_22_bytes
	[(sizeof(struct gem_request) == 22) ? 1 : -1];

#endif /* ELKS_GEM_REQUEST_H */
