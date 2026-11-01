#ifndef ACNET_LIB_H
#define ACNET_LIB_H

/* ACNet bsdsocket.library: what the implementation (bsdsocket.c) and the
 * generated vector table (gen_vectors.py) share. */
#include <exec/types.h>
#include <exec/libraries.h>
#include <exec/devices.h>
#include <dos/dos.h>
#include <sys/types.h>
#include <exec/io.h>
#include <devices/timer.h>
#include <utility/tagitem.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#include "acnet_device.h"
#include "card.h"

#define REG(r, decl) register decl __asm(#r)   /* bebbo gcc: an argument in a register */

#define ACNET_LIB_VERSION   4
#define ACNET_LIB_REVISION  99
#define DTABLE_DEFAULT      64
#define DTABLE_MAX          256
#define NAME_MAX_ADDRS      32
#define SCRATCH_SIZE        (16 + 0x4000)   /* a socket address and a receive buffer */

/* One descriptor in an opener's table. */
struct FD {
    LONG  handle;          /* the host's socket handle; 0: free */
    UBYTE nonblock, async, type, pad;
    struct timeval rcvtimeo, sndtimeo;
    ULONG eventmask;
};

#define ACNET_BPF_CHANNELS 8
struct BPFLocal {
    LONG handle;           /* HostSocket BPF handle; 0: this opener does not own it */
    ULONG buffer_size;
    struct timeval timeout;
    ULONG notify_mask;
    ULONG interrupt_mask;
    UBYTE attached;
    UBYTE immediate;
    UBYTE pad[2];
};

/* What all openers share: the handle counts and the sockets waiting to be
 * obtained, changed under Forbid(), and the backend the card layer chose at
 * init (card.c). In memory the master allocates at init, not in globals, so
 * the library runs from a ROM (amigachrome-guest, libraries/opensocket). */
struct SocketShared {
    struct { LONG handle, refs; } refs[DTABLE_MAX];
    struct { LONG id, handle; UBYTE type, used, pad[2]; } released[16];
    LONG next_id;
    struct CardConfig card;
    APTR direct;                   /* the Direct backend's stack, when it runs */
};

/* Every OpenLibrary gets a base of its own, as classic Amiga socket libraries give: the
 * master's jump table and Library header are copied in front of it. */
struct SocketBase {
    struct Library lib;
    UWORD pad;
    struct SocketBase *master;     /* NULL in the master itself */
    BPTR seglist;                  /* the master's */
    struct SocketShared *shared;   /* the master's, in every base */
    /* per opener */
    struct Task *owner;
    struct Library *network;       /* acnetwork.library, opened per BSD opener */
    ULONG provider_sigmask;        /* event signal owned by acnetwork.library */
    struct IOStdReq *dev_io;       /* legacy direct-provider fields, currently unused */
    struct Library *dev;
    struct ACNWaiter waiter;
    BYTE sigbit;
    UBYTE pad2[3];
    LONG errno_val, herrno_val;
    ULONG last_err;                /* the provider's errno for the last call */
    UBYTE *scratch;                /* SCRATCH_SIZE bytes, allocated on first need */
    APTR errno_ptr; LONG errno_size;
    LONG *herrno_ptr;
    ULONG sigintr, sigio, sigurg, sigevent;
    LONG dtablesize;
    struct FD *fds;
    struct BPFLocal bpf[ACNET_BPF_CHANNELS];
    struct MsgPort *tport;
    struct timerequest *treq;      /* timer.device, opened for the first timeout */
    UBYTE tdev_open;
    UBYTE pad3[3];
    /* answers that stay valid until the next call of the same kind */
    struct hostent hent;
    char hname[256];
    char *hnull[1];
    char *haddrs[NAME_MAX_ADDRS + 1];
    ULONG haddr[NAME_MAX_ADDRS];
    struct servent sent;
    char sname[64], sproto[16];
    struct netent nent;
    char nname[64];
    char *naliases[1];
    struct protoent pent;
    char ntoa[16];
    char hostname[64];
};

/* amigachrome-guest, libraries/opensocket (the open ROM): no writable
 * globals, so the same code runs from the ROM. exec's base is read from
 * address 4 (AbsExecBase) each time, through a register, as gcc 16's
 * -Warray-bounds wants for a pointer made from a constant. */
static inline struct ExecBase *acn_sysbase(void)
{ struct ExecBase **p = (struct ExecBase **)4; __asm__ ("" : "+r"(p)); return *p; }
#define SysBase (acn_sysbase())

struct SocketBase *lib_open(REG(d0, ULONG version), REG(a6, struct SocketBase *master));
BPTR lib_close(REG(a6, struct SocketBase *sb));
BPTR lib_expunge(REG(a6, struct SocketBase *master));
ULONG lib_reserved(void);
void lib_unimplemented(struct SocketBase *sb);

#endif
