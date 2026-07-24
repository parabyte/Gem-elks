/*
 * Copyright (C) 2026 Goliath Keet <gatekeeper@xt-emporium.com>
 * SPDX-License-Identifier: GPL-2.0-only
 */

/*
 * gem_irc_protocol.c - native bounded IRC line engine for GEM on ELKS.
 *
 * This module deliberately stops at two callbacks: one complete outbound IRC
 * line and one parsed UI event.  ELKS owns the TCP descriptor and AES owns the
 * window/event loop, so duplicating either system here would waste near data
 * and add avoidable context switches on an 8088.  All protocol work uses byte
 * walks, pointer increments, compares, and 16-bit additions.  There is no
 * allocation, recursion, division, remainder, multiplication, variable shift,
 * formatted output, or scalar wider than one 8086 word.
 */

#include "gem_irc_protocol.h"

static const GEM_IRC_UWORD gem_irc_hundreds[10] = {
  0U, 100U, 200U, 300U, 400U, 500U, 600U, 700U, 800U, 900U
};

static const GEM_IRC_UWORD gem_irc_tens[10] = {
  0U, 10U, 20U, 30U, 40U, 50U, 60U, 70U, 80U, 90U
};

/* RFC1459 case folding used when deciding whether our own nick changed. */
static char
gem_irc_fold_char (char value)
{
  if (value >= 'A' && value <= 'Z')
    return (char) (value + ('a' - 'A'));
  if (value == '[')
    return '{';
  if (value == '\\')
    return '|';
  if (value == ']')
    return '}';
  if (value == '^')
    return '~';
  return value;
}

GEM_IRC_WORD
gem_irc_equal (const char *left, const char *right)
{
  if (!left || !right)
    return 0;
  while (*left && *right)
    {
      /* The overwhelmingly common uppercase match avoids two calls. */
      if (*left != *right
	  && gem_irc_fold_char (*left) != gem_irc_fold_char (*right))
	return 0;
      left++;
      right++;
    }
  return *left == '\0' && *right == '\0';
}

static GEM_IRC_WORD
gem_irc_starts_with (const char *text, const char *word)
{
  if (!text || !word)
    return 0;
  while (*word)
    {
      if (!*text || gem_irc_fold_char (*text) != gem_irc_fold_char (*word))
	return 0;
      text++;
      word++;
    }
  return 1;
}

/*
 * Store an accepted nick in the fixed state slot.  Server supplied nicks
 * longer than 31 bytes remain visible in their event but do not corrupt or
 * truncate the current-nick state.
 */
static GEM_IRC_WORD
gem_irc_store_nick (GEM_IRC_CLIENT *client, const char *nick)
{
  GEM_IRC_UWORD index;

  if (!client || !nick || !*nick)
    return GEM_IRC_BAD_ARGUMENT;
  index = 0;
  while (nick[index])
    {
      if (index >= GEM_IRC_NICK_SIZE - 1U)
	return GEM_IRC_LINE_TOO_LONG;
      index++;
    }
  index = 0;
  while (nick[index])
    {
      client->nick[index] = nick[index];
      index++;
    }
  client->nick[index] = '\0';
  return GEM_IRC_OK;
}

static GEM_IRC_WORD
gem_irc_append_char (GEM_IRC_CLIENT *client, GEM_IRC_UWORD *length,
		     char value)
{
  if (!client || !length)
    return GEM_IRC_BAD_ARGUMENT;
  if (*length >= GEM_IRC_PAYLOAD_MAX)
    return GEM_IRC_LINE_TOO_LONG;
  client->transmit_line[*length] = value;
  (*length)++;
  return GEM_IRC_OK;
}

/* Append user text while rejecting the two IRC command-injection bytes. */
static GEM_IRC_WORD
gem_irc_append_text (GEM_IRC_CLIENT *client, GEM_IRC_UWORD *length,
		     const char *text)
{
  if (!client || !length || !text)
    return GEM_IRC_BAD_ARGUMENT;
  while (*text)
    {
      if (*text == '\r' || *text == '\n')
	return GEM_IRC_MALFORMED;
      if (*length >= GEM_IRC_PAYLOAD_MAX)
	return GEM_IRC_LINE_TOO_LONG;
      client->transmit_line[*length] = *text++;
      (*length)++;
    }
  return GEM_IRC_OK;
}

/*
 * IRC middle parameters may not contain controls, spaces, or a colon.  This
 * validation covers nicks, targets, channel/key lists, and USER names before
 * one of them can alter the wire grammar.
 */
static GEM_IRC_WORD
gem_irc_append_middle (GEM_IRC_CLIENT *client, GEM_IRC_UWORD *length,
		       const char *text)
{
  GEM_IRC_UBYTE value;

  if (!client || !length || !text || !*text)
    return GEM_IRC_BAD_ARGUMENT;
  while (*text)
    {
      value = (GEM_IRC_UBYTE) * text;
      if (value <= 32U || *text == ':' || *text == '\r' || *text == '\n')
	return GEM_IRC_MALFORMED;
      if (*length >= GEM_IRC_PAYLOAD_MAX)
	return GEM_IRC_LINE_TOO_LONG;
      client->transmit_line[*length] = *text++;
      (*length)++;
    }
  return GEM_IRC_OK;
}

static GEM_IRC_WORD
gem_irc_begin (GEM_IRC_CLIENT *client, GEM_IRC_UWORD *length,
	       const char *command)
{
  if (!client || !length || !command || !*command)
    return GEM_IRC_BAD_ARGUMENT;
  *length = 0;
  client->transmit_line[0] = '\0';
  return gem_irc_append_text (client, length, command);
}

static GEM_IRC_WORD
gem_irc_add_middle (GEM_IRC_CLIENT *client, GEM_IRC_UWORD *length,
		    const char *parameter)
{
  GEM_IRC_WORD result;

  result = gem_irc_append_char (client, length, ' ');
  if (result != GEM_IRC_OK)
    return result;
  return gem_irc_append_middle (client, length, parameter);
}

static GEM_IRC_WORD
gem_irc_add_text (GEM_IRC_CLIENT *client, GEM_IRC_UWORD *length,
		  const char *parameter)
{
  GEM_IRC_WORD result;

  result = gem_irc_append_char (client, length, ' ');
  if (result != GEM_IRC_OK)
    return result;
  return gem_irc_append_text (client, length, parameter);
}

static GEM_IRC_WORD
gem_irc_add_trailing (GEM_IRC_CLIENT *client, GEM_IRC_UWORD *length,
		      const char *parameter)
{
  GEM_IRC_WORD result;

  result = gem_irc_append_char (client, length, ' ');
  if (result == GEM_IRC_OK)
    result = gem_irc_append_char (client, length, ':');
  if (result != GEM_IRC_OK)
    return result;
  return gem_irc_append_text (client, length, parameter);
}

/*
 * Add CR/LF after at most 510 payload bytes.  The two increments can reach
 * 512 but cannot overflow a 16-bit word; byte 512 is the private terminator.
 */
static GEM_IRC_WORD
gem_irc_finish (GEM_IRC_CLIENT *client, GEM_IRC_UWORD length)
{
  if (!client)
    return GEM_IRC_BAD_ARGUMENT;
  if (!client->output)
    return GEM_IRC_NO_OUTPUT;
  if (!length)
    return GEM_IRC_MALFORMED;
  if (length > GEM_IRC_PAYLOAD_MAX)
    return GEM_IRC_LINE_TOO_LONG;
  client->transmit_line[length++] = '\r';
  client->transmit_line[length++] = '\n';
  client->transmit_line[length] = '\0';
  return client->output (client->opaque, client->transmit_line, length);
}

static GEM_IRC_WORD
gem_irc_send_one_middle (GEM_IRC_CLIENT *client, const char *command,
			 const char *parameter)
{
  GEM_IRC_UWORD length;
  GEM_IRC_WORD result;

  result = gem_irc_begin (client, &length, command);
  if (result == GEM_IRC_OK)
    result = gem_irc_add_middle (client, &length, parameter);
  if (result != GEM_IRC_OK)
    return result;
  return gem_irc_finish (client, length);
}

static GEM_IRC_WORD
gem_irc_send_target_trailing (GEM_IRC_CLIENT *client, const char *command,
			      const char *target, const char *text)
{
  GEM_IRC_UWORD length;
  GEM_IRC_WORD result;

  if (!text)
    return GEM_IRC_BAD_ARGUMENT;
  result = gem_irc_begin (client, &length, command);
  if (result == GEM_IRC_OK)
    result = gem_irc_add_middle (client, &length, target);
  if (result == GEM_IRC_OK)
    result = gem_irc_add_trailing (client, &length, text);
  if (result != GEM_IRC_OK)
    return result;
  return gem_irc_finish (client, length);
}

static GEM_IRC_WORD
gem_irc_send_pong (GEM_IRC_CLIENT *client,
		   const char *const *parameters, GEM_IRC_UWORD count)
{
  GEM_IRC_UWORD length;
  GEM_IRC_WORD result;

  if (!parameters || !count || count > 2U)
    return GEM_IRC_MALFORMED;
  result = gem_irc_begin (client, &length, "PONG");
  if (result == GEM_IRC_OK && count == 2U)
    result = gem_irc_add_middle (client, &length, parameters[0]);
  if (result == GEM_IRC_OK)
    result = gem_irc_add_trailing (client, &length, parameters[count - 1U]);
  if (result != GEM_IRC_OK)
    return result;
  return gem_irc_finish (client, length);
}

void
gem_irc_init (GEM_IRC_CLIENT *client, GEM_IRC_OUTPUT_CALLBACK output,
	      GEM_IRC_EVENT_CALLBACK event, void *opaque)
{
  if (!client)
    return;
  client->output = output;
  client->event = event;
  client->opaque = opaque;
  client->registered = 0;
  client->receiving = 0;
  client->nick[0] = '\0';
  client->receive_line[0] = '\0';
  client->transmit_line[0] = '\0';
}

void
gem_irc_set_callbacks (GEM_IRC_CLIENT *client,
		       GEM_IRC_OUTPUT_CALLBACK output,
		       GEM_IRC_EVENT_CALLBACK event, void *opaque)
{
  if (!client)
    return;
  client->output = output;
  client->event = event;
  client->opaque = opaque;
}

void
gem_irc_connection_reset (GEM_IRC_CLIENT *client)
{
  /* Do not let a synchronous event callback enable recursive line parsing. */
  if (!client || client->receiving)
    return;
  client->registered = 0;
  client->receiving = 0;
  client->receive_line[0] = '\0';
  client->transmit_line[0] = '\0';
}

GEM_IRC_WORD
gem_irc_is_registered (const GEM_IRC_CLIENT *client)
{
  return client && client->registered ? 1 : 0;
}

const char *
gem_irc_current_nick (const GEM_IRC_CLIENT *client)
{
  if (!client)
    return (const char *) 0;
  return client->nick;
}

static void
gem_irc_prepare_event (GEM_IRC_EVENT *event, char *tags, char *command,
		       char *prefix, const char *const *parameters,
		       GEM_IRC_UWORD parameter_count)
{
  char *cursor;

  event->type = GEM_IRC_EVENT_STATUS;
  event->numeric = 0;
  event->parameter_count = parameter_count;
  event->tags = tags;
  event->command = command;
  event->source = prefix;
  event->nick = prefix;
  event->user = (const char *) 0;
  event->host = (const char *) 0;
  event->target = (const char *) 0;
  event->text = (const char *) 0;
  event->extra = (const char *) 0;
  event->parameters = parameters;

  /* Split :nick!user@host in place using only bounded pointer walks. */
  if (!prefix)
    return;
  cursor = prefix;
  while (*cursor && *cursor != '!' && *cursor != '@')
    cursor++;
  if (*cursor == '!')
    {
      *cursor++ = '\0';
      event->user = cursor;
      while (*cursor && *cursor != '@')
	cursor++;
      if (*cursor == '@')
	{
	  *cursor++ = '\0';
	  event->host = cursor;
	}
    }
  else if (*cursor == '@')
    {
      *cursor++ = '\0';
      event->host = cursor;
    }
}

static void
gem_irc_emit (GEM_IRC_CLIENT *client, GEM_IRC_EVENT *event)
{
  if (client->event)
    client->event (client->opaque, event);
}

static GEM_IRC_UWORD
gem_irc_numeric (const char *command)
{
  GEM_IRC_UWORD first;
  GEM_IRC_UWORD second;
  GEM_IRC_UWORD third;

  first = (GEM_IRC_UWORD) (command[0] - '0');
  second = (GEM_IRC_UWORD) (command[1] - '0');
  third = (GEM_IRC_UWORD) (command[2] - '0');
  return (GEM_IRC_UWORD) (gem_irc_hundreds[first]
			  + gem_irc_tens[second] + third);
}

static GEM_IRC_WORD
gem_irc_dispatch (GEM_IRC_CLIENT *client, GEM_IRC_EVENT *event,
		  const char *const *parameters, GEM_IRC_UWORD count)
{
  GEM_IRC_UWORD numeric;
  GEM_IRC_UWORD text_length;
  GEM_IRC_WORD our_nick;
  GEM_IRC_WORD result;
  char *ctcp;

  if (event->command[0] >= '0' && event->command[0] <= '9'
      && event->command[1] >= '0' && event->command[1] <= '9'
      && event->command[2] >= '0' && event->command[2] <= '9'
      && event->command[3] == '\0')
    {
      numeric = gem_irc_numeric (event->command);
      event->numeric = numeric;
      if (numeric == 1U)
	{
	  if (!count)
	    return GEM_IRC_MALFORMED;
	  client->registered = 1;
	  (void) gem_irc_store_nick (client, parameters[0]);
	  event->type = GEM_IRC_EVENT_REGISTERED;
	  event->target = parameters[0];
	  event->text = parameters[count - 1U];
	}
      else if (numeric == 332U)
	{
	  if (count < 3U)
	    return GEM_IRC_MALFORMED;
	  event->type = GEM_IRC_EVENT_TOPIC;
	  event->target = parameters[1];
	  event->text = parameters[2];
	}
      else if (numeric == 333U)
	{
	  if (count < 4U)
	    return GEM_IRC_MALFORMED;
	  event->type = GEM_IRC_EVENT_TOPIC_INFO;
	  event->target = parameters[1];
	  event->text = parameters[2];
	  event->extra = parameters[3];
	}
      else if (numeric == 353U)
	{
	  if (count < 4U)
	    return GEM_IRC_MALFORMED;
	  event->type = GEM_IRC_EVENT_NAMES;
	  event->target = parameters[2];
	  event->text = parameters[3];
	  event->extra = parameters[1];
	}
      else if (numeric == 366U)
	{
	  if (count < 2U)
	    return GEM_IRC_MALFORMED;
	  event->type = GEM_IRC_EVENT_NAMES_END;
	  event->target = parameters[1];
	  if (count > 2U)
	    event->text = parameters[2];
	}
      else if (numeric == 433U)
	{
	  event->type = GEM_IRC_EVENT_NICK_IN_USE;
	  if (count > 1U)
	    event->target = parameters[1];
	  else if (count)
	    event->target = parameters[0];
	  if (count)
	    event->text = parameters[count - 1U];
	}
      else if (numeric >= 400U && numeric <= 599U)
	{
	  event->type = GEM_IRC_EVENT_ERROR;
	  if (count > 1U)
	    event->target = parameters[1];
	  else if (count)
	    event->target = parameters[0];
	  if (count)
	    event->text = parameters[count - 1U];
	}
      else
	{
	  event->type = GEM_IRC_EVENT_STATUS;
	  if (count)
	    event->target = parameters[0];
	  if (count)
	    event->text = parameters[count - 1U];
	}
      gem_irc_emit (client, event);
      return GEM_IRC_OK;
    }

  if (gem_irc_equal (event->command, "PING"))
    {
      if (!count || count > 2U)
	return GEM_IRC_MALFORMED;
      event->type = GEM_IRC_EVENT_PING;
      if (count == 2U)
	event->target = parameters[0];
      event->text = parameters[count - 1U];
      result = gem_irc_send_pong (client, parameters, count);
      gem_irc_emit (client, event);
      return result;
    }

  if (gem_irc_equal (event->command, "PRIVMSG"))
    {
      if (count < 2U)
	return GEM_IRC_MALFORMED;
      event->target = parameters[0];
      event->text = parameters[1];
      ctcp = (char *) parameters[1];
      text_length = 0;
      while (ctcp[text_length])
	text_length++;
      if (text_length >= 2U && ctcp[0] == '\001'
	  && ctcp[text_length - 1U] == '\001')
	{
	  ctcp[text_length - 1U] = '\0';
	  ctcp++;
	  if (gem_irc_starts_with (ctcp, "ACTION "))
	    {
	      event->type = GEM_IRC_EVENT_ACTION;
	      event->text = ctcp + 7;
	    }
	  else if (gem_irc_equal (ctcp, "ACTION"))
	    {
	      event->type = GEM_IRC_EVENT_ACTION;
	      event->text = ctcp + 6;
	    }
	  else
	    {
	      event->type = GEM_IRC_EVENT_CTCP;
	      event->text = ctcp;
	    }
	}
      else
	{
	  event->type = GEM_IRC_EVENT_MESSAGE;
	}
    }
  else if (gem_irc_equal (event->command, "NOTICE"))
    {
      if (count < 2U)
	return GEM_IRC_MALFORMED;
      event->type = GEM_IRC_EVENT_NOTICE;
      event->target = parameters[0];
      event->text = parameters[1];
    }
  else if (gem_irc_equal (event->command, "JOIN"))
    {
      if (!count)
	return GEM_IRC_MALFORMED;
      event->type = GEM_IRC_EVENT_JOIN;
      event->target = parameters[0];
      if (count > 1U)
	event->extra = parameters[1];
      if (count > 2U)
	event->text = parameters[2];
    }
  else if (gem_irc_equal (event->command, "PART"))
    {
      if (!count)
	return GEM_IRC_MALFORMED;
      event->type = GEM_IRC_EVENT_PART;
      event->target = parameters[0];
      if (count > 1U)
	event->text = parameters[1];
    }
  else if (gem_irc_equal (event->command, "QUIT"))
    {
      event->type = GEM_IRC_EVENT_QUIT;
      if (count)
	event->text = parameters[0];
    }
  else if (gem_irc_equal (event->command, "NICK"))
    {
      if (!count)
	return GEM_IRC_MALFORMED;
      event->type = GEM_IRC_EVENT_NICK;
      event->target = parameters[0];
      our_nick = event->nick && client->nick[0]
	&& gem_irc_equal (event->nick, client->nick);
      if (our_nick)
	(void) gem_irc_store_nick (client, parameters[0]);
    }
  else if (gem_irc_equal (event->command, "TOPIC"))
    {
      if (!count)
	return GEM_IRC_MALFORMED;
      event->type = GEM_IRC_EVENT_TOPIC;
      event->target = parameters[0];
      if (count > 1U)
	event->text = parameters[1];
    }
  else if (gem_irc_equal (event->command, "KICK"))
    {
      if (count < 2U)
	return GEM_IRC_MALFORMED;
      event->type = GEM_IRC_EVENT_KICK;
      event->target = parameters[0];
      event->extra = parameters[1];
      if (count > 2U)
	event->text = parameters[2];
    }
  else if (gem_irc_equal (event->command, "MODE"))
    {
      if (!count)
	return GEM_IRC_MALFORMED;
      event->type = GEM_IRC_EVENT_MODE;
      event->target = parameters[0];
      if (count > 1U)
	event->text = parameters[1];
      if (count > 2U)
	event->extra = parameters[2];
    }
  else if (gem_irc_equal (event->command, "ERROR"))
    {
      event->type = GEM_IRC_EVENT_ERROR;
      if (count)
	event->text = parameters[0];
    }
  else
    {
      event->type = GEM_IRC_EVENT_STATUS;
      if (count)
	event->target = parameters[0];
      if (count)
	event->text = parameters[count - 1U];
    }
  gem_irc_emit (client, event);
  return GEM_IRC_OK;
}

static GEM_IRC_WORD
gem_irc_receive_done (GEM_IRC_CLIENT *client, GEM_IRC_WORD result)
{
  client->receiving = 0;
  return result;
}

GEM_IRC_WORD
gem_irc_receive_line (GEM_IRC_CLIENT *client, const char *line,
		      GEM_IRC_UWORD length)
{
  const char *parameters[GEM_IRC_PARAMETER_MAX];
  GEM_IRC_EVENT event;
  GEM_IRC_UWORD payload;
  GEM_IRC_UWORD index;
  GEM_IRC_UWORD count;
  GEM_IRC_WORD result;
  char *cursor;
  char *tags;
  char *prefix;
  char *command;

  if (!client || !line || !length)
    return GEM_IRC_BAD_ARGUMENT;
  if (client->receiving)
    return GEM_IRC_BUSY;
  if (length > GEM_IRC_LINE_MAX)
    return GEM_IRC_LINE_TOO_LONG;

  payload = length;
  if (line[payload - 1U] == '\n')
    {
      payload--;
      if (payload && line[payload - 1U] == '\r')
	payload--;
    }
  else if (payload > GEM_IRC_PAYLOAD_MAX)
    {
      return GEM_IRC_LINE_TOO_LONG;
    }
  if (!payload)
    return GEM_IRC_MALFORMED;
  if (payload > GEM_IRC_PAYLOAD_MAX)
    return GEM_IRC_LINE_TOO_LONG;

  client->receiving = 1;
  index = 0;
  while (index < payload)
    {
      if (!line[index] || line[index] == '\r' || line[index] == '\n')
	return gem_irc_receive_done (client, GEM_IRC_MALFORMED);
      client->receive_line[index] = line[index];
      index++;
    }
  client->receive_line[payload] = '\0';

  cursor = client->receive_line;
  tags = (char *) 0;
  prefix = (char *) 0;
  if (*cursor == '@')
    {
      tags = ++cursor;
      while (*cursor && *cursor != ' ')
	cursor++;
      if (!*cursor)
	return gem_irc_receive_done (client, GEM_IRC_MALFORMED);
      *cursor++ = '\0';
      if (!*tags)
	return gem_irc_receive_done (client, GEM_IRC_MALFORMED);
    }
  while (*cursor == ' ')
    cursor++;
  if (*cursor == ':')
    {
      prefix = ++cursor;
      while (*cursor && *cursor != ' ')
	cursor++;
      if (!*cursor || cursor == prefix)
	return gem_irc_receive_done (client, GEM_IRC_MALFORMED);
      *cursor++ = '\0';
    }
  while (*cursor == ' ')
    cursor++;
  if (!*cursor)
    return gem_irc_receive_done (client, GEM_IRC_MALFORMED);
  command = cursor;
  while (*cursor && *cursor != ' ')
    cursor++;
  if (*cursor)
    *cursor++ = '\0';

  count = 0;
  while (*cursor)
    {
      while (*cursor == ' ')
	cursor++;
      if (!*cursor)
	break;
      if (count >= GEM_IRC_PARAMETER_MAX)
	return gem_irc_receive_done (client, GEM_IRC_MALFORMED);
      if (*cursor == ':')
	{
	  parameters[count++] = ++cursor;
	  while (*cursor)
	    cursor++;
	  break;
	}
      parameters[count++] = cursor;
      while (*cursor && *cursor != ' ')
	cursor++;
      if (*cursor)
	*cursor++ = '\0';
    }

  gem_irc_prepare_event (&event, tags, command, prefix, parameters, count);
  result = gem_irc_dispatch (client, &event, parameters, count);
  return gem_irc_receive_done (client, result);
}

GEM_IRC_WORD
gem_irc_send_nick (GEM_IRC_CLIENT *client, const char *nick)
{
  GEM_IRC_UWORD length;
  GEM_IRC_UWORD index;
  GEM_IRC_WORD result;

  if (!client || !nick || !*nick)
    return GEM_IRC_BAD_ARGUMENT;
  index = 0;
  while (nick[index])
    {
      if (index >= GEM_IRC_NICK_SIZE - 1U)
	return GEM_IRC_LINE_TOO_LONG;
      index++;
    }
  result = gem_irc_begin (client, &length, "NICK");
  if (result == GEM_IRC_OK)
    result = gem_irc_add_middle (client, &length, nick);
  if (result != GEM_IRC_OK)
    return result;
  result = gem_irc_finish (client, length);
  if (result > 0)
    (void) gem_irc_store_nick (client, nick);
  return result;
}

GEM_IRC_WORD
gem_irc_send_user (GEM_IRC_CLIENT *client, const char *user,
		   const char *real_name)
{
  GEM_IRC_UWORD length;
  GEM_IRC_WORD result;

  if (!real_name)
    return GEM_IRC_BAD_ARGUMENT;
  result = gem_irc_begin (client, &length, "USER");
  if (result == GEM_IRC_OK)
    result = gem_irc_add_middle (client, &length, user);
  if (result == GEM_IRC_OK)
    result = gem_irc_add_middle (client, &length, "0");
  if (result == GEM_IRC_OK)
    result = gem_irc_add_middle (client, &length, "*");
  if (result == GEM_IRC_OK)
    result = gem_irc_add_trailing (client, &length, real_name);
  if (result != GEM_IRC_OK)
    return result;
  return gem_irc_finish (client, length);
}

GEM_IRC_WORD
gem_irc_send_join (GEM_IRC_CLIENT *client, const char *channels,
		   const char *keys)
{
  GEM_IRC_UWORD length;
  GEM_IRC_WORD result;

  result = gem_irc_begin (client, &length, "JOIN");
  if (result == GEM_IRC_OK)
    result = gem_irc_add_middle (client, &length, channels);
  if (result == GEM_IRC_OK && keys && *keys)
    result = gem_irc_add_middle (client, &length, keys);
  if (result != GEM_IRC_OK)
    return result;
  return gem_irc_finish (client, length);
}

GEM_IRC_WORD
gem_irc_send_part (GEM_IRC_CLIENT *client, const char *channel,
		   const char *reason)
{
  GEM_IRC_UWORD length;
  GEM_IRC_WORD result;

  result = gem_irc_begin (client, &length, "PART");
  if (result == GEM_IRC_OK)
    result = gem_irc_add_middle (client, &length, channel);
  if (result == GEM_IRC_OK && reason)
    result = gem_irc_add_trailing (client, &length, reason);
  if (result != GEM_IRC_OK)
    return result;
  return gem_irc_finish (client, length);
}

GEM_IRC_WORD
gem_irc_send_privmsg (GEM_IRC_CLIENT *client, const char *target,
		      const char *text)
{
  return gem_irc_send_target_trailing (client, "PRIVMSG", target, text);
}

GEM_IRC_WORD
gem_irc_send_notice (GEM_IRC_CLIENT *client, const char *target,
		     const char *text)
{
  return gem_irc_send_target_trailing (client, "NOTICE", target, text);
}

GEM_IRC_WORD
gem_irc_send_quit (GEM_IRC_CLIENT *client, const char *reason)
{
  GEM_IRC_UWORD length;
  GEM_IRC_WORD result;

  result = gem_irc_begin (client, &length, "QUIT");
  if (result == GEM_IRC_OK && reason)
    result = gem_irc_add_trailing (client, &length, reason);
  if (result != GEM_IRC_OK)
    return result;
  return gem_irc_finish (client, length);
}

GEM_IRC_WORD
gem_irc_send_whois (GEM_IRC_CLIENT *client, const char *nick)
{
  return gem_irc_send_one_middle (client, "WHOIS", nick);
}

GEM_IRC_WORD
gem_irc_send_topic (GEM_IRC_CLIENT *client, const char *channel,
		    const char *topic)
{
  GEM_IRC_UWORD length;
  GEM_IRC_WORD result;

  result = gem_irc_begin (client, &length, "TOPIC");
  if (result == GEM_IRC_OK)
    result = gem_irc_add_middle (client, &length, channel);
  if (result == GEM_IRC_OK && topic)
    result = gem_irc_add_trailing (client, &length, topic);
  if (result != GEM_IRC_OK)
    return result;
  return gem_irc_finish (client, length);
}

GEM_IRC_WORD
gem_irc_send_mode (GEM_IRC_CLIENT *client, const char *target,
		   const char *modes, const char *arguments)
{
  GEM_IRC_UWORD length;
  GEM_IRC_WORD result;

  result = gem_irc_begin (client, &length, "MODE");
  if (result == GEM_IRC_OK)
    result = gem_irc_add_middle (client, &length, target);
  if (result == GEM_IRC_OK && modes && *modes)
    result = gem_irc_add_middle (client, &length, modes);
  if (result == GEM_IRC_OK && arguments && *arguments)
    result = gem_irc_add_text (client, &length, arguments);
  if (result != GEM_IRC_OK)
    return result;
  return gem_irc_finish (client, length);
}

GEM_IRC_WORD
gem_irc_send_kick (GEM_IRC_CLIENT *client, const char *channel,
		   const char *nick, const char *reason)
{
  GEM_IRC_UWORD length;
  GEM_IRC_WORD result;

  result = gem_irc_begin (client, &length, "KICK");
  if (result == GEM_IRC_OK)
    result = gem_irc_add_middle (client, &length, channel);
  if (result == GEM_IRC_OK)
    result = gem_irc_add_middle (client, &length, nick);
  if (result == GEM_IRC_OK && reason)
    result = gem_irc_add_trailing (client, &length, reason);
  if (result != GEM_IRC_OK)
    return result;
  return gem_irc_finish (client, length);
}

GEM_IRC_WORD
gem_irc_send_action (GEM_IRC_CLIENT *client, const char *target,
		     const char *text)
{
  GEM_IRC_UWORD length;
  GEM_IRC_WORD result;

  if (!text)
    return GEM_IRC_BAD_ARGUMENT;
  result = gem_irc_begin (client, &length, "PRIVMSG");
  if (result == GEM_IRC_OK)
    result = gem_irc_add_middle (client, &length, target);
  if (result == GEM_IRC_OK)
    result = gem_irc_append_char (client, &length, ' ');
  if (result == GEM_IRC_OK)
    result = gem_irc_append_char (client, &length, ':');
  if (result == GEM_IRC_OK)
    result = gem_irc_append_char (client, &length, '\001');
  if (result == GEM_IRC_OK)
    result = gem_irc_append_text (client, &length, "ACTION ");
  if (result == GEM_IRC_OK)
    result = gem_irc_append_text (client, &length, text);
  if (result == GEM_IRC_OK)
    result = gem_irc_append_char (client, &length, '\001');
  if (result != GEM_IRC_OK)
    return result;
  return gem_irc_finish (client, length);
}

GEM_IRC_WORD
gem_irc_send_raw (GEM_IRC_CLIENT *client, const char *command)
{
  GEM_IRC_UWORD length;
  GEM_IRC_WORD result;
  GEM_IRC_UBYTE first;

  if (!client || !command || !*command)
    return GEM_IRC_BAD_ARGUMENT;
  first = (GEM_IRC_UBYTE) * command;
  if (first <= 32U || *command == ':')
    return GEM_IRC_MALFORMED;
  length = 0;
  client->transmit_line[0] = '\0';
  result = gem_irc_append_text (client, &length, command);
  if (result != GEM_IRC_OK)
    return result;
  return gem_irc_finish (client, length);
}
