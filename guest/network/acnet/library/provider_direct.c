/* bsdsocket.library's Direct backend: GFOpenSocketDirect, GFOpenSocket's own
 * TCP/IP stack (lwIP) over the SANA-II device the card layer chose (card.c).
 *
 * The stack keeps writable globals, so it is not linked into the library:
 * the library carries it as a hunk image (direct_image.c, made by
 * direct/build.sh) and loads it into RAM with romseg.c at the init, when
 * Direct is chosen. The library itself keeps nothing writable, so it still
 * runs from the ROM. The stack then stays for the library's life.
 *
 * Each opener gets a signal of its own; the stack signals it, while armed,
 * whenever anything happens on any socket, and the front end polls again.
 * The stack keeps a waiter per task, so another task of the opener's program
 * arms with its own task and signal (struct Wake): before 4.5 every task armed
 * as the opener, so the opener was woken in their place.
 * With no stack (memory ran out), every call answers ENETDOWN. MIT. */
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/resident.h>
#include <proto/exec.h>

#include "provider.h"
#include "card.h"
#include "direct.h"
#include "romseg.h"

#define ENETDOWN 50
#define ENOMEM   12

extern const unsigned long direct_image_longs;
extern const unsigned long direct_image[];

struct DirectLoaded {
    BPTR seglist;
    const struct DirectOps *ops;
    APTR stack;
};

#define LOADED(sb) ((struct DirectLoaded *)(sb)->shared->direct)

/* From the init (lib_base.c), when the card layer chose Direct. */
void direct_load(struct SocketShared *sh)
{
    struct DirectLoaded *d = AllocMem(sizeof(*d), MEMF_PUBLIC | MEMF_CLEAR);
    struct Resident *rt;
    if (!d) return;
    d->seglist = rs_load((const ULONG *)direct_image, direct_image_longs);
    rt = d->seglist ? rs_romtag(d->seglist) : NULL;
    if (rt) d->ops = (const struct DirectOps *)rt->rt_Init;
    if (!d->ops || d->ops->magic != DIRECT_MAGIC || d->ops->version != DIRECT_VERSION ||
        !(d->stack = d->ops->start(&sh->card))) {
        if (d->seglist) rs_unload(d->seglist);
        FreeMem(d, sizeof(*d));
        return;
    }
    sh->direct = d;
}

LONG direct_open(struct SocketBase *sb)
{
    if (!LOADED(sb)) return 0;                 /* each call: ENETDOWN */
    sb->sigbit = AllocSignal(-1);
    if (sb->sigbit < 0) return ENOMEM;
    sb->provider_sigmask = 1UL << sb->sigbit;
    return 0;
}

void direct_close(struct SocketBase *sb)
{
    if (LOADED(sb)) LOADED(sb)->ops->disarm(LOADED(sb)->stack, sb->owner);
    if (sb->sigbit >= 0) FreeSignal(sb->sigbit);
    sb->sigbit = -1;
    sb->provider_sigmask = 0;
}

LONG direct_call(struct SocketBase *sb, ULONG cmd, ULONG a0, ULONG a1, ULONG a2, ULONG a3,
                 const void *tx, ULONG txlen, void *rx, ULONG rxmax, ULONG *rxlen)
{
    struct DirectLoaded *d = LOADED(sb);
    ULONG err = 0, n = 0;
    LONG r;
    if (!d) {
        sb->last_err = ENETDOWN;
        if (rxlen) *rxlen = 0;
        return -1;
    }
    r = d->ops->call(d->stack, cmd, a0, a1, a2, a3, tx, txlen, rx, rxmax, &n, &err);
    sb->last_err = err;
    if (rxlen) *rxlen = n;
    return r;
}

void direct_arm(struct SocketBase *sb, struct Wake *wk)
{
    if (LOADED(sb) && wk->mask)
        LOADED(sb)->ops->arm(LOADED(sb)->stack, wk->task, wk->mask);
}

void direct_disarm(struct SocketBase *sb, struct Wake *wk)
{
    if (LOADED(sb)) LOADED(sb)->ops->disarm(LOADED(sb)->stack, wk->task);
}
