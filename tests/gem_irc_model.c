/* SPDX-License-Identifier: GPL-2.0-only */

#include "gem_irc_transport.h"

#include <stdio.h>
#include <string.h>

static int writes;

GEM_IRC_WORD
gem_irc_transport_write(GEM_IRC_TRANSPORT *transport, const char *line,
	GEM_IRC_UWORD length)
{
	(void) transport;
	if (!line || !length)
		return GEM_IRC_OUTPUT_REJECTED;
	writes++;
	return GEM_IRC_TRANSPORT_OK;
}

GEM_IRC_WORD
gem_irc_transport_poll(GEM_IRC_TRANSPORT *transport, char *line,
	GEM_IRC_UWORD capacity, GEM_IRC_UWORD *length)
{
	(void) transport;
	(void) line;
	(void) capacity;
	*length = 0;
	return GEM_IRC_TRANSPORT_IDLE;
}

void
gem_irc_transport_close(GEM_IRC_TRANSPORT *transport)
{
	(void) transport;
}

static int
receive(GEM_IRC_APP *app, const char *line)
{
	return gem_irc_app_receive(app, line,
		(GEM_IRC_UWORD) strlen(line));
}

#define CHECK(test, message) do { \
	if (!(test)) { \
		fprintf(stderr, "%s\n", message); \
		return 1; \
	} \
} while (0)

int
main(void)
{
	GEM_IRC_APP app;
	GEM_IRC_APP_BUFFER *query;
	GEM_IRC_TRANSPORT socket;
	unsigned int server_lines;
	int index;

	memset(&socket, 0, sizeof(socket));
	gem_irc_app_init(&app, &socket);
	CHECK(gem_irc_app_start(&app, "Test", "test", "Test User") > 0,
		"registration failed");
	CHECK(receive(&app, ":server 001 Test :welcome") > 0,
		"welcome failed");
	server_lines = app.order[0]->line_count;
	CHECK(receive(&app, ":Test!u@h JOIN :#antler") > 0,
		"join failed");
	CHECK(app.order[0]->kind == GEM_IRC_APP_BUFFER_SERVER
		&& app.order[0]->line_count == server_lines,
		"server log replaced");
	CHECK(app.active == GEM_IRC_APP_CHANNEL, "channel not selected");
	CHECK(receive(&app,
		":server 353 Test = #antler :@Alice +bob carol") > 0,
		"names failed");
	CHECK(app.nick_count == 4, "roster count mismatch");
	CHECK(app.nicks[0].prefix == '@'
		&& !strcmp(app.nicks[0].text, "Alice"),
		"operator ordering failed");
	CHECK(app.nicks[1].prefix == '+'
		&& !strcmp(app.nicks[1].text, "bob"),
		"voice ordering failed");

	CHECK(gem_irc_app_set_input(&app, "Ali") > 0, "input failed");
	CHECK(gem_irc_app_key(&app, 9U, 10U) == GEM_IRC_APP_KEY_HANDLED,
		"completion ignored");
	CHECK(!strcmp(gem_irc_app_input(&app), "Alice: "),
		"completion failed");

	CHECK(gem_irc_app_set_input(&app, "first") > 0, "history failed");
	CHECK(gem_irc_app_submit(&app) > 0, "first send failed");
	CHECK(gem_irc_app_set_input(&app, "second") > 0, "history failed");
	CHECK(gem_irc_app_submit(&app) > 0, "second send failed");
	(void) gem_irc_app_key(&app, 0x4800U, 10U);
	CHECK(!strcmp(gem_irc_app_input(&app), "second"),
		"newest history failed");
	(void) gem_irc_app_key(&app, 0x4800U, 10U);
	CHECK(!strcmp(gem_irc_app_input(&app), "first"),
		"older history failed");
	(void) gem_irc_app_key(&app, 0x5000U, 10U);
	CHECK(!strcmp(gem_irc_app_input(&app), "second"),
		"forward history failed");

	CHECK(gem_irc_app_query(&app, "Alice") > 0, "first query failed");
	CHECK(gem_irc_app_query(&app, "bob") > 0, "second query failed");
	query = app.order[app.active];
	CHECK(gem_irc_app_close_buffer(&app, 2) > 0, "query close failed");
	CHECK(app.order[2] == query && !strcmp(query->name, "bob"),
		"query moved");
	CHECK(gem_irc_app_query(&app, "carol") > 0, "query reuse failed");

	gem_irc_app_switch(&app, 0);
	(void) gem_irc_app_take_dirty(&app);
	CHECK(receive(&app,
		":Alice!u@h PRIVMSG #antler :channel-only") > 0,
		"channel message failed");
	CHECK(app.order[0]->line_count == server_lines
		&& !(app.dirty & GEM_IRC_APP_DIRTY_TRANSCRIPT)
		&& app.order[1]->activity, "background channel failed");
	gem_irc_app_switch(&app, 1);
	CHECK(gem_irc_app_close_buffer(&app, 1) > 0,
		"channel close failed");
	CHECK(receive(&app, ":Test!u@h PART #antler :Leaving") > 0,
		"part failed");
	CHECK(app.active == 0
		&& app.order[1]->kind == GEM_IRC_APP_BUFFER_FREE,
		"part did not select server");
	gem_irc_app_cycle(&app, 1);
	CHECK(app.active == 2, "empty channel selected");

	gem_irc_app_clear(&app);
	for (index = 0; index < 70; index++)
		CHECK(receive(&app, ":bob!u@h PRIVMSG Test :ring wrap") > 0,
			"ring receive failed");
	CHECK(gem_irc_app_line_count(&app) == GEM_IRC_APP_SCROLL_LINES
		&& strstr(gem_irc_app_visible_line(&app, 9, 10)->text,
			"ring wrap"), "ring wrap failed");

	gem_irc_app_offline(&app, "test disconnect");
	CHECK(!app.connected && app.nick_count == 0, "offline failed");
	CHECK(writes >= 4, "IRC output missing");
	puts("GEM_IRC_MODEL_PASS");
	return 0;
}
