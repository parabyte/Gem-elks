/*
 * gem_main.c - GEM AES/VDI server for stock ELKS
 *
 * plain user process that owns the screen and input. starts the Desktop
 * with two pipes on descriptors 3 and 4 and serves the 22-byte records
 * the client writes; the AES/VDI reads the client's memory through the
 * segment values in the record. a blocking pipe read waits for work, a
 * 20 ms select() timeout is the timer tick, a closed pipe means the
 * client is gone
 */

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "gem_aes_resident.h"
#include "gem_shell_resident.h"
#include "gem_scrap_resident.h"
#include "gem_system_resource.h"
#include "gem_vdi_font.h"
#include "gem_vdi_resident.h"
#include "gemtrap.h"


#define GEM_SERVER_TICK_MS	20U
#define GEM_SERVER_TICK_US	20000U

/* the pipe descriptors the client finds after exec */
#define GEM_SERVER_REQUEST_FD	3
#define GEM_SERVER_REPLY_FD	4

/* argv[0], up to fifteen tail words, trailing NULL */
#define GEM_SERVER_ARGV_ENTRIES	17

static const char gem_server_desktop[] = "/bin/gemdesk";
static const char gem_server_self[] = "/bin/gem";

/* tell the user when GEM cant come up instead of leaving a blank
 * screen. these run once the screen is back in text mode; write() keeps
 * printf out of the server */
static void
gem_server_say(const char *text)
{
	const char *end = text;

	while (*end)
		end++;
	(void) write(2, text, (size_t) (end - text));
}

static void
gem_server_say_number(UWORD value)
{
	char digits[6];
	WORD index = 6;

	do {
		digits[--index] = (char) ('0' + value % 10U);
		value /= 10U;
	} while (value && index);
	(void) write(2, digits + index, (size_t) (6 - index));
}

static void
gem_server_report_death(const char *program, UWORD status)
{
	gem_server_say("GEM: ");
	gem_server_say(program);
	if ((status & 0x7fU) != 0) {
		gem_server_say(" died from signal ");
		gem_server_say_number(status & 0x7fU);
	} else {
		gem_server_say(" exited with status ");
		gem_server_say_number((status >> 8) & 0xffU);
	}
	gem_server_say("\r\n");
}

static int gem_server_request_fd = -1;
static int gem_server_reply_fd = -1;

static UBYTE gem_server_command[GEM_SHELL_COMMAND_BYTES];
static UBYTE gem_server_tail[GEM_SHELL_TAIL_BYTES];
static char *gem_server_argv[GEM_SERVER_ARGV_ENTRIES];

/* ELKS timeval, two four-byte fields, as little-endian word pairs */
typedef struct __attribute__((packed)) gem_server_timeval_words {
	UWORD seconds_lo;
	UWORD seconds_hi;
	UWORD useconds_lo;
	UWORD useconds_hi;
} GEM_SERVER_TIMEVAL_WORDS;

/* stand-in for the retired gemctl() syscall, nothing left to manage */
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

/* reply to every finished wait; *delivered goes TRUE if any went out */
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

/* launch argv tail passed to the initial client, or NULL */
static char *const *gem_initial_argv;

static WORD
gem_server_spawn(const char *program, UWORD *child_pid)
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
		char *default_argv[2];
		char *const *child_argv;
		int rfd = request_pipe[1];	/* child writes requests here */
		int wfd = reply_pipe[0];	/* child reads replies here */

		default_argv[0] = (char *) program;
		default_argv[1] = (char *) 0;
		child_argv = (char *const *) default_argv;
		if (gem_initial_argv && gem_initial_argv[0]
			&& !strcmp(gem_initial_argv[0], program))
			child_argv = gem_initial_argv;
		/* drop the two ends the server keeps, theyre never rfd or wfd */
		(void) close(request_pipe[0]);
		(void) close(reply_pipe[1]);
		/* put the request-write on 3 and the reply-read on 4 without a
		 * blind close clobbering a fd we just placed, so move a source
		 * off a target first if it sits on the one were about to write */
		if (wfd == GEM_SERVER_REQUEST_FD)
			wfd = dup(wfd);
		if (rfd == GEM_SERVER_REPLY_FD)
			rfd = dup(rfd);
		if (rfd != GEM_SERVER_REQUEST_FD) {
			if (dup2(rfd, GEM_SERVER_REQUEST_FD) < 0)
				_exit(126);
			(void) close(rfd);
		}
		if (wfd != GEM_SERVER_REPLY_FD) {
			if (dup2(wfd, GEM_SERVER_REPLY_FD) < 0)
				_exit(126);
			(void) close(wfd);
		}
		execv(program, child_argv);
		_exit(127);
	}
	(void) close(request_pipe[1]);
	(void) close(reply_pipe[0]);
	if (child == (pid_t) - 1) {
		(void) close(request_pipe[0]);
		(void) close(reply_pipe[1]);
		return FALSE;
	}
	gem_server_request_fd = request_pipe[0];
	gem_server_reply_fd = reply_pipe[1];
	*child_pid = (UWORD) child;
	return TRUE;
}

/* sleep one 20 ms tick. ELKS pipes have no select() handler and always
 * report a read end ready, so select() with empty sets is just a sleep */
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

/* handle one request record. VDI and plain AES calls reply at once, an
 * EVNT_* wait sets *deferred and flush_ready replies later */
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

/* block until one whole record is in; ELKS pipes can do short reads */
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
			return FALSE;	/* client closed the pipe */
		if (got < 0) {
			if (errno == EINTR)
				continue;
			return FALSE;
		}
		dst += got;
		need -= (UWORD) got;
	}
	return TRUE;
}

/* serve one client lifetime. blocking reads while the client calls;
 * while a deferred event is outstanding, poll resident input and timer
 * state on the 20 ms tick instead. returns on EOF */
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

/* split the classic command tail (one length byte, at most 127 bytes,
 * CR-terminated) into an argv, separators become NULs in place */
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
		if (bytes[index] == (UBYTE) '\r' || bytes[index] == (UBYTE) ' ')
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

/* run the recorded program on the text console and wait for it */
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
	if (child == (pid_t) - 1)
		return;
	do {
		waited = waitpid(child, (int *) &status, 0);
	} while (waited == (pid_t) - 1 && errno == EINTR);
	(void) waited;
	(void) status;
}

static VOID
gem_server_free_memory(VOID)
{
	static const char shell[] = "/bin/sh";
	static const char script[] =
		"ps | grep ktcp | grep -v grep | "
		"sed -n 's/^ *\\([0-9][0-9]*\\) .*/kill \\1/p' > /tmp/gk;"
		"ps | grep ftpd | grep -v grep | "
		"sed -n 's/^ *\\([0-9][0-9]*\\) .*/kill \\1/p' >> /tmp/gk;"
		"ps | grep telnetd | grep -v grep | "
		"sed -n 's/^ *\\([0-9][0-9]*\\) .*/kill \\1/p' >> /tmp/gk;"
		"sh /tmp/gk";
	char *child_argv[4];
	pid_t child;
	pid_t waited;
	WORD status;

	child = vfork();
	if (child == (pid_t) 0) {
		child_argv[0] = (char *) "sh";
		child_argv[1] = (char *) "-c";
		child_argv[2] = (char *) script;
		child_argv[3] = (char *) 0;
		execv(shell, child_argv);
		_exit(127);
	}
	if (child == (pid_t) - 1)
		return;
	do {
		waited = waitpid(child, (int *) &status, 0);
	} while (waited == (pid_t) - 1 && errno == EINTR);
	(void) waited;
	(void) status;
}

int
main(int argc, char **argv)
{
	WORD saved_errno;
	static UBYTE program[GEM_SHELL_COMMAND_BYTES];
	UWORD child_pid;
	pid_t waited;
	WORD status;
	WORD is_gem;
	WORD spawning_desktop;
	WORD desktop_failures;
	const char *initial_client;

	desktop_failures = 0;
	/* a client that dies mid-call leaves us writing its reply into a dead
	 * pipe; that must not take the server down with SIGPIPE */
	(void) signal(SIGPIPE, SIG_IGN);

	/* a restart through exec carries the failure count as -F<n> so a
	 * client that keeps dying cant cycle the screen forever */
	if (argc > 1 && argv[1] && argv[1][0] == '-' && argv[1][1] == 'F') {
		desktop_failures = (WORD) (argv[1][2] - '0');
		if (desktop_failures < 0 || desktop_failures > 9)
			desktop_failures = 0;
		argv++;
		argc--;
	}

	/* a path or program name on the command line replaces the Desktop,
	 * a bare name counts from /bin */
	initial_client = gem_server_desktop;
	if (argc > 1 && argv[1] && argv[1][0]) {
		if (argv[1][0] == '/') {
			initial_client = argv[1];
		} else {
			static char named_client[GEM_SHELL_COMMAND_BYTES];
			WORD at = 0;
			const char *from = "/bin/";

			while (*from)
				named_client[at++] = *from++;
			from = argv[1];
			while (*from && at
				< (WORD) (sizeof(named_client) - 1U))
				named_client[at++] = *from++;
			named_client[at] = 0;
			argv[1] = named_client;
			initial_client = named_client;
		}
		gem_initial_argv = (char *const *) (argv + 1);
	}
	if (initial_client == gem_server_desktop)
		gem_server_free_memory();
	/* run with GEMSYS as the current directory */
	if (chdir("/lib/gemsys") != 0)
		return 1;
	/* open the screen before taking AES calls */
	if (!gem_vdi_resident_startup()) {
		gem_server_say("GEM: cannot open the display or load the "
			"system font (SYSFONT.NN)\r\n");
		return 1;
	}
	/* load the AES's own GEM.RSC; everything comes from it and nothing is
	 * duplicated here, so this must work */
	if (!gem_system_resource_load()) {
		saved_errno = errno;
		gem_system_resource_free();
		gem_vdi_resident_shutdown();
		gem_server_say("GEM: cannot load " GEM_SYSTEM_RESOURCE_FILE
			", errno ");
		gem_server_say_number((UWORD) saved_errno);
		gem_server_say("\r\n");
		return 1;
	}
	/* the arrow is GEM.RSC's first mouse form */
	if (!gem_vdi_resident_default_mouse()) {
		gem_system_resource_free();
		gem_vdi_resident_shutdown();
		gem_server_say("GEM: no mouse form in GEM.RSC\r\n");
		return 1;
	}
	/* the Scrap Manager's dir is GEM.RSC's own STSCDIR */
	gem_scrap_resident_init();
	/* fonts live where the AES search path starts, GEM.RSC's STINPATH,
	 * not a directory written down here */
	{
		BYTE font_directory[GEM_VDI_FONT_DIRECTORY_MAX];
		UWORD index;

		if (gem_system_resource_string(GEM_SYSTEM_STINPATH,
				font_directory,
				GEM_VDI_FONT_DIRECTORY_MAX) >= 0) {
			for (index = 0; font_directory[index]
				&& font_directory[index] != ';'; index++)
				if (font_directory[index] == 0x5c)
					font_directory[index] = '/';
			font_directory[index] = 0;
			index = (font_directory[0]
				&& font_directory[1] == ':') ? 2U : 0U;
			gem_vdi_font_set_directory(font_directory + index);
		}
	}

	{
		const char *d = initial_client;
		UBYTE *p = program;
		while ((*p++ = (UBYTE) *d++) != 0);
	}
	spawning_desktop = TRUE;

	for (;;) {
		if (!gem_server_spawn((const char *) program, &child_pid)) {
			gem_system_resource_free();
			gem_vdi_resident_shutdown();
			return 1;
		}
		gem_server_serve();
		gem_server_close_pipes();
		do {
			waited = waitpid((pid_t) child_pid, (int *) &status, 0);
		} while (waited == (pid_t) - 1 && errno == EINTR);
		(void) waited;

		/* a client that dies without reaching APPL_EXIT is a failure.
		 * three strikes: text screen back, say what happened, stop */
		if (gem_aes_resident_active()
			|| (status & 0xff7fU) != 0) {
			desktop_failures++;
			if (desktop_failures >= 3) {
				gem_system_resource_free();
				gem_vdi_resident_shutdown();
				gem_server_report_death((const char *) program,
					(UWORD) status);
				gem_server_say
					("GEM is unable to continue.\r\n");
				return 1;
			}
		} else {
			desktop_failures = 0;
		}

		is_gem = FALSE;
		if (gem_shell_resident_take_command(gem_server_command,
				GEM_SHELL_COMMAND_BYTES, gem_server_tail,
				GEM_SHELL_TAIL_BYTES, &is_gem)) {
			if (is_gem) {
				/* serve a GEM app as the next client, screen live */
				UBYTE *p = program;
				const UBYTE *c = gem_server_command;
				while ((*p++ = *c++) != 0);
				spawning_desktop = FALSE;
				continue;
			}
			/* plain program: text console, run, reopen the screen */
			(void) gem_vdi_resident_suspend();
			gem_server_run_command();
			if (!gem_vdi_resident_resume()) {
				gem_system_resource_free();
				gem_vdi_resident_shutdown();
				return 1;
			}
		} else if (!spawning_desktop) {
			/* a GEM app exited without launching; Desktop comes back */
			(void) 0;
		} else if (gem_aes_resident_active()) {
			/* Desktop died without APPL_EXIT. restart through exec so
			 * every resident record starts fresh, carry the count in -F<n> */
			static char restart_flag[4] = "-F0";
			static char *restart_argv[GEM_SERVER_ARGV_ENTRIES];
			WORD entry;

			restart_flag[2] = (char) ('0' + desktop_failures);
			restart_argv[0] = argv[0];
			restart_argv[1] = restart_flag;
			for (entry = 1;
				entry < GEM_SERVER_ARGV_ENTRIES - 2
				&& argv[entry]; entry++)
				restart_argv[entry + 1] = argv[entry];
			restart_argv[entry + 1] = (char *) 0;
			gem_system_resource_free();
			gem_vdi_resident_shutdown();
			if (argv && argv[0] && argv[0][0] == '/')
				execv(argv[0], restart_argv);
			execv(gem_server_self, restart_argv);
			return 1;
		} else if ((status & 0xff7fU) != 0) {
			(void) 0;
		} else {
			break;	/* Desktop quit cleanly */
		}

		/* next client defaults to the first one */
		{
			const char *d = initial_client;
			UBYTE *p = program;
			while ((*p++ = (UBYTE) *d++) != 0);
		}
		spawning_desktop = TRUE;
	}
	gem_system_resource_free();
	gem_vdi_resident_shutdown();
	return 0;
}
