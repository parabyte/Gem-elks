/* resident AES/VDI owner for the ELKS GEM trap */

#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "gem_aes_resident.h"
#include "gem_proc.h"
#include "gem_scrap_resident.h"
#include "gem_system_resource.h"
#include "gem_vdi_font.h"
#include "gem_vdi_resident.h"
#include "gemtrap.h"

#define GEM_SERVER_TICK_MS	20U
#define GEM_SERVER_TICK_US	20000U
#define GEM_SERVER_ITIMER_REAL	0

/* exact setitimer wire layout */
typedef struct __attribute__((packed)) gem_server_itimer_words {
	UWORD interval_seconds_lo;
	UWORD interval_seconds_hi;
	UWORD interval_microseconds_lo;
	UWORD interval_microseconds_hi;
	UWORD value_seconds_lo;
	UWORD value_seconds_hi;
	UWORD value_microseconds_lo;
	UWORD value_microseconds_hi;
} GEM_SERVER_ITIMER_WORDS;

typedef BYTE GEM_SERVER_ITIMER_MUST_BE_16_BYTES
	[(sizeof(GEM_SERVER_ITIMER_WORDS) == 16) ? 1 : -1];

extern int setitimer(int which, const GEM_SERVER_ITIMER_WORDS *value,
	GEM_SERVER_ITIMER_WORDS *old_value);

static const BYTE gem_server_desktop[] = "/bin/gemdesk";
static volatile WORD gem_server_stop;
static volatile WORD gem_server_children_changed;
static volatile WORD gem_server_tick_pending;
static volatile WORD gem_server_timer_failed;
static UWORD gem_server_desktop_pid;

static WORD gem_server_timer(WORD enable);

static VOID
gem_server_say(const char *text)
{
	const char *end;

	end = text;
	while (*end)
		end++;
	(void) write(2, text, (size_t) (end - text));
}

static VOID
gem_server_say_number(UWORD value)
{
	char digits[6];
	WORD index;

	index = 6;
	do {
		digits[--index] = (char) ('0' + value % 10U);
		value /= 10U;
	} while (value && index);
	(void) write(2, digits + index, (size_t) (6 - index));
}

static VOID
gem_server_stop_signal(WORD signal_number)
{
	(void) signal_number;
	gem_server_stop = TRUE;
}

static VOID
gem_server_child_signal(WORD signal_number)
{
	(void) signal_number;
	gem_server_children_changed = TRUE;
	(void) signal(SIGCHLD, gem_server_child_signal);
}

static VOID
gem_server_tick_signal(WORD signal_number)
{
	WORD saved_errno;

	(void) signal_number;
	saved_errno = (WORD) errno;
	gem_server_tick_pending = TRUE;
	(void) signal(SIGALRM, gem_server_tick_signal);
	if (!gem_server_timer(TRUE))
		gem_server_timer_failed = TRUE;
	errno = saved_errno;
}

static WORD
gem_server_timer(WORD enable)
{
	GEM_SERVER_ITIMER_WORDS timer;

	timer.interval_seconds_lo = 0;
	timer.interval_seconds_hi = 0;
	timer.interval_microseconds_lo = enable ? GEM_SERVER_TICK_US : 0;
	timer.interval_microseconds_hi = 0;
	timer.value_seconds_lo = 0;
	timer.value_seconds_hi = 0;
	timer.value_microseconds_lo = enable ? GEM_SERVER_TICK_US : 0;
	timer.value_microseconds_hi = 0;
	return setitimer(GEM_SERVER_ITIMER_REAL, &timer, NULL) == 0;
}

static WORD
gem_server_reply(struct gemtrap_request *request)
{
	if (gemctl(GEMCTL_REPLY, request) != 0 && errno != ESRCH)
		return FALSE;
	return TRUE;
}

static WORD
gem_server_reply_ready(VOID)
{
	struct gemtrap_request ready;

	while (gem_aes_resident_ready(&ready)) {
		if (!gem_server_reply(&ready))
			return FALSE;
	}
	return TRUE;
}

/* reap apps and the Desktop by pid */
static VOID
gem_server_reap_children(VOID)
{
	pid_t waited;
	int status;

	do {
		gem_server_children_changed = FALSE;
		while (gem_proc_poll(NULL, NULL))
			;
		if (gem_server_desktop_pid) {
			do {
				waited = waitpid((pid_t) gem_server_desktop_pid,
					&status, WNOHANG);
			} while (waited == (pid_t) -1 && errno == EINTR);
			if (waited == (pid_t) gem_server_desktop_pid
				|| (waited == (pid_t) -1 && errno == ECHILD)) {
				gem_server_desktop_pid = 0;
				gem_server_stop = TRUE;
			}
		}
	} while (gem_server_children_changed);
}

static WORD
gem_server_startup(VOID)
{
	WORD saved_errno;
	BYTE font_directory[GEM_VDI_FONT_DIRECTORY_MAX];
	UWORD index;

	if (chdir("/lib/gemsys") != 0) {
		gem_server_say("GEM: cannot enter /lib/gemsys\r\n");
		return FALSE;
	}
	if (!gem_vdi_resident_startup()) {
		gem_server_say("GEM: cannot open the display or load the "
			"system font (SYSFONT.NN)\r\n");
		return FALSE;
	}
	if (!gem_system_resource_load()) {
		saved_errno = (WORD) errno;
		gem_system_resource_free();
		gem_vdi_resident_shutdown();
		gem_server_say("GEM: cannot load " GEM_SYSTEM_RESOURCE_FILE
			", errno ");
		gem_server_say_number((UWORD) saved_errno);
		gem_server_say("\r\n");
		return FALSE;
	}
	if (!gem_vdi_resident_default_mouse()) {
		gem_system_resource_free();
		gem_vdi_resident_shutdown();
		gem_server_say("GEM: no mouse form in GEM.RSC\r\n");
		return FALSE;
	}
	gem_scrap_resident_init();

	if (gem_system_resource_string(GEM_SYSTEM_STINPATH, font_directory,
			GEM_VDI_FONT_DIRECTORY_MAX) >= 0) {
		index = 0;
		while (font_directory[index]
			&& font_directory[index] != ';') {
			if (font_directory[index] == 0x5c)
				font_directory[index] = '/';
			index++;
		}
		font_directory[index] = 0;
		index = (font_directory[0] && font_directory[1] == ':')
			? 2U : 0U;
		gem_vdi_font_set_directory(font_directory + index);
	}
	return TRUE;
}

static VOID
gem_server_stop_clients(VOID)
{
	WORD channel;
	pid_t waited;
	int status;

	channel = 2;
	while (channel < GEM_PROC_CHANNELS) {
		(void) proc_delete(channel);
		channel++;
	}
	if (!gem_server_desktop_pid)
		return;
	(void) kill((pid_t) gem_server_desktop_pid, SIGTERM);
	do {
		waited = waitpid((pid_t) gem_server_desktop_pid,
			&status, WNOHANG);
	} while (waited == (pid_t) -1 && errno == EINTR);
	if (waited == 0) {
		(void) kill((pid_t) gem_server_desktop_pid, SIGKILL);
		do {
			waited = waitpid((pid_t) gem_server_desktop_pid,
				&status, 0);
		} while (waited == (pid_t) -1 && errno == EINTR);
	}
	gem_server_desktop_pid = 0;
}

int
main(void)
{
	struct gemtrap_request request;
	WORD failed;
	WORD result;

	gem_server_stop = FALSE;
	gem_server_children_changed = FALSE;
	gem_server_tick_pending = FALSE;
	gem_server_timer_failed = FALSE;
	gem_server_desktop_pid = 0;
	failed = FALSE;

	(void) signal(SIGTERM, gem_server_stop_signal);
	(void) signal(SIGINT, gem_server_stop_signal);
	if (signal(SIGCHLD, gem_server_child_signal) == SIG_ERR
		|| signal(SIGALRM, gem_server_tick_signal) == SIG_ERR)
		return 1;
	if (!gem_server_startup())
		return 1;
	if (gemctl(GEMCTL_REGISTER, NULL) != 0) {
		gem_system_resource_free();
		gem_vdi_resident_shutdown();
		return 1;
	}
	if (!gem_server_timer(TRUE)
		|| !gem_proc_spawn_external((LPBYTE) gem_server_desktop, NULL,
			&gem_server_desktop_pid)) {
		(void) gem_server_timer(FALSE);
		(void) gemctl(GEMCTL_UNREGISTER, NULL);
		gem_system_resource_free();
		gem_vdi_resident_shutdown();
		return 1;
	}

	while (!gem_server_stop) {
		if (gem_server_timer_failed) {
			failed = TRUE;
			break;
		}
		if (gem_server_tick_pending) {
			gem_server_tick_pending = FALSE;
			gem_aes_resident_poll(GEM_SERVER_TICK_MS);
		}
		if (!gem_server_reply_ready()) {
			failed = TRUE;
			break;
		}
		if (gem_server_children_changed)
			gem_server_reap_children();
		if (gem_server_stop)
			break;

		if (gemctl(GEMCTL_NEXT, &request) != 0) {
			if (errno == EINTR)
				continue;
			failed = TRUE;
			break;
		}
		if (request.cx == GEMTRAP_CX_EXIT) {
			if (!gem_aes_resident_exit(&request)
				|| !gem_server_reply_ready()) {
				failed = TRUE;
				break;
			}
			continue;
		}

		if (request.cx == GEM_VDI_RESIDENT_SELECTOR)
			result = gem_vdi_resident_request(&request,
				gem_aes_resident_application(&request));
		else {
			result = gem_aes_resident_request(&request);
			gem_aes_resident_poll(0);
		}
		if (result != GEM_AES_RESIDENT_DEFERRED) {
			request.ax = (UWORD) result;
			if (!gem_server_reply(&request)) {
				failed = TRUE;
				break;
			}
		}
		if (!gem_server_reply_ready()) {
			failed = TRUE;
			break;
		}
	}

	(void) gem_server_timer(FALSE);
	gem_server_stop_clients();
	if (gemctl(GEMCTL_UNREGISTER, NULL) != 0)
		failed = TRUE;
	gem_system_resource_free();
	gem_vdi_resident_shutdown();
	return failed ? 1 : 0;
}
