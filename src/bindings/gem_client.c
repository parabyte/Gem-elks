/*
 * gem_client.c - pipe transport from the Desktop to the GEM AES server
 *
 * each AES/VDI call crosses as a 22-byte record on pipes 3 (request) and
 * 4 (reply); the server reads and writes this process's memory through
 * the segment values in the record
 */

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include "gem_bindings_elks.h"
#include "gemtrap.h"

#define GEM_CLIENT_REQUEST_FD	3
#define GEM_CLIENT_REPLY_FD	4

static UWORD gem_client_pid;

static WORD
gem_client_io(int fd, UBYTE *bytes, UWORD count, WORD writing)
{
	int moved;

	while (count) {
		if (writing)
			moved = write(fd, bytes, count);
		else
			moved = read(fd, bytes, count);
		if (moved <= 0) {
			if (moved < 0 && errno == EINTR)
				continue;
			return FALSE;
		}
		bytes += moved;
		count -= (UWORD) moved;
	}
	return TRUE;
}

static WORD
gem_client_call(struct gemtrap_request *request)
{
	request->slot = 0;
	request->pid = gem_client_pid;
	request->data_limit = 0xffffU;	/* whole 64 KiB data segment */
	request->generation_lo = 1;
	request->generation_hi = 0;
	/* a broken pipe means the server is gone, so exit */
	if (!gem_client_io(GEM_CLIENT_REQUEST_FD, (UBYTE *) request,
			(UWORD) sizeof(*request), TRUE))
		_exit(125);
	if (!gem_client_io(GEM_CLIENT_REPLY_FD, (UBYTE *) request,
			(UWORD) sizeof(*request), FALSE))
		_exit(125);
	return (WORD) request->ax;
}

static WORD
gem_client_aes(GEM_BINDINGS_AESPB *parameter_block)
{
	struct gemtrap_request request;

	request.ax = 0;
	request.bx = (UWORD) parameter_block;
	request.cx = 200;
	request.dx = 0;
	request.es = gem_bindings_data_segment();
	request.ds = gem_bindings_data_segment();
	return gem_client_call(&request);
}

static WORD
gem_client_vdi(GEM_BINDINGS_VDIPB *parameter_block)
{
	struct gemtrap_request request;

	request.ax = 0;
	request.bx = 0;
	request.cx = 0x0473U;
	request.dx = (UWORD) parameter_block;
	request.es = gem_bindings_data_segment();
	request.ds = gem_bindings_data_segment();
	return gem_client_call(&request);
}

WORD
gem_client_install(VOID)
{
	pid_t pid;

	pid = getpid();
	if (pid <= 0)
		return FALSE;
	gem_client_pid = (UWORD) pid;
	(void) divert_aes(gem_client_aes);
	(void) divert_vdi(gem_client_vdi);
	return TRUE;
}
