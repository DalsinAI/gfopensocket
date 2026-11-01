/* nc: netcat. What you type (or pipe in) goes out; what comes back is shown.
 *   nc host port               a TCP connection
 *   nc -u host port            UDP datagrams
 *   nc -l port                 waits for one TCP connection (ports from 1024)
 *   nc -l -u port              waits for datagrams; answers go to the last sender
 *   nc -z host port[-port]     which ports answer
 *   -w seconds                 how long a connect may take (10)
 * Ctrl-C ends it.
 * GFOpenSocket ACTCPTools. MIT. */
#include <exec/types.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/filio.h>
#include <netinet/in.h>
#include <proto/bsdsocket.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ostool.h"

static const char version[] __attribute__((used)) = "$VER: nc 1.0 (4.10.2026) GFOpenSocket";

static char buf[4096];

static int usage(void)
{
    fprintf(stderr, "Usage: nc [-u] [-w seconds] host port\n"
                    "       nc -l [-u] port\n"
                    "       nc -z [-w seconds] host port[-port]\n");
    return 20;
}

static void address(struct sockaddr_in *sa, ULONG addr, unsigned port)
{
    memset(sa, 0, sizeof(*sa));
    sa->sin_len = sizeof(*sa);
    sa->sin_family = AF_INET;
    sa->sin_port = htons((UWORD)port);
    sa->sin_addr.s_addr = addr;
}

/* Moves data both ways until the other end closes, input ends (UDP) or
 * Ctrl-C. peer: where datagrams go (UDP), updated by what arrives when
 * learn is set. */
static int pump(LONG s, BOOL udp, struct sockaddr_in *peer, BOOL learn)
{
    BPTR in = Input(), out = Output();
    BOOL interactive = IsInteractive(in), in_open = TRUE, have_peer = !learn;
    LONG n;

    for (;;) {
        int w = ost_wait_read(s, interactive || !in_open ? 50 : 0);
        if (w < 0) return ost_break() ? 0 : 10;
        if (w > 0) {
            if (udp) {
                struct sockaddr_in from;
                socklen_t fl = sizeof(from);
                n = recvfrom(s, buf, sizeof(buf), 0, (struct sockaddr *)&from, &fl);
                if (n > 0 && learn) { *peer = from; have_peer = TRUE; }
            } else {
                n = recv(s, buf, sizeof(buf), 0);
                if (n == 0) return 0;                       /* the other end closed */
            }
            if (n < 0) { fprintf(stderr, "nc: receive failed (error %ld)\n", (long)Errno()); return 10; }
            Write(out, buf, n);
        }
        if (!in_open || (interactive && !WaitForChar(in, 0))) continue;
        n = Read(in, buf, sizeof(buf));
        if (n <= 0) {
            in_open = FALSE;
            if (udp) return 0;
            shutdown(s, 1);                                 /* no more from us; keep reading */
            continue;
        }
        if (udp) {
            if (!have_peer) { fprintf(stderr, "nc: no one to send to yet\n"); continue; }
            if (sendto(s, buf, n, 0, (struct sockaddr *)peer, sizeof(*peer)) != n) {
                fprintf(stderr, "nc: send failed (error %ld)\n", (long)Errno());
                return 10;
            }
        } else if (ost_send_all(s, buf, n)) {
            fprintf(stderr, "nc: send failed (error %ld)\n", (long)Errno());
            return 10;
        }
    }
}

static int connect_mode(const char *host, unsigned port, BOOL udp, int wait)
{
    struct sockaddr_in sa;
    ULONG addr;
    LONG s;
    int rc;
    if (!udp) {
        if ((s = ost_connect("nc", host, port, wait)) < 0) return 10;
        rc = pump(s, FALSE, NULL, FALSE);
    } else {
        if (!ost_resolve("nc", host, &addr)) return 10;
        if ((s = socket(AF_INET, SOCK_DGRAM, 0)) < 0) { fprintf(stderr, "nc: no socket\n"); return 10; }
        address(&sa, addr, port);
        rc = pump(s, TRUE, &sa, FALSE);
    }
    CloseSocket(s);
    return rc;
}

static int listen_mode(unsigned port, BOOL udp)
{
    struct sockaddr_in sa, peer;
    socklen_t pl = sizeof(peer);
    LONG s, c, one = 1;
    int rc = 10, w;
    s = socket(AF_INET, udp ? SOCK_DGRAM : SOCK_STREAM, 0);
    if (s < 0) { fprintf(stderr, "nc: no socket\n"); return 10; }
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    address(&sa, INADDR_ANY, port);
    if (bind(s, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        LONG e = Errno();
        if (e == EACCES) fprintf(stderr, "nc: port %u is not allowed (use 1024 or above)\n", port);
        else fprintf(stderr, "nc: cannot use port %u (error %ld)\n", port, (long)e);
        CloseSocket(s);
        return 10;
    }
    if (udp) {
        fprintf(stderr, "nc: waiting for datagrams on port %u (Ctrl-C ends)\n", port);
        rc = pump(s, TRUE, &peer, TRUE);
        CloseSocket(s);
        return rc;
    }
    listen(s, 1);
    fprintf(stderr, "nc: waiting for a connection on port %u (Ctrl-C ends)\n", port);
    while ((w = ost_wait_read(s, 1000)) == 0) ;
    if (w > 0 && (c = accept(s, (struct sockaddr *)&peer, &pl)) >= 0) {
        fprintf(stderr, "nc: connection from %s port %u\n", (char *)Inet_NtoA(peer.sin_addr.s_addr), (unsigned)ntohs(peer.sin_port));
        rc = pump(c, FALSE, NULL, FALSE);
        CloseSocket(c);
    } else if (w > 0) {
        fprintf(stderr, "nc: accept failed (error %ld)\n", (long)Errno());
    } else {
        rc = 0;
    }
    CloseSocket(s);
    return rc;
}

/* Does host:port take a TCP connection within `wait` seconds? 1, 0, -1 on Ctrl-C. */
static int probe(ULONG addr, unsigned port, int wait)
{
    struct sockaddr_in sa;
    struct timeval tv;
    fd_set wf;
    ULONG sigs = SIGBREAKF_CTRL_C;
    LONG s = socket(AF_INET, SOCK_STREAM, 0), on = 1, err = 0, n;
    socklen_t len = sizeof(err);
    if (s < 0) return 0;
    IoctlSocket(s, FIONBIO, (char *)&on);
    address(&sa, addr, port);
    if (connect(s, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        if (Errno() != EINPROGRESS) err = 1;
        else {
            FD_ZERO(&wf);
            FD_SET(s, &wf);
            tv.tv_sec = wait;
            tv.tv_usec = 0;
            n = WaitSelect(s + 1, NULL, &wf, NULL, &tv, &sigs);
            if (n == 0 && (sigs & SIGBREAKF_CTRL_C)) { CloseSocket(s); return -1; }
            if (n <= 0 || getsockopt(s, SOL_SOCKET, SO_ERROR, &err, &len) < 0) err = 1;
        }
    }
    CloseSocket(s);
    return !err;
}

static int scan_mode(const char *host, const char *ports, int wait)
{
    ULONG addr;
    long first = atol(ports), last = first, p, open = 0;
    const char *dash = strchr(ports, '-');
    if (dash) last = atol(dash + 1);
    if (first < 1 || last > 65535 || last < first) return usage();
    if (!ost_resolve("nc", host, &addr)) return 10;
    for (p = first; p <= last; ++p) {
        int r = probe(addr, (unsigned)p, wait);
        if (r < 0) { fprintf(stderr, "nc: stopped\n"); break; }
        if (r) { printf("%s port %ld is open\n", host, p); open++; }
    }
    if (!open) printf("No open ports from %ld to %ld\n", first, last);
    return open ? 0 : 5;
}

static int nc_main(int argc, char **argv)
{
    BOOL udp = FALSE, listen_ = FALSE, scan = FALSE;
    const char *arg[2] = { NULL, NULL };
    int i, args = 0, wait = 10, rc;

    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-u")) udp = TRUE;
        else if (!strcmp(argv[i], "-l")) listen_ = TRUE;
        else if (!strcmp(argv[i], "-z")) scan = TRUE;
        else if (!strcmp(argv[i], "-w") && i + 1 < argc) wait = atoi(argv[++i]);
        else if (argv[i][0] == '-' || args == 2) return usage();
        else arg[args++] = argv[i];
    }
    if (wait < 1) wait = 1;
    if (listen_ ? (args != 1 || scan) : (args != 2 || (scan && udp))) return usage();
    if (!ost_open("nc")) return 20;
    if (listen_) rc = listen_mode((unsigned)atol(arg[0]), udp);
    else if (scan) rc = scan_mode(arg[0], arg[1], wait > 5 ? 2 : wait);
    else rc = connect_mode(arg[0], (unsigned)atol(arg[1]), udp, wait);
    ost_close();
    return rc;
}

int main(int argc, char **argv)
{
    return ost_main(nc_main, argc, argv);
}
