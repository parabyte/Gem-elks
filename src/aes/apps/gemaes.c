/*
 * gemaes.c - sole userspace owner of the ELKS original GEM INT EF broker.
 *
 * This executable is the sole physical VDI owner as well as the resident AES
 * message/process owner.  Classic VDI requests retain their CX=0473h DS:DX
 * parameter block and are drawn by the shared native PC driver.  Per-PD RSC
 * ownership is resident here as well.  Graphical AES menu, object, form,
 * window, and event closure remains a separate porting boundary.
 */

#include <errno.h>
#include <signal.h>
#include <unistd.h>

#include "gem_aes_resident.h"
#include "gem_proc.h"
#include "gem_vdi_resident.h"

#define GEM_AES_EVENT_TICK_MS          20U
#define GEM_AES_EVENT_TICK_US          20000U
#define GEM_AES_ITIMER_REAL            0

/*
 * ELKS setitimer uses two historical four-byte timeval fields.  Keep that
 * external ABI as explicit little-endian 16-bit halves so the resident 8086
 * owner never introduces a C long or a wide arithmetic helper.  Both second
 * halves are zero; microseconds are exactly 20000, with no rounding.
 */
typedef struct __attribute__((packed)) gem_aes_itimer_words {
	UWORD interval_seconds_lo;
	UWORD interval_seconds_hi;
	UWORD interval_microseconds_lo;
	UWORD interval_microseconds_hi;
	UWORD value_seconds_lo;
	UWORD value_seconds_hi;
	UWORD value_microseconds_lo;
	UWORD value_microseconds_hi;
} GEM_AES_ITIMER_WORDS;

typedef BYTE GEM_AES_ITIMER_MUST_BE_16_BYTES
	[(sizeof(GEM_AES_ITIMER_WORDS) == 16) ? 1 : -1];

/* The library entry accepts the exact sixteen-byte kernel ABI above. */
extern int setitimer(int which, const GEM_AES_ITIMER_WORDS *value,
	GEM_AES_ITIMER_WORDS *old_value);

static volatile WORD gem_aes_stop;
static volatile WORD gem_aes_children_changed;
static volatile WORD gem_aes_tick_pending;
static volatile WORD gem_aes_timer_failed;

static WORD gem_aes_timer(WORD enable);

static VOID
gem_aes_signal(WORD signal_number)
{
	(void) signal_number;
	gem_aes_stop = TRUE;
}

/*
 * ELKS ignores SIGCHLD by default, so a resident owner blocked in
 * GEMCTL_NEXT would otherwise never run the process-table reaper after a GEM
 * application calls APPL_EXIT and terminates normally.  ELKS signal handlers
 * are one-shot: re-arm this tiny handler immediately, set one word, and leave
 * all waitpid and process-table work to ordinary process context.
 */
static VOID
gem_aes_child_signal(WORD signal_number)
{
	(void) signal_number;
	gem_aes_children_changed = TRUE;
	(void) signal(SIGCHLD, gem_aes_child_signal);
}

/*
 * ELKS handlers and setitimer alarms are one-shot.  Re-arm both immediately,
 * then set one resident word; input, video, queues, and timer subtraction
 * remain in normal process context.  Re-arming the alarm here is essential:
 * menu and window painting can take longer than one tick on a real 8088.  If
 * the alarm expired during that painting and normal context entered the
 * blocking GEMCTL_NEXT call, there would otherwise be no live alarm left to
 * wake the owner for the next mouse packet.
 *
 * setitimer is a direct ELKS system-call boundary and uses the fixed sixteen-
 * byte word-pair record built by gem_aes_timer().  It allocates no memory and
 * performs no wide arithmetic.  A failure is retained in one signal-safe word
 * and handled by normal process context.  errno is saved and restored so an
 * alarm cannot alter the interrupted owner's syscall result.
 */
static VOID
gem_aes_tick_signal(WORD signal_number)
{
	WORD saved_errno;

	(void) signal_number;
	saved_errno = (WORD) errno;
	gem_aes_tick_pending = TRUE;
	(void) signal(SIGALRM, gem_aes_tick_signal);
	if (!gem_aes_timer(TRUE))
		gem_aes_timer_failed = TRUE;
	errno = saved_errno;
}

static WORD
gem_aes_timer(WORD enable)
{
	GEM_AES_ITIMER_WORDS timer;

	timer.interval_seconds_lo = 0;
	timer.interval_seconds_hi = 0;
	timer.interval_microseconds_lo = enable ? GEM_AES_EVENT_TICK_US : 0;
	timer.interval_microseconds_hi = 0;
	timer.value_seconds_lo = 0;
	timer.value_seconds_hi = 0;
	timer.value_microseconds_lo = enable ? GEM_AES_EVENT_TICK_US : 0;
	timer.value_microseconds_hi = 0;
	return setitimer(GEM_AES_ITIMER_REAL, &timer, NULL) == 0;
}

/*
 * Drain every completed child with gem_proc_poll(), which uses only an exact
 * positive 16-bit PID and waitpid(WNOHANG).  Clear the flag before polling so
 * a second SIGCHLD delivered during the scan causes another complete bounded
 * scan instead of being lost.  No process-table code runs in the handler.
 */
static VOID
gem_aes_reap_children(VOID)
{
	do {
		gem_aes_children_changed = FALSE;
		while (gem_proc_poll(NULL, NULL))
			;
	} while (gem_aes_children_changed);
}

static WORD
gem_aes_reply(struct gemtrap_request *request)
{
	if (gemctl(GEMCTL_REPLY, request) != 0 && errno != ESRCH)
		return FALSE;
	return TRUE;
}

/*
 * APPL_WRITE can make a previously delivered APPL_READ ready, and a read can
 * in turn free space for a waiting writer.  Drain the bounded original-order
 * completion list before sleeping in NEXT again.  Each reply wakes the real
 * ELKS task which issued that exact generation of INT EF.
 */
static WORD
gem_aes_reply_ready(VOID)
{
	struct gemtrap_request ready;

	while (gem_aes_resident_ready(&ready)) {
		if (!gem_aes_reply(&ready))
			return FALSE;
	}
	return TRUE;
}

int
main(void)
{
	struct gemtrap_request request;
	WORD result;

	gem_aes_stop = FALSE;
	gem_aes_children_changed = FALSE;
	gem_aes_tick_pending = FALSE;
	gem_aes_timer_failed = FALSE;
	(void) signal(SIGTERM, gem_aes_signal);
	(void) signal(SIGINT, gem_aes_signal);
	if (signal(SIGCHLD, gem_aes_child_signal) == SIG_ERR)
		return 1;
	if (signal(SIGALRM, gem_aes_tick_signal) == SIG_ERR)
		return 1;
	/*
	 * Original GEM ran AES and Desktop with GEMSYS as their shared current
	 * directory.  ELKS gives each task a private POSIX cwd, so establish the
	 * resident owner's directory before the first relative RSRC_LOAD or
	 * SHEL_FIND request.  This is a single process-local chdir; filenames and
	 * original RSC/ICN bytes cross no translation or helper process.
	 */
	if (chdir("/GEMAPPS/GEMSYS") != 0)
		return 1;
	/*
	 * Classic GEM opens physical GSX before accepting AES calls.  Doing the
	 * same here makes GRAF_HANDLE and RSRC_LOAD geometry available to the
	 * first client while retaining the console lock in this one ELKS task.
	 */
	if (!gem_vdi_resident_startup())
		return 1;
	if (gemctl(GEMCTL_REGISTER, NULL) != 0) {
		gem_vdi_resident_shutdown();
		return 1;
	}
	if (!gem_aes_timer(TRUE)) {
		gem_vdi_resident_shutdown();
		(void) gemctl(GEMCTL_UNREGISTER, NULL);
		return 1;
	}

	while (!gem_aes_stop) {
		if (gem_aes_timer_failed) {
			gem_vdi_resident_shutdown();
			(void) gemctl(GEMCTL_UNREGISTER, NULL);
			return 1;
		}
		if (gem_aes_tick_pending) {
			gem_aes_tick_pending = FALSE;
			gem_aes_resident_poll(GEM_AES_EVENT_TICK_MS);
		}
		if (!gem_aes_reply_ready()) {
			(void) gem_aes_timer(FALSE);
			gem_vdi_resident_shutdown();
			(void) gemctl(GEMCTL_UNREGISTER, NULL);
			return 1;
		}
		if (gem_aes_children_changed)
			gem_aes_reap_children();
		if (gemctl(GEMCTL_NEXT, &request) != 0) {
			if (errno == EINTR)
				continue;
			gem_vdi_resident_shutdown();
			(void) gemctl(GEMCTL_UNREGISTER, NULL);
			return 1;
		}

		if (request.cx == GEMTRAP_CX_EXIT) {
			/* AX is the original PD tag carried by the synthetic EXIT. */
			if (request.ax < GEM_PROC_CHANNELS)
				gem_vdi_resident_release((WORD) request.ax);
			if (!gem_aes_resident_exit(&request)) {
				gem_vdi_resident_shutdown();
				(void) gemctl(GEMCTL_UNREGISTER, NULL);
				return 1;
			}
			if (!gem_aes_reply_ready()) {
				gem_vdi_resident_shutdown();
				(void) gemctl(GEMCTL_UNREGISTER, NULL);
				return 1;
			}
			continue;
		}

		if (request.cx == GEM_VDI_RESIDENT_SELECTOR) {
			result = gem_vdi_resident_request(&request,
				gem_aes_resident_application(&request));
		} else {
			result = gem_aes_resident_request(&request);
			/*
			 * A new AES wait can consume the current mouse state without
			 * another tick.  A VDI drawing request cannot install such a wait,
			 * so polling after every line, fill, glyph, or raster operation only
			 * duplicated device system calls and resident process scans.  The
			 * fixed periodic tick remains the sole input path during long draws.
			 */
			gem_aes_resident_poll(0);
		}
		if (result != GEM_AES_RESIDENT_DEFERRED) {
			request.ax = (UWORD) result;
			if (!gem_aes_reply(&request)) {
				gem_vdi_resident_shutdown();
				(void) gemctl(GEMCTL_UNREGISTER, NULL);
				return 1;
			}
		}
		if (!gem_aes_reply_ready()) {
			gem_vdi_resident_shutdown();
			(void) gemctl(GEMCTL_UNREGISTER, NULL);
			return 1;
		}
	}

	(void) gem_aes_timer(FALSE);
	gem_vdi_resident_shutdown();
	if (gemctl(GEMCTL_UNREGISTER, NULL) != 0)
		return 1;
	return 0;
}
