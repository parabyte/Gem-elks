/*
 * gem_main.c - GEM AES/VDI server for stock ELKS.
 *
 * The broker port required a modified kernel: an INT EF trap handler, the
 * gemctl() system call, task brokering, and medium-model signal delivery.
 * Following the upstream ELKS guidance, this server is instead an ordinary
 * user process on a stock kernel.  It owns the screen and input devices,
 * spawns the Desktop client with two ordinary kernel pipes on descriptors
 * 3 and 4, and serves the original 22-byte INT EF register records the
 * client writes.  On a real-mode machine the resident AES/VDI reads and
 * writes the client's memory directly through the recorded segment words,
 * so nothing is copied or converted in between.
 *
 * A blocking pipe read replaces the kernel wait; a 20 ms select() timeout
 * replaces the SIGALRM input tick; a closed pipe replaces the kernel EXIT
 * record.  No signal handler is installed anywhere.
 *
 * Program launch follows original single-tasking GEM exactly.  SHEL_WRITE
 * records the next command and the Desktop exits; this server then runs
 * the recorded program with plain vfork/execv/waitpid on the restored
 * text console and starts a fresh Desktop afterwards.  There is no process
 * table, logical channel, or memory arena: ELKS owns every process and
 * every byte of memory.
 */

#include <errno.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "gem_aes_resident.h"
#include "gem_shell_resident.h"
#include "gem_vdi_resident.h"
#include "gemtrap.h"


#define GEM_SERVER_TICK_MS	20U
#define GEM_SERVER_TICK_US	20000U

/* Client-side transport descriptors installed before exec. */
#define GEM_SERVER_REQUEST_FD	3
#define GEM_SERVER_REPLY_FD	4

/* argv[0] plus at most fifteen classic tail tokens plus the terminator. */
#define GEM_SERVER_ARGV_ENTRIES	17

static const char gem_server_desktop[] = "/bin/gemdesk";
static const char gem_server_self[] = "/bin/gem";

static int gem_server_request_fd = -1;
static int gem_server_reply_fd = -1;

static UBYTE gem_server_command[GEM_SHELL_COMMAND_BYTES];
static UBYTE gem_server_tail[GEM_SHELL_TAIL_BYTES];
static char *gem_server_argv[GEM_SERVER_ARGV_ENTRIES];

/*
 * ELKS select(2) keeps two historical four-byte timeval fields.  Retain
 * that wire layout as explicit little-endian word pairs so the server
 * needs no C long or wide arithmetic.
 */
typedef struct __attribute__((packed)) gem_server_timeval_words {
	UWORD seconds_lo;
	UWORD seconds_hi;
	UWORD useconds_lo;
	UWORD useconds_hi;
} GEM_SERVER_TIMEVAL_WORDS;

/*
 * In-process replacement for the retired gemctl() system call, still named
 * by the resident core at its attach/detach/cancel points.  With the whole
 * broker gone there is no cross-task segment pinning or delivery queue
 * left to manage, so every recognized operation is complete bookkeeping.
 */
int
gemctl(unsigned int operation, struct gemtrap_request *request)
{
	(void) request;
	switch (operation) {
	case GEMCTL_REGISTER:
	case GEMCTL_UNREGISTER:
	case GEMCTL_REPLY:
	case GEMCTL_CANCEL:
	case GEMCTL_ATTACH:
	case GEMCTL_DETACH:
		return 0;
	default:
		/* NEXT/NEXT_NOWAIT existed only for the kernel broker. */
		errno = EINVAL;
		return -1;
	}
}

static WORD
gem_server_io(int fd, UBYTE *bytes, UWORD count, WORD writing)
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
gem_server_reply(struct gemtrap_request *request)
{
	return gem_server_io(gem_server_reply_fd, (UBYTE *) request,
		(UWORD) sizeof(*request), TRUE);
}

/*
 * Deliver every wait completed by the resident event core.  *delivered is
 * set TRUE when at least one completion was sent, so the serve loop can tell
 * that an outstanding deferred event has now been answered.
 */
static WORD
gem_server_flush_ready(WORD *delivered)
{
	struct gemtrap_request done;

	*delivered = FALSE;
	while (gem_aes_resident_ready(&done)) {
		*delivered = TRUE;
		if (!gem_server_reply(&done))
			return FALSE;
	}
	return TRUE;
}

static VOID
gem_server_close_pipes(VOID)
{
	if (gem_server_request_fd >= 0)
		(void) close(gem_server_request_fd);
	if (gem_server_reply_fd >= 0)
		(void) close(gem_server_reply_fd);
	gem_server_request_fd = -1;
	gem_server_reply_fd = -1;
}

/*
 * Start one Desktop client with the transport pipes on its descriptors 3
 * and 4.  vfork() on stock ELKS is an ordinary fork; only descriptor moves,
 * the raw exec, and _exit run in the child either way.
 */
static WORD
gem_server_spawn(UWORD *desktop_pid)
{
	int request_pipe[2];
	int reply_pipe[2];
	pid_t child;

	if (pipe(request_pipe) < 0)
		return FALSE;
	if (pipe(reply_pipe) < 0) {
		(void) close(request_pipe[0]);
		(void) close(request_pipe[1]);
		return FALSE;
	}
	child = vfork();
	if (child == (pid_t) 0) {
		if (dup2(request_pipe[1], GEM_SERVER_REQUEST_FD) < 0
		    || dup2(reply_pipe[0], GEM_SERVER_REPLY_FD) < 0)
			_exit(126);
		(void) close(request_pipe[0]);
		(void) close(request_pipe[1]);
		(void) close(reply_pipe[0]);
		(void) close(reply_pipe[1]);
		static char *desktop_argv[2] = { (char *) gem_server_desktop, 0 };

		execv(gem_server_desktop, desktop_argv);
		_exit(127);
	}
	(void) close(request_pipe[1]);
	(void) close(reply_pipe[0]);
	if (child == (pid_t) -1) {
		(void) close(request_pipe[0]);
		(void) close(reply_pipe[1]);
		return FALSE;
	}
	gem_server_request_fd = request_pipe[0];
	gem_server_reply_fd = reply_pipe[1];
	/*
	 * The request pipe stays blocking.  ELKS pipes have no select() handler,
	 * but a blocking read is exactly what the serve loop wants while the
	 * Desktop is drawing: it returns each request the instant it arrives,
	 * with no per-call polling delay.  The 20 ms input tick is used only
	 * while a deferred AES event is outstanding (see gem_server_serve).
	 */
	*desktop_pid = (UWORD) child;
	return TRUE;
}

/*
 * Sleep one 20 ms tick.  ELKS pipes have no select() handler and report a
 * read-end as always ready, so select() cannot signal request-pipe
 * readiness.  A select() with no descriptor sets is unaffected by that and
 * is a portable fixed-interval sleep; it drives the resident mouse, menu,
 * and timer cadence the broker owner previously got from SIGALRM.
 */
static VOID
gem_server_tick_sleep(VOID)
{
	GEM_SERVER_TIMEVAL_WORDS timeout;

	timeout.seconds_lo = 0;
	timeout.seconds_hi = 0;
	timeout.useconds_lo = GEM_SERVER_TICK_US;
	timeout.useconds_hi = 0;
	(void) select(0, NULL, NULL, NULL,
		(struct timeval *) (VOID *) &timeout);
}

/*
 * Process one complete request record.  VDI draws and ordinary AES calls
 * reply immediately.  An EVNT_* wait defers: no reply is sent now, *deferred
 * is set, and the reply is delivered later by flush_ready once the resident
 * event core completes the wait.  Returns FALSE only on an unrecoverable
 * reply-pipe error.
 */
static WORD
gem_server_dispatch(struct gemtrap_request *request, WORD *deferred)
{
	WORD result;

	*deferred = FALSE;
	if (request->cx == GEM_VDI_RESIDENT_SELECTOR) {
		result = gem_vdi_resident_request(request,
			gem_aes_resident_application(request));
		request->ax = (UWORD) result;
		return gem_server_reply(request);
	}
	result = gem_aes_resident_request(request);
	gem_aes_resident_poll(0);
	if (result != GEM_AES_RESIDENT_DEFERRED) {
		request->ax = (UWORD) result;
		return gem_server_reply(request);
	}
	*deferred = TRUE;
	return TRUE;
}

/*
 * Block until one complete request record has been read.  ELKS pipes may
 * return a short read, so partial records are accumulated.  Returns FALSE on
 * EOF (Desktop exited) or an unexpected error.
 */
static WORD
gem_server_read_record(struct gemtrap_request *request)
{
	UBYTE *dst;
	UWORD need;
	int got;

	dst = (UBYTE *) request;
	need = (UWORD) sizeof(*request);
	while (need) {
		got = read(gem_server_request_fd, dst, (int) need);
		if (got == 0)
			return FALSE;		/* Desktop closed the pipe. */
		if (got < 0) {
			if (errno == EINTR)
				continue;
			return FALSE;		/* Unexpected pipe error. */
		}
		dst += got;
		need -= (UWORD) got;
	}
	return TRUE;
}

/*
 * Serve one Desktop lifetime.
 *
 * While the Desktop is issuing AES/VDI calls it blocks for each reply, so a
 * blocking read here returns the next request the instant it arrives and a
 * full redraw of hundreds of primitives runs at pipe speed - no per-call
 * delay.  The 20 ms input tick is used only while a deferred AES event
 * (EVNT_MULTI and friends) is outstanding: then the Desktop sends nothing
 * until the event completes, so the server polls resident input/timer state
 * on that cadence until the wait is satisfied and returns to blocking reads.
 *
 * Returns when the Desktop closes its request pipe (read returns EOF).
 */
static VOID
gem_server_serve(VOID)
{
	struct gemtrap_request request;
	WORD pending;
	WORD deferred;
	WORD delivered;

	pending = FALSE;
	for (;;) {
		if (!pending) {
			if (!gem_server_read_record(&request))
				return;
			if (!gem_server_dispatch(&request, &deferred))
				return;
			if (deferred)
				pending = TRUE;
		} else {
			gem_server_tick_sleep();
			gem_aes_resident_poll(GEM_SERVER_TICK_MS);
		}
		if (!gem_server_flush_ready(&delivered))
			return;
		if (delivered)
			pending = FALSE;
	}
}

/*
 * Split the classic command tail - one length byte, at most 127 bytes,
 * historically ended by a carriage return - into an ordinary ELKS argv.
 * Separators are rewritten to NUL in place; no byte is copied.
 */
static VOID
gem_server_build_argv(VOID)
{
	UBYTE *bytes;
	UWORD length;
	UWORD index;
	UWORD used;

	gem_server_argv[0] = (char *) gem_server_command;
	length = gem_server_tail[0];
	if (length > 127U)
		length = 127U;
	bytes = gem_server_tail + 1;
	bytes[length] = 0;
	index = 0;
	while (index < length) {
		if (bytes[index] == (UBYTE) '\r'
		    || bytes[index] == (UBYTE) ' ')
			bytes[index] = 0;
		index++;
	}
	used = 1;
	index = 0;
	while (index < length && used < GEM_SERVER_ARGV_ENTRIES - 1) {
		while (index < length && !bytes[index])
			index++;
		if (index >= length)
			break;
		gem_server_argv[used++] = (char *) (bytes + index);
		while (index < length && bytes[index])
			index++;
	}
	gem_server_argv[used] = (char *) 0;
}

/* Run the recorded program on the restored text console and wait for it. */
static VOID
gem_server_run_command(VOID)
{
	pid_t child;
	pid_t waited;
	WORD status;

	gem_server_build_argv();
	child = vfork();
	if (child == (pid_t) 0) {
		execv((const char *) gem_server_command, gem_server_argv);
		_exit(127);
	}
	if (child == (pid_t) -1)
		return;
	do {
		waited = waitpid(child, (int *) &status, 0);
	} while (waited == (pid_t) -1 && errno == EINTR);
	(void) waited;
	(void) status;
}

int
main(int argc, char **argv)
{
	UWORD desktop_pid;
	pid_t waited;
	WORD status;

	(void) argc;
	/*
	 * Original GEM ran with GEMSYS as the current directory; the Desktop
	 * client and every launched program inherit it.
	 */
	if (chdir("/GEMAPPS/GEMSYS") != 0)
		return 1;
	/*
	 * Classic GEM opens physical GSX before accepting AES calls, making
	 * GRAF_HANDLE and RSRC_LOAD geometry available to APPL_INIT.
	 */
	if (!gem_vdi_resident_startup())
		return 1;

	for (;;) {
		if (!gem_server_spawn(&desktop_pid)) {
			gem_vdi_resident_shutdown();
			return 1;
		}
		gem_server_serve();
		gem_server_close_pipes();
		do {
			waited = waitpid((pid_t) desktop_pid,
				(int *) &status, 0);
		} while (waited == (pid_t) -1 && errno == EINTR);
		(void) waited;
		(void) status;

		if (gem_shell_resident_take_command(gem_server_command,
		    GEM_SHELL_COMMAND_BYTES, gem_server_tail,
		    GEM_SHELL_TAIL_BYTES)) {
			/*
			 * Single-tasking launch: give the program a plain text
			 * console, run it to completion, then reopen the screen
			 * for the next Desktop.
			 */
			(void) gem_vdi_resident_suspend();
			gem_server_run_command();
			if (!gem_vdi_resident_resume()) {
				gem_vdi_resident_shutdown();
				return 1;
			}
			continue;
		}
		if (gem_aes_resident_active()) {
			/*
			 * The Desktop died without APPL_EXIT.  Restart the whole
			 * server through exec so every resident record begins
			 * fresh; the kernel reclaims everything else.
			 */
			gem_vdi_resident_shutdown();
			if (argv && argv[0] && argv[0][0] == '/')
				execv(argv[0], argv);
			execv(gem_server_self, argv);
			return 1;
		}
		break;
	}
	gem_vdi_resident_shutdown();
	return 0;
}
