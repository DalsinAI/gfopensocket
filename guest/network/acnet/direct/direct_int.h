/* GFOpenSocketDirect: what direct_stack.c and direct_sock.c share. MIT. */
#ifndef OPENSOCKET_DIRECT_INT_H
#define OPENSOCKET_DIRECT_INT_H

#include "lwip/netif.h"
#include <exec/types.h>
#include <exec/semaphores.h>
#include <exec/tasks.h>
#include <dos/dosextens.h>
#include <devices/sana2.h>

#include "card.h"

#define DIRECT_WAITERS 32

struct DirectStack {
    struct SignalSemaphore lock;   /* held for every call into lwIP */
    struct CardConfig cfg;
    struct Process *proc;
    struct IOSana2Req *ctl;        /* the device, open */
    struct netif en;               /* en0: the SANA-II device */
    struct { struct Task *task; ULONG mask; } wait[DIRECT_WAITERS];
    UBYTE mac[6];
    UWORD mtu;
    UBYTE started, online, en_added, dev_failed;
    UBYTE news, pad[3];            /* something happened: wake the waiters */
    ULONG rx_bytes, tx_bytes, tx_dropped;
    char hostname[32];
};

extern struct DirectStack S;

void direct_lock(void);
void direct_unlock(void);
void direct_pump(void);
void direct_sock_init(void);
LONG direct_command(ULONG cmd, ULONG a0, ULONG a1, ULONG a2, ULONG a3,
                    const UBYTE *tx, ULONG txlen, UBYTE *rx, ULONG rxmax, ULONG *rxlen, ULONG *err);

#endif
