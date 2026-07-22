/*
 * gem_shell_resident.c - bounded original GEMSHLIB.C logic for ELKS AES.
 *
 * The primary source is the GPL-released Digital Research GEMSHLIB.C:
 * sh_read(), sh_write(), sh_get(), sh_put(), sh_find(), sh_envrn(),
 * sh_rdef(), and sh_wdef().  The selector switch retains the corresponding
 * GEMSUPER.C cases 120 through 127.
 *
 * ELKS owns address spaces and scheduling.  As in original single-tasking
 * GEM, sh_write() only records the next command and tail; the shell entry
 * point in gem_main.c consumes that record with plain vfork/execv/waitpid
 * after the writer exits.  There is no process table, logical channel, DOS
 * arena, shell wrapper, converted command record, or polling child task.
 *
 * Original fixed byte representations remain unchanged: command and tail are
 * 128 bytes, each PD retains the GEMLIB.H default command/directory fields,
 * and Desktop owns one raw 2,048-byte saved context.  Every target scalar is
 * an eight- or sixteen-bit value.  Loops use pointer increments, and the file
 * contains no multiplication, division, variable shift, recursion, heap use,
 * floating-point operation, or instruction newer than the 8088/8086.
 */

#include "gem_shell_resident.h"

#include <unistd.h>

#if defined(ELKS) && ELKS
#include "gem_resident_memory.h"
#endif

#define GEM_SHELL_OWNER_NONE             0xffffU

typedef struct gem_shell_pd {
	UWORD generation_lo;
	UWORD generation_hi;
	WORD launch_channel;
	WORD do_execute;
	WORD is_gem;
	WORD overlay;
	UBYTE active;
	UBYTE command[GEM_SHELL_COMMAND_BYTES];
	UBYTE tail[GEM_SHELL_TAIL_BYTES];
	UBYTE default_command[GEM_SHELL_DEFAULT_COMMAND_BYTES];
	UBYTE default_directory[GEM_SHELL_DEFAULT_DIR_BYTES];
} GEM_SHELL_PD;

static GEM_SHELL_PD gem_shell_pds[GEM_SHELL_PD_COUNT];
static UBYTE gem_shell_context[GEM_SHELL_CONTEXT_BYTES];
static UWORD gem_shell_context_owner;
static UWORD gem_shell_context_generation_lo;
static UWORD gem_shell_context_generation_hi;

/* Static XIF scratch keeps the 8086 stack small and the dispatcher serial. */
static UBYTE gem_shell_command_scratch[GEM_SHELL_COMMAND_BYTES];
static UBYTE gem_shell_tail_scratch[GEM_SHELL_TAIL_BYTES];
static UBYTE gem_shell_path_scratch[GEM_SHELL_PATH_BYTES];
static UBYTE gem_shell_normal_path[GEM_SHELL_PATH_BYTES];
static UBYTE gem_shell_candidate_path[GEM_SHELL_PATH_BYTES];
static UBYTE
	gem_shell_default_command_scratch[GEM_SHELL_DEFAULT_COMMAND_BYTES];
static UBYTE gem_shell_default_dir_scratch[GEM_SHELL_DEFAULT_DIR_BYTES];
static UBYTE gem_shell_env_scratch[GEM_SHELL_ENV_SEARCH_BYTES];

static const UBYTE gem_shell_initial_command[] = "/bin/gemdesk";
static const UBYTE gem_shell_initial_directory[] = "/GEMAPPS/GEMSYS";
static const UBYTE gem_shell_gemsys_prefix[] = "/GEMAPPS/GEMSYS/";
static const UBYTE gem_shell_apps_prefix[] = "/GEMAPPS/";
static const UBYTE gem_shell_bin_prefix[] = "/bin/";
/* GEM configuration such as DESKTOP.INF lives in the standard ELKS /etc. */
static const UBYTE gem_shell_etc_prefix[] = "/etc/";

/* A half-open byte interval is valid only when subtraction cannot wrap. */
static WORD
gem_shell_range(UWORD offset, UWORD count, UWORD limit)
{
	if (offset > limit)
		return FALSE;
	return count <= (UWORD) (limit - offset);
}

static WORD
gem_shell_client_pointer(const GEM_SHELL_CALL *call,
	GEM_BINDINGS_POINTER_SLOT pointer, UWORD count)
{
	if (!call)
		return FALSE;
	if (!count)
		return TRUE;
	if (pointer.hi != call->client_segment)
		return FALSE;
	return gem_shell_range(pointer.lo, count, call->client_limit);
}

static WORD
gem_shell_client_from(const GEM_SHELL_CALL *call,
	GEM_BINDINGS_POINTER_SLOT source, UBYTE *destination, UWORD count)
{
	if (!destination || !gem_shell_client_pointer(call, source, count))
		return FALSE;
	if (!count)
		return TRUE;
#if defined(ELKS) && ELKS
	gem_resident_memory_from(call->client_segment, source.lo,
		destination, count);
#else
	if (!call->client_memory)
		return FALSE;
	while (count--) {
		*destination++ = call->client_memory[source.lo];
		source.lo++;
	}
#endif
	return TRUE;
}

static WORD
gem_shell_client_to(const GEM_SHELL_CALL *call, const UBYTE *source,
	GEM_BINDINGS_POINTER_SLOT destination, UWORD count)
{
	if (!source || !gem_shell_client_pointer(call, destination, count))
		return FALSE;
	if (!count)
		return TRUE;
#if defined(ELKS) && ELKS
	gem_resident_memory_to(source, call->client_segment,
		destination.lo, count);
#else
	if (!call->client_memory)
		return FALSE;
	while (count--) {
		call->client_memory[destination.lo] = *source++;
		destination.lo++;
	}
#endif
	return TRUE;
}

static VOID
gem_shell_clear_bytes(UBYTE *bytes, UWORD count)
{
	while (count--)
		*bytes++ = 0;
}

/* Copy a NUL string and zero the complete original fixed destination. */
static WORD
gem_shell_fixed_string(UBYTE *destination, UWORD capacity,
	const UBYTE *source)
{
	UWORD left;

	if (!destination || !capacity || !source)
		return FALSE;
	left = capacity;
	while (left && *source) {
		*destination++ = *source++;
		left--;
	}
	if (!left)
		return FALSE;
	*destination++ = 0;
	left--;
	while (left--)
		*destination++ = 0;
	return TRUE;
}

/* Return the byte count including NUL, or zero for an unterminated string. */
static UWORD
gem_shell_string_bytes(const UBYTE *string, UWORD capacity)
{
	UWORD count;

	if (!string)
		return 0;
	count = 1;
	while (capacity) {
		if (!*string)
			return count;
		string++;
		capacity--;
		count++;
	}
	return 0;
}

/*
 * Read one classic unbounded string with an explicit resident ceiling.
 * The largest safe span is copied once with REP MOVSB, then searched locally.
 */
static UWORD
gem_shell_client_string(const GEM_SHELL_CALL *call,
	GEM_BINDINGS_POINTER_SLOT source, UBYTE *destination, UWORD capacity)
{
	UWORD available;

	if (!call || !destination || !capacity
	    || source.hi != call->client_segment
	    || source.lo >= call->client_limit)
		return 0;
	available = (UWORD) (call->client_limit - source.lo);
	if (available > capacity)
		available = capacity;
	if (!gem_shell_client_from(call, source, destination, available))
		return 0;
	return gem_shell_string_bytes(destination, available);
}

static VOID
gem_shell_reset_pd(GEM_SHELL_PD *pd)
{
	pd->generation_lo = 0;
	pd->generation_hi = 0;
	pd->launch_channel = -1;
	pd->do_execute = FALSE;
	pd->is_gem = FALSE;
	pd->overlay = 0;
	pd->active = FALSE;
	gem_shell_clear_bytes(pd->command, GEM_SHELL_COMMAND_BYTES);
	gem_shell_clear_bytes(pd->tail, GEM_SHELL_TAIL_BYTES);
	(void) gem_shell_fixed_string(pd->default_command,
		GEM_SHELL_DEFAULT_COMMAND_BYTES, gem_shell_initial_command);
	(void) gem_shell_fixed_string(pd->default_directory,
		GEM_SHELL_DEFAULT_DIR_BYTES, gem_shell_initial_directory);
}

static GEM_SHELL_PD *
gem_shell_pd_at(UWORD owner)
{
	GEM_SHELL_PD *pd;

	if (owner >= GEM_SHELL_PD_COUNT)
		return (GEM_SHELL_PD *) 0;
	pd = gem_shell_pds;
	while (owner--)
		pd++;
	return pd;
}

/* Bind an unused original PD slot, but never let a stale generation reuse it. */
static GEM_SHELL_PD *
gem_shell_bind_pd(const GEM_SHELL_CALL *call)
{
	GEM_SHELL_PD *pd;

	if (!call)
		return (GEM_SHELL_PD *) 0;
	pd = gem_shell_pd_at(call->owner);
	if (!pd)
		return (GEM_SHELL_PD *) 0;
	if (pd->active) {
		if (pd->generation_lo != call->generation_lo
		    || pd->generation_hi != call->generation_hi)
			return (GEM_SHELL_PD *) 0;
		return pd;
	}
	gem_shell_reset_pd(pd);
	pd->generation_lo = call->generation_lo;
	pd->generation_hi = call->generation_hi;
	pd->active = TRUE;
	return pd;
}

static VOID
gem_shell_reset_context(VOID)
{
	/*
	 * GEMINIT.C places ad_ssave in zero-filled startup storage.  sh_rdinf()
	 * temporarily uses that storage while AES reads DESKTOP.INF and then
	 * explicitly clears it before Desktop starts.  The first SHEL_GET must
	 * therefore return zeros.  Desktop treats a leading byte other than '#'
	 * as the instruction to load the real DESKTOP.INF from its POSIX cwd.
	 * A later SHEL_PUT preserves the original '#' record byte normally.
	 */
	gem_shell_clear_bytes(gem_shell_context, GEM_SHELL_CONTEXT_BYTES);
	gem_shell_context_owner = GEM_SHELL_OWNER_NONE;
	gem_shell_context_generation_lo = 0;
	gem_shell_context_generation_hi = 0;
}

static WORD
gem_shell_bind_context(const GEM_SHELL_CALL *call)
{
	if (!call || call->owner != GEM_SHELL_DESKTOP_OWNER)
		return FALSE;
	if (gem_shell_context_owner == GEM_SHELL_OWNER_NONE) {
		gem_shell_context_owner = call->owner;
		gem_shell_context_generation_lo = call->generation_lo;
		gem_shell_context_generation_hi = call->generation_hi;
		return TRUE;
	}
	return gem_shell_context_owner == call->owner
		&& gem_shell_context_generation_lo == call->generation_lo
		&& gem_shell_context_generation_hi == call->generation_hi;
}

VOID
gem_shell_resident_reset(VOID)
{
	GEM_SHELL_PD *pd;
	UWORD count;

	pd = gem_shell_pds;
	count = GEM_SHELL_PD_COUNT;
	while (count--) {
		gem_shell_reset_pd(pd);
		pd++;
	}
	gem_shell_reset_context();
#if defined(ELKS) && ELKS
#endif
}

/* Copy prefix and suffix into one bounded native path without strcat(). */
static WORD
gem_shell_join(UBYTE *destination, const UBYTE *prefix,
	const UBYTE *suffix)
{
	UWORD left;

	left = GEM_SHELL_PATH_BYTES;
	while (*prefix) {
		if (left <= 1U)
			return FALSE;
		*destination++ = *prefix++;
		left--;
	}
	while (*suffix) {
		if (left <= 1U)
			return FALSE;
		*destination++ = *suffix++;
		left--;
	}
	*destination = 0;
	return TRUE;
}

/*
 * Map only GEM's path spelling to ELKS native spelling.  Drive letters are
 * discarded because the staged GEM tree is rooted at /GEMAPPS.  Backslashes
 * become slashes byte-for-byte; there is no case folding or host conversion.
 */
static WORD
gem_shell_normalize(const UBYTE *source, UBYTE *destination)
{
	UWORD left;
	UBYTE character;

	if (!source || !*source || !destination)
		return FALSE;
	left = GEM_SHELL_PATH_BYTES;
	if (source[0] && source[1] == (UBYTE) ':') {
		source += 2;
		if (*source == (UBYTE) '/' || *source == (UBYTE) '\\')
			source++;
		*destination++ = (UBYTE) '/';
		left--;
	}
	while (*source) {
		if (left <= 1U)
			return FALSE;
		character = *source++;
		if (character == (UBYTE) '\\')
			character = (UBYTE) '/';
		*destination++ = character;
		left--;
	}
	*destination = 0;
	return TRUE;
}

static WORD
gem_shell_path_exists(const UBYTE *path)
{
	return path && *path && access((const char *) path, F_OK) == 0;
}

static WORD
gem_shell_copy_path(UBYTE *destination, const UBYTE *source)
{
	if (destination == source)
		return TRUE;
	return gem_shell_fixed_string(destination, GEM_SHELL_PATH_BYTES, source);
}

/*
 * Original sh_find() searched current directory and PATH=.  The resident AES
 * has a different ELKS cwd from Desktop, so every relative lookup is rooted
 * explicitly at Desktop's /GEMAPPS/GEMSYS cwd, then /GEMAPPS, then /bin.
 */
static WORD
gem_shell_find_native(const UBYTE *input, UBYTE *found)
{
	if (!gem_shell_normalize(input, gem_shell_normal_path))
		return FALSE;
	if (gem_shell_normal_path[0] == (UBYTE) '/') {
		if (!gem_shell_path_exists(gem_shell_normal_path))
			return FALSE;
		return gem_shell_copy_path(found, gem_shell_normal_path);
	}
	/* A drive-less original path such as GEMAPPS/APP.APP is root-relative. */
	if (gem_shell_normal_path[0] == (UBYTE) 'G'
	    && gem_shell_normal_path[1] == (UBYTE) 'E'
	    && gem_shell_normal_path[2] == (UBYTE) 'M'
	    && gem_shell_normal_path[3] == (UBYTE) 'A'
	    && gem_shell_normal_path[4] == (UBYTE) 'P'
	    && gem_shell_normal_path[5] == (UBYTE) 'P'
	    && gem_shell_normal_path[6] == (UBYTE) 'S'
	    && gem_shell_normal_path[7] == (UBYTE) '/') {
		if (!gem_shell_join(gem_shell_candidate_path,
			(const UBYTE *) "/", gem_shell_normal_path))
			return FALSE;
		if (!gem_shell_path_exists(gem_shell_candidate_path))
			return FALSE;
		return gem_shell_copy_path(found, gem_shell_candidate_path);
	}
	/*
	 * Configuration files live in the standard ELKS /etc directory, so it
	 * is searched first: DESKTOP.INF resolves to /etc/DESKTOP.INF for the
	 * initial read and for every Save Desktop rewrite.
	 */
	if (!gem_shell_join(gem_shell_candidate_path,
		gem_shell_etc_prefix, gem_shell_normal_path))
		return FALSE;
	if (gem_shell_path_exists(gem_shell_candidate_path))
		return gem_shell_copy_path(found, gem_shell_candidate_path);
	if (!gem_shell_join(gem_shell_candidate_path,
		gem_shell_gemsys_prefix, gem_shell_normal_path))
		return FALSE;
	if (gem_shell_path_exists(gem_shell_candidate_path))
		return gem_shell_copy_path(found, gem_shell_candidate_path);
	if (!gem_shell_join(gem_shell_candidate_path,
		gem_shell_apps_prefix, gem_shell_normal_path))
		return FALSE;
	if (gem_shell_path_exists(gem_shell_candidate_path))
		return gem_shell_copy_path(found, gem_shell_candidate_path);
	if (!gem_shell_join(gem_shell_candidate_path,
		gem_shell_bin_prefix, gem_shell_normal_path))
		return FALSE;
	if (!gem_shell_path_exists(gem_shell_candidate_path))
		return FALSE;
	return gem_shell_copy_path(found, gem_shell_candidate_path);
}

static WORD
gem_shell_call_shape(const GEM_SHELL_CALL *call, UWORD input_words,
	UWORD output_words, UWORD addresses)
{
	if (!call || !call->control || !call->int_out)
		return FALSE;
	if (call->control[1] < input_words
	    || call->control[2] < output_words
	    || call->control[3] < addresses)
		return FALSE;
	if (input_words && !call->int_in)
		return FALSE;
	if (addresses && !call->addr_in)
		return FALSE;
	return TRUE;
}

static WORD
gem_shell_finish(const GEM_SHELL_CALL *call, WORD result)
{
	call->int_out[0] = (UWORD) result;
	return result;
}

static WORD
gem_shell_read(const GEM_SHELL_CALL *call, GEM_SHELL_PD *pd)
{
	if (!gem_shell_call_shape(call, 0U, 1U, 2U)
	    || !gem_shell_client_pointer(call, call->addr_in[0],
		GEM_SHELL_COMMAND_BYTES)
	    || !gem_shell_client_pointer(call, call->addr_in[1],
		GEM_SHELL_TAIL_BYTES))
		return FALSE;
	if (!gem_shell_client_to(call, pd->command, call->addr_in[0],
		GEM_SHELL_COMMAND_BYTES)
	    || !gem_shell_client_to(call, pd->tail, call->addr_in[1],
		GEM_SHELL_TAIL_BYTES))
		return FALSE;
	return gem_shell_finish(call, TRUE);
}

/*
 * Original single-tasking GEM: SHEL_WRITE only records the next command.
 * The requesting application then exits its event loop, and the shell entry
 * point runs the recorded program with plain vfork/execv/waitpid before
 * restarting the Desktop.  No process table, logical channel, DOS arena
 * record, or launch bookkeeping exists; ELKS owns the child completely.
 * The record is retained here, outside the PD slots, because the writer's
 * PD is detached by its own APPL_EXIT before the command is consumed.
 */
static UBYTE gem_shell_pending;
static UBYTE gem_shell_pending_command[GEM_SHELL_COMMAND_BYTES];
static UBYTE gem_shell_pending_tail[GEM_SHELL_TAIL_BYTES];

static WORD
gem_shell_launch(GEM_SHELL_PD *pd)
{
	UBYTE *destination;
	const UBYTE *source;
	UWORD count;

	destination = gem_shell_pending_command;
	source = pd->command;
	count = GEM_SHELL_COMMAND_BYTES;
	while (count--)
		*destination++ = *source++;
	destination = gem_shell_pending_tail;
	source = pd->tail;
	count = GEM_SHELL_TAIL_BYTES;
	while (count--)
		*destination++ = *source++;
	gem_shell_pending = TRUE;
	pd->launch_channel = -1;
	return TRUE;
}

WORD
gem_shell_resident_take_command(UBYTE *command, UWORD command_bytes,
	UBYTE *tail, UWORD tail_bytes)
{
	UWORD count;
	const UBYTE *source;
	UBYTE *destination;

	if (!gem_shell_pending || !command || !tail
	    || command_bytes < GEM_SHELL_COMMAND_BYTES
	    || tail_bytes < GEM_SHELL_TAIL_BYTES)
		return FALSE;
	source = gem_shell_pending_command;
	destination = command;
	count = GEM_SHELL_COMMAND_BYTES;
	while (count--)
		*destination++ = *source++;
	source = gem_shell_pending_tail;
	destination = tail;
	count = GEM_SHELL_TAIL_BYTES;
	while (count--)
		*destination++ = *source++;
	gem_shell_pending = FALSE;
	return TRUE;
}

static WORD
gem_shell_write(const GEM_SHELL_CALL *call, GEM_SHELL_PD *pd)
{
	UWORD command_bytes;

	if (!gem_shell_call_shape(call, 3U, 1U, 2U)
	    || !gem_shell_client_from(call, call->addr_in[0],
		gem_shell_command_scratch, GEM_SHELL_COMMAND_BYTES)
	    || !gem_shell_client_from(call, call->addr_in[1],
		gem_shell_tail_scratch, GEM_SHELL_TAIL_BYTES))
		return FALSE;
	command_bytes = gem_shell_string_bytes(gem_shell_command_scratch,
		GEM_SHELL_COMMAND_BYTES);
	if (!command_bytes || gem_shell_tail_scratch[0] > 127U)
		return FALSE;

	pd->do_execute = (WORD) call->int_in[0];
	pd->is_gem = (WORD) (call->int_in[1] != 0);
	pd->overlay = (WORD) call->int_in[2];
	if (!pd->do_execute) {
		if (!gem_shell_fixed_string(pd->command,
			GEM_SHELL_COMMAND_BYTES, gem_shell_command_scratch))
			return FALSE;
	} else {
		if (!gem_shell_find_native(gem_shell_command_scratch,
			gem_shell_candidate_path)
		    || !gem_shell_fixed_string(pd->command,
			GEM_SHELL_COMMAND_BYTES, gem_shell_candidate_path))
			return FALSE;
	}
	{
		UBYTE *destination;
		const UBYTE *source;
		UWORD count;

		destination = pd->tail;
		source = gem_shell_tail_scratch;
		count = GEM_SHELL_TAIL_BYTES;
		while (count--)
			*destination++ = *source++;
	}
	if (pd->do_execute && !gem_shell_launch(pd))
		return gem_shell_finish(call, FALSE);
	return gem_shell_finish(call, TRUE);
}

static WORD
gem_shell_get(const GEM_SHELL_CALL *call)
{
	UWORD length;

	if (!gem_shell_call_shape(call, 1U, 1U, 1U))
		return FALSE;
	length = call->int_in[0];
	if (length > GEM_SHELL_CONTEXT_BYTES
	    || !gem_shell_client_pointer(call, call->addr_in[0], length)
	    || !gem_shell_bind_context(call)
	    || !gem_shell_client_to(call, gem_shell_context,
		call->addr_in[0], length))
		return FALSE;
	return gem_shell_finish(call, TRUE);
}

static WORD
gem_shell_put(const GEM_SHELL_CALL *call)
{
	UWORD length;

	if (!gem_shell_call_shape(call, 1U, 1U, 1U))
		return FALSE;
	length = call->int_in[0];
	if (length > GEM_SHELL_CONTEXT_BYTES
	    || !gem_shell_client_pointer(call, call->addr_in[0], length)
	    || !gem_shell_bind_context(call)
	    || !gem_shell_client_from(call, call->addr_in[0],
		gem_shell_context, length))
		return FALSE;
	return gem_shell_finish(call, TRUE);
}

static WORD
gem_shell_find(const GEM_SHELL_CALL *call)
{
	UWORD bytes;

	if (!gem_shell_call_shape(call, 0U, 1U, 1U))
		return FALSE;
	if (!gem_shell_client_string(call, call->addr_in[0],
		gem_shell_path_scratch, GEM_SHELL_PATH_BYTES)
	    || !gem_shell_find_native(gem_shell_path_scratch,
		gem_shell_candidate_path))
		return gem_shell_finish(call, FALSE);
	bytes = gem_shell_string_bytes(gem_shell_candidate_path,
		GEM_SHELL_PATH_BYTES);
	if (!bytes || !gem_shell_client_to(call, gem_shell_candidate_path,
		call->addr_in[0], bytes))
		return gem_shell_finish(call, FALSE);
	return gem_shell_finish(call, TRUE);
}

static WORD
gem_shell_envrn(const GEM_SHELL_CALL *call)
{
	GEM_BINDINGS_POINTER_SLOT no_value;

	if (!gem_shell_call_shape(call, 0U, 1U, 2U)
	    || !gem_shell_client_pointer(call, call->addr_in[0], 4U)
	    || !gem_shell_client_string(call, call->addr_in[1],
		gem_shell_env_scratch, GEM_SHELL_ENV_SEARCH_BYTES))
		return FALSE;

	/*
	 * GEMSHLIB.C returned an owner-environment pointer.  That address cannot
	 * name bytes in a separated ELKS client DS.  With no caller-provided value
	 * buffer in selector 125, null is the only safe original far-pointer value.
	 * Report FALSE after clearing the caller's pointer: returning TRUE with a
	 * null address made the old DOS-only service look implemented when no
	 * usable POSIX value had crossed the process boundary.  Desktop invokes
	 * /bin/sh directly on ELKS, so no feature depends on this unsafe pointer.
	 */
	no_value.lo = 0;
	no_value.hi = 0;
	if (!gem_shell_client_to(call, (const UBYTE *) &no_value,
		call->addr_in[0], 4U))
		return FALSE;
	return gem_shell_finish(call, FALSE);
}

static WORD
gem_shell_rdef(const GEM_SHELL_CALL *call, GEM_SHELL_PD *pd)
{
	UWORD command_bytes;
	UWORD directory_bytes;

	if (!gem_shell_call_shape(call, 0U, 1U, 2U))
		return FALSE;
	command_bytes = gem_shell_string_bytes(pd->default_command,
		GEM_SHELL_DEFAULT_COMMAND_BYTES);
	directory_bytes = gem_shell_string_bytes(pd->default_directory,
		GEM_SHELL_DEFAULT_DIR_BYTES);
	if (!command_bytes || !directory_bytes
	    || !gem_shell_client_pointer(call, call->addr_in[0], command_bytes)
	    || !gem_shell_client_pointer(call, call->addr_in[1], directory_bytes)
	    || !gem_shell_client_to(call, pd->default_command,
		call->addr_in[0], command_bytes)
	    || !gem_shell_client_to(call, pd->default_directory,
		call->addr_in[1], directory_bytes))
		return FALSE;
	return gem_shell_finish(call, TRUE);
}

static WORD
gem_shell_wdef(const GEM_SHELL_CALL *call, GEM_SHELL_PD *pd)
{
	if (!gem_shell_call_shape(call, 0U, 1U, 2U)
	    || !gem_shell_client_string(call, call->addr_in[0],
		gem_shell_default_command_scratch,
		GEM_SHELL_DEFAULT_COMMAND_BYTES)
	    || !gem_shell_client_string(call, call->addr_in[1],
		gem_shell_default_dir_scratch,
		GEM_SHELL_DEFAULT_DIR_BYTES))
		return FALSE;
	if (!gem_shell_fixed_string(pd->default_command,
		GEM_SHELL_DEFAULT_COMMAND_BYTES,
		gem_shell_default_command_scratch)
	    || !gem_shell_fixed_string(pd->default_directory,
		GEM_SHELL_DEFAULT_DIR_BYTES, gem_shell_default_dir_scratch))
		return FALSE;
	return gem_shell_finish(call, TRUE);
}

static WORD
gem_shell_recognized(UWORD opcode)
{
	return opcode >= GEM_SHELL_READ && opcode <= GEM_SHELL_WDEF;
}

WORD
gem_shell_resident_dispatch(const GEM_SHELL_CALL *call, WORD *handled)
{
	GEM_SHELL_PD *pd;
	UWORD opcode;
	WORD result;

	if (!handled)
		return FALSE;
	*handled = FALSE;
	if (!call || !call->control)
		return FALSE;
	opcode = call->control[0];
	if (!gem_shell_recognized(opcode))
		return FALSE;
	*handled = TRUE;
	if (!call->int_out || call->control[2] < 1U)
		return FALSE;
	call->int_out[0] = FALSE;
	pd = gem_shell_bind_pd(call);
	if (!pd)
		return FALSE;

	switch (opcode) {
	case GEM_SHELL_READ:
		result = gem_shell_read(call, pd);
		break;
	case GEM_SHELL_WRITE:
		result = gem_shell_write(call, pd);
		break;
	case GEM_SHELL_GET:
		result = gem_shell_get(call);
		break;
	case GEM_SHELL_PUT:
		result = gem_shell_put(call);
		break;
	case GEM_SHELL_FIND:
		result = gem_shell_find(call);
		break;
	case GEM_SHELL_ENVRN:
		result = gem_shell_envrn(call);
		break;
	case GEM_SHELL_RDEF:
		result = gem_shell_rdef(call, pd);
		break;
	case GEM_SHELL_WDEF:
		result = gem_shell_wdef(call, pd);
		break;
	default:
		result = FALSE;
		break;
	}
	if (!result)
		call->int_out[0] = FALSE;
	return result;
}

VOID
gem_shell_resident_detach(UWORD owner, UWORD generation_lo,
	UWORD generation_hi)
{
	GEM_SHELL_PD *pd;

	pd = gem_shell_pd_at(owner);
	if (pd && pd->active && pd->generation_lo == generation_lo
	    && pd->generation_hi == generation_hi)
		gem_shell_reset_pd(pd);
	if (gem_shell_context_owner == owner
	    && gem_shell_context_generation_lo == generation_lo
	    && gem_shell_context_generation_hi == generation_hi)
		gem_shell_reset_context();
}
