#ifndef ACNETWORK_INTERNAL_H
#define ACNETWORK_INTERNAL_H

#include <exec/types.h>
#include <exec/libraries.h>
#include <exec/io.h>
#include <exec/tasks.h>
#include <exec/devices.h>
#include <dos/dos.h>

#include <acnetwork.h>
#include "acnet_device.h"

#define REG(r, decl) register decl __asm(#r)

struct ACNetworkBase {
    struct Library lib;
    UWORD pad;
    struct ACNetworkBase *master;
    BPTR seglist;
    struct Task *owner;
    struct IOStdReq *dev_io;
    struct Library *dev;
    struct ACNWaiter waiter;
    BYTE sigbit;
    UBYTE pad2[3];
    ULONG last_error;
};

/* amicradle-guest, libraries/opensocket (the open ROM): no writable
 * globals, so the same code runs from the ROM. exec's base is read from
 * address 4 (AbsExecBase) each time, through a register, as gcc 16's
 * -Warray-bounds wants for a pointer made from a constant. */
static inline struct ExecBase *osl_sysbase(void)
{ struct ExecBase **p = (struct ExecBase **)4; __asm__ ("" : "+r"(p)); return *p; }
#define SysBase (osl_sysbase())

#endif
