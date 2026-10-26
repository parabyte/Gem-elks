/* resident AES and VDI on stock ELKS */

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include "gem_aes_resident.h"
#include "gem_channels.h"
#include "gem_config.h"
#include "gem_exec.h"
#include "gem_scrap_resident.h"
#include "gem_system_resource.h"
#include "gem_vdi_font.h"
#include "gem_vdi_resident.h"
#include "gem_request.h"

#define GEM_SERVER_TICK_MS	20U
#define GEM_SERVER_TICK_US	20000L
#define GEM_SERVER_COLD __far __attribute__((far_section, noinline, \
	section(".fartext.gemserver")))

typedef struct gem_server_client {
	struct gem_request request;
	int descriptor;
	UWORD received;
	UWORD pid;
	UWORD segment;
	UWORD data_limit;
	UWORD generation_lo;
	UWORD generation_hi;
	UBYTE identified;
	UBYTE waiting;
} GEM_SERVER_CLIENT;

static const BYTE gem_server_desktop[] = "/bin/gemdesk";
static GEM_SERVER_CLIENT gem_server_clients[GEM_CHANNEL_COUNT];
static volatile WORD gem_server_stop;
static volatile WORD gem_server_children_changed;
static volatile WORD gem_server_tick_pending;
static UWORD gem_server_desktop_pid;
static UWORD gem_server_generation_lo;
static UWORD gem_server_generation_hi;
static int gem_server_listener = -1;

static VOID gem_server_drop_client(WORD channel);
static VOID gem_server_stop_pid(UWORD pid);

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
	WORD saved_errno;

	(void) signal_number;
	saved_errno = (WORD) errno;
	gem_server_children_changed = TRUE;
	(void) signal(SIGCHLD, gem_server_child_signal);
	errno = saved_errno;
}

static VOID
gem_server_tick_signal(WORD signal_number)
{
	WORD saved_errno;

	(void) signal_number;
	saved_errno = (WORD) errno;
	gem_server_tick_pending = TRUE;
	(void) signal(SIGALRM, gem_server_tick_signal);
	errno = saved_errno;
}

static WORD
gem_server_timer(WORD enable)
{
	struct itimerval timer;

	memset(&timer, 0, sizeof(timer));
	if (enable)
		timer.it_value.tv_usec = GEM_SERVER_TICK_US;
	return setitimer(ITIMER_REAL, &timer, NULL) == 0;
}

static WORD
gem_server_channel_valid(WORD channel)
{
	return channel == GEM_CHANNEL_DESKTOP
		|| (channel >= GEM_CHANNEL_FIRST_APP
			&& channel < GEM_CHANNEL_COUNT);
}

static VOID
gem_server_reset_client(GEM_SERVER_CLIENT *client)
{
	memset(client, 0, sizeof(*client));
	client->descriptor = -1;
}

static VOID
gem_server_initialize_clients(VOID)
{
	WORD channel;

	for (channel = 0; channel < GEM_CHANNEL_COUNT; channel++)
		gem_server_reset_client(gem_server_clients + channel);
}

static VOID
gem_server_next_generation(UWORD *low, UWORD *high)
{
	gem_server_generation_lo++;
	if (!gem_server_generation_lo) {
		gem_server_generation_hi++;
		if (!gem_server_generation_hi)
			gem_server_generation_lo = 1;
	}
	*low = gem_server_generation_lo;
	*high = gem_server_generation_hi;
}

static WORD GEM_SERVER_COLD
gem_server_socket_start(VOID)
{
	struct sockaddr_un address;
	int descriptor;

	(void) unlink(GEM_SERVER_SOCKET);
	descriptor = socket(AF_UNIX, SOCK_STREAM, 0);
	if (descriptor < 0)
		return FALSE;
	memset(&address, 0, sizeof(address));
	address.sun_family = AF_UNIX;
	strcpy(address.sun_path, GEM_SERVER_SOCKET);
	if (bind(descriptor, (struct sockaddr *) &address,
			SUN_LEN(&address)) < 0
		|| listen(descriptor, GEM_CHANNEL_COUNT - 1) < 0) {
		(void) close(descriptor);
		(void) unlink(GEM_SERVER_SOCKET);
		return FALSE;
	}
	(void) fcntl(descriptor, F_SETFD, FD_CLOEXEC);
	gem_server_listener = descriptor;
	return TRUE;
}

static VOID
gem_server_socket_stop(VOID)
{
	if (gem_server_listener >= 0)
		(void) close(gem_server_listener);
	gem_server_listener = -1;
	(void) unlink(GEM_SERVER_SOCKET);
}

static WORD
gem_server_reply(WORD channel, struct gem_request *request)
{
	GEM_SERVER_CLIENT *client;
	UBYTE *bytes;
	UWORD left;
	int moved;

	if (!gem_server_channel_valid(channel))
		return FALSE;
	client = gem_server_clients + channel;
	if (client->descriptor < 0 || !client->identified
		|| request->pid != client->pid
		|| request->ds != client->segment
		|| request->generation_lo != client->generation_lo
		|| request->generation_hi != client->generation_hi)
		return FALSE;
	bytes = (UBYTE *) request;
	left = (UWORD) sizeof(*request);
	while (left) {
		moved = write(client->descriptor, bytes, left);
		if (moved <= 0) {
			if (moved < 0 && errno == EINTR)
				continue;
			return FALSE;
		}
		bytes += moved;
		left -= (UWORD) moved;
	}
	return TRUE;
}

static VOID
gem_server_reply_ready(VOID)
{
	struct gem_request ready;
	GEM_SERVER_CLIENT *client;
	WORD channel;

	while (gem_aes_resident_ready(&ready)) {
		channel = (WORD) ready.slot;
		if (!gem_server_channel_valid(channel))
			continue;
		client = gem_server_clients + channel;
		client->waiting = FALSE;
		if (!gem_server_reply(channel, &ready))
			gem_server_drop_client(channel);
	}
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
		saved_errno = (WORD) errno;
		gem_server_say("GEM: cannot open display or font, errno ");
		gem_server_say_number((UWORD) saved_errno);
		gem_server_say("\r\n");
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
gem_server_drop_client(WORD channel)
{
	GEM_SERVER_CLIENT *client;
	struct gem_request request;
	int descriptor;
	int status;
	pid_t waited;
	UWORD pid;

	if (!gem_server_channel_valid(channel))
		return;
	client = gem_server_clients + channel;
	if (client->descriptor < 0)
		return;
	descriptor = client->descriptor;
	pid = client->identified ? client->pid : 0;
	if (client->identified) {
		memset(&request, 0, sizeof(request));
		request.slot = (UWORD) channel;
		request.pid = client->pid;
		request.ds = client->segment;
		request.es = client->segment;
		request.data_limit = client->data_limit;
		request.generation_lo = client->generation_lo;
		request.generation_hi = client->generation_hi;
		request.cx = GEM_REQUEST_EXIT;
		request.ax = (UWORD) channel;
		(void) gem_aes_resident_exit(&request);
	}
	(void) close(descriptor);
	gem_server_reset_client(client);
	if (!pid)
		return;
	do {
		waited = waitpid((pid_t) pid, &status, WNOHANG);
	} while (waited < 0 && errno == EINTR);
	if (!waited)
		gem_server_stop_pid(pid);
}

static VOID
gem_server_drop_pid(UWORD pid)
{
	WORD channel;

	for (channel = 0; channel < GEM_CHANNEL_COUNT; channel++)
		if (gem_server_channel_valid(channel)
			&& gem_server_clients[channel].identified
			&& gem_server_clients[channel].pid == pid) {
			gem_server_drop_client(channel);
			return;
		}
}

static VOID
gem_server_reap_children(VOID)
{
	pid_t waited;
	int status;

	do {
		gem_server_children_changed = FALSE;
		for (;;) {
			waited = waitpid((pid_t) -1, &status, WNOHANG);
			if (waited > 0) {
				gem_server_drop_pid((UWORD) waited);
				if ((UWORD) waited == gem_server_desktop_pid) {
					gem_server_desktop_pid = 0;
					gem_server_stop = TRUE;
				}
				continue;
			}
			if (waited < 0 && errno == EINTR)
				continue;
			break;
		}
	} while (gem_server_children_changed);
}

static WORD
gem_server_accept_client(VOID)
{
	GEM_SERVER_CLIENT *client;
	int descriptor;
	int flags;
	WORD channel;

	descriptor = accept(gem_server_listener, NULL, NULL);
	if (descriptor < 0)
		return errno == EINTR || errno == EAGAIN;
	channel = GEM_CHANNEL_DESKTOP;
	if (gem_server_clients[channel].descriptor >= 0) {
		channel = GEM_CHANNEL_FIRST_APP;
		while (channel < GEM_CHANNEL_COUNT
			&& gem_server_clients[channel].descriptor >= 0)
			channel++;
	}
	if (channel >= GEM_CHANNEL_COUNT) {
		(void) close(descriptor);
		return TRUE;
	}
	flags = fcntl(descriptor, F_GETFL, 0);
	if (flags < 0 || fcntl(descriptor, F_SETFL,
			flags | O_NONBLOCK) < 0) {
		(void) close(descriptor);
		return TRUE;
	}
	(void) fcntl(descriptor, F_SETFD, FD_CLOEXEC);
	client = gem_server_clients + channel;
	gem_server_reset_client(client);
	client->descriptor = descriptor;
	gem_server_next_generation(&client->generation_lo,
		&client->generation_hi);
	return TRUE;
}

static WORD
gem_server_identify(WORD channel, GEM_SERVER_CLIENT *client)
{
	struct gem_request *request;

	request = &client->request;
	if (!request->pid || !request->ds || request->es != request->ds)
		return FALSE;
	if (!client->identified) {
		if (channel == GEM_CHANNEL_DESKTOP
			&& request->pid != gem_server_desktop_pid)
			return FALSE;
		client->pid = request->pid;
		client->segment = request->ds;
		client->data_limit = request->data_limit;
		client->identified = TRUE;
	} else if (request->pid != client->pid
		|| request->ds != client->segment
		|| request->data_limit != client->data_limit) {
		return FALSE;
	}
	request->slot = (UWORD) channel;
	request->generation_lo = client->generation_lo;
	request->generation_hi = client->generation_hi;
	return TRUE;
}

static WORD
gem_server_dispatch(WORD channel, GEM_SERVER_CLIENT *client)
{
	struct gem_request *request;
	WORD result;

	request = &client->request;
	if (request->cx == GEM_VDI_RESIDENT_SELECTOR)
		result = gem_vdi_resident_request(request,
			gem_aes_resident_application(request));
	else {
		result = gem_aes_resident_request(request);
		gem_aes_resident_poll(0);
	}
	if (result == GEM_AES_RESIDENT_DEFERRED) {
		client->waiting = TRUE;
		return TRUE;
	}
	request->ax = (UWORD) result;
	return gem_server_reply(channel, request);
}

static WORD
gem_server_read_client(WORD channel)
{
	GEM_SERVER_CLIENT *client;
	UBYTE stray;
	UBYTE *next;
	UWORD left;
	int received;

	client = gem_server_clients + channel;
	if (client->waiting) {
		received = read(client->descriptor, &stray, 1U);
		if (received < 0 && (errno == EAGAIN || errno == EINTR))
			return TRUE;
		return FALSE;
	}
	next = (UBYTE *) &client->request + client->received;
	left = (UWORD) sizeof(client->request) - client->received;
	received = read(client->descriptor, next, left);
	if (received < 0) {
		if (errno == EAGAIN || errno == EINTR)
			return TRUE;
		return FALSE;
	}
	if (!received)
		return FALSE;
	client->received += (UWORD) received;
	if (client->received != (UWORD) sizeof(client->request))
		return TRUE;
	client->received = 0;
	if (!gem_server_identify(channel, client)
		|| !gem_server_dispatch(channel, client))
		return FALSE;
	return TRUE;
}

static VOID
gem_server_stop_pid(UWORD pid)
{
	pid_t waited;
	int status;

	if (!pid || (kill((pid_t) pid, SIGTERM) < 0 && errno == ESRCH))
		return;
	do {
		waited = waitpid((pid_t) pid, &status, WNOHANG);
	} while (waited < 0 && errno == EINTR);
	if (!waited) {
		(void) kill((pid_t) pid, SIGKILL);
		do {
			waited = waitpid((pid_t) pid, &status, 0);
		} while (waited < 0 && errno == EINTR);
	}
}

static VOID
gem_server_stop_clients(VOID)
{
	WORD channel;

	for (channel = GEM_CHANNEL_FIRST_APP;
			channel < GEM_CHANNEL_COUNT; channel++)
		gem_server_drop_client(channel);
	if (gem_server_clients[GEM_CHANNEL_DESKTOP].descriptor >= 0)
		gem_server_drop_client(GEM_CHANNEL_DESKTOP);
	else
		gem_server_stop_pid(gem_server_desktop_pid);
	gem_server_desktop_pid = 0;
}

static WORD
gem_server_wait(VOID)
{
	fd_set readers;
	struct timeval timeout;
	int maximum;
	int ready;
	WORD channel;

	FD_ZERO(&readers);
	FD_SET(gem_server_listener, &readers);
	maximum = gem_server_listener;
	for (channel = 0; channel < GEM_CHANNEL_COUNT; channel++) {
		if (!gem_server_channel_valid(channel)
			|| gem_server_clients[channel].descriptor < 0)
			continue;
		FD_SET(gem_server_clients[channel].descriptor, &readers);
		if (gem_server_clients[channel].descriptor > maximum)
			maximum = gem_server_clients[channel].descriptor;
	}
	/* the alarm can go off while drawing or building the fd set, ELKS
	 * only sends it once, so dont sleep forever before handling it
	 * a timeout also covers the gap between checking the flag and select
	 */
	timeout.tv_sec = 0;
	timeout.tv_usec = (gem_server_tick_pending
		|| gem_server_children_changed || gem_server_stop)
		? 0 : GEM_SERVER_TICK_US;
	ready = select(maximum + 1, &readers, NULL, NULL, &timeout);
	if (ready < 0)
		return errno == EINTR;
	if (FD_ISSET(gem_server_listener, &readers)
		&& !gem_server_accept_client())
		return FALSE;
	for (channel = 0; channel < GEM_CHANNEL_COUNT; channel++) {
		if (gem_server_channel_valid(channel)
			&& gem_server_clients[channel].descriptor >= 0
			&& FD_ISSET(gem_server_clients[channel].descriptor,
				&readers)
			&& !gem_server_read_client(channel))
			gem_server_drop_client(channel);
	}
	return TRUE;
}

int
main(void)
{
	WORD failed;

	if (gem_config_load() < 0) {
		gem_server_say("GEM: cannot read " GEM_CONFIG_FILE ", errno ");
		gem_server_say_number((UWORD) errno);
		gem_server_say("\r\n");
		return 1;
	}
	gem_server_stop = FALSE;
	gem_server_children_changed = FALSE;
	gem_server_tick_pending = FALSE;
	gem_server_desktop_pid = 0;
	gem_server_generation_lo = 0;
	gem_server_generation_hi = 0;
	failed = FALSE;
	gem_server_initialize_clients();

	(void) signal(SIGTERM, gem_server_stop_signal);
	(void) signal(SIGINT, gem_server_stop_signal);
	(void) signal(SIGPIPE, SIG_IGN);
	if (signal(SIGCHLD, gem_server_child_signal) == SIG_ERR
		|| signal(SIGALRM, gem_server_tick_signal) == SIG_ERR)
		return 1;
	if (!gem_server_startup())
		return 1;
	if (!gem_server_socket_start()
		|| !gem_server_timer(TRUE)
		|| !gem_exec((LPBYTE) gem_server_desktop, NULL,
			&gem_server_desktop_pid)) {
		(void) gem_server_timer(FALSE);
		gem_server_socket_stop();
		gem_system_resource_free();
		gem_vdi_resident_shutdown();
		return 1;
	}

	while (!gem_server_stop) {
		if (gem_server_tick_pending) {
			gem_server_tick_pending = FALSE;
			if (!gem_server_timer(TRUE)) {
				failed = TRUE;
				break;
			}
			gem_aes_resident_poll(GEM_SERVER_TICK_MS);
		}
		gem_server_reply_ready();
		if (gem_server_children_changed)
			gem_server_reap_children();
		if (gem_server_stop)
			break;
		if (!gem_server_wait()) {
			failed = TRUE;
			break;
		}
	}

	(void) gem_server_timer(FALSE);
	gem_server_stop_clients();
	gem_server_socket_stop();
	gem_system_resource_free();
	gem_vdi_resident_shutdown();
	return failed ? 1 : 0;
}
