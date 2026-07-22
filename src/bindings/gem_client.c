/*
 * gem_client.c - pipe transport from the Desktop to the GEM AES server.
 *
 * The AES/VDI owner is an ordinary ELKS process; this client is another.
 * Both are stock-kernel user programs connected by two ordinary kernel
 * pipes which the server passes down as file descriptors 3 and 4 before
 * exec.  Each AES or VDI call still crosses as the original 22-byte INT EF
 * register record; the server reads and writes this process's memory
 * directly through the recorded segment values, which needs no copying or
 * conversion on a real-mode machine.  A blocking pipe read is the wait
 * state for deferred AES events, so the client needs no signal, timer, or
 * polling loop of its own.
 */

#include <errno.h>
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
	/*
	 * The whole 64 KiB data segment is addressable, so the server-side
	 * pointer checks use the largest exclusive 16-bit limit.
	 */
	request->data_limit = 0xffffU;
	request->generation_lo = 1;
	request->generation_hi = 0;
	/*
	 * A dead server cannot be recovered from inside a drawing call; the
	 * kernel reparents and reaps this task like any other exiting process.
	 */
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
