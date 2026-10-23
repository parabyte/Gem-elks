/* AES calls over the stock ELKS UNIX socket */

#include "ppdgem.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include "gem_request.h"

static int gem_socket = -1;
static UWORD gem_socket_pid;

static VOID
gem_bindings_disconnect(VOID)
{
	if (gem_socket >= 0)
		(void) close(gem_socket);
	gem_socket = -1;
	gem_socket_pid = 0;
}

static WORD
gem_bindings_io(int descriptor, UBYTE *bytes, UWORD count, WORD writing)
{
	int moved;

	while (count) {
		if (writing)
			moved = write(descriptor, bytes, count);
		else
			moved = read(descriptor, bytes, count);
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

WORD
gem_bindings_connect(VOID)
{
	struct sockaddr_un address;
	pid_t pid;
	int descriptor;

	pid = getpid();
	if (gem_socket >= 0 && gem_socket_pid == (UWORD) pid)
		return TRUE;
	gem_bindings_disconnect();
	descriptor = socket(AF_UNIX, SOCK_STREAM, 0);
	if (descriptor < 0)
		return FALSE;
	memset(&address, 0, sizeof(address));
	address.sun_family = AF_UNIX;
	strcpy(address.sun_path, GEM_SERVER_SOCKET);
	if (connect(descriptor, (struct sockaddr *) &address,
			SUN_LEN(&address)) < 0) {
		(void) close(descriptor);
		return FALSE;
	}
	(void) fcntl(descriptor, F_SETFD, FD_CLOEXEC);
	(void) signal(SIGPIPE, SIG_IGN);
	gem_socket = descriptor;
	gem_socket_pid = (UWORD) pid;
	return TRUE;
}

WORD
gem_bindings_call(UWORD selector, UWORD parameter_offset)
{
	struct gem_request request;
	UWORD segment;

	if (!gem_bindings_connect())
		return -1;
	segment = gem_bindings_data_segment();
	memset(&request, 0, sizeof(request));
	request.pid = gem_socket_pid;
	request.cx = selector;
	request.ds = segment;
	request.es = segment;
	request.data_limit = 0xffffU;
	if (selector == 0x0473U)
		request.dx = parameter_offset;
	else
		request.bx = parameter_offset;
	if (!gem_bindings_io(gem_socket, (UBYTE *) &request,
			(UWORD) sizeof(request), TRUE)
		|| !gem_bindings_io(gem_socket, (UBYTE *) &request,
			(UWORD) sizeof(request), FALSE)) {
		gem_bindings_disconnect();
		return -1;
	}
	return (WORD) request.ax;
}

UWORD
gem_bindings_data_segment(VOID)
{
	UWORD segment;

	__asm__ volatile ("movw %%ds,%0" : "=r" (segment));
	return segment;
}

VOID
gem_bindings_store_pointer(GEM_BINDINGS_POINTER_SLOT *slot,
	const VOID FAR *pointer)
{
#if GEM_FAR_DATA
	union gem_binding_far_pointer {
		const VOID FAR *pointer;
		GEM_BINDINGS_POINTER_SLOT words;
	} value;
#endif

	if (!pointer) {
		slot->lo = 0;
		slot->hi = 0;
		return;
	}

#if GEM_FAR_DATA
	value.pointer = pointer;
	*slot = value.words;
#else
	slot->lo = (UWORD) pointer;
	slot->hi = gem_bindings_data_segment();
#endif
}

GEM_SLOT_POINTER
gem_bindings_load_pointer(const GEM_BINDINGS_POINTER_SLOT *slot)
{
#if GEM_FAR_DATA
	union gem_binding_far_pointer {
		GEM_SLOT_POINTER pointer;
		GEM_BINDINGS_POINTER_SLOT words;
	} value;

	value.words = *slot;
	return value.pointer;
#else
	if (!slot->lo && !slot->hi)
		return NULL;
	if (slot->hi != gem_bindings_data_segment())
		return NULL;
	return (GEM_SLOT_POINTER) slot->lo;
#endif
}

WORD
gem(GEM_BINDINGS_AESPB *parameter_block)
{
	return gem_bindings_call(200U, (UWORD) parameter_block);
}

WORD
aescheck(VOID)
{
	return gem_bindings_connect();
}
