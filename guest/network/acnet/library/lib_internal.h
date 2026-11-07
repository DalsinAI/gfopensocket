#ifndef ACNET_LIB_INTERNAL_H
#define ACNET_LIB_INTERNAL_H

/* What the four parts of ACNet's front end share: lib_base.c (the library,
 * errno, tags, descriptors), lib_sockets.c (the socket calls),
 * lib_select.c (waiting and WaitSelect) and lib_names.c (names and
 * addresses). MIT. */
#include "acnet_lib.h"
#include "provider.h"
#include "achostsocket.h"
#include "bsdsocket_protos.h"

/* The Amiga's errno numbers the library sets itself (netinclude/sys/errno.h;
 * the host tests check the full table against NDK 3.2). */
enum {
    AE_PERM = 1, AE_INTR = 4, AE_IO = 5, AE_NXIO = 6, AE_BADF = 9, AE_NOMEM = 12, AE_FAULT = 14,
    AE_BUSY = 16, AE_INVAL = 22, AE_MFILE = 24, AE_NOTTY = 25, AE_PIPE = 32,
    AE_AGAIN = 35, AE_INPROGRESS = 36, AE_ALREADY = 37,
    AE_NOTSOCK = 38, AE_DESTADDRREQ = 39, AE_MSGSIZE = 40, AE_NOPROTOOPT = 42,
    AE_OPNOTSUPP = 45, AE_AFNOSUPPORT = 47, AE_NETDOWN = 50, AE_NOTCONN = 57,
    AE_TIMEDOUT = 60, AE_NOSYS = 78, AE_LAST = 81
};

/* netdb.h's h_errno values. */
enum { AH_HOST_NOT_FOUND = 1, AH_TRY_AGAIN = 2, AH_NO_RECOVERY = 3, AH_NO_DATA = 4 };

/* Socket levels and options the front end answers itself (sys/socket.h). */
#define A_SOL_SOCKET    0xffff
#define A_SO_SNDTIMEO   0x1005
#define A_SO_RCVTIMEO   0x1006
#define A_SO_TYPE       0x1008
#define A_SO_EVENTMASK  0x2001
#define A_MSG_WAITALL   0x40
#define A_MSG_DONTWAIT  0x80
#define A_UNIQUE_ID     (-1)

/* lib_base.c */
void set_errno(struct SocketBase *sb, LONG e);
LONG fail(struct SocketBase *sb, LONG e);            /* sets errno, returns -1 */
LONG fail_provider(struct SocketBase *sb);           /* errno from the provider's last call */
struct FD *fd_get(struct SocketBase *sb, LONG s);    /* NULL with EBADF */
LONG fd_alloc(struct SocketBase *sb, LONG handle, UBYTE type, LONG at);   /* at < 0: the lowest free */
void fd_free(struct SocketBase *sb, LONG s);         /* the host socket closes with its last reference */
void handle_ref(struct SocketBase *sb, LONG handle);
UBYTE *scratch(struct SocketBase *sb);               /* SCRATCH_SIZE bytes, or NULL with ENOMEM */
const char *errno_string(LONG e);
const char *herrno_string(LONG e);
void set_herrno(struct SocketBase *sb, LONG e);

/* lib_select.c */
LONG wait_items(struct SocketBase *sb, ULONG *items, ULONG *revents, LONG n,
                const struct timeval *tv, ULONG extra, ULONG *got_extra);   /* >0 ready, 0 timeout or extra, -1 errno */
LONG wait_ready(struct SocketBase *sb, LONG handle, ULONG events, const struct timeval *tv);
/* A wait's wake-up for the calling task (struct Wake): wake_begin before
 * the first prov_arm, wake_end after the last prov_disarm. */
LONG wake_begin(struct SocketBase *sb, struct Wake *wk);   /* 0, or -1 ENOMEM */
void wake_end(struct SocketBase *sb, struct Wake *wk);
LONG wait_event(struct SocketBase *sb, struct Wake *wk);   /* the next provider event: 0, or -1 EINTR */
LONG wait_armed_event(struct SocketBase *sb, struct Wake *wk, const struct timeval *tv, ULONG extra, ULONG *got_extra);
void timer_close(struct SocketBase *sb);

/* optional Roadshow BPF compatibility */
void bpf_close_all(struct SocketBase *sb);

/* lib_names.c */
void names_close(struct SocketBase *sb);

#endif
