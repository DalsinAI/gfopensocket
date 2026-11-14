/* ACNet bsdsocket.library: the iovec helpers sendmsg/recvmsg use (lib_io.c
 * includes this once). Kept apart so the host test runs this very code:
 * the includer gives struct SocketBase, fail(), AE_FAULT, AE_MSGSIZE,
 * CopyMem and struct msghdr. MIT. */
#ifndef ACNET_LIB_MSGVEC_H
#define ACNET_LIB_MSGVEC_H

/* *total is the iovecs' size, cut to limit when clip is set: a stream
 * takes what fits (POSIX send/recv return a short count), and a receive
 * buffer bigger than one transfer is fine. Only a datagram that does not
 * fit is EMSGSIZE. */
static LONG msg_iov_size(struct SocketBase *sb, const struct msghdr *msg,
                         ULONG limit, int clip, ULONG *total)
{
    ULONG i, n = 0;

    if (!msg) return fail(sb, AE_FAULT);
    if (msg->msg_iovlen && !msg->msg_iov) return fail(sb, AE_FAULT);

    for (i = 0; i < msg->msg_iovlen; ++i) {
        ULONG len = (ULONG)msg->msg_iov[i].iov_len;
        if (len && !msg->msg_iov[i].iov_base) return fail(sb, AE_FAULT);
        if (len > limit - n) {
            if (!clip) return fail(sb, AE_MSGSIZE);
            n = limit;
            continue;
        }
        n += len;
    }
    *total = n;
    return 0;
}

static void msg_gather(UBYTE *dst, const struct msghdr *msg, ULONG total)
{
    ULONG i, pos = 0;
    for (i = 0; i < msg->msg_iovlen && pos < total; ++i) {
        ULONG len = (ULONG)msg->msg_iov[i].iov_len;
        if (len > total - pos) len = total - pos;
        if (len) CopyMem(msg->msg_iov[i].iov_base, dst + pos, len);
        pos += len;
    }
}

static void msg_scatter(const UBYTE *src, ULONG len, struct msghdr *msg)
{
    ULONG i, pos = 0;
    for (i = 0; i < msg->msg_iovlen && pos < len; ++i) {
        ULONG take = (ULONG)msg->msg_iov[i].iov_len;
        if (take > len - pos) take = len - pos;
        if (take) CopyMem((APTR)(src + pos), msg->msg_iov[i].iov_base, take);
        pos += take;
    }
}

#endif
