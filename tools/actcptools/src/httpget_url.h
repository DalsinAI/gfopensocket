/* httpget's addresses: a URL into host, port and path, and where a Location:
 * header leads. Plain C, so the host test (tests/httpget_url_test.c) builds it
 * as httpget does. A GitHub release download is sent on to a signed address of
 * over 1,000 characters, so an address may be up to HG_URL_MAX long; a longer
 * one is refused, never cut (a cut one is another address).
 * GFOpenSocket ACTCPTools. MIT. */
#ifndef HTTPGET_URL_H
#define HTTPGET_URL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define HG_URL_MAX 4096

struct url {
    int tls;
    char host[256];
    unsigned port;
    char path[HG_URL_MAX];
};

static int parse_url(const char *text, struct url *u)
{
    const char *p = text, *h, *e;
    size_t n;
    if (!strncasecmp(p, "http://", 7)) { u->tls = 0; u->port = 80; p += 7; }
    else if (!strncasecmp(p, "https://", 8)) { u->tls = 1; u->port = 443; p += 8; }
    else return 0;
    h = p;
    e = h + strcspn(h, ":/?#");
    n = e - h;
    if (!n || n >= sizeof(u->host)) return 0;
    memcpy(u->host, h, n);
    u->host[n] = 0;
    if (*e == ':') {
        u->port = (unsigned)atol(e + 1);
        e += 1 + strspn(e + 1, "0123456789");
        if (!u->port) return 0;
    }
    if (*e != '/' && *e != '?' && *e) return 0;
    if (snprintf(u->path, sizeof(u->path), "%s%s", *e == '/' ? "" : "/", e) >= (int)sizeof(u->path))
        return 0;                                   /* a path too long to send whole */
    if (strchr(u->path, '#')) *strchr(u->path, '#') = 0;
    return 1;
}

/* A Location: header, absolute or relative to u, into next. */
static int follow(const struct url *u, const char *loc, struct url *next)
{
    static char text[HG_URL_MAX];
    if (!strncasecmp(loc, "http://", 7) || !strncasecmp(loc, "https://", 8)) return parse_url(loc, next);
    *next = *u;
    /* a path too long for next is refused, not cut: a cut one is another address */
    if (loc[0] == '/') {
        if (snprintf(next->path, sizeof(next->path), "%s", loc) >= (int)sizeof(next->path)) return 0;
    } else {
        char *slash;
        snprintf(text, sizeof(text), "%s", u->path);
        if ((slash = strrchr(text, '/'))) slash[1] = 0;
        if (snprintf(next->path, sizeof(next->path), "%s%s", text, loc) >= (int)sizeof(next->path)) return 0;
    }
    return 1;
}

#endif
