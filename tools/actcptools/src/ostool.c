/* ostool: see ostool.h. MIT. */
#include <exec/types.h>
#include <exec/libraries.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <libraries/bsdsocket.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/filio.h>
#include <netinet/in.h>
#include <netdb.h>
#include <proto/bsdsocket.h>
#include <stdio.h>
#include <string.h>

#include "ostool.h"
#include "acnet_stack.h"

struct Library *SocketBase = NULL;

int ost_open(const char *prog)
{
    SocketBase = OpenLibrary("bsdsocket.library", 4);
    if (!SocketBase) fprintf(stderr, "%s: no bsdsocket.library (is the network on?)\n", prog);
    return SocketBase != NULL;
}

void ost_close(void)
{
    if (SocketBase) CloseLibrary(SocketBase);
    SocketBase = NULL;
}

int ost_resolve(const char *prog, const char *host, ULONG *addr)
{
    struct hostent *he;
    LONG a = inet_addr((STRPTR)host);
    if (a != -1 || !strcmp(host, "255.255.255.255")) {
        *addr = (ULONG)a;
        return 1;
    }
    he = gethostbyname((STRPTR)host);
    if (!he || he->h_addrtype != AF_INET || he->h_length != 4 || !he->h_addr) {
        fprintf(stderr, "%s: %s was not found\n", prog, host);
        return 0;
    }
    memcpy(addr, he->h_addr, 4);
    return 1;
}

int ost_break(void)
{
    return (SetSignal(0, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C) != 0;
}

LONG ost_connect(const char *prog, const char *host, unsigned port, int seconds)
{
    struct sockaddr_in sa;
    struct timeval tv;
    fd_set wf;
    ULONG addr, sigs = SIGBREAKF_CTRL_C;
    LONG s, on = 1, off = 0, err = 0, n;
    socklen_t len = sizeof(err);

    if (!ost_resolve(prog, host, &addr)) return -1;
    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        fprintf(stderr, "%s: no socket (error %ld)\n", prog, (long)Errno());
        return -1;
    }
    memset(&sa, 0, sizeof(sa));
    sa.sin_len = sizeof(sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons((UWORD)port);
    sa.sin_addr.s_addr = addr;
    IoctlSocket(s, FIONBIO, (char *)&on);
    if (connect(s, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        if (Errno() != EINPROGRESS) {
            err = Errno();
        } else {
            FD_ZERO(&wf);
            FD_SET(s, &wf);
            tv.tv_sec = seconds;
            tv.tv_usec = 0;
            n = WaitSelect(s + 1, NULL, &wf, NULL, &tv, &sigs);
            if (n < 0) err = Errno();
            else if (n == 0) err = (sigs & SIGBREAKF_CTRL_C) ? EINTR : ETIMEDOUT;
            else if (getsockopt(s, SOL_SOCKET, SO_ERROR, &err, &len) < 0) err = Errno();
        }
    }
    if (err) {
        if (err == ECONNREFUSED) fprintf(stderr, "%s: %s port %u refused the connection\n", prog, host, port);
        else if (err == ETIMEDOUT) fprintf(stderr, "%s: %s port %u: no answer in %d s\n", prog, host, port, seconds);
        else if (err == EINTR) fprintf(stderr, "%s: stopped\n", prog);
        else fprintf(stderr, "%s: %s port %u: connection failed (error %ld)\n", prog, host, port, (long)err);
        CloseSocket(s);
        return -1;
    }
    IoctlSocket(s, FIONBIO, (char *)&off);
    return s;
}

int ost_wait_read(LONG s, long ms)
{
    struct timeval tv;
    fd_set rf;
    ULONG sigs = SIGBREAKF_CTRL_C;
    LONG n;
    FD_ZERO(&rf);
    FD_SET(s, &rf);
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    n = WaitSelect(s + 1, &rf, NULL, NULL, &tv, &sigs);
    if (n < 0) return -1;
    if (n == 0) return (sigs & SIGBREAKF_CTRL_C) ? -1 : 0;
    return 1;
}

int ost_send_all(LONG s, const void *buf, long len)
{
    const char *p = buf;
    while (len > 0) {
        LONG n = send(s, (APTR)p, len, 0);
        if (n <= 0) return -1;
        p += n;
        len -= n;
    }
    return 0;
}

int ost_send_str(LONG s, const char *text)
{
    return ost_send_all(s, text, strlen(text));
}

void ost_lines_init(struct ost_lines *l, LONG s)
{
    l->s = s;
    l->have = l->at = 0;
}

long ost_line(struct ost_lines *l, char *out, long max, int seconds)
{
    long n = 0;
    for (;;) {
        while (l->at < l->have) {
            char c = l->buf[l->at++];
            if (c == '\n') {
                if (n && out[n - 1] == '\r') n--;
                out[n] = 0;
                return n;
            }
            if (n < max - 1) out[n++] = c;
        }
        {
            int w = ost_wait_read(l->s, seconds * 1000L);
            LONG got;
            if (w <= 0) return -1;
            got = recv(l->s, (APTR)l->buf, sizeof(l->buf), 0);
            if (got < 0) return -1;
            if (got == 0) {                    /* the end: a last line without LF, then -2 */
                out[n] = 0;
                return n ? n : -2;
            }
            l->have = got;
            l->at = 0;
        }
    }
}

long ost_lines_take(struct ost_lines *l, char *out, long max)
{
    long n = l->have - l->at;
    if (n > max) n = max;
    memcpy(out, l->buf + l->at, n);
    l->at += n;
    return n;
}

/* ---- saving a download (ostool.h) ---- */

static int is_drawer(const char *name)
{
    struct FileInfoBlock *fib;
    BPTR lock = Lock((STRPTR)name, SHARED_LOCK);
    int dir = 0;
    if (!lock) return 0;
    if ((fib = AllocDosObject(DOS_FIB, NULL)) != NULL) {
        if (Examine(lock, fib)) dir = fib->fib_DirEntryType > 0;
        FreeDosObject(DOS_FIB, fib);
    }
    UnLock(lock);
    return dir;
}

BPTR ost_save_open(struct ost_save *sv, const char *name)
{
    char part[16];
    ULONG len = (ULONG)(PathPart((STRPTR)name) - (STRPTR)name);

    sv->name = name;
    sv->tmp[0] = 0;
    if (!IsFileSystem((STRPTR)name)) return sv->fh = Open((STRPTR)name, MODE_NEWFILE);
    if (is_drawer(name)) { SetIoErr(ERROR_OBJECT_WRONG_TYPE); return sv->fh = 0; }
    if (len > sizeof(sv->tmp) - sizeof(part) - 2) { SetIoErr(ERROR_LINE_TOO_LONG); return sv->fh = 0; }
    memcpy(sv->tmp, name, len);
    sv->tmp[len] = 0;
    /* short (FFS keeps 30 characters), and this task's own */
    snprintf(part, sizeof(part), ".ost%08lx", (unsigned long)FindTask(NULL));
    AddPart((STRPTR)sv->tmp, (STRPTR)part, sizeof(sv->tmp));
    sv->fh = Open((STRPTR)sv->tmp, MODE_NEWFILE);
    if (!sv->fh) sv->tmp[0] = 0;
    return sv->fh;
}

int ost_save_close(struct ost_save *sv, int ok, const char *prog)
{
    if (sv->fh && !Close(sv->fh)) ok = 0;          /* the last write failed */
    sv->fh = 0;
    if (!sv->tmp[0]) return ok ? 0 : -1;
    if (!ok) { DeleteFile((STRPTR)sv->tmp); return -1; }
    DeleteFile((STRPTR)sv->name);                  /* Rename does not replace */
    if (!Rename((STRPTR)sv->tmp, (STRPTR)sv->name)) {
        fprintf(stderr, "%s: cannot replace %s; what was fetched is in %s\n", prog, sv->name, sv->tmp);
        return -1;
    }
    return 0;
}

int ost_main(int (*body)(int, char **), int argc, char **argv)
{
    return acnet_main_with_stack(body, argc, argv, 16384);
}
