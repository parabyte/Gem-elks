/*
 * gem_shell_resident.c - GEM shell calls for ELKS AES
 *
 * the SHEL_* shell calls (read/write/get/put/find/rdef/wdef), SHEL_WRITE
 * just records the next command and tail, gem_main.c runs it with
 * vfork/execv/waitpid once the writer exits
 */

#include "gem_shell_resident.h"
#include "gem_aes_call.h"
#include "gem_system_resource.h"

#include <unistd.h>

#include "gem_resident_memory.h"

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

static UBYTE gem_shell_command_scratch[GEM_SHELL_COMMAND_BYTES];
static UBYTE gem_shell_tail_scratch[GEM_SHELL_TAIL_BYTES];
static UBYTE gem_shell_path_scratch[GEM_SHELL_PATH_BYTES];
static UBYTE gem_shell_normal_path[GEM_SHELL_PATH_BYTES];
static UBYTE gem_shell_candidate_path[GEM_SHELL_PATH_BYTES];
static UBYTE gem_shell_prefix_path[GEM_SHELL_PATH_BYTES];
static UBYTE
 gem_shell_default_command_scratch[GEM_SHELL_DEFAULT_COMMAND_BYTES];
static UBYTE gem_shell_default_dir_scratch[GEM_SHELL_DEFAULT_DIR_BYTES];
static UBYTE gem_shell_env_scratch[GEM_SHELL_ENV_SEARCH_BYTES];

static const UBYTE gem_shell_initial_command[] = "/bin/gemdesk";
/*
 * ELKS puts the GEM binaries in /bin like everything else, so /bin is
 * searched after the resource list, the rest comes from GEM.RSC STINPATH
 */
static const UBYTE gem_shell_bin_prefix[] = "/bin/";

/*
 * STINPATH, GEM.RSC semicolon list, normalised to ELKS spelling once,
 * this is the program search path
 */
static UBYTE gem_shell_search_path[GEM_SHELL_PATH_BYTES];
static UBYTE gem_shell_search_ready;

static VOID gem_shell_remap_root(UBYTE *path);

static VOID
gem_shell_load_search_path(VOID)
{
	UWORD index;

	if (gem_shell_search_ready)
		return;
	gem_shell_search_ready = TRUE;
	if (gem_system_resource_string(GEM_SYSTEM_STINPATH,
			(BYTE *) gem_shell_search_path,
			GEM_SHELL_PATH_BYTES) < 0) {
		gem_shell_search_path[0] = 0;
		return;
	}
	/* drop drive letters, flip backslashes, keep the list */
	for (index = 0; gem_shell_search_path[index]; index++)
		if (gem_shell_search_path[index] == (UBYTE) 0x5c)
			gem_shell_search_path[index] = (UBYTE) '/';
}

/*
 * copy the element'th search entry into destination with a trailing slash
 * returns FALSE past the last one
 */
static WORD
gem_shell_search_element(UWORD element, UBYTE *destination, UWORD size)
{
	UWORD index;
	UWORD out;

	gem_shell_load_search_path();
	index = 0;
	while (element) {
		if (!gem_shell_search_path[index])
			return FALSE;
		if (gem_shell_search_path[index] == (UBYTE) ';')
			element--;
		index++;
	}
	if (!gem_shell_search_path[index])
		return FALSE;
	/* drop a leading drive letter, keep the root slash */
	if (gem_shell_search_path[index]
		&& gem_shell_search_path[index + 1U] == (UBYTE) ':')
		index += 2U;
	out = 0;
	while (gem_shell_search_path[index]
		&& gem_shell_search_path[index] != (UBYTE) ';') {
		if (out + 2U >= size)
			return FALSE;
		destination[out++] = gem_shell_search_path[index++];
	}
	if (!out)
		return FALSE;
	if (destination[out - 1U] != (UBYTE) '/')
		destination[out++] = (UBYTE) '/';
	destination[out] = 0;
	gem_shell_remap_root(destination);
	return TRUE;
}

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
	gem_resident_memory_from(call->client_segment, source.lo,
		destination, count);
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
	gem_resident_memory_to(source, call->client_segment,
		destination.lo, count);
	return TRUE;
}

static VOID
gem_shell_clear_bytes(UBYTE *bytes, UWORD count)
{
	while (count--)
		*bytes++ = 0;
}

/* copy a NUL string, zero the rest of the fixed destination */
static WORD
gem_shell_fixed_string(UBYTE *destination, UWORD capacity, const UBYTE *source)
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

/* byte count including the NUL, zero if it never ends */
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

/* read one unbounded string with a hard resident ceiling */
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
	/* start dir is the first STINPATH element */
	if (gem_shell_search_element(0, gem_shell_candidate_path,
			GEM_SHELL_PATH_BYTES))
		(void) gem_shell_fixed_string(pd->default_directory,
			GEM_SHELL_DEFAULT_DIR_BYTES, gem_shell_candidate_path);
	else
		pd->default_directory[0] = 0;
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

/* bind a PD slot, a stale generation wont reuse one */
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
	 * first SHEL_GET returns zeros, the Desktop reads a leading byte thats
	 * not '#' as load the real DESKTOP.INF
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
}

/* join prefix and suffix into one bounded native path */
static WORD
gem_shell_join(UBYTE *destination, const UBYTE *prefix, const UBYTE *suffix)
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
 * GEM roots its tree at GEMAPPS, ELKS keeps it under /lib, so a leading
 * /GEMAPPS (any case) becomes /lib in place, e.g. /GEMAPPS/GEMSYS is
 * /lib/gemsys
 */
static VOID
gem_shell_remap_root(UBYTE *path)
{
	UWORD i;
	static const BYTE root[] = "gemapps";

	if (path[0] != (UBYTE) '/')
		return;
	for (i = 0; i < 7U; i++)
		if ((path[1 + i] | 0x20) != (UBYTE) root[i])
			return;
	if (path[8] != (UBYTE) '/' && path[8] != 0)
		return;
	/* shift the tail from /GEMAPPS(8) down to /lib(4), then write /lib */
	i = 0;
	while (path[8 + i]) {
		path[4 + i] = path[8 + i];
		i++;
	}
	path[4 + i] = 0;
	path[0] = (UBYTE) '/';
	path[1] = (UBYTE) 'l';
	path[2] = (UBYTE) 'i';
	path[3] = (UBYTE) 'b';
}

/*
 * GEM path to ELKS path, drop drive letters (tree is rooted at /lib/gemsys)
 * and turn backslashes into slashes
 */
static WORD
gem_shell_normalize(const UBYTE *source, UBYTE *destination)
{
	UBYTE *start = destination;
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
	gem_shell_remap_root(start);
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
	return gem_shell_fixed_string(destination, GEM_SHELL_PATH_BYTES,
		source);
}

/*
 * the resident AES has a different ELKS cwd from Desktop, so a relative
 * name is looked up against the resource path list, then /bin
 */
static WORD
gem_shell_find_native(const UBYTE *input, UBYTE *found)
{
	UWORD element;

	if (!gem_shell_normalize(input, gem_shell_normal_path))
		return FALSE;
	if (gem_shell_normal_path[0] == (UBYTE) '/') {
		if (!gem_shell_path_exists(gem_shell_normal_path))
			return FALSE;
		return gem_shell_copy_path(found, gem_shell_normal_path);
	}
	/* a drive-less path like GEMAPPS/APP.APP counts from root */
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
		gem_shell_remap_root(gem_shell_candidate_path);
		if (!gem_shell_path_exists(gem_shell_candidate_path))
			return FALSE;
		return gem_shell_copy_path(found, gem_shell_candidate_path);
	}
	element = 0;
	while (gem_shell_search_element(element, gem_shell_prefix_path,
			GEM_SHELL_PATH_BYTES)) {
		if (gem_shell_join(gem_shell_candidate_path,
				gem_shell_prefix_path, gem_shell_normal_path)
			&& gem_shell_path_exists(gem_shell_candidate_path))
			return gem_shell_copy_path(found,
				gem_shell_candidate_path);
		element++;
	}
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
	/* every SHEL_* selector answers, so int_out is always needed */
	if (!call || !call->int_out)
		return FALSE;
	return gem_aes_call_counts(call->control, input_words, output_words,
		addresses)
		&& gem_aes_call_arrays(call->int_in, input_words,
		call->int_out, output_words, call->addr_in, addresses);
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
 * pending record lives outside the PD slots, the writer's PD is gone
 * (APPL_EXIT) before the command gets used
 */
static UBYTE gem_shell_pending;
static UBYTE gem_shell_pending_command[GEM_SHELL_COMMAND_BYTES];
static UBYTE gem_shell_pending_tail[GEM_SHELL_TAIL_BYTES];
static WORD gem_shell_pending_is_gem;

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
	gem_shell_pending_is_gem = pd->is_gem;
	gem_shell_pending = TRUE;
	pd->launch_channel = -1;
	return TRUE;
}

WORD
gem_shell_resident_take_command(UBYTE *command, UWORD command_bytes,
	UBYTE *tail, UWORD tail_bytes, WORD *is_gem)
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
	if (is_gem)
		*is_gem = gem_shell_pending_is_gem;
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
				GEM_SHELL_COMMAND_BYTES,
				gem_shell_command_scratch))
			return FALSE;
	} else {
		if (!gem_shell_find_native(gem_shell_command_scratch,
				gem_shell_candidate_path)
			|| !gem_shell_fixed_string(pd->command,
				GEM_SHELL_COMMAND_BYTES,
				gem_shell_candidate_path))
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
	 * this used to return a pointer into the owner's environment, that cant
	 * name bytes in a separate ELKS client DS, so we clear it and return FALSE
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
		|| !gem_shell_client_pointer(call, call->addr_in[0],
			command_bytes)
		|| !gem_shell_client_pointer(call, call->addr_in[1],
			directory_bytes)
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
			GEM_SHELL_DEFAULT_DIR_BYTES,
			gem_shell_default_dir_scratch))
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
gem_shell_resident_detach(UWORD owner, UWORD generation_lo, UWORD generation_hi)
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
