/*
 * Copyright (C) 2026 Goliath Keet <gatekeeper@xt-emporium.com>
 * SPDX-License-Identifier: GPL-2.0-only
 */

/*
 * gem_irc_app.c - native fixed-buffer GEM IRC application controller.
 *
 * This is the window-independent half of the client.  It turns protocol
 * callbacks into a set of bounded scrollback buffers, edits one bounded
 * command line, translates essential slash commands, and polls a
 * complete-line transport.  The GEM front end draws the active buffer and
 * forwards keys, clicks, and messages.
 *
 * Buffer zero is the permanent server/channel buffer and carries the
 * app-global roster.  A private message addressed to our own nick opens (or
 * reuses) one bounded PRIVATE buffer keyed by the sender, so channel and
 * private conversations never share a scrollback.  The active index selects
 * the drawn buffer; a tab bar in the front end switches it.
 *
 * Every counter, cursor, index, and return value is eight or sixteen bits.
 * Ring and buffer access use short pointer walks so ia16-gcc never needs a
 * variable structure multiply.  There is no allocation, recursion, formatted
 * output, division, remainder, multiplication, variable shift, or scalar
 * wider than one 8086 word.  A closed private buffer is compacted with one
 * plain structure assignment because a buffer holds no pointer.
 */

#include "gem_irc_app.h"

/*
 * A scrollback record is 97 bytes.  When this cold controller is compiled
 * with -Os, ia16 GCC otherwise replaces the deliberately bounded pointer
 * walk below with a variable 16-bit MUL.  One immediate ADD is both smaller
 * and much cheaper on an 8088.  The pointer and immediate are one 16-bit
 * word each; the thirty-two-record array is only 3104 bytes, so the near
 * pointer cannot wrap.  Host smoke tests retain ordinary C pointer walks.
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

/*
 * The same bounded-walk technique advances one whole conversation buffer.
 * A buffer is 3272 bytes; the five-buffer array is 16360 near bytes and the
 * immediate is one 16-bit word, so an ADDW cannot wrap the segment and no
 * variable multiply is emitted for buffers[index].
 */
#if defined(ELKS) && ELKS
#define GEM_IRC_APP_NEXT_BUFFER(buffer) \
	__asm__ volatile ("addw %1,%0" : "+r" (buffer) \
			  : "i" (sizeof(GEM_IRC_APP_BUFFER)) : "cc")
#else
#define GEM_IRC_APP_NEXT_BUFFER(buffer) ((buffer)++)
#endif

static GEM_IRC_APP_BUFFER *
gem_irc_app_buffer_ptr (GEM_IRC_APP *app, GEM_IRC_UWORD index)
{
  GEM_IRC_APP_BUFFER *buffer;

  buffer = app->buffers;
  while (index)
    {
      GEM_IRC_APP_NEXT_BUFFER (buffer);
      index--;
    }
  return buffer;
}

static const GEM_IRC_APP_BUFFER *
gem_irc_app_const_buffer_ptr (const GEM_IRC_APP *app, GEM_IRC_UWORD index)
{
  const GEM_IRC_APP_BUFFER *buffer;

  buffer = app->buffers;
  while (index)
    {
      GEM_IRC_APP_NEXT_BUFFER (buffer);
      index--;
    }
  return buffer;
}

static GEM_IRC_APP_BUFFER *
gem_irc_app_active_buffer (GEM_IRC_APP *app)
{
  return gem_irc_app_buffer_ptr (app, (GEM_IRC_UWORD) app->active);
}

static GEM_IRC_APP_LINE *
gem_irc_app_line_at (GEM_IRC_APP_BUFFER *buffer, GEM_IRC_UWORD index)
{
  GEM_IRC_APP_LINE *line;

  line = buffer->lines;
  while (index)
    {
      GEM_IRC_APP_NEXT_LINE (line);
      index--;
    }
  return line;
}

static const GEM_IRC_APP_LINE *
gem_irc_app_const_line_at (const GEM_IRC_APP_BUFFER *buffer,
			   GEM_IRC_UWORD index)
{
  const GEM_IRC_APP_LINE *line;

  line = buffer->lines;
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

/*
 * Append one bounded row to a specific buffer.  A row committed to the buffer
 * currently drawn dirties the transcript; a row committed to any other buffer
 * only raises that buffer's unread marker and dirties the tab bar, so a
 * background private message repaints one tab instead of the whole window.
 */
static void
gem_irc_app_append (GEM_IRC_APP *app, GEM_IRC_APP_BUFFER *buffer,
		    GEM_IRC_UBYTE kind, const char *text)
{
  GEM_IRC_APP_LINE *line;
  GEM_IRC_UWORD slot;

  if (!app || !buffer || !text)
    return;
  if (buffer->line_count < GEM_IRC_APP_SCROLL_LINES)
    {
      slot = buffer->line_head + buffer->line_count;
      if (slot >= GEM_IRC_APP_SCROLL_LINES)
	slot -= GEM_IRC_APP_SCROLL_LINES;
      buffer->line_count++;
    }
  else
    {
      slot = buffer->line_head;
      buffer->line_head++;
      if (buffer->line_head >= GEM_IRC_APP_SCROLL_LINES)
	buffer->line_head = 0;
    }
  line = gem_irc_app_line_at (buffer, slot);
  line->kind = kind;
  gem_irc_app_copy_clipped (line->text, GEM_IRC_APP_DISPLAY_SIZE, text);

  /* Keep an older viewport stable when new network text arrives. */
  if (buffer->scroll_offset
      && buffer->scroll_offset + 1U < GEM_IRC_APP_SCROLL_LINES)
    buffer->scroll_offset++;

  if (buffer == gem_irc_app_active_buffer (app))
    app->dirty |= GEM_IRC_APP_DIRTY_TRANSCRIPT;
  else
    {
      buffer->activity = 1;
      app->dirty |= GEM_IRC_APP_DIRTY_TABS;
    }
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
gem_irc_app_stream_text (GEM_IRC_APP *app, GEM_IRC_APP_BUFFER *buffer,
			 GEM_IRC_UBYTE kind, GEM_IRC_UWORD *length,
			 const char *text)
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
	  gem_irc_app_append (app, buffer, kind, app->format_line);
	  *length = 0;
	}
      app->format_line[*length] = *text++;
      (*length)++;
    }
}

static void
gem_irc_app_stream_finish (GEM_IRC_APP *app, GEM_IRC_APP_BUFFER *buffer,
			   GEM_IRC_UBYTE kind, GEM_IRC_UWORD length)
{
  if (!length)
    return;
  app->format_line[length] = '\0';
  gem_irc_app_append (app, buffer, kind, app->format_line);
}

static void
gem_irc_app_status (GEM_IRC_APP *app, GEM_IRC_APP_BUFFER *buffer,
		    GEM_IRC_UBYTE kind, const char *prefix,
		    const char *first, const char *second)
{
  GEM_IRC_UWORD length;

  length = 0;
  gem_irc_app_stream_text (app, buffer, kind, &length, prefix);
  gem_irc_app_stream_text (app, buffer, kind, &length, first);
  gem_irc_app_stream_text (app, buffer, kind, &length, second);
  gem_irc_app_stream_finish (app, buffer, kind, length);
}

/*
 * Network events often consist of several short fields.  Keep their assembly
 * in one shared routine: five optional views preserve useful IRC fields
 * while avoiding a copy of the same begin/append sequence in every switch
 * arm.  Each pointer is near in the ELKS small model.
 */
static void
gem_irc_app_event_line (GEM_IRC_APP *app, GEM_IRC_APP_BUFFER *buffer,
			GEM_IRC_UBYTE kind, const char *first,
			const char *second, const char *third,
			const char *fourth, const char *fifth)
{
  GEM_IRC_UWORD length;

  length = 0;
  gem_irc_app_stream_text (app, buffer, kind, &length, first);
  gem_irc_app_stream_text (app, buffer, kind, &length, second);
  gem_irc_app_stream_text (app, buffer, kind, &length, third);
  gem_irc_app_stream_text (app, buffer, kind, &length, fourth);
  gem_irc_app_stream_text (app, buffer, kind, &length, fifth);
  gem_irc_app_stream_finish (app, buffer, kind, length);
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

/* Reset one buffer to an empty ring of the requested kind. */
static void
gem_irc_app_buffer_reset (GEM_IRC_APP_BUFFER *buffer, GEM_IRC_UBYTE kind)
{
  buffer->kind = kind;
  buffer->activity = 0;
  buffer->line_head = 0;
  buffer->line_count = 0;
  buffer->scroll_offset = 0;
  buffer->name[0] = '\0';
  buffer->topic[0] = '\0';
}

/*
 * Find, or optionally open, the private buffer for one nick.  Buffer zero is
 * never a private target, so the search starts at one.  A full table returns
 * buffer zero, folding the overflow message back into the main transcript
 * instead of allocating.  The return is a scale-one buffer index.
 */
static GEM_IRC_UWORD
gem_irc_app_pm_index (GEM_IRC_APP *app, const char *nick, GEM_IRC_WORD create)
{
  GEM_IRC_UWORD index;
  GEM_IRC_APP_BUFFER *buffer;

  if (!nick || !*nick)
    return 0;
  index = 1;
  while (index < (GEM_IRC_UWORD) app->buffer_count)
    {
      buffer = gem_irc_app_buffer_ptr (app, index);
      if (buffer->kind == GEM_IRC_APP_BUFFER_PRIVATE
	  && gem_irc_equal (buffer->name, nick))
	return index;
      index++;
    }
  if (!create || app->buffer_count >= GEM_IRC_APP_BUFFERS)
    return 0;
  index = (GEM_IRC_UWORD) app->buffer_count;
  buffer = gem_irc_app_buffer_ptr (app, index);
  gem_irc_app_buffer_reset (buffer, GEM_IRC_APP_BUFFER_PRIVATE);
  (void) gem_irc_app_copy_exact (buffer->name, GEM_IRC_APP_TARGET_SIZE, nick);
  app->buffer_count++;
  app->dirty |= GEM_IRC_APP_DIRTY_TABS;
  return index;
}

static GEM_IRC_APP_BUFFER *
gem_irc_app_pm_buffer (GEM_IRC_APP *app, const char *nick, GEM_IRC_WORD create)
{
  return gem_irc_app_buffer_ptr (app, gem_irc_app_pm_index (app, nick, create));
}

/* Rename any open private buffer whose peer changed nick. */
static void
gem_irc_app_pm_rename (GEM_IRC_APP *app, const char *old_name,
		       const char *new_name)
{
  GEM_IRC_UWORD index;
  GEM_IRC_APP_BUFFER *buffer;

  if (!old_name || !new_name)
    return;
  index = 1;
  while (index < (GEM_IRC_UWORD) app->buffer_count)
    {
      buffer = gem_irc_app_buffer_ptr (app, index);
      if (buffer->kind == GEM_IRC_APP_BUFFER_PRIVATE
	  && gem_irc_equal (buffer->name, old_name))
	{
	  (void) gem_irc_app_copy_exact (buffer->name,
					 GEM_IRC_APP_TARGET_SIZE, new_name);
	  app->dirty |= GEM_IRC_APP_DIRTY_TABS;
	  return;
	}
      index++;
    }
}

/*
 * Decide which buffer receives one incoming message.  Channel-directed text
 * stays in buffer zero.  Text addressed to our own nick from a real user
 * opens or reuses that user's private buffer; anything else is main-buffer
 * status.
 */
static GEM_IRC_APP_BUFFER *
gem_irc_app_route (GEM_IRC_APP *app, const GEM_IRC_EVENT *event,
		   const char *source)
{
  if (event->target && gem_irc_app_is_channel (event->target))
    return gem_irc_app_buffer_ptr (app, 0);
  if (event->nick && *event->nick && app->protocol.nick[0]
      && event->target && gem_irc_equal (event->target, app->protocol.nick))
    return gem_irc_app_pm_buffer (app, source, 1);
  return gem_irc_app_buffer_ptr (app, 0);
}

/* Set or clear the channel identity carried by buffer zero. */
static GEM_IRC_WORD
gem_irc_app_channel_set (GEM_IRC_APP *app, const char *channel)
{
  GEM_IRC_APP_BUFFER *home;
  GEM_IRC_WORD result;

  home = gem_irc_app_buffer_ptr (app, 0);
  result = gem_irc_app_copy_exact (home->name, GEM_IRC_APP_TARGET_SIZE,
				   channel);
  if (result == GEM_IRC_OK)
    {
      home->kind = GEM_IRC_APP_BUFFER_CHANNEL;
      app->dirty |= GEM_IRC_APP_DIRTY_TARGET | GEM_IRC_APP_DIRTY_TOPIC
	| GEM_IRC_APP_DIRTY_TABS;
    }
  return result;
}

static void
gem_irc_app_channel_clear (GEM_IRC_APP *app)
{
  GEM_IRC_APP_BUFFER *home;

  home = gem_irc_app_buffer_ptr (app, 0);
  home->name[0] = '\0';
  home->topic[0] = '\0';
  home->kind = GEM_IRC_APP_BUFFER_SERVER;
  app->dirty |= GEM_IRC_APP_DIRTY_TARGET | GEM_IRC_APP_DIRTY_TOPIC
    | GEM_IRC_APP_DIRTY_TABS;
}

/* True when target names the channel currently carried by buffer zero. */
static GEM_IRC_WORD
gem_irc_app_is_home_channel (GEM_IRC_APP *app, const char *target)
{
  GEM_IRC_APP_BUFFER *home;

  home = gem_irc_app_buffer_ptr (app, 0);
  if (home->kind != GEM_IRC_APP_BUFFER_CHANNEL || !home->name[0])
    return 0;
  return target && gem_irc_equal (target, home->name);
}

static const char *
gem_irc_app_home_channel (GEM_IRC_APP *app)
{
  GEM_IRC_APP_BUFFER *home;

  home = gem_irc_app_buffer_ptr (app, 0);
  if (home->kind == GEM_IRC_APP_BUFFER_CHANNEL && home->name[0])
    return home->name;
  return "";
}

static GEM_IRC_APP_BUFFER *
gem_irc_app_echo_buffer (GEM_IRC_APP *app, const char *target)
{
  if (gem_irc_app_is_channel (target))
    return gem_irc_app_buffer_ptr (app, 0);
  return gem_irc_app_pm_buffer (app, target, 1);
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
  GEM_IRC_APP_BUFFER *home;
  GEM_IRC_APP_BUFFER *dest;
  const char *source;
  const char *text;
  GEM_IRC_WORD ours;

  app = (GEM_IRC_APP *) opaque;
  if (!app || !event)
    return;
  home = gem_irc_app_buffer_ptr (app, 0);
  source = gem_irc_app_source (event);
  text = event->text ? event->text : "";

  switch (event->type)
    {
    case GEM_IRC_EVENT_REGISTERED:
      app->connected = 1;
      app->dirty |= GEM_IRC_APP_DIRTY_TOPIC;
      gem_irc_app_status (app, home, GEM_IRC_APP_LINE_STATUS,
			  "*** ", text, (const char *) 0);
      break;
    case GEM_IRC_EVENT_PING:
      /* Successful automatic PONGs do not consume scarce scrollback. */
      break;
    case GEM_IRC_EVENT_MESSAGE:
      dest = gem_irc_app_route (app, event, source);
      ours = event->target && gem_irc_app_is_channel (event->target);
      gem_irc_app_event_line (app, dest, GEM_IRC_APP_LINE_NORMAL,
			      ours ? "<" : "[", source, ours ? "> " : "] ",
			      text, (const char *) 0);
      break;
    case GEM_IRC_EVENT_ACTION:
      dest = gem_irc_app_route (app, event, source);
      gem_irc_app_event_line (app, dest, GEM_IRC_APP_LINE_ACTION,
			      "* ", source, " ", text, (const char *) 0);
      break;
    case GEM_IRC_EVENT_CTCP:
      gem_irc_app_event_line (app, home, GEM_IRC_APP_LINE_STATUS,
			      "*** CTCP ", source, ": ", text,
			      (const char *) 0);
      break;
    case GEM_IRC_EVENT_NOTICE:
      dest = gem_irc_app_route (app, event, source);
      gem_irc_app_event_line (app, dest, GEM_IRC_APP_LINE_NOTICE,
			      "-", source, "- ", text, (const char *) 0);
      break;
    case GEM_IRC_EVENT_JOIN:
      gem_irc_app_event_line (app, home, GEM_IRC_APP_LINE_STATUS,
			      "*** ", source, " + ", event->target,
			      (const char *) 0);
      if (app->protocol.nick[0] && gem_irc_equal (source, app->protocol.nick))
	{
	  (void) gem_irc_app_channel_set (app, event->target);
	  gem_irc_app_roster_clear (app);
	  gem_irc_app_roster_add (app, source);
	}
      else if (gem_irc_app_is_home_channel (app, event->target))
	gem_irc_app_roster_add (app, source);
      break;
    case GEM_IRC_EVENT_PART:
      gem_irc_app_event_line (app, home, GEM_IRC_APP_LINE_STATUS,
			      "*** ", source, " left: ",
			      *text ? text : event->target, (const char *) 0);
      if (app->protocol.nick[0]
	  && gem_irc_equal (source, app->protocol.nick)
	  && gem_irc_app_is_home_channel (app, event->target))
	{
	  gem_irc_app_channel_clear (app);
	  gem_irc_app_roster_clear (app);
	}
      else if (gem_irc_app_is_home_channel (app, event->target))
	gem_irc_app_roster_remove (app, source);
      break;
    case GEM_IRC_EVENT_QUIT:
      gem_irc_app_event_line (app, home, GEM_IRC_APP_LINE_STATUS,
			      "*** ", source, " quit",
			      *text ? ": " : (const char *) 0,
			      *text ? text : (const char *) 0);
      gem_irc_app_roster_remove (app, source);
      break;
    case GEM_IRC_EVENT_NICK:
      gem_irc_app_event_line (app, home, GEM_IRC_APP_LINE_STATUS,
			      "*** ", source, " -> ", event->target,
			      (const char *) 0);
      gem_irc_app_pm_rename (app, source, event->target);
      gem_irc_app_roster_rename (app, source, event->target);
      break;
    case GEM_IRC_EVENT_TOPIC:
      gem_irc_app_event_line (app, home, GEM_IRC_APP_LINE_STATUS,
			      "*** Topic ", event->target, ": ", text,
			      (const char *) 0);
      if (gem_irc_app_is_home_channel (app, event->target))
	{
	  gem_irc_app_copy_clipped (home->topic, GEM_IRC_APP_DISPLAY_SIZE,
				    text);
	  app->dirty |= GEM_IRC_APP_DIRTY_TOPIC;
	}
      break;
    case GEM_IRC_EVENT_TOPIC_INFO:
      gem_irc_app_event_line (app, home, GEM_IRC_APP_LINE_STATUS,
			      "*** By ", text,
			      event->extra
			      && *event->extra ? " at " : (const char *) 0,
			      event->extra, (const char *) 0);
      break;
    case GEM_IRC_EVENT_NAMES:
      if (gem_irc_app_is_home_channel (app, event->target))
	gem_irc_app_roster_names (app, text);
      break;
    case GEM_IRC_EVENT_NAMES_END:
      app->dirty |= GEM_IRC_APP_DIRTY_ROSTER;
      break;
    case GEM_IRC_EVENT_KICK:
      gem_irc_app_event_line (app, home, GEM_IRC_APP_LINE_STATUS,
			      "*** ", event->extra, " kick ", source,
			      *text ? text : (const char *) 0);
      ours = event->extra && app->protocol.nick[0]
	&& gem_irc_equal (event->extra, app->protocol.nick);
      if (ours && gem_irc_app_is_home_channel (app, event->target))
	{
	  gem_irc_app_channel_clear (app);
	  gem_irc_app_roster_clear (app);
	}
      else if (gem_irc_app_is_home_channel (app, event->target))
	gem_irc_app_roster_remove (app, event->extra);
      break;
    case GEM_IRC_EVENT_MODE:
      gem_irc_app_event_line (app, home, GEM_IRC_APP_LINE_STATUS,
			      "*** ", source, " mode ", text,
			      (const char *) 0);
      break;
    case GEM_IRC_EVENT_NICK_IN_USE:
      gem_irc_app_status (app, home, GEM_IRC_APP_LINE_ERROR,
			  "*** Nick in use: ", event->target,
			  (const char *) 0);
      break;
    case GEM_IRC_EVENT_ERROR:
      gem_irc_app_status (app, home, GEM_IRC_APP_LINE_ERROR,
			  "*** Error: ", text, (const char *) 0);
      break;
    default:
      gem_irc_app_event_line (app, home, GEM_IRC_APP_LINE_STATUS,
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
  GEM_IRC_UWORD index;

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
  app->active = 0;
  app->buffer_count = 1;
  app->display_columns = GEM_IRC_APP_DISPLAY_SIZE - 1U;
  app->input_length = 0;
  app->input_cursor = 0;
  app->nick_count = 0;
  app->nick_overflow = 0;
  app->input[0] = '\0';
  app->last_input[0] = '\0';
  app->network_line[0] = '\0';
  app->command_line[0] = '\0';
  app->format_line[0] = '\0';
  index = 0;
  while (index < GEM_IRC_APP_BUFFERS)
    {
      gem_irc_app_buffer_reset (gem_irc_app_buffer_ptr (app, index),
			       GEM_IRC_APP_BUFFER_FREE);
      index++;
    }
  gem_irc_app_buffer_reset (gem_irc_app_buffer_ptr (app, 0),
			   GEM_IRC_APP_BUFFER_SERVER);
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
  gem_irc_app_status (app, gem_irc_app_buffer_ptr (app, 0),
		      GEM_IRC_APP_LINE_STATUS,
		      "*** Connecting to ", host ? host : "server",
		      (const char *) 0);
}

void
gem_irc_app_connection_failed (GEM_IRC_APP *app)
{
  if (!app)
    return;
  app->connected = 0;
  app->dirty |= GEM_IRC_APP_DIRTY_TOPIC;
  gem_irc_app_roster_clear (app);
  gem_irc_app_status (app, gem_irc_app_buffer_ptr (app, 0),
		      GEM_IRC_APP_LINE_ERROR,
		      "*** Connection failed; retrying", (const char *) 0,
		      (const char *) 0);
}

GEM_IRC_WORD
gem_irc_app_start (GEM_IRC_APP *app, const char *nick, const char *user,
		   const char *real_name)
{
  GEM_IRC_APP_BUFFER *home;
  GEM_IRC_WORD result;

  if (!app || !nick || !user || !real_name)
    return GEM_IRC_BAD_ARGUMENT;
  home = gem_irc_app_buffer_ptr (app, 0);
  gem_irc_app_status (app, home, GEM_IRC_APP_LINE_STATUS,
		      "*** Registering as ", nick, (const char *) 0);
  result = gem_irc_send_nick (&app->protocol, nick);
  if (result <= 0)
    {
      gem_irc_app_status (app, home, GEM_IRC_APP_LINE_ERROR,
			  "*** Connection write failed", (const char *) 0,
			  (const char *) 0);
      return result;
    }
  result = gem_irc_send_user (&app->protocol, user, real_name);
  if (result > 0)
    app->connected = 1;
  else
    gem_irc_app_status (app, home, GEM_IRC_APP_LINE_ERROR,
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
   * this point, into the buffer that owns this target, so a rejected write
   * can be retried without a false line and an accepted one is latched once.
   */
  gem_irc_app_event_line (app, gem_irc_app_echo_buffer (app, target),
			  GEM_IRC_APP_LINE_NORMAL,
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
    gem_irc_app_status (app, gem_irc_app_buffer_ptr (app, 0),
			GEM_IRC_APP_LINE_ERROR,
			"*** Rejected IRC line", (const char *) 0,
			(const char *) 0);
  else if (result == GEM_IRC_OUTPUT_REJECTED)
    gem_irc_app_status (app, gem_irc_app_buffer_ptr (app, 0),
			GEM_IRC_APP_LINE_ERROR,
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
	      app->dirty |= GEM_IRC_APP_DIRTY_TOPIC;
	      gem_irc_app_roster_clear (app);
	      gem_irc_connection_reset (&app->protocol);
	      if (app->transport.close)
		app->transport.close (app->transport.context);
	      gem_irc_app_status (app, gem_irc_app_buffer_ptr (app, 0),
				  GEM_IRC_APP_LINE_ERROR,
				  "*** Connection closed", (const char *) 0,
				  (const char *) 0);
	    }
	  return result;
	}
      if (!length || length > GEM_IRC_LINE_MAX)
	{
	  gem_irc_app_status (app, gem_irc_app_buffer_ptr (app, 0),
			      GEM_IRC_APP_LINE_ERROR,
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
gem_irc_app_help (GEM_IRC_APP *app, GEM_IRC_APP_BUFFER *view)
{
  gem_irc_app_append (app, view, GEM_IRC_APP_LINE_STATUS,
		      "*** /join /part /query /msg /notice /me /nick");
  gem_irc_app_append (app, view, GEM_IRC_APP_LINE_STATUS,
		      "*** /whois /names /topic /mode /kick /away");
  gem_irc_app_append (app, view, GEM_IRC_APP_LINE_STATUS,
		      "*** /close /raw /clear /quit /help");
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
gem_irc_app_send_failure (GEM_IRC_APP *app, GEM_IRC_APP_BUFFER *view,
			  GEM_IRC_WORD result)
{
  if (result == GEM_IRC_LINE_TOO_LONG)
    gem_irc_app_status (app, view, GEM_IRC_APP_LINE_ERROR,
			"*** Command exceeds IRC line limit",
			(const char *) 0, (const char *) 0);
  else if (result == GEM_IRC_MALFORMED)
    gem_irc_app_status (app, view, GEM_IRC_APP_LINE_ERROR,
			"*** Invalid command characters", (const char *) 0,
			(const char *) 0);
  else
    gem_irc_app_status (app, view, GEM_IRC_APP_LINE_ERROR,
			"*** Transport did not accept command",
			(const char *) 0, (const char *) 0);
  return result;
}

GEM_IRC_WORD
gem_irc_app_submit (GEM_IRC_APP *app)
{
  GEM_IRC_APP_BUFFER *view;
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
  view = gem_irc_app_active_buffer (app);
  if (gem_irc_app_copy_exact (app->command_line,
			      GEM_IRC_APP_INPUT_SIZE,
			      app->input) != GEM_IRC_OK)
    return GEM_IRC_LINE_TOO_LONG;

  if (app->command_line[0] != '/')
    {
      target = view->name;
      if (!target[0])
	{
	  gem_irc_app_status (app, view, GEM_IRC_APP_LINE_ERROR,
			      "*** Join or query a target first",
			      (const char *) 0, (const char *) 0);
	  return GEM_IRC_BAD_ARGUMENT;
	}
      result = gem_irc_app_send_message (app, target, app->input);
      if (result <= 0)
	return gem_irc_app_send_failure (app, view, result);
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
	  target = gem_irc_app_home_channel (app);
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
	gem_irc_app_event_line (app, gem_irc_app_echo_buffer (app, first),
				GEM_IRC_APP_LINE_NORMAL,
				"<", gem_irc_current_nick (&app->protocol),
				"> ", cursor, (const char *) 0);
    }
  else if (gem_irc_equal (command, "notice"))
    {
      first = gem_irc_app_word (&cursor);
      result = gem_irc_send_notice (&app->protocol, first, cursor);
    }
  else if (gem_irc_equal (command, "me"))
    {
      result = gem_irc_send_action (&app->protocol, view->name, arguments);
      if (result > 0)
	gem_irc_app_event_line (app, view, GEM_IRC_APP_LINE_ACTION,
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
      result = gem_irc_app_query (app, first);
      local = 1;
    }
  else if (gem_irc_equal (command, "close"))
    {
      result = gem_irc_app_close_buffer (app,
					 (GEM_IRC_UWORD) app->active);
      if (result <= 0)
	result = gem_irc_app_request_close (app, (const char *) 0);
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
      target = first ? first : gem_irc_app_home_channel (app);
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
	  target = gem_irc_app_home_channel (app);
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
	target = gem_irc_app_home_channel (app);
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
	target = gem_irc_app_home_channel (app);
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
      gem_irc_app_help (app, view);
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
      gem_irc_app_status (app, view, GEM_IRC_APP_LINE_ERROR,
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
  return gem_irc_app_send_failure (app, view, result);
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
  /* Ctrl-N and Ctrl-P step the tab bar without leaving the keyboard. */
  if (ascii == 14U)
    {
      gem_irc_app_cycle (app, 1);
      return GEM_IRC_APP_KEY_HANDLED;
    }
  if (ascii == 16U)
    {
      gem_irc_app_cycle (app, -1);
      return GEM_IRC_APP_KEY_HANDLED;
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
      gem_irc_app_status (app, gem_irc_app_active_buffer (app),
			  GEM_IRC_APP_LINE_ERROR,
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
gem_irc_app_max_scroll (const GEM_IRC_APP_BUFFER *buffer,
			GEM_IRC_UWORD visible_rows)
{
  if (!buffer || buffer->line_count <= visible_rows)
    return 0;
  return buffer->line_count - visible_rows;
}

void
gem_irc_app_scroll_up (GEM_IRC_APP *app, GEM_IRC_UWORD amount,
		       GEM_IRC_UWORD visible_rows)
{
  GEM_IRC_APP_BUFFER *buffer;
  GEM_IRC_UWORD maximum;

  if (!app)
    return;
  buffer = gem_irc_app_active_buffer (app);
  maximum = gem_irc_app_max_scroll (buffer, visible_rows);
  while (amount && buffer->scroll_offset < maximum)
    {
      buffer->scroll_offset++;
      amount--;
    }
  app->dirty |= GEM_IRC_APP_DIRTY_TRANSCRIPT;
}

void
gem_irc_app_scroll_down (GEM_IRC_APP *app, GEM_IRC_UWORD amount,
			 GEM_IRC_UWORD visible_rows)
{
  GEM_IRC_APP_BUFFER *buffer;

  if (!app)
    return;
  buffer = gem_irc_app_active_buffer (app);
  while (amount && buffer->scroll_offset)
    {
      buffer->scroll_offset--;
      amount--;
    }
  app->dirty |= GEM_IRC_APP_DIRTY_TRANSCRIPT;
  (void) visible_rows;
}

void
gem_irc_app_clear (GEM_IRC_APP *app)
{
  GEM_IRC_APP_BUFFER *buffer;

  if (!app)
    return;
  buffer = gem_irc_app_active_buffer (app);
  buffer->line_head = 0;
  buffer->line_count = 0;
  buffer->scroll_offset = 0;
  app->dirty |= GEM_IRC_APP_DIRTY_TRANSCRIPT;
}

GEM_IRC_UWORD
gem_irc_app_line_count (const GEM_IRC_APP *app)
{
  const GEM_IRC_APP_BUFFER *buffer;

  if (!app)
    return 0;
  buffer = gem_irc_app_const_buffer_ptr (app, (GEM_IRC_UWORD) app->active);
  return buffer->line_count;
}

GEM_IRC_UWORD
gem_irc_app_visible_count (const GEM_IRC_APP *app, GEM_IRC_UWORD visible_rows)
{
  const GEM_IRC_APP_BUFFER *buffer;

  if (!app)
    return 0;
  buffer = gem_irc_app_const_buffer_ptr (app, (GEM_IRC_UWORD) app->active);
  return buffer->line_count < visible_rows ? buffer->line_count : visible_rows;
}

const GEM_IRC_APP_LINE *
gem_irc_app_visible_line (const GEM_IRC_APP *app, GEM_IRC_UWORD row,
			  GEM_IRC_UWORD visible_rows)
{
  const GEM_IRC_APP_BUFFER *buffer;
  GEM_IRC_UWORD shown;
  GEM_IRC_UWORD start;
  GEM_IRC_UWORD scroll;
  GEM_IRC_UWORD slot;

  if (!app)
    return (const GEM_IRC_APP_LINE *) 0;
  buffer = gem_irc_app_const_buffer_ptr (app, (GEM_IRC_UWORD) app->active);
  shown = buffer->line_count < visible_rows ? buffer->line_count : visible_rows;
  if (row >= shown)
    return (const GEM_IRC_APP_LINE *) 0;
  start = buffer->line_count - shown;
  scroll = buffer->scroll_offset;
  if (scroll > start)
    scroll = start;
  start -= scroll;
  slot = buffer->line_head + start + row;
  while (slot >= GEM_IRC_APP_SCROLL_LINES)
    slot -= GEM_IRC_APP_SCROLL_LINES;
  return gem_irc_app_const_line_at (buffer, slot);
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
  const GEM_IRC_APP_BUFFER *buffer;

  if (!app)
    return (const char *) 0;
  buffer = gem_irc_app_const_buffer_ptr (app, (GEM_IRC_UWORD) app->active);
  return buffer->name;
}

const char *
gem_irc_app_topic (const GEM_IRC_APP *app)
{
  const GEM_IRC_APP_BUFFER *buffer;

  if (!app)
    return (const char *) 0;
  buffer = gem_irc_app_const_buffer_ptr (app, (GEM_IRC_UWORD) app->active);
  return buffer->topic;
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

GEM_IRC_UWORD
gem_irc_app_buffer_count (const GEM_IRC_APP *app)
{
  return app ? (GEM_IRC_UWORD) app->buffer_count : 0;
}

GEM_IRC_UWORD
gem_irc_app_active_index (const GEM_IRC_APP *app)
{
  return app ? (GEM_IRC_UWORD) app->active : 0;
}

GEM_IRC_UWORD
gem_irc_app_active_kind (const GEM_IRC_APP *app)
{
  const GEM_IRC_APP_BUFFER *buffer;

  if (!app)
    return GEM_IRC_APP_BUFFER_FREE;
  buffer = gem_irc_app_const_buffer_ptr (app, (GEM_IRC_UWORD) app->active);
  return buffer->kind;
}

const char *
gem_irc_app_buffer_label (const GEM_IRC_APP *app, GEM_IRC_UWORD index)
{
  const GEM_IRC_APP_BUFFER *buffer;

  if (!app || index >= (GEM_IRC_UWORD) app->buffer_count)
    return "";
  buffer = gem_irc_app_const_buffer_ptr (app, index);
  if (buffer->name[0])
    return buffer->name;
  return "Server";
}

GEM_IRC_UWORD
gem_irc_app_buffer_kind (const GEM_IRC_APP *app, GEM_IRC_UWORD index)
{
  if (!app || index >= (GEM_IRC_UWORD) app->buffer_count)
    return GEM_IRC_APP_BUFFER_FREE;
  return gem_irc_app_const_buffer_ptr (app, index)->kind;
}

GEM_IRC_UWORD
gem_irc_app_buffer_activity (const GEM_IRC_APP *app, GEM_IRC_UWORD index)
{
  if (!app || index >= (GEM_IRC_UWORD) app->buffer_count)
    return 0;
  return gem_irc_app_const_buffer_ptr (app, index)->activity;
}

void
gem_irc_app_switch (GEM_IRC_APP *app, GEM_IRC_UWORD index)
{
  GEM_IRC_APP_BUFFER *buffer;

  if (!app || index >= (GEM_IRC_UWORD) app->buffer_count)
    return;
  buffer = gem_irc_app_buffer_ptr (app, index);
  buffer->activity = 0;
  app->active = (GEM_IRC_UBYTE) index;
  /* A new active buffer repaints its transcript, panes, and every tab. */
  app->dirty |= GEM_IRC_APP_DIRTY_ALL;
}

void
gem_irc_app_cycle (GEM_IRC_APP *app, GEM_IRC_WORD direction)
{
  GEM_IRC_UWORD index;

  if (!app || app->buffer_count <= 1)
    return;
  index = (GEM_IRC_UWORD) app->active;
  if (direction < 0)
    {
      if (index == 0)
	index = (GEM_IRC_UWORD) app->buffer_count;
      index--;
    }
  else
    {
      index++;
      if (index >= (GEM_IRC_UWORD) app->buffer_count)
	index = 0;
    }
  gem_irc_app_switch (app, index);
}

GEM_IRC_WORD
gem_irc_app_close_buffer (GEM_IRC_APP *app, GEM_IRC_UWORD index)
{
  GEM_IRC_UWORD slot;
  GEM_IRC_APP_BUFFER *destination;
  GEM_IRC_APP_BUFFER *source;

  if (!app)
    return GEM_IRC_BAD_ARGUMENT;
  /* Buffer zero is the permanent server/channel view and is never closed. */
  if (index == 0 || index >= (GEM_IRC_UWORD) app->buffer_count)
    return 0;
  /*
   * Compact the table so the tab bar stays contiguous.  A buffer holds no
   * pointer, so one plain structure assignment moves each higher buffer down
   * a slot; the vacated top slot is reset to FREE.
   */
  slot = index;
  while (slot + 1U < (GEM_IRC_UWORD) app->buffer_count)
    {
      destination = gem_irc_app_buffer_ptr (app, slot);
      source = gem_irc_app_buffer_ptr (app, slot + 1U);
      *destination = *source;
      slot++;
    }
  gem_irc_app_buffer_reset (gem_irc_app_buffer_ptr (app, slot),
			   GEM_IRC_APP_BUFFER_FREE);
  app->buffer_count--;
  if ((GEM_IRC_UWORD) app->active >= index)
    {
      if (app->active > 0)
	app->active--;
    }
  if ((GEM_IRC_UWORD) app->active >= (GEM_IRC_UWORD) app->buffer_count)
    app->active = (GEM_IRC_UBYTE) (app->buffer_count - 1U);
  gem_irc_app_buffer_ptr (app, (GEM_IRC_UWORD) app->active)->activity = 0;
  app->dirty |= GEM_IRC_APP_DIRTY_ALL;
  return GEM_IRC_OK;
}

GEM_IRC_WORD
gem_irc_app_query (GEM_IRC_APP *app, const char *nick)
{
  GEM_IRC_UWORD index;
  GEM_IRC_APP_BUFFER *buffer;

  if (!app || !nick || !*nick || gem_irc_app_is_channel (nick))
    return GEM_IRC_BAD_ARGUMENT;
  index = gem_irc_app_pm_index (app, nick, 1);
  if (index == 0)
    {
      gem_irc_app_status (app, gem_irc_app_buffer_ptr (app, 0),
			  GEM_IRC_APP_LINE_ERROR,
			  "*** No free private tab", (const char *) 0,
			  (const char *) 0);
      return GEM_IRC_BAD_ARGUMENT;
    }
  buffer = gem_irc_app_buffer_ptr (app, index);
  gem_irc_app_status (app, buffer, GEM_IRC_APP_LINE_STATUS,
		      "*** Private chat with ", nick, (const char *) 0);
  gem_irc_app_switch (app, index);
  return GEM_IRC_OK;
}

GEM_IRC_WORD
gem_irc_app_part_active (GEM_IRC_APP *app)
{
  GEM_IRC_APP_BUFFER *view;

  if (!app)
    return GEM_IRC_BAD_ARGUMENT;
  view = gem_irc_app_active_buffer (app);
  if (view->kind == GEM_IRC_APP_BUFFER_CHANNEL && view->name[0])
    return gem_irc_send_part (&app->protocol, view->name, (const char *) 0);
  /* On a private tab the same control simply closes the conversation. */
  if (app->active != 0)
    return gem_irc_app_close_buffer (app, (GEM_IRC_UWORD) app->active);
  return GEM_IRC_BAD_ARGUMENT;
}

GEM_IRC_WORD
gem_irc_app_names (GEM_IRC_APP *app)
{
  const char *channel;

  if (!app)
    return GEM_IRC_BAD_ARGUMENT;
  channel = gem_irc_app_home_channel (app);
  if (!*channel)
    return GEM_IRC_BAD_ARGUMENT;
  return gem_irc_app_raw_target (app, "NAMES", channel);
}
