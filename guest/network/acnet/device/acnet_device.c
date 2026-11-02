/* acnet.device: the one driver for ACNet, AmiCradle's network card
 * (Zorro II, AmiCradle Team product 6; common/protocol/achostsocket.h).
 *
 * It finds the card through expansion.library and claims it, says hello to
 * the host (dropping the last boot's sockets), adds one server to the INT2
 * chain and turns the card's interrupt on. ACNet's bsdsocket.library calls
 * its private vectors (include/acnet_device.h): a command, run under
 * Forbid() with the card's one set of arguments; and waiters, tasks the
 * interrupt signals on each card event. Without a card the device still
 * opens and every command fails ENETDOWN.
 *
 * BeginIO answers IOERR_NOCMD until the SANA-II side exists. Built bare by
 * ../build.sh (bebbo's m68k-amigaos-gcc, NDK 3.2); design
 * docs/architecture/ACNET_DESIGN.md. MIT.
 */
#include <exec/types.h>
#include <exec/resident.h>
#include <exec/devices.h>
#include <exec/errors.h>
#include <exec/execbase.h>
#include <exec/interrupts.h>
#include <exec/memory.h>
#include <exec/tasks.h>
#include <devices/sana2.h>
#include <devices/newstyle.h>
#include <utility/tagitem.h>
#include <hardware/intbits.h>
#include <libraries/configvars.h>
#include <proto/exec.h>
#include <proto/expansion.h>

#include "achostsocket.h"
#include <acnetwork.h>
#include "acnet_device.h"

#define REG(r, decl) register decl __asm(#r)
#define ENETDOWN 50
#define EAGAIN 35
#define ACPACKET_BACKLOG 8
#define ACPACKET_MAX_FRAME 1514
#define ACNET_WORK_SIG SIGBREAKF_CTRL_E
#define ACNET_STOP_SIG SIGBREAKF_CTRL_F

struct ACPacketBacklog {
    UWORD len;
    UBYTE used;
    UBYTE reserved;
    UBYTE data[ACPACKET_MAX_FRAME];
};

struct ACTrackedType {
    ULONG type;
    UBYTE used;
    UBYTE reserved[3];
    struct Sana2PacketTypeStats stats;
};

struct ACNetBase {
    struct Library lib;
    UWORD pad;
    BPTR seglist;
    struct ConfigDev *cd;
    volatile UBYTE *card;          /* the card's base, or NULL */
    ULONG state;                   /* ACN_STATE_* from the last hello */
    UBYTE started, irq_added;
    UBYTE packet_online, packet_configured;
    struct Interrupt irq;
    struct MinList waiters;        /* struct ACNWaiter */
    struct List reads;             /* queued IOSana2Req CMD_READ/READORPHAN */
    struct List events;            /* queued S2_ONEVENT requests */
    struct Task *packet_task;
    struct ACPacketInfo packet_info;
    struct ACPacketBacklog backlog[ACPACKET_BACKLOG];
    struct ACTrackedType tracked[16];
    ULONG reconfigurations;
};

/* amicradle-guest, libraries/opensocket (the open ROM): no writable
 * globals, so the same code runs from the ROM. exec's base is read from
 * address 4 (AbsExecBase) each time, through a register, as gcc 16's
 * -Warray-bounds wants for a pointer made from a constant. */
static inline struct ExecBase *osd_sysbase(void)
{ struct ExecBase **p = (struct ExecBase **)4; __asm__ ("" : "+r"(p)); return *p; }
#define SysBase (osd_sysbase())

/* Run as a program, the device does nothing. This must stay the first code
 * in the file: build.sh keeps source order (-fno-toplevel-reorder). */
int start(void) { return -1; }

/* ACNET_DEVICE_NODE_NAME may name it otherwise at build time (-D), so a
 * test can install it in DEVS:Networks/ as a SANA-II driver that is not
 * GFOpenSocket's own (the card layer's Direct test). */
#ifndef ACNET_DEVICE_NODE_NAME
#define ACNET_DEVICE_NODE_NAME OPENSOCKET_DEVICE_NAME
#endif
static const char dev_name[] = ACNET_DEVICE_NODE_NAME;
/* ACNET_DEVICE_REVISION may be raised at build time (-D), for a test of
 * the card layer's version choice (bsdsocket.library, card.c). */
#ifndef ACNET_DEVICE_REVISION
#define ACNET_DEVICE_REVISION 1
#endif
#define DEV_STR2(x) #x
#define DEV_STR(x) DEV_STR2(x)
#define DEV_VR DEV_STR(ACNET_DEVICE_VERSION) "." DEV_STR(ACNET_DEVICE_REVISION)
static const char dev_id[] = "gfopensocket.device " DEV_VR " (16.10.2026) GFOpenSocket (AmiCradle)\r\n";
static const char ver[] __attribute__((used)) = "$VER: gfopensocket.device " DEV_VR " (16.10.2026) GFOpenSocket (AmiCradle)";

#define R(off) (*(volatile ULONG *)(b->card + (off)))

/* ---- the card ----------------------------------------------------------------- */

/* Copies to and from the card a long at a time (its buffers are long
 * aligned): never CopyMem, which a system patch may do with MOVE16. */
static void to_card(volatile UBYTE *dst, const UBYTE *src, ULONG n)
{
    volatile ULONG *d = (volatile ULONG *)dst;
    while (n >= 4) { *d++ = (ULONG)src[0] << 24 | (ULONG)src[1] << 16 | (ULONG)src[2] << 8 | src[3]; src += 4; n -= 4; }
    volatile UBYTE *db = (volatile UBYTE *)d;
    while (n--) *db++ = *src++;
}
static void from_card(UBYTE *dst, volatile const UBYTE *src, ULONG n)
{
    volatile const ULONG *s = (volatile const ULONG *)src;
    while (n >= 4) { ULONG v = *s++; dst[0] = (UBYTE)(v >> 24); dst[1] = (UBYTE)(v >> 16); dst[2] = (UBYTE)(v >> 8); dst[3] = (UBYTE)v; dst += 4; n -= 4; }
    volatile const UBYTE *sb = (volatile const UBYTE *)s;
    while (n--) *dst++ = *sb++;
}

static LONG call(struct ACNetBase *b, struct ACNCall *c)
{
    if (!b->card) { c->result = -1; c->err = ENETDOWN; c->rxlen = 0; return -1; }
    ULONG tl = c->txlen > ACHS_TX_SIZE ? ACHS_TX_SIZE : c->txlen;
    Forbid();
    R(ACHS_REG_ARG0) = c->arg[0];
    R(ACHS_REG_ARG1) = c->arg[1];
    R(ACHS_REG_ARG2) = c->arg[2];
    R(ACHS_REG_ARG3) = c->arg[3];
    R(ACHS_REG_TXLEN) = tl;
    if (tl) to_card(b->card + ACHS_TX, c->tx, tl);
    R(ACHS_REG_COMMAND) = c->cmd;                 /* the host runs it now */
    c->result = (LONG)R(ACHS_REG_RESULT);
    c->err = R(ACHS_REG_ERRNO);
    c->rxlen = R(ACHS_REG_RXLEN);
    ULONG n = c->rxlen < c->rxmax ? c->rxlen : c->rxmax;
    if (n > ACHS_RX_SIZE) n = ACHS_RX_SIZE;
    if (n && c->rx) from_card(c->rx, b->card + ACHS_RX, n);
    Permit();
    return c->result;
}

/* ---- SANA-II packet facade ---------------------------------------------------- */

/* The packet task finds its device base in its tc_UserData (amicradle-guest, the open ROM: no global). */

static ULONG tag_data(APTR raw, ULONG wanted)
{
    struct TagItem *t = (struct TagItem *)raw;
    while (t) {
        ULONG tag = t->ti_Tag;
        if (tag == TAG_DONE || tag == TAG_END) break;
        if (tag == TAG_IGNORE) { ++t; continue; }
        if (tag == TAG_SKIP) { t += 1 + t->ti_Data; continue; }
        if (tag == TAG_MORE) { t = (struct TagItem *)t->ti_Data; continue; }
        if (tag == wanted) return t->ti_Data;
        ++t;
    }
    return 0;
}

static BOOL copy_callback(APTR fn, APTR to, APTR from, ULONG n)
{
    if (!fn) { if (n) CopyMem(from, to, n); return TRUE; }
    register APTR a0 __asm("a0") = to;
    register APTR a1 __asm("a1") = from;
    register APTR a2 __asm("a2") = fn;
    register ULONG d0 __asm("d0") = n;
    /* The callback is a C function: d0, d1, a0 and a1 are its scratch
     * registers, so a0 and a1 are outputs here too, not inputs only. */
    __asm volatile ("jsr (%%a2)" : "+r"(d0), "+r"(a0), "+r"(a1) : "r"(a2)
                    : "d1", "cc", "memory");
    return d0 != 0;
}

static BOOL copy_from_client(struct IOSana2Req *io, APTR dst, APTR src, ULONG n)
{
    APTR fn = (APTR)tag_data(io->ios2_BufferManagement, S2_CopyFromBuff);
    return copy_callback(fn, dst, src, n);
}

static BOOL copy_to_client(struct IOSana2Req *io, APTR dst, APTR src, ULONG n)
{
    APTR fn = (APTR)tag_data(io->ios2_BufferManagement, S2_CopyToBuff);
    return copy_callback(fn, dst, src, n);
}

static void sana_reply(struct IOSana2Req *io, BYTE error, ULONG wire)
{
    io->ios2_Req.io_Error = error;
    io->ios2_WireError = wire;
    io->ios2_Req.io_Message.mn_Node.ln_Type = NT_REPLYMSG;
    ReplyMsg(&io->ios2_Req.io_Message);
}

static LONG packet_command(struct ACNetBase *b, ULONG command, ULONG a0,
                           const UBYTE *tx, ULONG txlen, UBYTE *rx, ULONG rxmax,
                           ULONG *error, ULONG *rxlen)
{
    struct ACNCall c = { command, { a0, 0, 0, 0 }, tx, txlen, rx, rxmax, -1, 0, 0 };
    LONG r = call(b, &c);
    if (error) *error = c.err;
    if (rxlen) *rxlen = c.rxlen;
    return r;
}

static BOOL packet_info(struct ACNetBase *b)
{
    ULONG err = 0, got = 0;
    LONG r = packet_command(b, ACHS_CMD_PACKET_INFO, 0, NULL, 0,
                            (UBYTE *)&b->packet_info, sizeof(b->packet_info), &err, &got);
    return r >= 0 && err == 0 && got >= sizeof(b->packet_info);
}

static struct ACTrackedType *tracked_type(struct ACNetBase *b, ULONG type)
{
    int i;
    for (i = 0; i < 16; ++i)
        if (b->tracked[i].used && b->tracked[i].type == type) return &b->tracked[i];
    return NULL;
}

static struct ACTrackedType *track_type(struct ACNetBase *b, ULONG type)
{
    int i;
    struct ACTrackedType *t = tracked_type(b, type);
    if (t) return t;
    for (i = 0; i < 16; ++i) if (!b->tracked[i].used) {
        b->tracked[i].used = 1;
        b->tracked[i].type = type;
        b->tracked[i].stats.PacketsSent = 0;
        b->tracked[i].stats.PacketsReceived = 0;
        b->tracked[i].stats.BytesSent = 0;
        b->tracked[i].stats.BytesReceived = 0;
        b->tracked[i].stats.PacketsDropped = 0;
        return &b->tracked[i];
    }
    return NULL;
}

static struct IOSana2Req *take_read_for(struct ACNetBase *b, ULONG type)
{
    struct Node *n, *orphan = NULL;
    struct IOSana2Req *found = NULL;
    Disable();
    for (n = b->reads.lh_Head; n->ln_Succ; n = n->ln_Succ) {
        struct IOSana2Req *io = (struct IOSana2Req *)n;
        if (io->ios2_Req.io_Command == CMD_READ && io->ios2_PacketType == type) {
            found = io; break;
        }
        if (io->ios2_Req.io_Command == S2_READORPHAN && !orphan)
            orphan = n;
    }
    if (!found && !tracked_type(b, type) && orphan)
        found = (struct IOSana2Req *)orphan;
    if (found) Remove((struct Node *)found);
    Enable();
    return found;
}

static BOOL deliver_frame(struct ACNetBase *b, UBYTE *frame, ULONG len)
{
    ULONG type, n;
    UBYTE *src, *dst, *payload;
    struct IOSana2Req *io;
    struct ACTrackedType *t;

    if (len < 14 || len > ACPACKET_MAX_FRAME) return TRUE;
    dst = frame; src = frame + 6;
    type = ((ULONG)frame[12] << 8) | frame[13];
    io = take_read_for(b, type);
    if (!io) return FALSE;

    io->ios2_PacketType = type;
    CopyMem(src, io->ios2_SrcAddr, 6);
    CopyMem(dst, io->ios2_DstAddr, 6);
    io->ios2_Req.io_Flags &= ~(SANA2IOF_BCAST | SANA2IOF_MCAST);
    if (dst[0] == 0xff && dst[1] == 0xff && dst[2] == 0xff &&
        dst[3] == 0xff && dst[4] == 0xff && dst[5] == 0xff)
        io->ios2_Req.io_Flags |= SANA2IOF_BCAST;
    else if (dst[0] & 1)
        io->ios2_Req.io_Flags |= SANA2IOF_MCAST;

    payload = (io->ios2_Req.io_Flags & SANA2IOF_RAW) ? frame : frame + 14;
    n = (io->ios2_Req.io_Flags & SANA2IOF_RAW) ? len : len - 14;
    if (n > io->ios2_DataLength) {
        sana_reply(io, S2ERR_MTU_EXCEEDED, S2WERR_GENERIC_ERROR);
        return TRUE;
    }
    if (!copy_to_client(io, io->ios2_Data, payload, n)) {
        sana_reply(io, S2ERR_SOFTWARE, S2WERR_BUFF_ERROR);
        return TRUE;
    }
    io->ios2_DataLength = n;
    t = tracked_type(b, type);
    if (t) {
        t->stats.PacketsReceived++;
        t->stats.BytesReceived += n;
    }
    sana_reply(io, 0, 0);
    return TRUE;
}

static BOOL backlog_store(struct ACNetBase *b, UBYTE *frame, ULONG len)
{
    int i;
    for (i = 0; i < ACPACKET_BACKLOG; ++i) if (!b->backlog[i].used) {
        b->backlog[i].used = 1;
        b->backlog[i].len = (UWORD)len;
        CopyMem(frame, b->backlog[i].data, len);
        return TRUE;
    }
    return FALSE;
}

static void service_backlog(struct ACNetBase *b)
{
    int i;
    for (i = 0; i < ACPACKET_BACKLOG; ++i)
        if (b->backlog[i].used &&
            deliver_frame(b, b->backlog[i].data, b->backlog[i].len))
            b->backlog[i].used = 0;
}

static BOOL reads_pending(struct ACNetBase *b)
{
    BOOL pending;
    Disable();
    pending = b->reads.lh_Head->ln_Succ != NULL;
    Enable();
    return pending;
}

static void service_packet_reads(struct ACNetBase *b)
{
    UBYTE frame[ACPACKET_MAX_FRAME];
    ULONG err = 0, got = 0;
    LONG n;
    int budget = ACPACKET_BACKLOG;

    service_backlog(b);
    while (b->packet_online && reads_pending(b) && budget-- > 0) {
        n = packet_command(b, ACHS_CMD_PACKET_RECV, sizeof(frame), NULL, 0,
                           frame, sizeof(frame), &err, &got);
        if (n < 0) {
            if (err == EAGAIN) return;
            return;
        }
        if (got > sizeof(frame)) got = sizeof(frame);
        if (!deliver_frame(b, frame, got) && !backlog_store(b, frame, got))
            return;
    }
}

struct ACNewMemList {
    struct Node node;
    UWORD entries;
    struct MemEntry me[2];
};

static struct Task *create_packet_task(STRPTR name, LONG pri, APTR entry, ULONG stacksize, APTR data)
{
    struct ACNewMemList request;
    struct MemList *ml;
    struct Task *task;

    stacksize = (stacksize + 3) & ~3UL;
    request.node.ln_Succ = request.node.ln_Pred = NULL;
    request.node.ln_Type = 0;
    request.node.ln_Pri = 0;
    request.node.ln_Name = NULL;
    request.entries = 2;
    request.me[0].me_Reqs = MEMF_CLEAR | MEMF_PUBLIC;
    request.me[0].me_Length = sizeof(struct Task);
    request.me[1].me_Reqs = MEMF_CLEAR | MEMF_PUBLIC;
    request.me[1].me_Length = stacksize;

    ml = AllocEntry((struct MemList *)&request);
    if ((LONG)ml < 0) return NULL;
    task = (struct Task *)ml->ml_ME[0].me_Addr;
    task->tc_Node.ln_Type = NT_TASK;
    task->tc_Node.ln_Pri = (BYTE)pri;
    task->tc_Node.ln_Name = name;
    task->tc_SPReg = (APTR)((ULONG)ml->ml_ME[1].me_Addr + stacksize);
    task->tc_SPLower = ml->ml_ME[1].me_Addr;
    task->tc_SPUpper = task->tc_SPReg;
    task->tc_MemEntry.lh_Head = (struct Node *)&task->tc_MemEntry.lh_Tail;
    task->tc_MemEntry.lh_Tail = NULL;
    task->tc_MemEntry.lh_TailPred = (struct Node *)&task->tc_MemEntry.lh_Head;
    AddHead(&task->tc_MemEntry, &ml->ml_Node);
    task->tc_UserData = data;
    if (!AddTask(task, entry, NULL)) {
        FreeEntry(ml);
        return NULL;
    }
    return task;
}

static void packet_worker(void)
{
    struct ACNetBase *b = (struct ACNetBase *)FindTask(NULL)->tc_UserData;
    if (!b) { RemTask(NULL); return; }
    for (;;) {
        ULONG sig = Wait(ACNET_WORK_SIG | ACNET_STOP_SIG);
        if (sig & ACNET_STOP_SIG) break;
        if (sig & ACNET_WORK_SIG) service_packet_reads(b);
    }
    b->packet_task = NULL;
    RemTask(NULL);
}

static void signal_packet_worker(struct ACNetBase *b)
{
    if (b->packet_task) Signal(b->packet_task, ACNET_WORK_SIG);
}

static void post_sana_event(struct ACNetBase *b, ULONG event)
{
    for (;;) {
        struct Node *n;
        struct IOSana2Req *match = NULL;
        Disable();
        for (n = b->events.lh_Head; n->ln_Succ; n = n->ln_Succ) {
            struct IOSana2Req *io = (struct IOSana2Req *)n;
            if (io->ios2_WireError & event) { match = io; break; }
        }
        if (match) Remove((struct Node *)match);
        Enable();
        if (!match) break;
        match->ios2_WireError = event;
        sana_reply(match, 0, event);
    }
}

/* The INT2 server: acknowledge the card's event and wake every waiting task.
 * It never claims the interrupt (the CIA-A shares the chain). */
ULONG irq_c(struct ACNetBase *b) __attribute__((used));
ULONG irq_c(struct ACNetBase *b)
{
    if (!b->card || !R(ACHS_REG_EVENT)) return 0;
    R(ACHS_REG_EVENT) = 0;
    struct ACNWaiter *w;
    for (w = (struct ACNWaiter *)b->waiters.mlh_Head; w->node.mln_Succ; w = (struct ACNWaiter *)w->node.mln_Succ)
        if (w->waiting) Signal(w->task, w->sigmask);
    signal_packet_worker(b);
    return 0;
}
void irq_entry(void);
__asm__(
    "    .text\n"
    "    .even\n"
    "_irq_entry:\n"              /* a1 = is_Data; the chain goes on when Z is set */
    "    move.l a1,-(sp)\n"
    "    jsr _irq_c\n"
    "    addq.l #4,sp\n"
    "    tst.l d0\n"
    "    rts\n");

static void hello(struct ACNetBase *b, ULONG first)
{
    ULONG rx[2] = { 0, 0 };
    struct ACNCall c = { ACHS_CMD_HELLO, { first, 0, 0, 0 }, NULL, 0, (UBYTE *)rx, sizeof rx, 0, 0, 0 };
    call(b, &c);
    b->state = (ULONG)c.result == ACHS_VERSION && c.err == 0 && c.rxlen >= 4 ? (rx[0] & ACHS_STATE_ONLINE ? ACN_STATE_CARD | ACN_STATE_ONLINE : ACN_STATE_CARD) : 0;
}

static void start_card(struct ACNetBase *b)
{
    b->started = 1;
    struct Library *ExpansionBase = OpenLibrary("expansion.library", 37);
    if (!ExpansionBase) return;
    struct ConfigDev *cd = FindConfigDev(NULL, ACNET_MANUFACTURER, ACNET_PRODUCT);
    if (!cd) cd = FindConfigDev(NULL, ACNET_MANUFACTURER_OLD, ACNET_PRODUCT);
    CloseLibrary(ExpansionBase);
    if (!cd || !cd->cd_BoardAddr) return;
    volatile UBYTE *card = cd->cd_BoardAddr;
    if (*(volatile ULONG *)(card + ACHS_REG_SIGNATURE) != ACHS_SIGNATURE ||
        (*(volatile ULONG *)(card + ACHS_REG_VERSION) >> 16) != (ACHS_VERSION >> 16)) return;
    b->cd = cd;
    cd->cd_Flags &= ~CDF_CONFIGME;                /* claimed: ShowConfig names the driver */
    cd->cd_Driver = b;
    b->card = card;
    hello(b, 1);                                  /* the host drops the last boot's sockets */
    b->irq.is_Node.ln_Type = NT_INTERRUPT;
    b->irq.is_Node.ln_Pri = 0;
    b->irq.is_Node.ln_Name = (char *)dev_name;
    b->irq.is_Data = b;
    b->irq.is_Code = (void (*)(void))irq_entry;
    AddIntServer(INTB_PORTS, &b->irq);
    b->irq_added = 1;
    R(ACHS_REG_IRQ_ENABLE) = 1;
    packet_info(b);
    if (!b->packet_task)
        b->packet_task = create_packet_task((STRPTR)"gfopensocket.packet", 5, packet_worker, 8192, b);
}

static void stop_card(struct ACNetBase *b)
{
    if (b->card && b->packet_online) {
        packet_command(b, ACHS_CMD_PACKET_OFFLINE, 0, NULL, 0, NULL, 0, NULL, NULL);
        b->packet_online = 0;
    }
    if (b->card) R(ACHS_REG_IRQ_ENABLE) = 0;
    if (b->irq_added) RemIntServer(INTB_PORTS, &b->irq);
    b->irq_added = 0;
    Forbid();
    if (b->packet_task) {
        RemTask(b->packet_task);
        b->packet_task = NULL;
    }
    Permit();
    if (b->cd) { b->cd->cd_Driver = NULL; b->cd->cd_Flags |= CDF_CONFIGME; }
    b->card = NULL;
}

static const UWORD sana_commands[] = {
    CMD_READ, CMD_WRITE, CMD_FLUSH,
    S2_DEVICEQUERY, S2_GETSTATIONADDRESS, S2_CONFIGINTERFACE,
    S2_ADDMULTICASTADDRESS, S2_DELMULTICASTADDRESS,
    S2_MULTICAST, S2_BROADCAST, S2_TRACKTYPE, S2_UNTRACKTYPE,
    S2_GETTYPESTATS, S2_GETSPECIALSTATS, S2_GETGLOBALSTATS,
    S2_ONEVENT, S2_READORPHAN, S2_ONLINE, S2_OFFLINE,
    S2_ADDMULTICASTADDRESSES, S2_DELMULTICASTADDRESSES,
    NSCMD_DEVICEQUERY, 0
};

static void set_sana_error(struct IOSana2Req *io, BYTE error, ULONG wire)
{
    io->ios2_Req.io_Error = error;
    io->ios2_WireError = wire;
}

static BOOL packet_is_multicast(UBYTE *a)
{
    return (a[0] & 1) != 0 &&
           !(a[0] == 0xff && a[1] == 0xff && a[2] == 0xff &&
             a[3] == 0xff && a[4] == 0xff && a[5] == 0xff);
}

static void sana_device_query(struct ACNetBase *b, struct IOSana2Req *io)
{
    struct Sana2DeviceQuery *q = (struct Sana2DeviceQuery *)io->ios2_StatData;
    if (!q) { set_sana_error(io, S2ERR_BAD_ARGUMENT, S2WERR_BAD_STATDATA); return; }
    packet_info(b);
    q->SizeAvailable = sizeof(*q);
    q->SizeSupplied = sizeof(*q);
    q->DevQueryFormat = 0;
    q->DeviceLevel = 0;
    q->AddrFieldSize = 48;
    q->MTU = b->packet_info.mtu ? b->packet_info.mtu : 1500;
    q->BPS = b->packet_info.bps ? b->packet_info.bps : 100000000;
    q->HardwareType = S2WireType_Ethernet;
    q->RawMTU = 1514;
    set_sana_error(io, 0, 0);
}

static LONG sana_online(struct ACNetBase *b)
{
    ULONG err = 0;
    LONG r;
    if (b->packet_online) return 1;
    r = packet_command(b, ACHS_CMD_PACKET_ONLINE, 0, NULL, 0, NULL, 0, &err, NULL);
    if (r < 0 || err) return -1;
    b->packet_online = 1;
    b->reconfigurations++;
    packet_info(b);
    post_sana_event(b, S2EVENT_ONLINE | S2EVENT_CONFIGCHANGED);
    signal_packet_worker(b);
    return 0;
}

static LONG sana_offline(struct ACNetBase *b)
{
    ULONG err = 0;
    LONG r;
    if (!b->packet_online) return 1;
    r = packet_command(b, ACHS_CMD_PACKET_OFFLINE, 0, NULL, 0, NULL, 0, &err, NULL);
    if (r < 0 || err) return -1;
    b->packet_online = 0;
    b->reconfigurations++;
    post_sana_event(b, S2EVENT_OFFLINE | S2EVENT_CONFIGCHANGED);
    return 0;
}

static void sana_write(struct ACNetBase *b, struct IOSana2Req *io)
{
    UBYTE frame[ACPACKET_MAX_FRAME];
    ULONG payload = io->ios2_DataLength;
    ULONG frame_len, err = 0;
    LONG r;
    struct ACTrackedType *t;

    if (!b->packet_online) {
        set_sana_error(io, S2ERR_OUTOFSERVICE, S2WERR_UNIT_OFFLINE);
        return;
    }
    if (io->ios2_Req.io_Flags & SANA2IOF_RAW) {
        if (payload < 14 || payload > sizeof(frame)) {
            set_sana_error(io, S2ERR_MTU_EXCEEDED, S2WERR_GENERIC_ERROR); return;
        }
        if (!copy_from_client(io, frame, io->ios2_Data, payload)) {
            set_sana_error(io, S2ERR_SOFTWARE, S2WERR_BUFF_ERROR); return;
        }
        frame_len = payload;
    } else {
        if (payload > 1500) {
            set_sana_error(io, S2ERR_MTU_EXCEEDED, S2WERR_GENERIC_ERROR); return;
        }
        CopyMem(io->ios2_DstAddr, frame, 6);
        CopyMem(b->packet_info.mac, frame + 6, 6);
        frame[12] = (UBYTE)(io->ios2_PacketType >> 8);
        frame[13] = (UBYTE)io->ios2_PacketType;
        if (!copy_from_client(io, frame + 14, io->ios2_Data, payload)) {
            set_sana_error(io, S2ERR_SOFTWARE, S2WERR_BUFF_ERROR); return;
        }
        frame_len = payload + 14;
    }
    r = packet_command(b, ACHS_CMD_PACKET_SEND, 0, frame, frame_len,
                       NULL, 0, &err, NULL);
    if (r < 0 || err) {
        set_sana_error(io, S2ERR_TX_FAILURE, S2WERR_GENERIC_ERROR); return;
    }
    t = tracked_type(b, io->ios2_PacketType);
    if (t) {
        t->stats.PacketsSent++;
        t->stats.BytesSent += payload;
    }
    set_sana_error(io, 0, 0);
}

static void sana_global_stats(struct ACNetBase *b, struct IOSana2Req *io)
{
    struct ACPacketStats ps;
    struct Sana2DeviceStats *s = (struct Sana2DeviceStats *)io->ios2_StatData;
    ULONG err = 0, got = 0;
    if (!s) { set_sana_error(io, S2ERR_BAD_ARGUMENT, S2WERR_BAD_STATDATA); return; }
    if (packet_command(b, ACHS_CMD_PACKET_STATS, 0, NULL, 0,
                       (UBYTE *)&ps, sizeof(ps), &err, &got) < 0 ||
        err || got < sizeof(ps)) {
        set_sana_error(io, S2ERR_SOFTWARE, S2WERR_GENERIC_ERROR); return;
    }
    s->PacketsReceived = ps.rx_packets;
    s->PacketsSent = ps.tx_packets;
    s->BadData = ps.rx_dropped;
    s->Overruns = ps.rx_dropped;
    s->Unused = 0;
    s->UnknownTypesReceived = 0;
    s->Reconfigurations = b->reconfigurations;
    s->LastStart.tv_secs = 0;
    s->LastStart.tv_micro = 0;
    set_sana_error(io, 0, 0);
}

static void abort_list(struct List *list)
{
    for (;;) {
        struct Node *n;
        Disable();
        n = RemHead(list);
        Enable();
        if (!n) break;
        sana_reply((struct IOSana2Req *)n, IOERR_ABORTED, 0);
    }
}

static BOOL remove_pending(struct List *list, struct IORequest *target)
{
    struct Node *n;
    BOOL found = FALSE;
    Disable();
    for (n = list->lh_Head; n->ln_Succ; n = n->ln_Succ) {
        if ((struct IORequest *)n == target) {
            Remove(n); found = TRUE; break;
        }
    }
    Enable();
    return found;
}

/* ---- device vectors ------------------------------------------------------------ */

static BPTR dev_expunge(REG(a6, struct ACNetBase *b))
{
    if (b->lib.lib_OpenCnt) { b->lib.lib_Flags |= LIBF_DELEXP; return 0; }
    stop_card(b);
    BPTR seg = b->seglist;
    Remove(&b->lib.lib_Node);
    FreeMem((UBYTE *)b - b->lib.lib_NegSize, b->lib.lib_NegSize + b->lib.lib_PosSize);
    return seg;
}

static LONG dev_open(REG(a1, struct IORequest *io), REG(d0, ULONG unit), REG(d1, ULONG flags), REG(a6, struct ACNetBase *b))
{
    (void)flags;
    b->lib.lib_OpenCnt++;
    if (unit != 0) {
        b->lib.lib_OpenCnt--;
        io->io_Error = IOERR_OPENFAIL;
        return IOERR_OPENFAIL;
    }
    if (!b->started) start_card(b);
    b->lib.lib_Flags &= ~LIBF_DELEXP;
    io->io_Device = (struct Device *)b;
    io->io_Unit = NULL;
    io->io_Error = 0;
    io->io_Message.mn_Node.ln_Type = NT_REPLYMSG;
    return 0;
}

static BPTR dev_close(REG(a1, struct IORequest *io), REG(a6, struct ACNetBase *b))
{
    io->io_Device = (struct Device *)-1;
    io->io_Unit = (struct Unit *)-1;
    if (--b->lib.lib_OpenCnt == 0 && (b->lib.lib_Flags & LIBF_DELEXP)) return dev_expunge(b);
    return 0;
}

static ULONG dev_null(void) { return 0; }

static void dev_beginio(REG(a1, struct IORequest *raw), REG(a6, struct ACNetBase *b))
{
    struct IOSana2Req *io = (struct IOSana2Req *)raw;
    UWORD cmd = raw->io_Command;
    BOOL queued = FALSE;

    raw->io_Message.mn_Node.ln_Type = NT_MESSAGE;
    raw->io_Error = 0;

    if (cmd == NSCMD_DEVICEQUERY) {
        struct IOStdReq *std = (struct IOStdReq *)raw;
        struct NSDeviceQueryResult *q = (struct NSDeviceQueryResult *)std->io_Data;
        if (!q || std->io_Length < sizeof(*q)) raw->io_Error = IOERR_BADLENGTH;
        else {
            q->nsdqr_DevQueryFormat = 0;
            q->nsdqr_SizeAvailable = sizeof(*q);
            q->nsdqr_DeviceType = NSDEVTYPE_SANA2;
            q->nsdqr_DeviceSubType = 0;
            q->nsdqr_SupportedCommands = (APTR)sana_commands;
            std->io_Actual = sizeof(*q);
        }
        goto done;
    }

    if (raw->io_Message.mn_Length < sizeof(struct IOSana2Req)) {
        raw->io_Error = IOERR_BADLENGTH;
        goto done;
    }

    io->ios2_WireError = 0;
    switch (cmd) {
        case CMD_READ:
        case S2_READORPHAN:
            if (!b->packet_online) {
                set_sana_error(io, S2ERR_OUTOFSERVICE, S2WERR_UNIT_OFFLINE);
                break;
            }
            raw->io_Flags &= ~IOF_QUICK;
            Disable();
            AddTail(&b->reads, (struct Node *)io);
            Enable();
            signal_packet_worker(b);
            queued = TRUE;
            break;

        case CMD_WRITE:
            sana_write(b, io);
            break;

        case S2_BROADCAST:
            io->ios2_DstAddr[0] = 0xff; io->ios2_DstAddr[1] = 0xff;
            io->ios2_DstAddr[2] = 0xff; io->ios2_DstAddr[3] = 0xff;
            io->ios2_DstAddr[4] = 0xff; io->ios2_DstAddr[5] = 0xff;
            sana_write(b, io);
            break;

        case S2_MULTICAST:
            if (!packet_is_multicast(io->ios2_DstAddr))
                set_sana_error(io, S2ERR_BAD_ADDRESS, S2WERR_BAD_MULTICAST);
            else
                sana_write(b, io);
            break;

        case CMD_FLUSH:
            abort_list(&b->reads);
            abort_list(&b->events);
            break;

        case S2_DEVICEQUERY:
            sana_device_query(b, io);
            break;

        case S2_GETSTATIONADDRESS:
            if (!packet_info(b))
                set_sana_error(io, S2ERR_SOFTWARE, S2WERR_GENERIC_ERROR);
            else {
                CopyMem(b->packet_info.mac, io->ios2_SrcAddr, 6);
                set_sana_error(io, 0, 0);
            }
            break;

        case S2_CONFIGINTERFACE:
            b->packet_configured = 1;
            b->reconfigurations++;
            post_sana_event(b, S2EVENT_CONFIGCHANGED);
            break;

        case S2_ADDMULTICASTADDRESS:
        case S2_DELMULTICASTADDRESS:
        case S2_ADDMULTICASTADDRESSES:
        case S2_DELMULTICASTADDRESSES:
            if (!packet_is_multicast(io->ios2_SrcAddr))
                set_sana_error(io, S2ERR_BAD_ADDRESS, S2WERR_BAD_MULTICAST);
            break;

        case S2_TRACKTYPE:
            if (tracked_type(b, io->ios2_PacketType))
                set_sana_error(io, S2ERR_BAD_STATE, S2WERR_ALREADY_TRACKED);
            else if (!track_type(b, io->ios2_PacketType))
                set_sana_error(io, S2ERR_NO_RESOURCES, S2WERR_GENERIC_ERROR);
            break;

        case S2_UNTRACKTYPE: {
            struct ACTrackedType *t = tracked_type(b, io->ios2_PacketType);
            if (!t) set_sana_error(io, S2ERR_BAD_STATE, S2WERR_NOT_TRACKED);
            else t->used = 0;
            break;
        }

        case S2_GETTYPESTATS: {
            struct ACTrackedType *t = tracked_type(b, io->ios2_PacketType);
            if (!io->ios2_StatData)
                set_sana_error(io, S2ERR_BAD_ARGUMENT, S2WERR_BAD_STATDATA);
            else if (!t)
                set_sana_error(io, S2ERR_BAD_STATE, S2WERR_NOT_TRACKED);
            else
                CopyMem(&t->stats, io->ios2_StatData, sizeof(t->stats));
            break;
        }

        case S2_GETSPECIALSTATS:
            if (!io->ios2_StatData)
                set_sana_error(io, S2ERR_BAD_ARGUMENT, S2WERR_BAD_STATDATA);
            else
                ((struct Sana2SpecialStatHeader *)io->ios2_StatData)->RecordCountSupplied = 0;
            break;

        case S2_GETGLOBALSTATS:
            sana_global_stats(b, io);
            break;

        case S2_ONEVENT:
            if (!(io->ios2_WireError & (S2EVENT_ONLINE | S2EVENT_OFFLINE |
                                        S2EVENT_CONFIGCHANGED | S2EVENT_ERROR |
                                        S2EVENT_TX | S2EVENT_RX | S2EVENT_BUFF |
                                        S2EVENT_SOFTWARE))) {
                set_sana_error(io, S2ERR_NOT_SUPPORTED, S2WERR_BAD_EVENT);
            } else {
                raw->io_Flags &= ~IOF_QUICK;
                Disable();
                AddTail(&b->events, (struct Node *)io);
                Enable();
                queued = TRUE;
            }
            break;

        case S2_ONLINE: {
            LONG r = sana_online(b);
            if (r == 1) set_sana_error(io, S2ERR_BAD_STATE, S2WERR_UNIT_ONLINE);
            else if (r < 0) set_sana_error(io, S2ERR_OUTOFSERVICE, S2WERR_GENERIC_ERROR);
            break;
        }

        case S2_OFFLINE: {
            LONG r = sana_offline(b);
            if (r == 1) set_sana_error(io, S2ERR_BAD_STATE, S2WERR_UNIT_OFFLINE);
            else if (r < 0) set_sana_error(io, S2ERR_SOFTWARE, S2WERR_GENERIC_ERROR);
            if (r == 0) abort_list(&b->reads);
            break;
        }

        default:
            raw->io_Error = IOERR_NOCMD;
            break;
    }

done:
    if (queued) return;
    raw->io_Message.mn_Node.ln_Type = NT_REPLYMSG;
    if (!(raw->io_Flags & IOF_QUICK)) ReplyMsg(&raw->io_Message);
}

static LONG dev_abortio(REG(a1, struct IORequest *io), REG(a6, struct ACNetBase *b))
{
    if (remove_pending(&b->reads, io) || remove_pending(&b->events, io)) {
        io->io_Error = IOERR_ABORTED;
        io->io_Message.mn_Node.ln_Type = NT_REPLYMSG;
        ReplyMsg(&io->io_Message);
        return 0;
    }
    return IOERR_NOCMD;
}

/* ---- private vectors (acnet_device.h) -------------------------------------------- */

static LONG acn_call(REG(a0, struct ACNCall *c), REG(a6, struct ACNetBase *b)) { return call(b, c); }

static void acn_addwaiter(REG(a0, struct ACNWaiter *w), REG(a6, struct ACNetBase *b))
{
    Disable();
    AddTail((struct List *)&b->waiters, (struct Node *)&w->node);
    Enable();
}

static void acn_remwaiter(REG(a0, struct ACNWaiter *w), REG(a6, struct ACNetBase *b))
{
    (void)b;
    Disable();
    Remove((struct Node *)&w->node);
    Enable();
}

static ULONG acn_state(REG(a6, struct ACNetBase *b))
{
    if (!b->card) return 0;
    hello(b, 0);
    return b->state;
}

/* ---- the ROMTag --------------------------------------------------------------------- */

static struct ACNetBase *dev_init(REG(d0, struct ACNetBase *b), REG(a0, BPTR seglist), REG(a6, struct ExecBase *sys))
{
    (void)sys;
    b->seglist = seglist;
    b->lib.lib_Node.ln_Type = NT_DEVICE;
    b->lib.lib_Node.ln_Name = (char *)dev_name;
    b->lib.lib_Flags = LIBF_SUMUSED | LIBF_CHANGED;
    b->lib.lib_Version = ACNET_DEVICE_VERSION;
    b->lib.lib_Revision = ACNET_DEVICE_REVISION;
    b->lib.lib_IdString = (APTR)dev_id;
    b->waiters.mlh_Head = (struct MinNode *)&b->waiters.mlh_Tail;
    b->waiters.mlh_Tail = NULL;
    b->waiters.mlh_TailPred = (struct MinNode *)&b->waiters.mlh_Head;
    b->reads.lh_Head = (struct Node *)&b->reads.lh_Tail;
    b->reads.lh_Tail = NULL;
    b->reads.lh_TailPred = (struct Node *)&b->reads.lh_Head;
    b->events.lh_Head = (struct Node *)&b->events.lh_Tail;
    b->events.lh_Tail = NULL;
    b->events.lh_TailPred = (struct Node *)&b->events.lh_Head;
    return b;
}

static const APTR dev_vectors[] = {
    (APTR)dev_open, (APTR)dev_close, (APTR)dev_expunge, (APTR)dev_null,
    (APTR)dev_beginio, (APTR)dev_abortio,
    (APTR)acn_call, (APTR)acn_addwaiter, (APTR)acn_remwaiter, (APTR)acn_state,
    (APTR)-1
};
static const struct { ULONG size; const APTR *vectors; APTR data; APTR init; } dev_inittable = {
    sizeof(struct ACNetBase), dev_vectors, NULL, (APTR)dev_init,
};
const struct Resident dev_romtag = {
    RTC_MATCHWORD, (struct Resident *)&dev_romtag, (APTR)(&dev_romtag + 1),
    /* No start class: in the open ROM (amicradle-guest) nothing starts it
     * at boot. bsdsocket.library's card layer starts it after DOS when the
     * card is fitted and no newer gfopensocket.device is on the disk (card.c);
     * then "DEVS:Networks/gfopensocket.device" opens it from memory. */
    RTF_AUTOINIT, ACNET_DEVICE_VERSION, NT_DEVICE, -120, (char *)dev_name, (char *)dev_id, (APTR)&dev_inittable,
};
