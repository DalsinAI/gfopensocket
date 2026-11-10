/*
 * GFOpenSocket: non-blocking sockets and the IoctlSocket() requests, as
 * Boost.Asio (aMule) and other POSIX ports use them, straight through
 * bsdsocket.library (no libnix socket glue in between).
 *   FIONBIO on and off, FIONREAD, FIOASYNC, SIOCATMARK, a bad descriptor
 *   and a NULL argument; recv/recvfrom/recvmsg/accept on an empty
 *   non-blocking socket give EWOULDBLOCK; a non-blocking connect gives
 *   EINPROGRESS (or connects at once), WaitSelect() sees it writable and
 *   getsockopt(SO_ERROR) gives 0; sendmsg/recvmsg on UDP with two iovecs.
 * Prints one PASS/FAIL line each and PASS=n FAIL=n; RC 20 on any FAIL.
 * MIT.
 */
#include <exec/types.h>
#include <proto/exec.h>
#include <proto/bsdsocket.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/filio.h>
#include <sys/sockio.h>
#include <sys/uio.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

struct Library *SocketBase;

static int passes, failures;

static void result(const char *name, int ok, LONG detail)
{
    printf("%-44s %s", name, ok ? "PASS" : "FAIL");
    if (!ok) printf(" (%ld)", (long)detail);
    printf("\n");
    fflush(stdout);
    if (ok) passes++; else failures++;
}

static void loopback(struct sockaddr_in *a, int port)
{
    memset(a, 0, sizeof(*a));
    a->sin_len = sizeof(*a);
    a->sin_family = AF_INET;
    a->sin_port = htons(port);
    a->sin_addr.s_addr = htonl(0x7f000001UL);
}

static LONG wait_one(LONG s, int write, LONG secs)
{
    fd_set f;
    struct timeval tv;
    ULONG sigs = 0;
    FD_ZERO(&f);
    FD_SET(s, &f);
    tv.tv_secs = secs;
    tv.tv_micro = 0;
    return write ? WaitSelect(s + 1, NULL, &f, NULL, &tv, &sigs)
                 : WaitSelect(s + 1, &f, NULL, NULL, &tv, &sigs);
}

static void test_ioctl(void)
{
    LONG s = socket(AF_INET, SOCK_DGRAM, 0), on = 1, off = 0, v, r;
    result("UDP socket", s >= 0, Errno());
    if (s < 0) return;

    r = IoctlSocket(s, FIONBIO, (char *)&on);
    result("IoctlSocket FIONBIO on", r == 0, Errno());
    r = IoctlSocket(s, FIONBIO, (char *)&off);
    result("IoctlSocket FIONBIO off", r == 0, Errno());
    v = -1;
    r = IoctlSocket(s, FIONREAD, (char *)&v);
    result("IoctlSocket FIONREAD empty is 0", r == 0 && v == 0, r ? Errno() : v);
    r = IoctlSocket(s, FIOASYNC, (char *)&off);
    result("IoctlSocket FIOASYNC off", r == 0, Errno());
    v = -1;
    r = IoctlSocket(s, SIOCATMARK, (char *)&v);
    result("IoctlSocket SIOCATMARK is 0", r == 0 && v == 0, r ? Errno() : v);
    r = IoctlSocket(s, FIONBIO, NULL);
    result("IoctlSocket NULL argument EFAULT", r == -1 && Errno() == EFAULT, Errno());
    r = IoctlSocket(200, FIONBIO, (char *)&on);
    result("IoctlSocket bad descriptor EBADF", r == -1 && Errno() == EBADF, Errno());
    CloseSocket(s);
}

static void test_udp(void)
{
    LONG a = -1, b = -1, on = 1, r;
    struct sockaddr_in aa, from;
    socklen_t len;
    char buf[32], h1[4], h2[16];
    struct iovec iov[2];
    struct msghdr m;

    a = socket(AF_INET, SOCK_DGRAM, 0);
    b = socket(AF_INET, SOCK_DGRAM, 0);
    result("UDP sockets", a >= 0 && b >= 0, Errno());
    if (a < 0 || b < 0) goto out;
    loopback(&aa, 23461);
    result("UDP bind 127.0.0.1:23461", bind(a, (struct sockaddr *)&aa, sizeof(aa)) == 0, Errno());
    result("UDP FIONBIO on", IoctlSocket(a, FIONBIO, (char *)&on) == 0, Errno());

    r = recv(a, buf, sizeof(buf), 0);
    result("UDP recv empty EWOULDBLOCK", r == -1 && Errno() == EWOULDBLOCK, r < 0 ? Errno() : r);
    len = sizeof(from);
    r = recvfrom(a, buf, sizeof(buf), 0, (struct sockaddr *)&from, &len);
    result("UDP recvfrom empty EWOULDBLOCK", r == -1 && Errno() == EWOULDBLOCK, r < 0 ? Errno() : r);
    memset(&m, 0, sizeof(m));
    iov[0].iov_base = buf; iov[0].iov_len = sizeof(buf);
    m.msg_name = (caddr_t)&from; m.msg_namelen = sizeof(from);
    m.msg_iov = iov; m.msg_iovlen = 1;
    r = recvmsg(a, &m, 0);
    result("UDP recvmsg empty EWOULDBLOCK", r == -1 && Errno() == EWOULDBLOCK, r < 0 ? Errno() : r);
    result("WaitSelect empty UDP times out", wait_one(a, 0, 0) == 0, Errno());

    /* sendmsg: "ping" and "-pong" in two pieces, to a */
    memset(&m, 0, sizeof(m));
    iov[0].iov_base = "ping"; iov[0].iov_len = 4;
    iov[1].iov_base = "-pong"; iov[1].iov_len = 5;
    m.msg_name = (caddr_t)&aa; m.msg_namelen = sizeof(aa);
    m.msg_iov = iov; m.msg_iovlen = 2;
    r = sendmsg(b, &m, 0);
    result("UDP sendmsg two iovecs", r == 9, r < 0 ? Errno() : r);
    result("WaitSelect sees the datagram", wait_one(a, 0, 2) == 1, Errno());

    /* recvmsg into 4 + 16 bytes, with the sender's address */
    memset(h1, 0, sizeof(h1)); memset(h2, 0, sizeof(h2)); memset(&from, 0, sizeof(from));
    memset(&m, 0, sizeof(m));
    iov[0].iov_base = h1; iov[0].iov_len = sizeof(h1);
    iov[1].iov_base = h2; iov[1].iov_len = sizeof(h2);
    m.msg_name = (caddr_t)&from; m.msg_namelen = sizeof(from);
    m.msg_iov = iov; m.msg_iovlen = 2;
    r = recvmsg(a, &m, 0);
    result("UDP recvmsg scatters into two iovecs", r == 9 && !memcmp(h1, "ping", 4) && !memcmp(h2, "-pong", 5),
           r < 0 ? Errno() : r);
    result("UDP recvmsg gives the sender", from.sin_family == AF_INET && from.sin_addr.s_addr == htonl(0x7f000001UL),
           from.sin_family);
    r = recv(a, buf, sizeof(buf), 0);
    result("UDP recv empty again EWOULDBLOCK", r == -1 && Errno() == EWOULDBLOCK, r < 0 ? Errno() : r);
    on = 0;
    result("UDP FIONBIO off", IoctlSocket(a, FIONBIO, (char *)&on) == 0, Errno());
    r = sendto(b, "x", 1, 0, (struct sockaddr *)&aa, sizeof(aa));
    r = recv(a, buf, sizeof(buf), 0);
    result("UDP blocking recv after FIONBIO off", r == 1 && buf[0] == 'x', r < 0 ? Errno() : r);
out:
    if (a >= 0) CloseSocket(a);
    if (b >= 0) CloseSocket(b);
}

static void test_tcp(void)
{
    LONG ls = -1, cs = -1, as = -1, on = 1, r, e;
    struct sockaddr_in la;
    socklen_t len;
    char buf[8];

    ls = socket(AF_INET, SOCK_STREAM, 0);
    cs = socket(AF_INET, SOCK_STREAM, 0);
    result("TCP sockets", ls >= 0 && cs >= 0, Errno());
    if (ls < 0 || cs < 0) goto out;
    loopback(&la, 23462);
    result("TCP bind and listen", bind(ls, (struct sockaddr *)&la, sizeof(la)) == 0 && listen(ls, 2) == 0, Errno());
    result("TCP listener FIONBIO on", IoctlSocket(ls, FIONBIO, (char *)&on) == 0, Errno());
    r = accept(ls, NULL, NULL);
    result("TCP accept with none waiting EWOULDBLOCK", r == -1 && Errno() == EWOULDBLOCK, r < 0 ? Errno() : r);
    if (r >= 0) CloseSocket(r);

    result("TCP client FIONBIO on", IoctlSocket(cs, FIONBIO, (char *)&on) == 0, Errno());
    r = connect(cs, (struct sockaddr *)&la, sizeof(la));
    e = r < 0 ? Errno() : 0;
    result("TCP non-blocking connect: 0 or EINPROGRESS", r == 0 || e == EINPROGRESS, e);
    result("WaitSelect sees the connect writable", wait_one(cs, 1, 3) == 1, Errno());
    e = -1; len = sizeof(e);
    r = getsockopt(cs, SOL_SOCKET, SO_ERROR, &e, &len);
    result("getsockopt SO_ERROR after connect is 0", r == 0 && e == 0, r ? Errno() : e);
    result("WaitSelect sees the listener readable", wait_one(ls, 0, 3) == 1, Errno());
    as = accept(ls, NULL, NULL);
    result("TCP accept", as >= 0, Errno());
    if (as < 0) goto out;
    result("TCP accepted FIONBIO on", IoctlSocket(as, FIONBIO, (char *)&on) == 0, Errno());
    r = recv(as, buf, sizeof(buf), 0);
    result("TCP recv empty EWOULDBLOCK", r == -1 && Errno() == EWOULDBLOCK, r < 0 ? Errno() : r);
    result("TCP send", send(cs, "hello", 5, 0) == 5, Errno());
    result("WaitSelect sees the data", wait_one(as, 0, 3) == 1, Errno());
    r = recv(as, buf, sizeof(buf), 0);
    result("TCP recv", r == 5 && !memcmp(buf, "hello", 5), r < 0 ? Errno() : r);
    CloseSocket(as); as = -1;
    CloseSocket(cs);

    /* nothing listens on 23463: the connect fails, at once or by SO_ERROR */
    cs = socket(AF_INET, SOCK_STREAM, 0);
    loopback(&la, 23463);
    IoctlSocket(cs, FIONBIO, (char *)&on);
    r = connect(cs, (struct sockaddr *)&la, sizeof(la));
    e = r < 0 ? Errno() : 0;
    if (r < 0 && e == EINPROGRESS) {
        wait_one(cs, 1, 3);
        e = 0; len = sizeof(e);
        getsockopt(cs, SOL_SOCKET, SO_ERROR, &e, &len);
    }
    result("TCP refused connect: ECONNREFUSED", e == ECONNREFUSED, e);
out:
    if (as >= 0) CloseSocket(as);
    if (cs >= 0) CloseSocket(cs);
    if (ls >= 0) CloseSocket(ls);
}

int main(void)
{
    printf("GFOpenSocket non-blocking and IoctlSocket probe\n");
    SocketBase = OpenLibrary("bsdsocket.library", 4);
    if (!SocketBase) {
        printf("OpenLibrary bsdsocket.library v4 FAIL\n");
        return 20;
    }
    printf("bsdsocket.library %d.%d\n", SocketBase->lib_Version, SocketBase->lib_Revision);
    test_ioctl();
    test_udp();
    test_tcp();
    printf("PASS=%d FAIL=%d\n", passes, failures);
    CloseLibrary(SocketBase);
    return failures ? 20 : 0;
}
