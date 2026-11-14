/* lib_msgvec.h (sendmsg/recvmsg iovecs), run on the build machine: a stream
 * send bigger than one transfer is cut to it (POSIX gives a short count),
 * and the gather copies only what was cut; a datagram that does not fit is
 * EMSGSIZE; a receive buffer bigger than one transfer is fine; NULL bases
 * are EFAULT. libtorrent's TCP writes failed "Message too long" without
 * the cut. MIT. Built by the host-tests workflow:
 *   cc -Wall -Werror -I../../library -o msgvec_test msgvec_test.c */
#include <stdio.h>
#include <string.h>
#include <sys/uio.h>
#include <sys/socket.h>

typedef unsigned long ULONG;
typedef long LONG;
typedef unsigned char UBYTE;
typedef void *APTR;
struct SocketBase { int last_err; };
#define AE_FAULT 14
#define AE_MSGSIZE 40
static LONG fail(struct SocketBase *sb, int e) { sb->last_err = e; return -1; }
#define CopyMem(s, d, n) memcpy((d), (s), (n))

#include "lib_msgvec.h"

#define TX 16128UL
static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } \
                      else printf("PASS %s\n", #c); } while (0)

int main(void)
{
    struct SocketBase sb = { 0 };
    static UBYTE a[10000], b[10000], dst[TX + 8], in[TX];
    struct iovec v[3];
    struct msghdr m;
    ULONG tot = 0;

    memset(a, 1, sizeof a);
    memset(b, 2, sizeof b);
    v[0].iov_base = a; v[0].iov_len = sizeof a;
    v[1].iov_base = b; v[1].iov_len = sizeof b;
    memset(&m, 0, sizeof m);
    m.msg_iov = v;
    m.msg_iovlen = 2;

    /* stream: 20000 bytes cut to one transfer */
    CHECK(msg_iov_size(&sb, &m, TX, 1, &tot) == 0 && tot == TX);
    memset(dst, 0xee, sizeof dst);
    msg_gather(dst, &m, tot);
    CHECK(dst[0] == 1 && dst[9999] == 1 && dst[10000] == 2 && dst[TX - 1] == 2);
    CHECK(dst[TX] == 0xee); /* nothing past the cut */

    /* datagram over the limit */
    sb.last_err = 0;
    CHECK(msg_iov_size(&sb, &m, TX, 0, &tot) == -1 && sb.last_err == AE_MSGSIZE);

    /* fits whole either way */
    v[1].iov_len = 100;
    CHECK(msg_iov_size(&sb, &m, TX, 0, &tot) == 0 && tot == 10100);
    CHECK(msg_iov_size(&sb, &m, TX, 1, &tot) == 0 && tot == 10100);

    /* NULL base after the cut is still EFAULT; a zero-length one is fine */
    v[1].iov_len = sizeof b;
    v[2].iov_base = NULL; v[2].iov_len = 5;
    m.msg_iovlen = 3;
    sb.last_err = 0;
    CHECK(msg_iov_size(&sb, &m, TX, 1, &tot) == -1 && sb.last_err == AE_FAULT);
    v[2].iov_len = 0;
    CHECK(msg_iov_size(&sb, &m, TX, 1, &tot) == 0 && tot == TX);
    sb.last_err = 0;
    CHECK(msg_iov_size(&sb, NULL, TX, 1, &tot) == -1 && sb.last_err == AE_FAULT);

    /* receive: scatter fills the iovecs in order and stops at len */
    memset(in, 3, sizeof in);
    memset(a, 0, sizeof a);
    memset(b, 0, sizeof b);
    m.msg_iovlen = 2;
    msg_scatter(in, 10050, &m);
    CHECK(a[9999] == 3 && b[0] == 3 && b[49] == 3 && b[50] == 0);

    printf("%s\n", fails ? "FAILED" : "all passed");
    return fails != 0;
}
