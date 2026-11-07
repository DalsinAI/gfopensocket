/* bsdsocket.library's Host backend: the calls go through gfopensocket.library
 * (acnetwork.library), gfopensocket.device and the GFOpenSocket card to the
 * host's sockets. Each is the ROM's, or the disk's when that is newer: the
 * card layer chose and started them (card.c). Once
 * the card layer has chosen Host, an open never fails here: with either
 * file missing, or the bridge down, each call answers ENETDOWN.
 * MIT. */
#include <exec/types.h>
#include <proto/exec.h>

#include <acnetwork.h>
#include "provider.h"

#define ENETDOWN 50

static inline LONG net_call(struct Library *lib, struct ACNetworkRequest *request)
{
    register LONG d0 __asm("d0");
    register struct ACNetworkRequest *a0 __asm("a0") = request;
    register struct Library *a6 __asm("a6") = lib;
    __asm volatile ("jsr -30(%%a6)" : "=r"(d0), "+r"(a0) : "r"(a6) : "d1", "a1", "cc", "memory");
    return d0;
}

static inline void net_arm(struct Library *lib)
{
    register struct Library *a6 __asm("a6") = lib;
    __asm volatile ("jsr -36(%%a6)" : : "r"(a6) : "d0", "d1", "a0", "a1", "cc", "memory");
}

static inline void net_disarm(struct Library *lib)
{
    register struct Library *a6 __asm("a6") = lib;
    __asm volatile ("jsr -42(%%a6)" : : "r"(a6) : "d0", "d1", "a0", "a1", "cc", "memory");
}

/* gfopensocket.library 1.2: a waiter of the caller's own */
static inline void net_waiter(struct Library *lib, struct ACNWaiter *w, int add)
{
    register struct ACNWaiter *a0 __asm("a0") = w;
    register struct Library *a6 __asm("a6") = lib;
    if (add) __asm volatile ("jsr -66(%%a6)" : "+r"(a0) : "r"(a6) : "d0", "d1", "a1", "cc", "memory");
    else     __asm volatile ("jsr -72(%%a6)" : "+r"(a0) : "r"(a6) : "d0", "d1", "a1", "cc", "memory");
}

static inline ULONG net_signalmask(struct Library *lib)
{
    register ULONG d0 __asm("d0");
    register struct Library *a6 __asm("a6") = lib;
    __asm volatile ("jsr -48(%%a6)" : "=r"(d0) : "r"(a6) : "d1", "a0", "a1", "cc", "memory");
    return d0;
}

LONG host_open(struct SocketBase *sb)
{
    sb->network = OpenLibrary(sb->shared->card.lib_disk ? (STRPTR)"LIBS:" ACNETWORK_LIBRARY_NAME
                                                        : (STRPTR)ACNETWORK_LIBRARY_NAME,
                              ACNETWORK_LIBRARY_VERSION);
    if (!sb->network) return 0;                /* each call: ENETDOWN */
    sb->provider_sigmask = net_signalmask(sb->network);
    if (!sb->provider_sigmask) {
        CloseLibrary(sb->network);
        sb->network = NULL;
        return 0;
    }
    sb->net_waiters = sb->network->lib_Version > ACNETWORK_LIBRARY_VERSION ||
                      sb->network->lib_Revision >= ACNETWORK_WAITERS_REVISION;
    return 0;
}

void host_close(struct SocketBase *sb)
{
    if (sb->network) CloseLibrary(sb->network);
    sb->network = NULL;
    sb->provider_sigmask = 0;
}

LONG host_call(struct SocketBase *sb, ULONG cmd, ULONG a0, ULONG a1, ULONG a2, ULONG a3,
               const void *tx, ULONG txlen, void *rx, ULONG rxmax, ULONG *rxlen)
{
    struct ACNetworkRequest request;
    request.command = cmd;
    request.arg[0] = a0;
    request.arg[1] = a1;
    request.arg[2] = a2;
    request.arg[3] = a3;
    request.tx = tx;
    request.txlen = txlen;
    request.rx = rx;
    request.rxmax = rxmax;
    request.result = -1;
    request.error = ENETDOWN;
    request.rxlen = 0;
    if (sb->network) net_call(sb->network, &request);
    sb->last_err = request.error;
    if (rxlen) *rxlen = request.rxlen;
    return request.result;
}

/* The opener arms its gfopensocket.library base's waiter; another task of its
 * program arms its own (wk->sig, from wake_begin), so each waiting task is
 * signalled. With an older gfopensocket.library every task arms the base's
 * one waiter, as before 4.5. */
void host_arm(struct SocketBase *sb, struct Wake *wk)
{
    if (wk->sig >= 0) wk->w.waiting = 1;
    else if (sb->network) net_arm(sb->network);
}

void host_disarm(struct SocketBase *sb, struct Wake *wk)
{
    if (wk->sig >= 0) wk->w.waiting = 0;
    else if (sb->network) net_disarm(sb->network);
}

void host_add(struct SocketBase *sb, struct Wake *wk)
{
    wk->w.task = wk->task;
    wk->w.sigmask = wk->mask;
    wk->w.waiting = 0;
    net_waiter(sb->network, &wk->w, 1);
}

void host_rem(struct SocketBase *sb, struct Wake *wk)
{
    net_waiter(sb->network, &wk->w, 0);
}
