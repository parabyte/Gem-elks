/*
 * Copyright (C) 2026 Goliath Keet <gatekeeper@xt-emporium.com>
 * SPDX-License-Identifier: GPL-2.0-only
 */

/*
 * gem_irc_app.c - native fixed-buffer GEM IRC application controller.
 *
 * This is the window-independent half of the client.  It turns protocol
 * callbacks into a bounded scrollback ring, edits one bounded command line,
 * translates essential slash commands, and polls a complete-line transport.
 * The GEM front end only draws returned lines and forwards keys/messages.
 *
 * Every counter, cursor, index, and return value is eight or sixteen bits.
 * Ring access uses short pointer walks so ia16-gcc never needs a variable
 * structure multiply.  There is no allocation, recursion, formatted output,
 * division, remainder, multiplication, variable shift, or scalar wider than
 * one 8086 word.
 */

#include "gem_irc_app.h"

/*
 * A scrollback record is 97 bytes.  When this cold controller is compiled
 * with -Os, ia16 GCC otherwise replaces the deliberately bounded pointer
 * walk below with a variable 16-bit MUL.  One immediate ADD is both smaller
 * and much cheaper on an 8088.  The pointer and immediate are one 16-bit
 * word each; the forty-record array is only 3880 bytes, so the near pointer
 * cannot wrap.  Host smoke tests retain ordinary C pointer increments.
 */
#if defined(ELKS) && ELKS
#define GEM_IRC_APP_NEXT_LINE(line) \
	__asm__ volatile ("addw %1,%0" : "+r" (line) \
			  : "i" (sizeof(GEM_IRC_APP_LINE)) : "cc")
#else
#define GEM_IRC_APP_NEXT_LINE(line) ((line)++)
#endif

/*
 * Keep variable roster lookup as a short pointer walk.  A 32-byte immediate
 * ADD is legal on the 8086 and avoids a compiler-generated variable shift or
 * multiply.  Twelve cells occupy 384 near bytes and cannot wrap a segment.
 */
#if defined(ELKS) && ELKS
#define GEM_IRC_APP_NEXT_NICK(nick) \
	__asm__ volatile ("addw %1,%0" : "+r" (nick) \
			  : "i" (sizeof(GEM_IRC_APP_NICK)) : "cc")
#else
#define GEM_IRC_APP_NEXT_NICK(nick) ((nick)++)
#endif

static GEM_IRC_APP_LINE *
gem_irc_app_line_at (GEM_IRC_APP *app, GEM_IRC_UWORD index)
{
  GEM_IRC_APP_LINE *line;

  line = app->lines;
  while (index)
    {
      GEM_IRC_APP_NEXT_LINE (line);
      index--;
    }
  return line;
}

static const GEM_IRC_APP_LINE *
gem_irc_app_const_line_at (const GEM_IRC_APP *app, GEM_IRC_UWORD index)
{
  const GEM_IRC_APP_LINE *line;

  line = app->lines;
  while (index)
    {
      GEM_IRC_APP_NEXT_LINE (line);
      index--;
    }
  return line;
}

static GEM_IRC_APP_NICK *
gem_irc_app_nick_at (GEM_IRC_APP *app, GEM_IRC_UWORD index)
{
  GEM_IRC_APP_NICK *nick;

  nick = app->nicks;
  while (index)
    {
      GEM_IRC_APP_NEXT_NICK (nick);
      index--;
    }
  return nick;
}

/* Copy all bytes or reject without changing the published destination. */
static GEM_IRC_WORD
gem_irc_app_copy_exact (char *destination, GEM_IRC_UWORD capacity,
			const char *source)
{
  GEM_IRC_UWORD length;
  GEM_IRC_UWORD index;

  if (!destination || !capacity || !source)
    return GEM_IRC_BAD_ARGUMENT;
  length = 0;
  while (source[length])
    {
      if (length + 1U >= capacity)
	return GEM_IRC_LINE_TOO_LONG;
      length++;
    }
  index = 0;
  while (index < length)
    {
      destination[index] = source[index];
      index++;
    }
  destination[index] = '\0';
  return GEM_IRC_OK;
}

/* Copy a display string with saturation at capacity minus one byte. */
static void
gem_irc_app_copy_clipped (char *destination, GEM_IRC_UWORD capacity,
			  const char *source)
{
  GEM_IRC_UWORD index;

  index = 0;
  while (source[index] && index + 1U < capacity)
    {
      destination[index] = source[index];
      index++;
    }
  destination[index] = '\0';
}

static void
gem_irc_app_roster_clear (GEM_IRC_APP *app)
{
  app->nick_count = 0;
  app->nick_overflow = 0;
  app->dirty |= GEM_IRC_APP_DIRTY_ROSTER;
}

static GEM_IRC_WORD
gem_irc_app_roster_find (const GEM_IRC_APP *app, const char *name)
{
  GEM_IRC_UWORD index;
  const GEM_IRC_APP_NICK *nick;

  index = 0;
  nick = app->nicks;
  while (index < (GEM_IRC_UWORD) app->nick_count)
    {
      if (gem_irc_equal (nick->text, name))
	return (GEM_IRC_WORD) index;
      GEM_IRC_APP_NEXT_NICK (nick);
      index++;
    }
  return -1;
}

static void
gem_irc_app_roster_add (GEM_IRC_APP *app, const char *name)
{
  GEM_IRC_APP_NICK *nick;

  if (!name)
    return;
  if (!*name || gem_irc_app_roster_find (app, name) >= 0)
    return;
  if (app->nick_count >= GEM_IRC_APP_NICK_SLOTS)
    {
      app->nick_overflow = 1;
      app->dirty |= GEM_IRC_APP_DIRTY_ROSTER;
      return;
    }
  nick = gem_irc_app_nick_at (app, (GEM_IRC_UWORD) app->nick_count);
  if (gem_irc_app_copy_exact (nick->text, GEM_IRC_NICK_SIZE, name)
      != GEM_IRC_OK)
    {
      app->nick_overflow = 1;
      app->dirty |= GEM_IRC_APP_DIRTY_ROSTER;
      return;
    }
  app->nick_count++;
  app->dirty |= GEM_IRC_APP_DIRTY_ROSTER;
}

static void
gem_irc_app_roster_remove (GEM_IRC_APP *app, const char *name)
{
  GEM_IRC_WORD found;
  GEM_IRC_UWORD last;
  GEM_IRC_APP_NICK *destination;
  GEM_IRC_APP_NICK *source;

  if (!name)
    return;
  found = gem_irc_app_roster_find (app, name);
  if (found < 0)
    return;
  app->nick_count--;
  last = (GEM_IRC_UWORD) app->nick_count;
  if ((GEM_IRC_UWORD) found != last)
    {
      destination = gem_irc_app_nick_at (app, (GEM_IRC_UWORD) found);
      source = gem_irc_app_nick_at (app, last);
      (void) gem_irc_app_copy_exact (destination->text,
				     GEM_IRC_NICK_SIZE, source->text);
    }
  app->dirty |= GEM_IRC_APP_DIRTY_ROSTER;
}

static void
gem_irc_app_roster_rename (GEM_IRC_APP *app, const char *old_name,
			   const char *new_name)
{
  GEM_IRC_WORD found;

  found = gem_irc_app_roster_find (app, old_name);
  if (found < 0)
    return;
  gem_irc_app_roster_remove (app, old_name);
  gem_irc_app_roster_add (app, new_name);
}

static char *gem_irc_app_word (char **remaining);

/*
 * Split the numeric 353 view in place.  Protocol event strings point into the
 * client's private receive buffer and expire when this callback returns, so
 * replacing name separators with terminators neither changes caller storage
 * nor survives the event.  Reusing the slash-command word walker avoids a
 * second parser and its stack buffer.
 */
static void
gem_irc_app_roster_names (GEM_IRC_APP *app, const char *text)
{
  char *remaining;
  char *name;

  remaining = (char *) text;
  while ((name = gem_irc_app_word (&remaining)) != (char *) 0)
    {
      while (*name == '~' || *name == '&' || *name == '@'
	     || *name == '%' || *name == '+')
	name++;
      gem_irc_app_roster_add (app, name);
    }
}

static void
gem_irc_app_append_line (GEM_IRC_APP *app, GEM_IRC_UBYTE kind,
			 const char *text)
{
  GEM_IRC_APP_LINE *line;
  GEM_IRC_UWORD slot;

  if (!app || !text)
    return;
  if (app->line_count < GEM_IRC_APP_SCROLL_LINES)
    {
      slot = app->line_head + app->line_count;
      if (slot >= GEM_IRC_APP_SCROLL_LINES)
	slot -= GEM_IRC_APP_SCROLL_LINES;
      app->line_count++;
    }
  else
    {
      slot = app->line_head;
      app->line_head++;
      if (app->line_head >= GEM_IRC_APP_SCROLL_LINES)
	app->line_head = 0;
    }
  line = gem_irc_app_line_at (app, slot);
  line->kind = kind;
  gem_irc_app_copy_clipped (line->text, GEM_IRC_APP_DISPLAY_SIZE, text);

  /* Keep an older viewport stable when new network text arrives. */
  if (app->scroll_offset
      && app->scroll_offset + 1U < GEM_IRC_APP_SCROLL_LINES)
    app->scroll_offset++;
  app->dirty |= GEM_IRC_APP_DIRTY_TRANSCRIPT;
}

/*
 * Append one part of a logical transcript line to the shared fixed scratch
 * row.  A full row is committed before its next byte is copied, so no byte
 * from an IRC passage is silently discarded.  display_columns has scale one
 * byte per GEM text cell and is saturated by the public setter to the
 * 95-byte record payload.  The loop uses only pointer increments, byte
 * stores, and 16-bit compares on the 8088/8086.
 */
static void
gem_irc_app_stream_text (GEM_IRC_APP *app, GEM_IRC_UBYTE kind,
			 GEM_IRC_UWORD *length, const char *text)
{
  GEM_IRC_UWORD limit;

  if (!text)
    return;
  limit = app->display_columns;
  while (*text)
    {
      if (*length >= limit)
	{
	  app->format_line[*length] = '\0';
	  gem_irc_app_append_line (app, kind, app->format_line);
	  *length = 0;
	}
      app->format_line[*length] = *text++;
      (*length)++;
    }
}

static void
gem_irc_app_stream_finish (GEM_IRC_APP *app, GEM_IRC_UBYTE kind,
			   GEM_IRC_UWORD length)
{
  if (!length)
    return;
  app->format_line[length] = '\0';
  gem_irc_app_append_line (app, kind, app->format_line);
}

static void
gem_irc_app_status (GEM_IRC_APP *app, GEM_IRC_UBYTE kind,
		    const char *prefix, const char *first, const char *second)
{
  GEM_IRC_UWORD length;

  length = 0;
  gem_irc_app_stream_text (app, kind, &length, prefix);
  gem_irc_app_stream_text (app, kind, &length, first);
  gem_irc_app_stream_text (app, kind, &length, second);
  gem_irc_app_stream_finish (app, kind, length);
}

/*
 * Network events often consist of several short fields.  Keep their assembly
 * in one shared routine: five optional views preserve useful IRC fields
 * while avoiding a copy of the same begin/append sequence in every switch
 * arm.  Each pointer is near in the ELKS small model.
 */
static void
gem_irc_app_event_line (GEM_IRC_APP *app, GEM_IRC_UBYTE kind,
			const char *first, const char *second,
			const char *third, const char *fourth,
			const char *fifth)
{
  GEM_IRC_UWORD length;

  length = 0;
  gem_irc_app_stream_text (app, kind, &length, first);
  gem_irc_app_stream_text (app, kind, &length, second);
  gem_irc_app_stream_text (app, kind, &length, third);
  gem_irc_app_stream_text (app, kind, &length, fourth);
  gem_irc_app_stream_text (app, kind, &length, fifth);
  gem_irc_app_stream_finish (app, kind, length);
}

static const char *
gem_irc_app_source (const GEM_IRC_EVENT *event)
{
  if (event->nick && *event->nick)
    return event->nick;
  if (event->source && *event->source)
    return event->source;
  return "server";
}

static GEM_IRC_WORD
gem_irc_app_is_channel (const char *target)
{
  if (!target || !*target)
    return 0;
  return *target == '#' || *target == '&' || *target == '!' || *target == '+';
}

static GEM_IRC_WORD
gem_irc_app_set_target (GEM_IRC_APP *app, const char *target)
{
  GEM_IRC_WORD result;

  result = gem_irc_app_copy_exact (app->target,
				   GEM_IRC_APP_TARGET_SIZE, target);
  if (result == GEM_IRC_OK)
    app->dirty |= GEM_IRC_APP_DIRTY_TARGET | GEM_IRC_APP_DIRTY_TOPIC;
  return result;
}

static void
gem_irc_app_clear_target (GEM_IRC_APP *app)
{
  app->target[0] = '\0';
  app->dirty |= GEM_IRC_APP_DIRTY_TARGET | GEM_IRC_APP_DIRTY_TOPIC;
}

static GEM_IRC_WORD
gem_irc_app_transport_output (void *opaque, const char *line,
			      GEM_IRC_UWORD length)
{
  GEM_IRC_APP *app;
  GEM_IRC_WORD result;

  app = (GEM_IRC_APP *) opaque;
  if (!app || !app->transport.write)
    return GEM_IRC_NO_OUTPUT;
  result = app->transport.write (app->transport.context, line, length);
  if (result < 0)
    app->connected = 0;
  return result;
}

static void
gem_irc_app_protocol_event (void *opaque, const GEM_IRC_EVENT *event)
{
  GEM_IRC_APP *app;
  const char *source;
  const char *text;
  GEM_IRC_WORD ours;

  app = (GEM_IRC_APP *) opaque;
  if (!app || !event)
    return;
  source = gem_irc_app_source (event);
  text = event->text ? event->text : "";

  switch (event->type)
    {
    case GEM_IRC_EVENT_REGISTERED:
      app->connected = 1;
      app->dirty |= GEM_IRC_APP_DIRTY_TOPIC;
      gem_irc_app_status (app, GEM_IRC_APP_LINE_STATUS,
			  "*** ", text, (const char *) 0);
      break;
    case GEM_IRC_EVENT_PING:
      /* Successful automatic PONGs do not consume scarce scrollback. */
      break;
    case GEM_IRC_EVENT_MESSAGE:
      ours = event->target && gem_irc_app_is_channel (event->target);
      gem_irc_app_event_line (app, GEM_IRC_APP_LINE_NORMAL,
			      ours ? "<" : "[", source, ours ? "> " : "] ",
			      text, (const char *) 0);
      break;
    case GEM_IRC_EVENT_ACTION:
      gem_irc_app_event_line (app, GEM_IRC_APP_LINE_ACTION,
			      "* ", source, " ", text, (const char *) 0);
      break;
    case GEM_IRC_EVENT_CTCP:
      gem_irc_app_event_line (app, GEM_IRC_APP_LINE_STATUS,
			      "*** CTCP ", source, ": ", text,
			      (const char *) 0);
      break;
    case GEM_IRC_EVENT_NOTICE:
      gem_irc_app_event_line (app, GEM_IRC_APP_LINE_NOTICE,
			      "-", source, "- ", text, (const char *) 0);
      break;
    case GEM_IRC_EVENT_JOIN:
      gem_irc_app_event_line (app, GEM_IRC_APP_LINE_STATUS,
			      "*** ", source, " + ", event->target,
			      (const char *) 0);
      if (app->protocol.nick[0] && gem_irc_equal (source, app->protocol.nick))
	{
	  (void) gem_irc_app_set_target (app, event->target);
	  app->topic[0] = '\0';
	  gem_irc_app_roster_clear (app);
	  gem_irc_app_roster_add (app, source);
	}
      else if (event->target && gem_irc_equal (event->target, app->target))
	gem_irc_app_roster_add (app, source);
      break;
    case GEM_IRC_EVENT_PART:
      gem_irc_app_event_line (app, GEM_IRC_APP_LINE_STATUS,
			      "*** ", source, " left: ",
			      *text ? text : event->target, (const char *) 0);
      if (app->protocol.nick[0]
	  && gem_irc_equal (source, app->protocol.nick)
	  && event->target && gem_irc_equal (event->target, app->target))
	{
	  gem_irc_app_clear_target (app);
	  app->topic[0] = '\0';
	  gem_irc_app_roster_clear (app);
	}
      else if (event->target && gem_irc_equal (event->target, app->target))
	gem_irc_app_roster_remove (app, source);
      break;
    case GEM_IRC_EVENT_QUIT:
      gem_irc_app_event_line (app, GEM_IRC_APP_LINE_STATUS,
			      "*** ", source, " quit",
			      *text ? ": " : (const char *) 0,
			      *text ? text : (const char *) 0);
      gem_irc_app_roster_remove (app, source);
      break;
    case GEM_IRC_EVENT_NICK:
      gem_irc_app_event_line (app, GEM_IRC_APP_LINE_STATUS,
			      "*** ", source, " -> ", event->target,
			      (const char *) 0);
      if (gem_irc_equal (app->target, source))
	(void) gem_irc_app_set_target (app, event->target);
      gem_irc_app_roster_rename (app, source, event->target);
      break;
    case GEM_IRC_EVENT_TOPIC:
      gem_irc_app_event_line (app, GEM_IRC_APP_LINE_STATUS,
			      "*** Topic ", event->target, ": ", text,
			      (const char *) 0);
      if (event->target && gem_irc_equal (event->target, app->target))
	{
	  gem_irc_app_copy_clipped (app->topic, GEM_IRC_APP_DISPLAY_SIZE,
				    text);
	  app->dirty |= GEM_IRC_APP_DIRTY_TOPIC;
	}
      break;
    case GEM_IRC_EVENT_TOPIC_INFO:
      gem_irc_app_event_line (app, GEM_IRC_APP_LINE_STATUS,
			      "*** By ", text,
			      event->extra
			      && *event->extra ? " at " : (const char *) 0,
			      event->extra, (const char *) 0);
      break;
    case GEM_IRC_EVENT_NAMES:
      if (event->target && gem_irc_equal (event->target, app->target))
	gem_irc_app_roster_names (app, text);
      break;
    case GEM_IRC_EVENT_NAMES_END:
      app->dirty |= GEM_IRC_APP_DIRTY_ROSTER;
      break;
    case GEM_IRC_EVENT_KICK:
      gem_irc_app_event_line (app, GEM_IRC_APP_LINE_STATUS,
			      "*** ", event->extra, " kick ", source,
			      *text ? text : (const char *) 0);
      ours = event->extra && app->protocol.nick[0]
	&& gem_irc_equal (event->extra, app->protocol.nick);
      if (ours && event->target && gem_irc_equal (event->target, app->target))
	{
	  gem_irc_app_clear_target (app);
	  app->topic[0] = '\0';
	  gem_irc_app_roster_clear (app);
	}
      else if (event->target && gem_irc_equal (event->target, app->target))
	gem_irc_app_roster_remove (app, event->extra);
      break;
    case GEM_IRC_EVENT_MODE:
      gem_irc_app_event_line (app, GEM_IRC_APP_LINE_STATUS,
			      "*** ", source, " mode ", text,
			      (const char *) 0);
      break;
    case GEM_IRC_EVENT_NICK_IN_USE:
      gem_irc_app_status (app, GEM_IRC_APP_LINE_ERROR,
			  "*** Nick in use: ", event->target,
			  (const char *) 0);
      break;
    case GEM_IRC_EVENT_ERROR:
      gem_irc_app_status (app, GEM_IRC_APP_LINE_ERROR,
			  "*** Error: ", text, (const char *) 0);
      break;
    default:
      gem_irc_app_event_line (app, GEM_IRC_APP_LINE_STATUS,
			      "*** ",
			      event->command ? event->command : "status",
			      *text ? " " : (const char *) 0,
			      *text ? text : (const char *) 0,
			      (const char *) 0);
      break;
    }
}

void
gem_irc_app_init (GEM_IRC_APP *app, const GEM_IRC_APP_TRANSPORT *transport)
{
  if (!app)
    return;
  if (transport)
    app->transport = *transport;
  else
    {
      app->transport.write = (GEM_IRC_APP_WRITE) 0;
      app->transport.poll = (GEM_IRC_APP_POLL) 0;
      app->transport.close = (GEM_IRC_APP_CLOSE) 0;
      app->transport.context = (void *) 0;
    }
  /* Callback availability is not a completed TCP connection. */
  app->connected = 0;
  app->closing = 0;
  app->dirty = GEM_IRC_APP_DIRTY_ALL;
  app->line_head = 0;
  app->line_count = 0;
  app->scroll_offset = 0;
  app->display_columns = GEM_IRC_APP_DISPLAY_SIZE - 1U;
  app->input_length = 0;
  app->input_cursor = 0;
  app->nick_count = 0;
  app->nick_overflow = 0;
  app->target[0] = '\0';
  app->topic[0] = '\0';
  app->input[0] = '\0';
  app->last_input[0] = '\0';
  app->network_line[0] = '\0';
  app->command_line[0] = '\0';
  app->format_line[0] = '\0';
  gem_irc_init (&app->protocol, gem_irc_app_transport_output,
		gem_irc_app_protocol_event, app);
}

void
gem_irc_app_set_display_columns (GEM_IRC_APP *app, GEM_IRC_UWORD columns)
{
  if (!app)
    return;
  if (!columns)
    columns = 1U;
  if (columns >= GEM_IRC_APP_DISPLAY_SIZE)
    columns = GEM_IRC_APP_DISPLAY_SIZE - 1U;
  app->display_columns = columns;
}

void
gem_irc_app_connecting (GEM_IRC_APP *app, const char *host)
{
  if (!app)
    return;
  gem_irc_app_status (app, GEM_IRC_APP_LINE_STATUS,
		      "*** Connecting to ", host ? host : "server",
		      (const char *) 0);
}

void
gem_irc_app_connection_failed (GEM_IRC_APP *app)
{
  if (!app)
    return;
  app->connected = 0;
  app->topic[0] = '\0';
  app->dirty |= GEM_IRC_APP_DIRTY_TOPIC;
  gem_irc_app_roster_clear (app);
  gem_irc_app_status (app, GEM_IRC_APP_LINE_ERROR,
		      "*** Connection failed; retrying", (const char *) 0,
		      (const char *) 0);
}

GEM_IRC_WORD
gem_irc_app_start (GEM_IRC_APP *app, const char *nick, const char *user,
		   const char *real_name)
{
  GEM_IRC_WORD result;

  if (!app || !nick || !user || !real_name)
    return GEM_IRC_BAD_ARGUMENT;
  gem_irc_app_status (app, GEM_IRC_APP_LINE_STATUS,
		      "*** Registering as ", nick, (const char *) 0);
  result = gem_irc_send_nick (&app->protocol, nick);
  if (result <= 0)
    {
      gem_irc_app_status (app, GEM_IRC_APP_LINE_ERROR,
			  "*** Connection write failed", (const char *) 0,
			  (const char *) 0);
      return result;
    }
  result = gem_irc_send_user (&app->protocol, user, real_name);
  if (result > 0)
    app->connected = 1;
  else
    gem_irc_app_status (app, GEM_IRC_APP_LINE_ERROR,
			"*** Registration write failed", (const char *) 0,
			(const char *) 0);
  return result;
}

GEM_IRC_WORD
gem_irc_app_join (GEM_IRC_APP *app, const char *channel, const char *key)
{
  if (!app)
    return GEM_IRC_BAD_ARGUMENT;
  return gem_irc_send_join (&app->protocol, channel, key);
}

GEM_IRC_WORD
gem_irc_app_send_message (GEM_IRC_APP *app, const char *target,
			  const char *text)
{
  GEM_IRC_WORD result;

  if (!app || !target || !*target || !text || !*text)
    return GEM_IRC_BAD_ARGUMENT;
  result = gem_irc_send_privmsg (&app->protocol, target, text);
  if (result <= 0)
    return result;

  /*
   * The queue owns the complete line now.  Publish the local echo only at
   * this point, so a rejected write can be retried without a false line in
   * the transcript and an accepted one can be latched exactly once.
   */
  gem_irc_app_event_line (app, GEM_IRC_APP_LINE_NORMAL,
			  "<", gem_irc_current_nick (&app->protocol), "> ",
			  text, (const char *) 0);
  return result;
}

GEM_IRC_WORD
gem_irc_app_request_close (GEM_IRC_APP *app, const char *reason)
{
  GEM_IRC_WORD result;

  if (!app)
    return GEM_IRC_BAD_ARGUMENT;
  if (app->closing)
    return GEM_IRC_OK;
  result = GEM_IRC_OK;
  if (app->connected && app->transport.write)
    result = gem_irc_send_quit (&app->protocol,
				reason ? reason : "GEM IRC closed");
  app->closing = 1;
  app->dirty |= GEM_IRC_APP_DIRTY_TOPIC;
  return result;
}

void
gem_irc_app_disconnect (GEM_IRC_APP *app, const char *reason)
{
  if (!app)
    return;
  /* /quit has already emitted QUIT before setting the closing latch. */
  if (app->connected && !app->closing && app->transport.write)
    (void) gem_irc_send_quit (&app->protocol,
			      reason ? reason : "GEM IRC closed");
  if (app->transport.close)
    app->transport.close (app->transport.context);
  app->connected = 0;
  app->closing = 1;
  app->topic[0] = '\0';
  app->dirty |= GEM_IRC_APP_DIRTY_TOPIC;
  gem_irc_app_roster_clear (app);
  gem_irc_connection_reset (&app->protocol);
}

GEM_IRC_WORD
gem_irc_app_receive (GEM_IRC_APP *app, const char *line, GEM_IRC_UWORD length)
{
  GEM_IRC_WORD result;

  if (!app)
    return GEM_IRC_BAD_ARGUMENT;
  result = gem_irc_receive_line (&app->protocol, line, length);
  if (result < 0)
    gem_irc_app_status (app, GEM_IRC_APP_LINE_ERROR,
			"*** Rejected IRC line", (const char *) 0,
			(const char *) 0);
  else if (result == GEM_IRC_OUTPUT_REJECTED)
    gem_irc_app_status (app, GEM_IRC_APP_LINE_ERROR,
			"*** PONG output blocked", (const char *) 0,
			(const char *) 0);
  return result;
}

GEM_IRC_WORD
gem_irc_app_poll (GEM_IRC_APP *app, GEM_IRC_UWORD limit)
{
  GEM_IRC_UWORD processed;
  GEM_IRC_UWORD length;
  GEM_IRC_WORD result;

  if (!app || !app->transport.poll)
    return GEM_IRC_NO_OUTPUT;
  processed = 0;
  while (processed < limit)
    {
      length = 0;
      result = app->transport.poll (app->transport.context,
				    app->network_line, GEM_IRC_LINE_MAX,
				    &length);
      if (result == 0)
	break;
      if (result < 0)
	{
	  if (app->connected)
	    {
	      app->connected = 0;
	      app->topic[0] = '\0';
	      app->dirty |= GEM_IRC_APP_DIRTY_TOPIC;
	      gem_irc_app_roster_clear (app);
	      gem_irc_connection_reset (&app->protocol);
	      if (app->transport.close)
		app->transport.close (app->transport.context);
	      gem_irc_app_status (app, GEM_IRC_APP_LINE_ERROR,
				  "*** Connection closed", (const char *) 0,
				  (const char *) 0);
	    }
	  return result;
	}
      if (!length || length > GEM_IRC_LINE_MAX)
	{
	  gem_irc_app_status (app, GEM_IRC_APP_LINE_ERROR,
			      "*** Invalid transport line", (const char *) 0,
			      (const char *) 0);
	  return GEM_IRC_MALFORMED;
	}
      app->network_line[length] = '\0';
      (void) gem_irc_app_receive (app, app->network_line, length);
      processed++;
    }
  return (GEM_IRC_WORD) processed;
}

static char *
gem_irc_app_skip_spaces (char *text)
{
  while (*text == ' ')
    text++;
  return text;
}

static char *
gem_irc_app_word (char **remaining)
{
  char *word;
  char *cursor;

  if (!remaining || !*remaining)
    return (char *) 0;
  cursor = gem_irc_app_skip_spaces (*remaining);
  if (!*cursor)
    {
      *remaining = cursor;
      return (char *) 0;
    }
  word = cursor;
  while (*cursor && *cursor != ' ')
    cursor++;
  if (*cursor)
    *cursor++ = '\0';
  *remaining = gem_irc_app_skip_spaces (cursor);
  return word;
}

static GEM_IRC_WORD
gem_irc_app_raw_target (GEM_IRC_APP *app, const char *command,
			const char *target)
{
  GEM_IRC_UWORD length;
  const char *cursor;

  if (!command || !target || !*target)
    return GEM_IRC_BAD_ARGUMENT;
  length = 0;
  cursor = command;
  while (*cursor)
    {
      if (length + 1U >= GEM_IRC_APP_INPUT_SIZE)
	return GEM_IRC_LINE_TOO_LONG;
      app->format_line[length++] = *cursor++;
    }
  if (length + 1U >= GEM_IRC_APP_INPUT_SIZE)
    return GEM_IRC_LINE_TOO_LONG;
  app->format_line[length++] = ' ';
  cursor = target;
  while (*cursor)
    {
      if (length + 1U >= GEM_IRC_APP_INPUT_SIZE)
	return GEM_IRC_LINE_TOO_LONG;
      app->format_line[length++] = *cursor++;
    }
  app->format_line[length] = '\0';
  return gem_irc_send_raw (&app->protocol, app->format_line);
}

static void
gem_irc_app_help (GEM_IRC_APP *app)
{
  gem_irc_app_append_line (app, GEM_IRC_APP_LINE_STATUS,
			   "*** /join /part /query /msg /notice /me /nick");
  gem_irc_app_append_line (app, GEM_IRC_APP_LINE_STATUS,
			   "*** /whois /names /topic /mode /kick /away");
  gem_irc_app_append_line (app, GEM_IRC_APP_LINE_STATUS,
			   "*** /raw /clear /quit /help");
}

static void
gem_irc_app_submit_done (GEM_IRC_APP *app)
{
  (void) gem_irc_app_copy_exact (app->last_input,
				 GEM_IRC_APP_INPUT_SIZE, app->input);
  app->input[0] = '\0';
  app->input_length = 0;
  app->input_cursor = 0;
  app->dirty |= GEM_IRC_APP_DIRTY_INPUT;
}

static GEM_IRC_WORD
gem_irc_app_send_failure (GEM_IRC_APP *app, GEM_IRC_WORD result)
{
  if (result == GEM_IRC_LINE_TOO_LONG)
    gem_irc_app_status (app, GEM_IRC_APP_LINE_ERROR,
			"*** Command exceeds IRC line limit",
			(const char *) 0, (const char *) 0);
  else if (result == GEM_IRC_MALFORMED)
    gem_irc_app_status (app, GEM_IRC_APP_LINE_ERROR,
			"*** Invalid command characters", (const char *) 0,
			(const char *) 0);
  else
    gem_irc_app_status (app, GEM_IRC_APP_LINE_ERROR,
			"*** Transport did not accept command",
			(const char *) 0, (const char *) 0);
  return result;
}

GEM_IRC_WORD
gem_irc_app_submit (GEM_IRC_APP *app)
{
  char *cursor;
  char *command;
  char *first;
  char *second;
  char *arguments;
  const char *target;
  GEM_IRC_WORD result;
  GEM_IRC_WORD local;
  GEM_IRC_UWORD length;

  if (!app)
    return GEM_IRC_BAD_ARGUMENT;
  if (!app->input_length || !app->input[0])
    return GEM_IRC_OK;
  if (gem_irc_app_copy_exact (app->command_line,
			      GEM_IRC_APP_INPUT_SIZE,
			      app->input) != GEM_IRC_OK)
    return GEM_IRC_LINE_TOO_LONG;

  if (app->command_line[0] != '/')
    {
      if (!app->target[0])
	{
	  gem_irc_app_status (app, GEM_IRC_APP_LINE_ERROR,
			      "*** Join or query a target first",
			      (const char *) 0, (const char *) 0);
	  return GEM_IRC_BAD_ARGUMENT;
	}
      result = gem_irc_app_send_message (app, app->target, app->input);
      if (result <= 0)
	return gem_irc_app_send_failure (app, result);
      gem_irc_app_submit_done (app);
      return result;
    }

  cursor = app->command_line + 1;
  command = gem_irc_app_word (&cursor);
  arguments = cursor;
  if (!command || !*command)
    {
      gem_irc_app_submit_done (app);
      return GEM_IRC_OK;
    }
  local = 0;
  result = GEM_IRC_BAD_ARGUMENT;

  if (gem_irc_equal (command, "join"))
    {
      first = gem_irc_app_word (&cursor);
      second = gem_irc_app_word (&cursor);
      result = gem_irc_send_join (&app->protocol, first, second);
    }
  else if (gem_irc_equal (command, "part"))
    {
      if (*arguments && gem_irc_app_is_channel (arguments))
	{
	  cursor = arguments;
	  target = gem_irc_app_word (&cursor);
	}
      else
	{
	  target = app->target;
	  cursor = arguments;
	}
      result = gem_irc_send_part (&app->protocol, target,
				  *cursor ? cursor : (const char *) 0);
    }
  else if (gem_irc_equal (command, "msg"))
    {
      first = gem_irc_app_word (&cursor);
      result = gem_irc_send_privmsg (&app->protocol, first, cursor);
      if (result > 0)
	gem_irc_app_event_line (app, GEM_IRC_APP_LINE_NORMAL,
				"-> ", first, ": ", cursor, (const char *) 0);
    }
  else if (gem_irc_equal (command, "notice"))
    {
      first = gem_irc_app_word (&cursor);
      result = gem_irc_send_notice (&app->protocol, first, cursor);
    }
  else if (gem_irc_equal (command, "me"))
    {
      result = gem_irc_send_action (&app->protocol, app->target, arguments);
      if (result > 0)
	gem_irc_app_event_line (app, GEM_IRC_APP_LINE_ACTION,
				"* ", gem_irc_current_nick (&app->protocol),
				" ", arguments, (const char *) 0);
    }
  else if (gem_irc_equal (command, "nick"))
    {
      first = gem_irc_app_word (&cursor);
      result = gem_irc_send_nick (&app->protocol, first);
    }
  else if (gem_irc_equal (command, "query"))
    {
      first = gem_irc_app_word (&cursor);
      result = gem_irc_app_set_target (app, first);
      if (result == GEM_IRC_OK)
	gem_irc_app_status (app, GEM_IRC_APP_LINE_STATUS,
			    "*** Active target: ", first, (const char *) 0);
      local = 1;
    }
  else if (gem_irc_equal (command, "whois"))
    {
      first = gem_irc_app_word (&cursor);
      result = gem_irc_send_whois (&app->protocol, first);
    }
  else if (gem_irc_equal (command, "names"))
    {
      first = gem_irc_app_word (&cursor);
      target = first ? first : app->target;
      result = gem_irc_app_raw_target (app, "NAMES", target);
    }
  else if (gem_irc_equal (command, "topic"))
    {
      if (*arguments && gem_irc_app_is_channel (arguments))
	{
	  cursor = arguments;
	  first = gem_irc_app_word (&cursor);
	  target = first;
	  arguments = cursor;
	}
      else
	{
	  target = app->target;
	}
      result = gem_irc_send_topic (&app->protocol, target,
				   *arguments ? arguments : (const char *) 0);
    }
  else if (gem_irc_equal (command, "mode"))
    {
      cursor = arguments;
      if (*cursor && (*cursor == '#' || *cursor == '&' || *cursor == '!'))
	target = gem_irc_app_word (&cursor);
      else
	target = app->target;
      first = gem_irc_app_word (&cursor);
      result = gem_irc_send_mode (&app->protocol, target, first,
				  *cursor ? cursor : (const char *) 0);
    }
  else if (gem_irc_equal (command, "kick"))
    {
      cursor = arguments;
      if (*cursor && gem_irc_app_is_channel (cursor))
	target = gem_irc_app_word (&cursor);
      else
	target = app->target;
      first = gem_irc_app_word (&cursor);
      result = gem_irc_send_kick (&app->protocol, target, first,
				  *cursor ? cursor : (const char *) 0);
    }
  else if (gem_irc_equal (command, "away"))
    {
      if (*arguments)
	{
	  length = 0;
	  app->format_line[length++] = 'A';
	  app->format_line[length++] = 'W';
	  app->format_line[length++] = 'A';
	  app->format_line[length++] = 'Y';
	  app->format_line[length++] = ' ';
	  app->format_line[length++] = ':';
	  cursor = arguments;
	  while (*cursor && length + 1U < GEM_IRC_APP_INPUT_SIZE)
	    app->format_line[length++] = *cursor++;
	  if (*cursor)
	    result = GEM_IRC_LINE_TOO_LONG;
	  else
	    {
	      app->format_line[length] = '\0';
	      result = gem_irc_send_raw (&app->protocol, app->format_line);
	    }
	}
      else
	{
	  result = gem_irc_send_raw (&app->protocol, "AWAY");
	}
    }
  else if (gem_irc_equal (command, "raw"))
    {
      result = gem_irc_send_raw (&app->protocol, arguments);
    }
  else if (gem_irc_equal (command, "clear"))
    {
      gem_irc_app_clear (app);
      result = GEM_IRC_OK;
      local = 1;
    }
  else if (gem_irc_equal (command, "help"))
    {
      gem_irc_app_help (app);
      result = GEM_IRC_OK;
      local = 1;
    }
  else if (gem_irc_equal (command, "quit"))
    {
      result = gem_irc_app_request_close (app,
					  *arguments ? arguments : (const char
								    *) 0);
    }
  else
    {
      gem_irc_app_status (app, GEM_IRC_APP_LINE_ERROR,
			  "*** Unknown command: /", command,
			  (const char *) 0);
      result = GEM_IRC_BAD_ARGUMENT;
      local = 1;
    }

  if (result > 0 || local)
    {
      gem_irc_app_submit_done (app);
      return result;
    }
  return gem_irc_app_send_failure (app, result);
}

static void
gem_irc_app_delete_range (GEM_IRC_APP *app, GEM_IRC_UWORD first,
			  GEM_IRC_UWORD last)
{
  GEM_IRC_UWORD source;
  GEM_IRC_UWORD destination;
  GEM_IRC_UWORD removed;

  if (first >= last || last > app->input_length)
    return;
  removed = last - first;
  destination = first;
  source = last;
  while (source <= app->input_length)
    app->input[destination++] = app->input[source++];
  app->input_length -= removed;
  app->input_cursor = first;
  app->dirty |= GEM_IRC_APP_DIRTY_INPUT;
}

static void
gem_irc_app_restore_last (GEM_IRC_APP *app)
{
  GEM_IRC_UWORD length;

  if (!app->last_input[0])
    return;
  (void) gem_irc_app_copy_exact (app->input, GEM_IRC_APP_INPUT_SIZE,
				 app->last_input);
  length = 0;
  while (app->input[length])
    length++;
  app->input_length = length;
  app->input_cursor = length;
  app->dirty |= GEM_IRC_APP_DIRTY_INPUT;
}

GEM_IRC_WORD
gem_irc_app_key (GEM_IRC_APP *app, GEM_IRC_UWORD key,
		 GEM_IRC_UWORD visible_rows)
{
  GEM_IRC_UWORD ascii;
  GEM_IRC_UWORD scan;
  GEM_IRC_UWORD index;
  GEM_IRC_UWORD start;

  if (!app)
    return GEM_IRC_APP_KEY_UNHANDLED;
  ascii = key & 0x00ffU;
  scan = key & 0xff00U;
  if (ascii == '\r' || ascii == '\n')
    {
      (void) gem_irc_app_submit (app);
      return app->closing ? GEM_IRC_APP_KEY_CLOSE : GEM_IRC_APP_KEY_SUBMITTED;
    }
  if (ascii == 17U)
    {
      return GEM_IRC_APP_KEY_CLOSE;
    }
  if (scan == 0x4900U)
    {
      gem_irc_app_scroll_up (app, visible_rows, visible_rows);
      return GEM_IRC_APP_KEY_HANDLED;
    }
  if (scan == 0x5100U)
    {
      gem_irc_app_scroll_down (app, visible_rows, visible_rows);
      return GEM_IRC_APP_KEY_HANDLED;
    }
  if (scan == 0x4800U)
    {
      gem_irc_app_restore_last (app);
      return GEM_IRC_APP_KEY_HANDLED;
    }
  if (scan == 0x5000U)
    {
      app->input[0] = '\0';
      app->input_length = 0;
      app->input_cursor = 0;
      app->dirty |= GEM_IRC_APP_DIRTY_INPUT;
      return GEM_IRC_APP_KEY_HANDLED;
    }
  if (scan == 0x4b00U)
    {
      if (app->input_cursor)
	app->input_cursor--;
      app->dirty |= GEM_IRC_APP_DIRTY_INPUT;
      return GEM_IRC_APP_KEY_HANDLED;
    }
  if (scan == 0x4d00U)
    {
      if (app->input_cursor < app->input_length)
	app->input_cursor++;
      app->dirty |= GEM_IRC_APP_DIRTY_INPUT;
      return GEM_IRC_APP_KEY_HANDLED;
    }
  if (scan == 0x4700U)
    {
      app->input_cursor = 0;
      app->dirty |= GEM_IRC_APP_DIRTY_INPUT;
      return GEM_IRC_APP_KEY_HANDLED;
    }
  if (scan == 0x4f00U)
    {
      app->input_cursor = app->input_length;
      app->dirty |= GEM_IRC_APP_DIRTY_INPUT;
      return GEM_IRC_APP_KEY_HANDLED;
    }
  if (ascii == 8U)
    {
      if (app->input_cursor)
	gem_irc_app_delete_range (app, app->input_cursor - 1U,
				  app->input_cursor);
      return GEM_IRC_APP_KEY_HANDLED;
    }
  if (scan == 0x5300U)
    {
      if (app->input_cursor < app->input_length)
	gem_irc_app_delete_range (app, app->input_cursor,
				  app->input_cursor + 1U);
      return GEM_IRC_APP_KEY_HANDLED;
    }
  if (ascii == 21U || ascii == 27U)
    {
      gem_irc_app_delete_range (app, 0, app->input_length);
      return GEM_IRC_APP_KEY_HANDLED;
    }
  if (ascii == 23U)
    {
      start = app->input_cursor;
      while (start && app->input[start - 1U] == ' ')
	start--;
      while (start && app->input[start - 1U] != ' ')
	start--;
      gem_irc_app_delete_range (app, start, app->input_cursor);
      return GEM_IRC_APP_KEY_HANDLED;
    }
  if (ascii < 32U || ascii > 126U)
    return GEM_IRC_APP_KEY_UNHANDLED;
  if (app->input_length + 1U >= GEM_IRC_APP_INPUT_SIZE)
    {
      gem_irc_app_status (app, GEM_IRC_APP_LINE_ERROR,
			  "*** Input line is full", (const char *) 0,
			  (const char *) 0);
      return GEM_IRC_APP_KEY_HANDLED;
    }
  index = app->input_length;
  while (index > app->input_cursor)
    {
      app->input[index] = app->input[index - 1U];
      index--;
    }
  app->input[app->input_cursor] = (char) ascii;
  app->input_cursor++;
  app->input_length++;
  app->input[app->input_length] = '\0';
  app->dirty |= GEM_IRC_APP_DIRTY_INPUT;
  return GEM_IRC_APP_KEY_HANDLED;
}

static GEM_IRC_UWORD
gem_irc_app_max_scroll (const GEM_IRC_APP *app, GEM_IRC_UWORD visible_rows)
{
  if (!app || app->line_count <= visible_rows)
    return 0;
  return app->line_count - visible_rows;
}

void
gem_irc_app_scroll_up (GEM_IRC_APP *app, GEM_IRC_UWORD amount,
		       GEM_IRC_UWORD visible_rows)
{
  GEM_IRC_UWORD maximum;

  if (!app)
    return;
  maximum = gem_irc_app_max_scroll (app, visible_rows);
  while (amount && app->scroll_offset < maximum)
    {
      app->scroll_offset++;
      amount--;
    }
  app->dirty |= GEM_IRC_APP_DIRTY_TRANSCRIPT;
}

void
gem_irc_app_scroll_down (GEM_IRC_APP *app, GEM_IRC_UWORD amount,
			 GEM_IRC_UWORD visible_rows)
{
  if (!app)
    return;
  while (amount && app->scroll_offset)
    {
      app->scroll_offset--;
      amount--;
    }
  app->dirty |= GEM_IRC_APP_DIRTY_TRANSCRIPT;
  (void) visible_rows;
}

void
gem_irc_app_clear (GEM_IRC_APP *app)
{
  if (!app)
    return;
  app->line_head = 0;
  app->line_count = 0;
  app->scroll_offset = 0;
  app->dirty |= GEM_IRC_APP_DIRTY_TRANSCRIPT;
}

GEM_IRC_UWORD
gem_irc_app_line_count (const GEM_IRC_APP *app)
{
  return app ? app->line_count : 0;
}

GEM_IRC_UWORD
gem_irc_app_visible_count (const GEM_IRC_APP *app, GEM_IRC_UWORD visible_rows)
{
  if (!app)
    return 0;
  return app->line_count < visible_rows ? app->line_count : visible_rows;
}

const GEM_IRC_APP_LINE *
gem_irc_app_visible_line (const GEM_IRC_APP *app, GEM_IRC_UWORD row,
			  GEM_IRC_UWORD visible_rows)
{
  GEM_IRC_UWORD shown;
  GEM_IRC_UWORD start;
  GEM_IRC_UWORD scroll;
  GEM_IRC_UWORD slot;

  if (!app)
    return (const GEM_IRC_APP_LINE *) 0;
  shown = gem_irc_app_visible_count (app, visible_rows);
  if (row >= shown)
    return (const GEM_IRC_APP_LINE *) 0;
  start = app->line_count - shown;
  scroll = app->scroll_offset;
  if (scroll > start)
    scroll = start;
  start -= scroll;
  slot = app->line_head + start + row;
  while (slot >= GEM_IRC_APP_SCROLL_LINES)
    slot -= GEM_IRC_APP_SCROLL_LINES;
  return gem_irc_app_const_line_at (app, slot);
}

const char *
gem_irc_app_input (const GEM_IRC_APP *app)
{
  return app ? app->input : (const char *) 0;
}

GEM_IRC_UWORD
gem_irc_app_input_cursor (const GEM_IRC_APP *app)
{
  return app ? app->input_cursor : 0;
}

const char *
gem_irc_app_target (const GEM_IRC_APP *app)
{
  return app ? app->target : (const char *) 0;
}

GEM_IRC_WORD
gem_irc_app_is_closing (const GEM_IRC_APP *app)
{
  return app && app->closing ? 1 : 0;
}

GEM_IRC_WORD
gem_irc_app_take_dirty (GEM_IRC_APP *app)
{
  GEM_IRC_WORD dirty;

  if (!app)
    return 0;
  dirty = app->dirty;
  app->dirty = 0;
  return dirty;
}
