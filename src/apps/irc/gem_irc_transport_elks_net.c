/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

/*
 * gem_irc_transport_elks_net.c - the IRC client's network helpers
 *
 * plain C over the ELKS socket api: look a host up, connect to it, and
 * poll the socket, the transport only ever sees 16-bit words
 */

#include "gem_irc_transport.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/time.h>

/* look up a host, hand the IPv4 address back as two words, 0 on failure */
GEM_IRC_WORD
gem_irc_elks_resolve_ipv4(const char *host, GEM_IRC_UWORD *address_lo,
	GEM_IRC_UWORD *address_hi)
{
	ipaddr_t address;

	if (!host || !address_lo || !address_hi)
		return 0;
	address = in_gethostbyname(host);
	/* 0 and 0xffffffff both mean the lookup failed */
	if (address == 0 || address == 0xffffffffUL)
		return 0;
	*address_lo = (GEM_IRC_UWORD) address;
	*address_hi = (GEM_IRC_UWORD) (address >> 16);
	return 1;
}

/*
 * connect the socket to address:port, ktcp wants it bound to any
 * address/port first so we do that and ignore the result
 */
GEM_IRC_WORD
gem_irc_elks_connect_ipv4(GEM_IRC_WORD descriptor, GEM_IRC_UWORD address_lo,
	GEM_IRC_UWORD address_hi, GEM_IRC_UWORD port)
{
	struct sockaddr_in any;
	struct sockaddr_in remote;

	any.sin_family = AF_INET;
	any.sin_port = 0;
	any.sin_addr.s_addr = INADDR_ANY;
	(void) bind((int) descriptor, (struct sockaddr *) &any, sizeof(any));

	remote.sin_family = AF_INET;
	remote.sin_port = htons(port);
	remote.sin_addr.s_addr = ((unsigned long) address_hi << 16)
		| (unsigned long) address_lo;
	return (GEM_IRC_WORD) connect((int) descriptor,
		(struct sockaddr *) &remote, sizeof(remote));
}

/*
 * zero-wait poll of one socket, returns a 1/2/4 mask for
 * readable/writable/error, 0 if nothing, -1 if the descriptor is silly
 */
GEM_IRC_WORD
gem_irc_elks_select_ready(GEM_IRC_WORD descriptor, GEM_IRC_UWORD want_write)
{
	fd_set readers;
	fd_set writers;
	fd_set errors;
	struct timeval now;
	GEM_IRC_WORD ready;

	if (descriptor < 0 || descriptor >= FD_SETSIZE)
		return -1;
	FD_ZERO(&readers);
	FD_ZERO(&writers);
	FD_ZERO(&errors);
	FD_SET((int) descriptor, &readers);
	FD_SET((int) descriptor, &errors);
	if (want_write)
		FD_SET((int) descriptor, &writers);
	now.tv_sec = 0;
	now.tv_usec = 0;
	if (select((int) descriptor + 1, &readers,
			want_write ? &writers : (fd_set *) 0, &errors, &now) <= 0)
		return 0;

	ready = 0;
	if (FD_ISSET((int) descriptor, &readers))
		ready |= 1;
	if (FD_ISSET((int) descriptor, &writers))
		ready |= 2;
	if (FD_ISSET((int) descriptor, &errors))
		ready |= 4;
	return ready;
}
