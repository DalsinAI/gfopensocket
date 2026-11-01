/* acnetwork.library: native ACNet session API for AmigaOS 3.x.
 * bsdsocket.library is a compatibility client of this library.
 * MIT. */
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/resident.h>
#include <exec/execbase.h>
#include <proto/exec.h>

#include "acnetwork_internal.h"

#define ENETDOWN 50
#define ENOMEM   12

int start(void) { return -1; }

static const char lib_name[] = ACNETWORK_LIBRARY_NAME;
/* ACNETWORK_LIBRARY_REVISION may be raised at build time (-D), as the
 * device's may (acnet_device.c). */
#ifndef ACNETWORK_LIBRARY_REVISION
#define ACNETWORK_LIBRARY_REVISION 1
#endif
#define LIB_STR2(x) #x
#define LIB_STR(x) LIB_STR2(x)
#define LIB_VR LIB_STR(ACNETWORK_LIBRARY_VERSION) "." LIB_STR(ACNETWORK_LIBRARY_REVISION)
static const char lib_id[] = "gfopensocket.library " LIB_VR " (16.10.2026) GFOpenSocket (AmiCradle)\r\n";
static const char lib_ver[] __attribute__((used)) = "$VER: gfopensocket.library " LIB_VR " (16.10.2026) GFOpenSocket (AmiCradle)";

/* The RomTag near the start, its table defined at the end. A RomTag that
 * ends exactly where the hunk ends is not found when the library is
 * loaded, and GCC 16 put it last (9 Oct 2026). */
struct lib_init_table { ULONG size; const APTR *vectors; APTR data; APTR init; };
static const struct lib_init_table lib_inittable;
const struct Resident lib_romtag = {
    RTC_MATCHWORD, (struct Resident *)&lib_romtag, (APTR)(&lib_romtag + 1),
    RTF_AUTOINIT, ACNETWORK_LIBRARY_VERSION, NT_LIBRARY, 0,
    (char *)lib_name, (char *)lib_id, (APTR)&lib_inittable,
};

static inline LONG dev_call(struct Library *dev, struct ACNCall *call)
{
    register LONG d0 __asm("d0");
    register struct ACNCall *a0 __asm("a0") = call;
    register struct Library *a6 __asm("a6") = dev;
    __asm volatile ("jsr -42(%%a6)" : "=r"(d0), "+r"(a0) : "r"(a6) : "d1", "a1", "cc", "memory");
    return d0;
}

static inline void dev_waiter(struct Library *dev, struct ACNWaiter *waiter, int add)
{
    register struct ACNWaiter *a0 __asm("a0") = waiter;
    register struct Library *a6 __asm("a6") = dev;
    if (add)
        __asm volatile ("jsr -48(%%a6)" : "+r"(a0) : "r"(a6) : "d0", "d1", "a1", "cc", "memory");
    else
        __asm volatile ("jsr -54(%%a6)" : "+r"(a0) : "r"(a6) : "d0", "d1", "a1", "cc", "memory");
}

static inline ULONG dev_state(struct Library *dev)
{
    register ULONG d0 __asm("d0");
    register struct Library *a6 __asm("a6") = dev;
    __asm volatile ("jsr -60(%%a6)" : "=r"(d0) : "r"(a6) : "d1", "a0", "a1", "cc", "memory");
    return d0;
}

static LONG session_open(struct ACNetworkBase *base)
{
    base->dev_io = AllocMem(sizeof(struct IOStdReq), MEMF_PUBLIC | MEMF_CLEAR);
    if (!base->dev_io) return ENOMEM;
    if (OPENSOCKET_OPEN_DEVICE(base->dev_io)) return ENETDOWN;
    base->dev = (struct Library *)base->dev_io->io_Device;
    base->sigbit = AllocSignal(-1);
    if (base->sigbit < 0) return ENOMEM;
    base->waiter.task = base->owner;
    base->waiter.sigmask = 1UL << base->sigbit;
    base->waiter.waiting = 0;
    dev_waiter(base->dev, &base->waiter, 1);
    return 0;
}

static void session_close(struct ACNetworkBase *base)
{
    if (base->dev && base->sigbit >= 0)
        dev_waiter(base->dev, &base->waiter, 0);
    if (base->sigbit >= 0) FreeSignal(base->sigbit);
    base->sigbit = -1;
    if (base->dev_io) {
        if (base->dev) CloseDevice((struct IORequest *)base->dev_io);
        FreeMem(base->dev_io, sizeof(struct IOStdReq));
    }
    base->dev_io = NULL;
    base->dev = NULL;
}

static struct ACNetworkBase *lib_init(REG(d0, struct ACNetworkBase *base), REG(a0, BPTR seglist), REG(a6, struct ExecBase *sys))
{
    (void)sys;
    base->seglist = seglist;
    base->master = NULL;
    base->sigbit = -1;
    base->lib.lib_Node.ln_Type = NT_LIBRARY;
    base->lib.lib_Node.ln_Name = (char *)lib_name;
    base->lib.lib_Flags = LIBF_SUMUSED | LIBF_CHANGED;
    base->lib.lib_Version = ACNETWORK_LIBRARY_VERSION;
    base->lib.lib_Revision = ACNETWORK_LIBRARY_REVISION;
    base->lib.lib_IdString = (APTR)lib_id;
    return base;
}

static struct ACNetworkBase *lib_open(REG(d0, ULONG version), REG(a6, struct ACNetworkBase *master))
{
    (void)version;
    master->lib.lib_OpenCnt++;
    master->lib.lib_Flags &= ~LIBF_DELEXP;
    ULONG neg = master->lib.lib_NegSize;
    ULONG total = neg + master->lib.lib_PosSize;
    UBYTE *mem = AllocMem(total, MEMF_PUBLIC | MEMF_CLEAR);
    if (!mem) { master->lib.lib_OpenCnt--; return NULL; }
    CopyMem((UBYTE *)master - neg, mem, neg + sizeof(struct Library));
    CacheClearU();
    struct ACNetworkBase *base = (struct ACNetworkBase *)(mem + neg);
    base->lib.lib_Node.ln_Succ = base->lib.lib_Node.ln_Pred = NULL;
    base->lib.lib_OpenCnt = 1;
    base->master = master;
    base->owner = FindTask(NULL);
    base->sigbit = -1;
    base->last_error = 0;
    if (session_open(base)) {
        session_close(base);
        FreeMem(mem, total);
        master->lib.lib_OpenCnt--;
        return NULL;
    }
    return base;
}

static BPTR lib_expunge(REG(a6, struct ACNetworkBase *master))
{
    if (master->master) return 0;
    if (master->lib.lib_OpenCnt) { master->lib.lib_Flags |= LIBF_DELEXP; return 0; }
    BPTR seg = master->seglist;
    Remove(&master->lib.lib_Node);
    FreeMem((UBYTE *)master - master->lib.lib_NegSize,
            master->lib.lib_NegSize + master->lib.lib_PosSize);
    return seg;
}

static BPTR lib_close(REG(a6, struct ACNetworkBase *base))
{
    struct ACNetworkBase *master = base->master;
    if (!master) return 0;
    session_close(base);
    FreeMem((UBYTE *)base - base->lib.lib_NegSize,
            base->lib.lib_NegSize + base->lib.lib_PosSize);
    if (--master->lib.lib_OpenCnt == 0 && (master->lib.lib_Flags & LIBF_DELEXP))
        return lib_expunge(master);
    return 0;
}

static ULONG lib_reserved(void) { return 0; }

static LONG acnetwork_call(REG(a0, struct ACNetworkRequest *request), REG(a6, struct ACNetworkBase *base))
{
    if (!request) return -1;
    struct ACNCall call;
    call.cmd = request->command;
    call.arg[0] = request->arg[0];
    call.arg[1] = request->arg[1];
    call.arg[2] = request->arg[2];
    call.arg[3] = request->arg[3];
    call.tx = request->tx;
    call.txlen = request->txlen;
    call.rx = request->rx;
    call.rxmax = request->rxmax;
    call.result = -1;
    call.err = ENETDOWN;
    call.rxlen = 0;
    if (base->dev) dev_call(base->dev, &call);
    request->result = call.result;
    request->error = call.err;
    request->rxlen = call.rxlen;
    base->last_error = call.err;
    return call.result;
}

static void acnetwork_arm(REG(a6, struct ACNetworkBase *base))
{
    base->waiter.task = FindTask(NULL);
    base->waiter.waiting = 1;
}

static void acnetwork_disarm(REG(a6, struct ACNetworkBase *base))
{
    base->waiter.waiting = 0;
}

static ULONG acnetwork_signalmask(REG(a6, struct ACNetworkBase *base))
{
    return base->waiter.sigmask;
}

static ULONG acnetwork_lasterror(REG(a6, struct ACNetworkBase *base))
{
    return base->last_error;
}

static ULONG acnetwork_state(REG(a6, struct ACNetworkBase *base))
{
    return base->dev ? dev_state(base->dev) : 0;
}

static const APTR acnetwork_vectors[] = {
    (APTR)lib_open, (APTR)lib_close, (APTR)lib_expunge, (APTR)lib_reserved,
    (APTR)acnetwork_call, (APTR)acnetwork_arm, (APTR)acnetwork_disarm,
    (APTR)acnetwork_signalmask, (APTR)acnetwork_lasterror, (APTR)acnetwork_state,
    (APTR)-1
};

static const struct lib_init_table lib_inittable = {
    sizeof(struct ACNetworkBase), acnetwork_vectors, NULL, (APTR)lib_init,
};
