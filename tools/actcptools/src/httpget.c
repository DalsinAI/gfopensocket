/* httpget: a file from a web server.
 *   httpget URL                saves it under the last part of its path
 *                              (the URL's as typed, not a redirect's)
 *   httpget -o file URL        saves it as file (-o - shows it)
 *   httpget -I URL             only the server's headers
 *   httpget -q ...             no progress
 * http:// always; https:// through GFOpenTLS (gfopentls.library, GFOpenUp's GFOpenTLS
 * part): TLS 1.2 or later, the server's certificate checked against
 * S:GFOpenTLS/ca-bundle.pem and ENVARC:GFOpenTLS/certs/, and its name. Follows
 * up to 5 redirects. (Until 1.1 https:// needed a build with AmiSSL 5.)
 * GFOpenSocket ACTCPTools. MIT. */
#include <exec/types.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <proto/bsdsocket.h>
#include <proto/gfopentls.h>
#include <libraries/gfopentls.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "ostool.h"

static const char version[] __attribute__((used)) = "$VER: httpget 1.1 (19.10.2026) GFOpenSocket";

struct Library *OpenTLSBase;
static struct OTContext *ot_ctx;           /* made on the first https://, kept for redirects */
static struct OTConnection *ot;

static LONG sock = -1;
static BOOL quiet;
static char in_buf[4096];
static long in_have, in_at;

/* ---- the connection: plain or TLS ------------------------------------------ */

static int tls_start(const char *host)
{
    LONG error = 0, r;
    if (!OpenTLSBase && !(OpenTLSBase = OpenLibrary((CONST_STRPTR)OPENTLSLIB_NAME, OPENTLSLIB_VERSION))) {
        fprintf(stderr, "httpget: https:// needs GFOpenTLS (gfopentls.library, GFOpenUp's GFOpenTLS part)\n");
        return 0;
    }
    if (!ot_ctx) {
        struct OTContextConfig config;
        memset(&config, 0, sizeof config);
        config.Size = sizeof config;
        config.MinVersion = OT_TLS12;
        config.ALPN = (STRPTR)"http/1.1";
        if (!(ot_ctx = OT_NewContext(&config, &error))) {
            fprintf(stderr, "httpget: GFOpenTLS could not be set up: %s\n", (const char *)OT_ErrorString(error));
            return 0;
        }
    }
    if (!(ot = OT_NewConnection(ot_ctx, (CONST_STRPTR)host, &error))) {
        fprintf(stderr, "httpget: GFOpenTLS could not be set up: %s\n", (const char *)OT_ErrorString(error));
        return 0;
    }
    OT_SetSocket(ot, sock, SocketBase);
    if ((r = OT_Handshake(ot)) != OTERR_OK) {
        CONST_STRPTR why = OT_GetErrorText(ot);
        if (r == OTERR_UNTRUSTED || r == OTERR_HOSTNAME || r == OTERR_EXPIRED || r == OTERR_BADCERT ||
            r == OTERR_TRUSTSTORE)
            fprintf(stderr, "httpget: %s's certificate was not accepted: %s\n", host, why ? (const char *)why : "?");
        else
            fprintf(stderr, "httpget: the TLS handshake with %s failed: %s\n", host, why ? (const char *)why : "?");
        return 0;
    }
    return 1;
}

static void tls_end(void)
{
    if (ot) { OT_Close(ot); OT_FreeConnection(ot); ot = NULL; }
}

static void conn_close(void)
{
    tls_end();
    if (sock >= 0) CloseSocket(sock);
    sock = -1;
    in_have = in_at = 0;
}

static int conn_open(const char *host, unsigned port, BOOL tls)
{
    if ((sock = ost_connect("httpget", host, port, 20)) < 0) return 0;
    if (tls && !tls_start(host)) { conn_close(); return 0; }
    return 1;
}

static int conn_write(const char *text)
{
    if (ot) return OT_Write(ot, (CONST_APTR)text, (LONG)strlen(text)) == (LONG)strlen(text) ? 0 : -1;
    return ost_send_str(sock, text);
}

/* Up to max bytes: their count, 0 at the end, -1 on an error or Ctrl-C. */
static long conn_read(char *buf, long max)
{
    long n;
    if (in_at < in_have) {
        n = in_have - in_at < max ? in_have - in_at : max;
        memcpy(buf, in_buf + in_at, n);
        in_at += n;
        return n;
    }
    if (ot && OT_Pending(ot) > 0) {
        n = OT_Read(ot, buf, max);
        return n > 0 ? n : -1;
    }
    if (ost_wait_read(sock, 60000) <= 0) return -1;
    if (ot) {
        /* 0: close_notify; OTERR_CLOSED: closed without one, the end as on a
         * plain socket (a Content-Length still catches a cut-off body) */
        n = OT_Read(ot, buf, max);
        if (n >= 0) return n;
        return n == OTERR_CLOSED ? 0 : -1;
    }
    n = recv(sock, buf, max, 0);
    return n;
}

/* A header line (CR LF dropped): its length, -1 at the end or on an error. */
static long conn_line(char *out, long max)
{
    long n = 0;
    for (;;) {
        if (in_at >= in_have) {
            long got;
            in_at = in_have = 0;
            got = conn_read(in_buf, sizeof(in_buf));
            if (got <= 0) return -1;
            in_have = got;
        }
        while (in_at < in_have) {
            char c = in_buf[in_at++];
            if (c == '\n') {
                if (n && out[n - 1] == '\r') n--;
                out[n] = 0;
                return n;
            }
            if (n < max - 1) out[n++] = c;
        }
    }
}

/* ---- URLs ------------------------------------------------------------------- */

struct url {
    BOOL tls;
    char host[128];
    unsigned port;
    char path[512];
};

static int parse_url(const char *text, struct url *u)
{
    const char *p = text, *h, *e;
    size_t n;
    if (!strncasecmp(p, "http://", 7)) { u->tls = FALSE; u->port = 80; p += 7; }
    else if (!strncasecmp(p, "https://", 8)) { u->tls = TRUE; u->port = 443; p += 8; }
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
    char text[700];
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

/* The last part of u's path (before any query) as a plain name in the current
 * drawer: a ':' would name a volume or assign (/S:User-Startup), so it and
 * control characters become '_'; nothing usable gives index.html. */
static const char *default_name(const struct url *u)
{
    static char name[108];
    const char *p = u->path, *q;
    size_t len = strcspn(p, "?"), n, i;
    for (q = p + len; q > p && q[-1] != '/'; --q) ;
    n = (size_t)(p + len - q);
    if (n >= sizeof(name)) n = sizeof(name) - 1;
    for (i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)q[i];
        name[i] = c == ':' || c == '/' || c < 0x20 || (c >= 0x7f && c < 0xa0) ? '_' : (char)c;
    }
    name[n] = 0;
    if (!n || !strcmp(name, ".") || !strcmp(name, "..")) return "index.html";
    return name;
}

/* ---- the transfer ------------------------------------------------------------- */

static BPTR out_fh;
static struct ost_save save;           /* the file being saved (ostool.h) */
static long long done, total;

static void progress(BOOL last)
{
    if (quiet) return;
    if (total > 0) fprintf(stderr, "\r%lld of %lld bytes (%lld%%)", done, total, done * 100 / total);
    else fprintf(stderr, "\r%lld bytes", done);
    if (last) fprintf(stderr, "\n");
}

static int store(const char *buf, long n)
{
    static long long shown;
    if (Write(out_fh, (APTR)buf, n) != n) return -1;
    done += n;
    if (done - shown >= 32768) { shown = done; progress(FALSE); }
    return 0;
}

static int body(BOOL chunked)
{
    static char buf[8192];
    char line[64];
    long n;
    if (!chunked) {
        while ((total <= 0 || done < total) && (n = conn_read(buf, sizeof(buf))) > 0)
            if (store(buf, total > 0 && done + n > total ? (long)(total - done) : n)) return -1;
        return total > 0 && done < total ? -1 : 0;
    }
    for (;;) {                                          /* chunked: size line, data, CR LF */
        long size;
        if (conn_line(line, sizeof(line)) < 0) return -1;
        size = strtol(line, NULL, 16);
        if (size <= 0) return 0;
        while (size > 0) {
            n = conn_read(buf, size < (long)sizeof(buf) ? size : (long)sizeof(buf));
            if (n <= 0 || store(buf, n)) return -1;
            size -= n;
        }
        if (conn_line(line, sizeof(line)) < 0) return -1;
    }
}

static int httpget_main(int argc, char **argv)
{
    struct url u, next;
    const char *url = NULL, *to = NULL;
    static char line[1024], request[1024], location[sizeof line];   /* a whole Location: header */
    BOOL head_only = FALSE, chunked;
    int i, hops, status, rc = 10;

    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-o") && i + 1 < argc) to = argv[++i];
        else if (!strcmp(argv[i], "-q")) quiet = TRUE;
        else if (!strcmp(argv[i], "-I")) head_only = TRUE;
        else if (argv[i][0] == '-' || url) url = NULL, i = argc + 1;
        else url = argv[i];
    }
    if (!url || i > argc || !parse_url(url, &u)) {
        fprintf(stderr, "Usage: httpget [-q] [-I] [-o file|-] http[s]://host[:port]/path\n");
        return 20;
    }
    /* named from the URL as typed, not from where a redirect leads (as curl -O, wget) */
    if (!to) to = default_name(&u);
    if (!ost_open("httpget")) return 20;

    for (hops = 0; hops <= 5; ++hops) {
        if (!conn_open(u.host, u.port, u.tls)) goto out;
        snprintf(request, sizeof(request),
                 "%s %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: GFOpenSocket-httpget/1.0 (AmigaOS)\r\n"
                 "Accept: */*\r\nConnection: close\r\n\r\n", head_only ? "HEAD" : "GET", u.path, u.host);
        if (conn_write(request)) { fprintf(stderr, "httpget: could not send the request\n"); goto out; }
        if (conn_line(line, sizeof(line)) < 0 || strncmp(line, "HTTP/", 5) || !strchr(line, ' ')) {
            fprintf(stderr, "httpget: %s did not answer as a web server\n", u.host);
            goto out;
        }
        status = atoi(strchr(line, ' ') + 1);
        if (head_only) printf("%s\n", line);
        total = -1;
        chunked = FALSE;
        location[0] = 0;
        while (conn_line(line, sizeof(line)) > 0) {
            if (head_only) printf("%s\n", line);
            if (!strncasecmp(line, "Content-Length:", 15)) {
                const char *p = line + 15;
                while (*p == ' ') p++;
                for (total = 0; *p >= '0' && *p <= '9'; ++p) total = total * 10 + (*p - '0');
            }
            else if (!strncasecmp(line, "Transfer-Encoding:", 18) && strstr(line + 18, "chunked")) chunked = TRUE;
            else if (!strncasecmp(line, "Location:", 9)) {
                const char *p = line + 9;
                while (*p == ' ') p++;
                snprintf(location, sizeof(location), "%s", p);
            }
        }
        if (head_only) { rc = status < 400 ? 0 : 10; goto out; }
        if (status >= 300 && status < 400 && location[0]) {
            if (!follow(&u, location, &next)) { fprintf(stderr, "httpget: cannot follow %s\n", location); goto out; }
            if (!quiet) fprintf(stderr, "Moved: %s\n", location);
            conn_close();
            u = next;
            continue;
        }
        if (status != 200) { fprintf(stderr, "httpget: the server answered %d\n", status); goto out; }
        break;
    }
    if (hops > 5) { fprintf(stderr, "httpget: too many redirects\n"); goto out; }

    /* to a temporary file, kept under `to` only when the transfer completes */
    out_fh = !strcmp(to, "-") ? Output() : ost_save_open(&save, to);
    if (!out_fh) { fprintf(stderr, "httpget: cannot write %s\n", to); goto out; }
    if (body(chunked)) {
        fprintf(stderr, "\nhttpget: the transfer broke off after %lld bytes\n", done);
        if (out_fh != Output()) ost_save_close(&save, 0, "httpget");
    } else {
        progress(TRUE);
        if (out_fh == Output()) rc = 0;
        else if (ost_save_close(&save, 1, "httpget") == 0) rc = 0;
        if (rc == 0 && strcmp(to, "-") && !quiet) printf("Saved %s\n", to);
    }

out:
    conn_close();
    if (ot_ctx) { OT_FreeContext(ot_ctx); ot_ctx = NULL; }
    if (OpenTLSBase) { CloseLibrary(OpenTLSBase); OpenTLSBase = NULL; }
    ost_close();
    return rc;
}

int main(int argc, char **argv)
{
    return ost_main(httpget_main, argc, argv);
}
