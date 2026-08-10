/*
 * gemview_net.c - URLs and the browser's HTTP fetch
 *
 * bare HTTP/1.0 GET over ELKS TCP, split a URL, fetch the page into
 * g_html[], strip the headers, undo chunked transfer, relative links
 * and the search shortcut resolve here too
 */

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "gemview.h"

static char net_host[128];
static char net_path[URL_MAX + 1];
static WORD net_port;
char net_location[URL_MAX + 1];	/* Location: header of a 3xx redirect */

/* case-insensitive n-char prefix test */
WORD
ci_prefix(const char *s, const char *pfx, WORD n)
{
	WORD i;
	for (i = 0; i < n; i++)
		if ((s[i] | 0x20) != (pfx[i] | 0x20))
			return 0;
	return 1;
}

/* split "http://host[:port]/path" into net_host/net_port/net_path */
WORD
parse_url(const char *url)
{
	const char *p = url;
	WORD i;

	if (!strncmp(p, "http://", 7))
		p += 7;
	else if (!strncmp(p, "HTTP://", 7))
		p += 7;
	net_port = 80;
	i = 0;
	while (*p && *p != '/' && *p != ':' && i < (WORD) sizeof(net_host) - 1)
		net_host[i++] = *p++;
	net_host[i] = 0;
	if (i == 0)
		return FALSE;
	if (*p == ':') {
		p++;
		net_port = 0;
		while (*p >= '0' && *p <= '9')
			net_port = net_port * 10 + (*p++ - '0');
	}
	while (*p && *p != '/')
		p++;
	if (*p != '/') {
		strcpy(net_path, "/");
	} else {
		i = 0;
		while (*p && i < (WORD) sizeof(net_path) - 1)
			net_path[i++] = *p++;
		net_path[i] = 0;
	}
	return TRUE;
}

/*
 * undo chunked transfer in place, hex size line, data, CRLF, a zero-size
 * chunk ends it, some servers chunk even for HTTP/1.0
 */
static WORD
decode_chunked(char *buf, WORD len)
{
	WORD in = 0, out = 0;

	while (in < len) {
		WORD sz = 0, any = 0;
		char c;

		while (in < len) {
			c = buf[in];
			if (c >= '0' && c <= '9') {
				sz = sz * 16 + (c - '0');
				any = 1;
				in++;
			} else if ((c | 0x20) >= 'a' && (c | 0x20) <= 'f') {
				sz = sz * 16 + ((c | 0x20) - 'a' + 10);
				any = 1;
				in++;
			} else
				break;
		}
		while (in < len && buf[in] != '\n')
			in++;	/* end of size line */
		if (in < len)
			in++;
		if (!any || sz == 0)
			break;	/* final chunk */
		if ((WORD) (in + sz) > len)
			sz = len - in;
		memmove(buf + out, buf + in, sz);
		out += sz;
		in += sz;
		while (in < len && (buf[in] == '\r' || buf[in] == '\n'))
			in++;
	}
	buf[out] = 0;
	return out;
}

/*
 * fetch net_path from net_host:net_port into g_html[] and strip the
 * headers, returns the HTTP status, negative on a socket error
 */
WORD
net_fetch(void)
{
	struct sockaddr_in sin;
	ipaddr_t ip;
	char req[URL_MAX + 160];
	char *body;
	WORD s, status;
	WORD chunked = 0;
	unsigned got = 0;
	int r;

	ip = in_gethostbyname(net_host);
	if (!ip)
		return -1;
	s = socket(AF_INET, SOCK_STREAM, 0);
	if (s < 0)
		return -2;
	/*
	 * ELKS ktcp needs a local bind before connect(), and a zero
	 * linger so close() sends RST
	 */
	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = 0;
	sin.sin_addr.s_addr = INADDR_ANY;
	if (bind(s, (struct sockaddr *) &sin, sizeof(sin)) < 0) {
		close(s);
		return -3;
	}
	{
		struct linger lg;
		lg.l_onoff = 1;
		lg.l_linger = 0;
		setsockopt(s, SOL_SOCKET, SO_LINGER, &lg, sizeof(lg));
	}
	sin.sin_family = AF_INET;
	sin.sin_port = htons(net_port);
	sin.sin_addr.s_addr = ip;
	if (in_connect(s, (struct sockaddr *) &sin, sizeof(sin), 15) < 0) {
		close(s);
		return -4;
	}
	strcpy(req, "GET ");
	strcat(req, net_path);
	strcat(req, " HTTP/1.0\r\nHost: ");
	strcat(req, net_host);
	strcat(req, "\r\nUser-Agent: ELKS-HighWire/0.1\r\n"
		"Connection: close\r\n\r\n");
	if (write(s, req, strlen(req)) < 0) {
		close(s);
		return -4;
	}
	while (got < HTML_MAX - 1 &&
		(r = read(s, g_html + got, HTML_MAX - 1 - got)) > 0)
		got += (unsigned) r;
	close(s);
	g_html[got] = 0;

	status = 0;		/* status line, "HTTP/1.x NNN ..." */
	{
		char *sp = strchr(g_html, ' ');
		if (sp)
			status = (WORD) atoi(sp + 1);
	}
	body = strstr(g_html, "\r\n\r\n");	/* end of the headers */
	if (body)
		body += 4;
	else if ((body = strstr(g_html, "\n\n")) != 0)
		body += 2;
	else
		body = g_html;

	/* scan the headers for a redirect target and for chunking */
	net_location[0] = 0;
	{
		char *h = g_html;
		while (h < body) {
			if (ci_prefix(h, "location:", 9)) {
				char *v = h + 9;
				WORD i = 0;
				while (v < body && (*v == ' ' || *v == '\t'))
					v++;
				while (v < body && *v != '\r'
					&& *v != '\n' && i < URL_MAX)
					net_location[i++] = *v++;
				net_location[i] = 0;
			} else if (ci_prefix(h, "transfer-encoding:", 18)) {
				char *v = h;
				while (v < body && *v != '\n') {
					if (ci_prefix(v, "chunked", 7)) {
						chunked = 1;
						break;
					}
					v++;
				}
			}
			while (h < body && *h != '\n')
				h++;
			if (h < body)
				h++;
		}
	}
	g_htmllen = (WORD) (got - (unsigned) (body - g_html));
	memmove(g_html, body, g_htmllen + 1);
	if (chunked)
		g_htmllen = decode_chunked(g_html, g_htmllen);
	return status;
}

/*
 * rewrite g_url as a web search, the big engines are HTTPS-only and
 * theres no TLS, so the query goes to FrogFind over plain HTTP
 */
VOID
make_search_url(const char *query)
{
	char enc[URL_MAX + 1];
	WORD i = 0;
	const char *s = query;
	while (*s && i < (WORD) sizeof(enc) - 1) {
		char c = *s++;
		if (c == ' ')
			enc[i++] = '+';
		else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
			(c >= '0' && c <= '9'))
			enc[i++] = c;
		/* other characters are dropped */
	}
	enc[i] = 0;
	strcpy(g_url, "http://frogfind.com/?q=");
	strncat(g_url, enc, URL_MAX - strlen(g_url));
	g_urllen = (WORD) strlen(g_url);
}

/* record the shown page's host/port/directory for relative links */
VOID
set_base(void)
{
	WORD i, last = 0;
	strcpy(base_host, net_host);
	base_port = net_port;
	for (i = 0; net_path[i]; i++)
		if (net_path[i] == '/')
			last = i;
	memcpy(base_dir, net_path, last + 1);
	base_dir[last + 1] = 0;
	if (!base_dir[0]) {
		base_dir[0] = '/';
		base_dir[1] = 0;
	}
}

/* resolve an href, relative or absolute, against the current page */
WORD
resolve_url(const char *href, char *dst)
{
	if (ci_prefix(href, "https://", 8))
		return 0;	/* no TLS */
	if (ci_prefix(href, "http://", 7)) {
		strncpy(dst, href, URL_MAX);
		dst[URL_MAX] = 0;
		return 1;
	}
	strcpy(dst, "http://");
	strncat(dst, base_host, 100);
	if (base_port != 80) {
		char pd[8];
		WORD pv = base_port, k = 0, j;
		char *e;
		while (pv && k < 6) {
			pd[k++] = (char) ('0' + pv % 10);
			pv /= 10;
		}
		strcat(dst, ":");
		e = dst + strlen(dst);
		for (j = 0; j < k; j++)
			e[j] = pd[k - 1 - j];
		e[k] = 0;
	}
	if (!href[0] || href[0] == '?')	/* same directory */
		strncat(dst, base_dir, URL_MAX - strlen(dst));
	else if (href[0] == '/')
		strncat(dst, href, URL_MAX - strlen(dst));
	else {
		strncat(dst, base_dir, URL_MAX - strlen(dst));
		strncat(dst, href, URL_MAX - strlen(dst));
	}
	if (href[0] == '?')	/* keep a query on the same dir */
		strncat(dst, href, URL_MAX - strlen(dst));
	return 1;
}
