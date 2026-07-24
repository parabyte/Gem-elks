/*
 * Copyright (C) 2026 Goliath Keet <gatekeeper@xt-emporium.com>
 * SPDX-License-Identifier: GPL-2.0-only
 */

/*
 * gem_irc_protocol.h - bounded native IRC protocol engine for GEM/ELKS.
 *
 * IRC wire lines are limited to 512 bytes including their final CR/LF pair.
 * Every stored length is an unsigned 16-bit byte count with scale 1.  A line
 * which would cross that boundary is rejected before the output callback is
 * entered; values are never truncated and no arithmetic is allowed to wrap.
 *
 * The client owns two fixed near-data line buffers.  There is no allocation,
 * recursion, converted protocol object, or socket wrapper.  On an ELKS small
 * model build the client pointer, callback pointers, opaque pointer, and all
 * event string pointers are near pointers.  Event strings point into the
 * receive buffer and are valid only until the event callback returns.
 */

#ifndef ELKS_GEM_IRC_PROTOCOL_H
#define ELKS_GEM_IRC_PROTOCOL_H

typedef signed short GEM_IRC_WORD;
typedef unsigned short GEM_IRC_UWORD;
typedef unsigned char GEM_IRC_UBYTE;

typedef char GEM_IRC_UWORD_MUST_BE_TWO_BYTES
  [(sizeof (GEM_IRC_UWORD) == 2) ? 1 : -1];

#define GEM_IRC_LINE_MAX              512U
#define GEM_IRC_PAYLOAD_MAX           510U
#define GEM_IRC_PARAMETER_MAX         15U
#define GEM_IRC_NICK_SIZE             32U

#define GEM_IRC_OK                    1
#define GEM_IRC_OUTPUT_REJECTED       0
#define GEM_IRC_BAD_ARGUMENT          (-1)
#define GEM_IRC_LINE_TOO_LONG         (-2)
#define GEM_IRC_MALFORMED             (-3)
#define GEM_IRC_BUSY                  (-4)
#define GEM_IRC_NO_OUTPUT             (-5)

/*
 * Event selectors are stable 16-bit values for a GEM window controller.
 * NAMES is one 353 fragment and NAMES_END is 366, so a UI can append each
 * bounded fragment directly instead of needing a second names allocation.
 */
#define GEM_IRC_EVENT_STATUS          1U
#define GEM_IRC_EVENT_REGISTERED      2U
#define GEM_IRC_EVENT_PING            3U
#define GEM_IRC_EVENT_MESSAGE         4U
#define GEM_IRC_EVENT_ACTION          5U
#define GEM_IRC_EVENT_CTCP            6U
#define GEM_IRC_EVENT_NOTICE          7U
#define GEM_IRC_EVENT_JOIN            8U
#define GEM_IRC_EVENT_PART            9U
#define GEM_IRC_EVENT_QUIT            10U
#define GEM_IRC_EVENT_NICK            11U
#define GEM_IRC_EVENT_TOPIC           12U
#define GEM_IRC_EVENT_TOPIC_INFO      13U
#define GEM_IRC_EVENT_NAMES           14U
#define GEM_IRC_EVENT_NAMES_END       15U
#define GEM_IRC_EVENT_KICK            16U
#define GEM_IRC_EVENT_MODE            17U
#define GEM_IRC_EVENT_ERROR           18U
#define GEM_IRC_EVENT_NICK_IN_USE     19U

struct gem_irc_event;

/*
 * A positive output result means that the complete line was accepted by the
 * caller's bounded transmit queue.  Zero or a negative result is returned to
 * the command caller unchanged.  The callback must copy or consume all bytes
 * before returning because the transmit buffer is reused by the next call.
 */
typedef GEM_IRC_WORD (*GEM_IRC_OUTPUT_CALLBACK) (void *opaque,
						 const char *line,
						 GEM_IRC_UWORD length);

typedef void (*GEM_IRC_EVENT_CALLBACK) (void *opaque,
					const struct gem_irc_event * event);

/*
 * Parsed fields are synchronous views of one receive line.  source is the
 * prefix name before optional !user@host components.  parameters preserves
 * all fifteen RFC parameters for commands such as MODE which need more than
 * the convenient target/text/extra views.
 */
typedef struct gem_irc_event
{
  GEM_IRC_UWORD type;
  GEM_IRC_UWORD numeric;
  GEM_IRC_UWORD parameter_count;
  const char *tags;
  const char *command;
  const char *source;
  const char *nick;
  const char *user;
  const char *host;
  const char *target;
  const char *text;
  const char *extra;
  const char *const *parameters;
} GEM_IRC_EVENT;

/*
 * Two 513-byte arrays include one local zero terminator beyond the largest
 * legal wire line.  All indexes into them are 16-bit and saturate by rejecting
 * an append at 510 payload bytes.  Keep this object in static near data on an
 * XT; placing it on the small ELKS process stack is intentionally discouraged.
 */
typedef struct gem_irc_client
{
  GEM_IRC_OUTPUT_CALLBACK output;
  GEM_IRC_EVENT_CALLBACK event;
  void *opaque;
  GEM_IRC_UBYTE registered;
  GEM_IRC_UBYTE receiving;
  char nick[GEM_IRC_NICK_SIZE];
  char receive_line[GEM_IRC_LINE_MAX + 1U];
  char transmit_line[GEM_IRC_LINE_MAX + 1U];
} GEM_IRC_CLIENT;

void gem_irc_init (GEM_IRC_CLIENT * client,
		   GEM_IRC_OUTPUT_CALLBACK output,
		   GEM_IRC_EVENT_CALLBACK event, void *opaque);
void gem_irc_set_callbacks (GEM_IRC_CLIENT * client,
			    GEM_IRC_OUTPUT_CALLBACK output,
			    GEM_IRC_EVENT_CALLBACK event, void *opaque);
void gem_irc_connection_reset (GEM_IRC_CLIENT * client);

/* RFC1459 case-insensitive comparison shared by protocol and UI state. */
GEM_IRC_WORD gem_irc_equal (const char *left, const char *right);

GEM_IRC_WORD gem_irc_is_registered (const GEM_IRC_CLIENT * client);
const char *gem_irc_current_nick (const GEM_IRC_CLIENT * client);

/* Parse one complete line, with CR/LF present or already removed by the UI. */
GEM_IRC_WORD gem_irc_receive_line (GEM_IRC_CLIENT * client,
				   const char *line, GEM_IRC_UWORD length);

GEM_IRC_WORD gem_irc_send_nick (GEM_IRC_CLIENT * client, const char *nick);
GEM_IRC_WORD gem_irc_send_user (GEM_IRC_CLIENT * client,
				const char *user, const char *real_name);
GEM_IRC_WORD gem_irc_send_join (GEM_IRC_CLIENT * client,
				const char *channels, const char *keys);
GEM_IRC_WORD gem_irc_send_part (GEM_IRC_CLIENT * client,
				const char *channel, const char *reason);
GEM_IRC_WORD gem_irc_send_privmsg (GEM_IRC_CLIENT * client,
				   const char *target, const char *text);
GEM_IRC_WORD gem_irc_send_notice (GEM_IRC_CLIENT * client,
				  const char *target, const char *text);
GEM_IRC_WORD gem_irc_send_quit (GEM_IRC_CLIENT * client, const char *reason);
GEM_IRC_WORD gem_irc_send_whois (GEM_IRC_CLIENT * client, const char *nick);
GEM_IRC_WORD gem_irc_send_topic (GEM_IRC_CLIENT * client,
				 const char *channel, const char *topic);
GEM_IRC_WORD gem_irc_send_mode (GEM_IRC_CLIENT * client,
				const char *target, const char *modes,
				const char *arguments);
GEM_IRC_WORD gem_irc_send_kick (GEM_IRC_CLIENT * client, const char *channel,
				const char *nick, const char *reason);
GEM_IRC_WORD gem_irc_send_action (GEM_IRC_CLIENT * client, const char *target,
				  const char *text);
GEM_IRC_WORD gem_irc_send_raw (GEM_IRC_CLIENT * client, const char *command);

#endif /* ELKS_GEM_IRC_PROTOCOL_H */
