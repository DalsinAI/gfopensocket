/* bsdsocket.library: each provider call goes to the backend the card layer
 * chose at init (card.c), kept in the master's shared block. MIT. */
#include <exec/types.h>
#include <proto/exec.h>

#include "provider.h"
#include "card.h"

#define BACKEND(sb) ((sb)->shared->card.backend)

LONG prov_open(struct SocketBase *sb)
{
    return BACKEND(sb) == BACKEND_DIRECT ? direct_open(sb) : host_open(sb);
}

void prov_close(struct SocketBase *sb)
{
    if (BACKEND(sb) == BACKEND_DIRECT) direct_close(sb); else host_close(sb);
}

LONG prov_call(struct SocketBase *sb, ULONG cmd, ULONG a0, ULONG a1, ULONG a2, ULONG a3,
               const void *tx, ULONG txlen, void *rx, ULONG rxmax, ULONG *rxlen)
{
    if (BACKEND(sb) == BACKEND_DIRECT)
        return direct_call(sb, cmd, a0, a1, a2, a3, tx, txlen, rx, rxmax, rxlen);
    return host_call(sb, cmd, a0, a1, a2, a3, tx, txlen, rx, rxmax, rxlen);
}

void prov_arm(struct SocketBase *sb, struct Wake *wk)
{
    if (BACKEND(sb) == BACKEND_DIRECT) direct_arm(sb, wk); else host_arm(sb, wk);
}

void prov_disarm(struct SocketBase *sb, struct Wake *wk)
{
    if (BACKEND(sb) == BACKEND_DIRECT) direct_disarm(sb, wk); else host_disarm(sb, wk);
}

/* Direct's stack keeps a waiter per task already; Host needs
 * gfopensocket.library 1.2's AddWaiter and RemWaiter. */
int prov_own_waiter(struct SocketBase *sb)
{
    if (BACKEND(sb) == BACKEND_DIRECT) return sb->provider_sigmask != 0;
    return sb->net_waiters;
}

void prov_add(struct SocketBase *sb, struct Wake *wk)
{
    if (BACKEND(sb) != BACKEND_DIRECT) host_add(sb, wk);
}

void prov_rem(struct SocketBase *sb, struct Wake *wk)
{
    if (BACKEND(sb) != BACKEND_DIRECT) host_rem(sb, wk);
}
