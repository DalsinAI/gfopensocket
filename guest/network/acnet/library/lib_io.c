/* ACNet bsdsocket.library: stream/datagram I/O. MIT. */
#include <exec/types.h>
#include <proto/exec.h>
#include <sys/socket.h>
#include "lib_internal.h"

static LONG io_wait(struct SocketBase *sb, struct FD *f, ULONG ev, int send)
{
    struct timeval *tv=send?&f->sndtimeo:&f->rcvtimeo;
    LONG r=wait_ready(sb,f->handle,ev,tv);
    if(r==0) return fail(sb,AE_TIMEDOUT);
    return r<0?-1:0;
}

LONG bsd_send(struct SocketBase *sb,LONG s,APTR buf,LONG len,LONG flags)
{
    struct FD *f=fd_get(sb,s); if(!f)return -1; if(len<0)return fail(sb,AE_INVAL);
    for(;;){LONG r=prov_call(sb,ACHS_CMD_SEND,f->handle,flags,0,0,buf,len,NULL,0,NULL); if(r>=0)return r;
      if(sb->last_err!=AE_AGAIN||f->nonblock||(flags&A_MSG_DONTWAIT))return fail_provider(sb);
      if(io_wait(sb,f,ACHS_POLLOUT,1)<0)return -1;}
}
LONG bsd_recv(struct SocketBase *sb,LONG s,APTR buf,LONG len,LONG flags)
{
    struct FD *f=fd_get(sb,s); ULONG n=0; if(!f)return -1; if(len<0)return fail(sb,AE_INVAL);
    for(;;){LONG r=prov_call(sb,ACHS_CMD_RECV,f->handle,len,flags,0,NULL,0,buf,len,&n); if(r>=0)return r;
      if(sb->last_err!=AE_AGAIN||f->nonblock||(flags&A_MSG_DONTWAIT))return fail_provider(sb);
      if(io_wait(sb,f,ACHS_POLLIN,0)<0)return -1;}
}
LONG bsd_sendto(struct SocketBase *sb,LONG s,APTR buf,LONG len,LONG flags,struct sockaddr *to,socklen_t tolen)
{
    struct FD *f=fd_get(sb,s); UBYTE *x=scratch(sb); if(!f||!x)return -1; if(len<0||!to||tolen>16)return fail(sb,AE_INVAL);
    if((ULONG)len>ACHS_TX_SIZE-16)return fail(sb,AE_MSGSIZE);   /* the address and the data must fit the card's transmit buffer (and scratch), as sendmsg checks */
    CopyMem(to,x,tolen); CopyMem(buf,x+16,len); for(;;){LONG r=prov_call(sb,ACHS_CMD_SENDTO,f->handle,flags,0,0,x,16+len,NULL,0,NULL); if(r>=0)return r;
      if(sb->last_err!=AE_AGAIN||f->nonblock||(flags&A_MSG_DONTWAIT))return fail_provider(sb);
      if(io_wait(sb,f,ACHS_POLLOUT,1)<0)return -1;}
}
LONG bsd_recvfrom(struct SocketBase *sb,LONG s,APTR buf,LONG len,LONG flags,struct sockaddr *addr,socklen_t *addrlen)
{
    struct FD *f=fd_get(sb,s); UBYTE *x=scratch(sb); ULONG n=0; if(!f||!x)return -1;
    for(;;){LONG r=prov_call(sb,ACHS_CMD_RECVFROM,f->handle,len,flags,0,NULL,0,x,SCRATCH_SIZE,&n); if(r>=0){
      if(n>=16){ULONG d=n-16; if(d>(ULONG)len)d=len; CopyMem(x+16,buf,d); if(addr&&addrlen){ULONG m=*addrlen<16?*addrlen:16;CopyMem(x,addr,m);*addrlen=16;} return r;} return r;}
      if(sb->last_err!=AE_AGAIN||f->nonblock||(flags&A_MSG_DONTWAIT))return fail_provider(sb);
      if(io_wait(sb,f,ACHS_POLLIN,0)<0)return -1;}
}


/* ---- scatter/gather message I/O -------------------------------------------
 *
 * The classic Amiga socket ABI exposes sendmsg()/recvmsg(). HostSocket transports
 * one contiguous payload, so gather/scatter lives here in the guest library.
 * Ancillary data is not part of ACNet v1.
 */

#include "lib_msgvec.h"

LONG bsd_sendmsg(struct SocketBase *sb, LONG s, struct msghdr *msg, LONG flags)
{
    struct FD *f = fd_get(sb, s);
    UBYTE *x;
    ULONG total, limit, off = 0;
    LONG r;

    if (!f) return -1;
    if (!msg) return fail(sb, AE_FAULT);
    if (msg->msg_control && msg->msg_controllen) return fail(sb, AE_OPNOTSUPP);

    if (msg->msg_name) {
        if (msg->msg_namelen < 8 || msg->msg_namelen > 16)
            return fail(sb, AE_INVAL);
        limit = ACHS_TX_SIZE - 16;
        off = 16;
    } else {
        limit = ACHS_TX_SIZE;
    }

    /* a stream (no name) sends what fits; a datagram must fit whole */
    if (msg_iov_size(sb, msg, limit, !off, &total) < 0) return -1;
    x = scratch(sb);
    if (!x) return -1;

    if (off) {
        ULONG n = (ULONG)msg->msg_namelen;
        ULONG i;
        for (i = 0; i < 16; ++i) x[i] = 0;
        CopyMem(msg->msg_name, x, n);
    }
    msg_gather(x + off, msg, total);

    for (;;) {
        if (off)
            r = prov_call(sb, ACHS_CMD_SENDTO, f->handle, flags, 0, 0,
                          x, total + 16, NULL, 0, NULL);
        else
            r = prov_call(sb, ACHS_CMD_SEND, f->handle, flags, 0, 0,
                          x, total, NULL, 0, NULL);
        if (r >= 0) return r;
        if (sb->last_err != AE_AGAIN ||
            f->nonblock || (flags & A_MSG_DONTWAIT))
            return fail_provider(sb);
        if (io_wait(sb, f, ACHS_POLLOUT, 1) < 0) return -1;
    }
}

LONG bsd_recvmsg(struct SocketBase *sb, LONG s, struct msghdr *msg, LONG flags)
{
    struct FD *f = fd_get(sb, s);
    UBYTE *x;
    ULONG capacity, limit, n = 0;
    LONG r;

    if (!f) return -1;
    if (!msg) return fail(sb, AE_FAULT);

    limit = msg->msg_name ? (ACHS_RX_SIZE - 16) : ACHS_RX_SIZE;
    if (msg_iov_size(sb, msg, limit, 1, &capacity) < 0) return -1;
    x = scratch(sb);
    if (!x) return -1;

    for (;;) {
        if (msg->msg_name) {
            r = prov_call(sb, ACHS_CMD_RECVFROM, f->handle, capacity, flags, 0,
                          NULL, 0, x, ACHS_RX_SIZE, &n);
        } else {
            r = prov_call(sb, ACHS_CMD_RECV, f->handle, capacity, flags, 0,
                          NULL, 0, x, ACHS_RX_SIZE, &n);
        }

        if (r >= 0) {
            ULONG data_len = (ULONG)r;

            msg->msg_flags = 0;
            if (msg->msg_control) msg->msg_controllen = 0;

            if (msg->msg_name) {
                ULONG avail = n >= 16 ? n - 16 : 0;
                ULONG name_len = (ULONG)msg->msg_namelen;
                ULONG copy_name = name_len < 16 ? name_len : 16;
                if (n < 16) return fail(sb, AE_INVAL);
                if (copy_name) CopyMem(x, msg->msg_name, copy_name);
                msg->msg_namelen = 16;
                if (data_len > avail) data_len = avail;
                if (data_len > capacity) data_len = capacity;
                msg_scatter(x + 16, data_len, msg);
            } else {
                if (data_len > n) data_len = n;
                if (data_len > capacity) data_len = capacity;
                msg_scatter(x, data_len, msg);
            }
            return r;
        }

        if (sb->last_err != AE_AGAIN ||
            f->nonblock || (flags & A_MSG_DONTWAIT))
            return fail_provider(sb);
        if (io_wait(sb, f, ACHS_POLLIN, 0) < 0) return -1;
    }
}
