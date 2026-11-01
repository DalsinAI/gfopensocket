/* OpenSocketDirect: the stack's process, its SANA-II interface, and what
 * lwIP asks of the platform (memory, time, random numbers).
 *
 * lwIP runs with NO_SYS: it is single-threaded, so every call into it holds
 * S.lock, a SignalSemaphore, whether from this process (frames in, timers)
 * or from a bsdsocket.library caller (direct_sock.c). Frames go out from
 * whichever task holds the lock: each write has an IORequest and buffer of
 * its own, replied to a port nobody waits on, and is taken back once
 * CheckIO says it is done. Frames come in on CMD_READ requests (raw, for
 * IPv4 and ARP) replied to this process.
 *
 * This file is part of the stack's own hunk image (direct.h), loaded into
 * RAM, so it keeps its state in globals. MIT. */
#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/etharp.h"
#include "lwip/dhcp.h"
#include "lwip/dns.h"
#include "lwip/timeouts.h"
#include "lwip/pbuf.h"
#include "netif/ethernet.h"

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/execbase.h>
#include <exec/resident.h>
#include <exec/semaphores.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <devices/sana2.h>
#include <devices/timer.h>
#include <utility/tagitem.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/timer.h>

#include "card.h"
#include "direct.h"
#include "direct_int.h"

struct ExecBase *SysBase;
struct DosLibrary *DOSBase;
struct Device *TimerBase;
struct DirectStack S;

#define REG(r, decl) register decl __asm(#r)
#define NREAD   8          /* CMD_READs kept posted: 6 for IPv4, 2 for ARP */
#define NWRITE  16         /* frames that can be on their way out at once */
#define FRAME   1536

static struct MsgPort *rport, *wport, *tport;
static struct IOSana2Req *rd[NREAD], *wr[NWRITE];
static UBYTE *rdbuf[NREAD], *wrbuf[NWRITE];
static UBYTE wrbusy[NWRITE];
static struct timerequest *treq;
static struct TagItem bufmgmt[3];
static ULONG rnd_state;

/* ---- what lwIP asks of the platform ----------------------------------- */

void *direct_malloc(size_t n) { return AllocVec(n ? n : 1, MEMF_PUBLIC); }
void direct_free(void *p) { if (p) FreeVec(p); }
void *direct_calloc(size_t n, size_t m) { size_t t = n * m; return AllocVec(t ? t : 1, MEMF_PUBLIC | MEMF_CLEAR); }

u32_t sys_now(void)
{
    struct timeval tv;
    if (!TimerBase) return 0;
    GetSysTime(&tv);
    return (u32_t)(tv.tv_secs * 1000UL + tv.tv_micro / 1000UL);
}

/* netif.c's netif_find() reads an interface number with atoi */
int atoi(const char *s)
{
    int v = 0, neg = 0;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-' || *s == '+') neg = *s++ == '-';
    while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
    return neg ? -v : v;
}

unsigned long direct_rand(void)
{
    ULONG x = rnd_state ? rnd_state : 0x2545f491UL;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    rnd_state = x;
    return x;
}

/* ---- the lock, and waking the waiters --------------------------------- */

void direct_lock(void) { ObtainSemaphore(&S.lock); }

/* Loopback frames queued while the lock was held are delivered (lwIP's
 * loop interface, NO_SYS: polled), then the lock goes, then every armed
 * waiter is signalled if anything happened. */
void direct_unlock(void)
{
    int news, i;
    direct_pump();
    news = S.news;
    S.news = 0;
    ReleaseSemaphore(&S.lock);
    if (!news) return;
    Forbid();
    for (i = 0; i < DIRECT_WAITERS; i++)
        if (S.wait[i].task) Signal(S.wait[i].task, S.wait[i].mask);
    Permit();
}

void direct_pump(void)
{
    int n;
    for (n = 0; n < 64; n++) {
        struct netif *ni;
        int queued = 0;
        NETIF_FOREACH(ni) if (ni->loop_first) queued = 1;
        if (!queued) break;
        netif_poll_all();
    }
}

static void arm(APTR st, struct Task *task, ULONG mask)
{
    int i, free = -1;
    (void)st;
    Forbid();
    for (i = 0; i < DIRECT_WAITERS; i++) {
        if (S.wait[i].task == task) { S.wait[i].mask = mask; Permit(); return; }
        if (!S.wait[i].task && free < 0) free = i;
    }
    if (free >= 0) { S.wait[free].task = task; S.wait[free].mask = mask; }
    Permit();
}

static void disarm(APTR st, struct Task *task)
{
    int i;
    (void)st;
    Forbid();
    for (i = 0; i < DIRECT_WAITERS; i++)
        if (S.wait[i].task == task) S.wait[i].task = NULL;
    Permit();
}

/* ---- the SANA-II interface -------------------------------------------- */

/* Both directions copy into or out of one of our FRAME-byte buffers (a
 * CMD_READ's rdbuf, a CMD_WRITE's wrbuf). The driver chooses n: a frame
 * longer than FRAME (jumbo, stacked VLAN tags) is refused here, before the
 * copy, and the device answers that read with an error; frames_in posts
 * the read again. */
static BOOL copy_buf(REG(a0, APTR to), REG(a1, APTR from), REG(d0, ULONG n))
{
    if (n > FRAME) return FALSE;
    CopyMem(from, to, n);
    return TRUE;
}

/* SANA-II's flags (SANA2IOF_RAW) travel in io_Flags, which SendIO and DoIO
 * overwrite (to 0, or IOF_QUICK): measured on t5-net, a raw CMD_WRITE sent
 * with SendIO left with a second Ethernet header in front. So reads and
 * writes go to the device's BeginIO vector directly, as SANA-II wants. */
static void begin_io(struct IOSana2Req *io)
{
    register struct IORequest *a1 __asm("a1") = (struct IORequest *)io;
    register struct Device *a6 __asm("a6") = io->ios2_Req.io_Device;
    __asm volatile ("jsr -30(%%a6)" : "+r"(a1) : "r"(a6) : "d0", "d1", "a0", "cc", "memory");
}

static void write_reap(void)
{
    int i;
    for (i = 0; i < NWRITE; i++)
        if (wrbusy[i] && CheckIO((struct IORequest *)wr[i])) {
            WaitIO((struct IORequest *)wr[i]);       /* done: off the port */
            wrbusy[i] = 0;
        }
}

static err_t sana_output(struct netif *ni, struct pbuf *p)
{
    int i;
    struct IOSana2Req *io;
    (void)ni;
    if (!S.online || p->tot_len < 14 || p->tot_len > FRAME) return ERR_IF;
    write_reap();
    for (i = 0; i < NWRITE && wrbusy[i]; i++) ;
    if (i == NWRITE) { S.tx_dropped++; return ERR_MEM; }
    io = wr[i];
    pbuf_copy_partial(p, wrbuf[i], p->tot_len, 0);
    io->ios2_Req.io_Command = CMD_WRITE;
    io->ios2_Req.io_Flags = SANA2IOF_RAW;
    io->ios2_Data = wrbuf[i];
    io->ios2_DataLength = p->tot_len;
    io->ios2_PacketType = (ULONG)wrbuf[i][12] << 8 | wrbuf[i][13];
    CopyMem(wrbuf[i], io->ios2_DstAddr, 6);
    wrbusy[i] = 1;
    begin_io(io);
    S.tx_bytes += p->tot_len;
    return ERR_OK;
}

static err_t sana_netif_init(struct netif *ni)
{
    ni->name[0] = 'e';
    ni->name[1] = 'n';
    ni->output = etharp_output;
    ni->linkoutput = sana_output;
    ni->mtu = S.mtu;
    ni->hwaddr_len = 6;
    CopyMem(S.mac, ni->hwaddr, 6);
    ni->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET;
    netif_set_hostname(ni, S.hostname);
    return ERR_OK;
}

static void read_post(int i)
{
    struct IOSana2Req *io = rd[i];
    io->ios2_Req.io_Command = CMD_READ;
    io->ios2_Req.io_Flags = SANA2IOF_RAW;
    io->ios2_PacketType = i < 6 ? 0x0800 : 0x0806;
    io->ios2_Data = rdbuf[i];
    io->ios2_DataLength = FRAME;
    begin_io(io);
}

static void status_cb(struct netif *ni)
{
    (void)ni;
    S.news = 1;
}

/* Open the device and bring en0 up: 0, or 1 with nothing kept. */
static int sana_open(void)
{
    struct IOSana2Req *io;
    struct Sana2DeviceQuery q;
    int i;

    rport = CreateMsgPort();
    wport = CreateMsgPort();
    if (!rport || !wport) return 1;
    wport->mp_Flags = PA_IGNORE;                   /* writes are reaped by CheckIO */
    bufmgmt[0].ti_Tag = S2_CopyToBuff;   bufmgmt[0].ti_Data = (ULONG)copy_buf;
    bufmgmt[1].ti_Tag = S2_CopyFromBuff; bufmgmt[1].ti_Data = (ULONG)copy_buf;
    bufmgmt[2].ti_Tag = TAG_DONE;
    io = (struct IOSana2Req *)CreateIORequest(rport, sizeof(struct IOSana2Req));
    if (!io) return 1;
    io->ios2_BufferManagement = bufmgmt;
    if (OpenDevice((STRPTR)S.cfg.device, S.cfg.unit, (struct IORequest *)io, 0)) {
        DeleteIORequest((struct IORequest *)io);
        return 1;
    }
    S.ctl = io;

    q.SizeAvailable = sizeof(q);
    q.SizeSupplied = 0;
    io->ios2_Req.io_Command = S2_DEVICEQUERY;
    io->ios2_StatData = &q;
    S.mtu = (DoIO((struct IORequest *)io) == 0 && q.MTU >= 576 && q.MTU <= 1500) ? q.MTU : 1500;

    io->ios2_Req.io_Command = S2_GETSTATIONADDRESS;
    if (DoIO((struct IORequest *)io) == 0) {
        UBYTE any = 0;
        for (i = 0; i < 6; i++) any |= io->ios2_SrcAddr[i];
        if (!any) {                                /* not configured yet: its default */
            CopyMem(io->ios2_DstAddr, io->ios2_SrcAddr, 6);
            io->ios2_Req.io_Command = S2_CONFIGINTERFACE;
            DoIO((struct IORequest *)io);           /* "already configured" is fine */
            io->ios2_Req.io_Command = S2_GETSTATIONADDRESS;
            DoIO((struct IORequest *)io);
        }
        CopyMem(io->ios2_SrcAddr, S.mac, 6);
    }
    io->ios2_Req.io_Command = S2_ONLINE;
    DoIO((struct IORequest *)io);                   /* "already online" is fine */

    for (i = 0; i < NREAD; i++) {
        rd[i] = (struct IOSana2Req *)CreateIORequest(rport, sizeof(struct IOSana2Req));
        rdbuf[i] = AllocVec(FRAME, MEMF_PUBLIC);
        if (!rd[i] || !rdbuf[i]) return 1;
        CopyMem(io, rd[i], sizeof(struct IOSana2Req));
        rd[i]->ios2_Req.io_Message.mn_ReplyPort = rport;
    }
    for (i = 0; i < NWRITE; i++) {
        wr[i] = (struct IOSana2Req *)CreateIORequest(wport, sizeof(struct IOSana2Req));
        wrbuf[i] = AllocVec(FRAME, MEMF_PUBLIC);
        if (!wr[i] || !wrbuf[i]) return 1;
        CopyMem(io, wr[i], sizeof(struct IOSana2Req));
        wr[i]->ios2_Req.io_Message.mn_ReplyPort = wport;
    }

    direct_lock();
    S.online = 1;
    {
        ip4_addr_t a, m, g;
        a.addr = S.cfg.address;
        m.addr = S.cfg.netmask;
        g.addr = S.cfg.gateway;
        if (S.cfg.dhcp) a.addr = m.addr = g.addr = 0;
        else if (!m.addr) m.addr = PP_HTONL(0xffffff00UL);
        netif_add(&S.en, &a, &m, &g, NULL, sana_netif_init, ethernet_input);
    }
    netif_set_status_callback(&S.en, status_cb);
    netif_set_default(&S.en);
    netif_set_up(&S.en);
    netif_set_link_up(&S.en);
    if (S.cfg.dhcp) dhcp_start(&S.en);
    S.en_added = 1;
    direct_unlock();

    for (i = 0; i < NREAD; i++) read_post(i);
    return 0;
}

static void frames_in(void)
{
    struct IOSana2Req *io;
    while ((io = (struct IOSana2Req *)GetMsg(rport)) != NULL) {
        int i;
        for (i = 0; i < NREAD && rd[i] != io; i++) ;
        if (i == NREAD) continue;
        if (io->ios2_Req.io_Error == 0 && io->ios2_DataLength >= 14 && io->ios2_DataLength <= FRAME) {
            struct pbuf *p = pbuf_alloc(PBUF_RAW, (u16_t)io->ios2_DataLength, PBUF_RAM);
            if (p) {
                pbuf_take(p, rdbuf[i], (u16_t)io->ios2_DataLength);
                S.rx_bytes += io->ios2_DataLength;
                if (S.en.input(p, &S.en) != ERR_OK) pbuf_free(p);
            }
            read_post(i);
        } else if (io->ios2_Req.io_Error != IOERR_ABORTED) {
            read_post(i);
        }
    }
}

static void proc_main(void)
{
    ULONG rsig = 0, tsig, sigs;
    struct Library *tdev = NULL;

    tport = CreateMsgPort();
    treq = tport ? (struct timerequest *)CreateIORequest(tport, sizeof(struct timerequest)) : NULL;
    if (treq && OpenDevice((STRPTR)TIMERNAME, UNIT_VBLANK, (struct IORequest *)treq, 0) == 0)
        tdev = (struct Library *)treq->tr_node.io_Device;
    if (!tdev) return;                              /* lwIP's timers are this process's */
    tsig = 1UL << tport->mp_SigBit;

    if (sana_open() == 0) rsig = 1UL << rport->mp_SigBit;
    else S.dev_failed = 1;

    treq->tr_node.io_Command = TR_ADDREQUEST;
    treq->tr_time.tv_secs = 0;
    treq->tr_time.tv_micro = 50000;
    SendIO((struct IORequest *)treq);
    for (;;) {
        sigs = Wait(rsig | tsig);
        direct_lock();
        if (sigs & rsig) frames_in();
        if (rport) write_reap();
        if (sigs & tsig) {
            if (GetMsg(tport)) {
                sys_check_timeouts();
                treq->tr_node.io_Command = TR_ADDREQUEST;
                treq->tr_time.tv_secs = 0;
                treq->tr_time.tv_micro = 50000;
                SendIO((struct IORequest *)treq);
            }
        }
        direct_unlock();
    }
}

/* ---- start ------------------------------------------------------------ */

static APTR start(const struct CardConfig *cfg)
{
    static const char host[] = "amiga";
    struct timerequest tr;
    ULONG i;

    { struct ExecBase **p = (struct ExecBase **)4; __asm__ ("" : "+r"(p)); SysBase = *p; }
    if (S.started) return &S;
    for (i = 0; i < sizeof(S.cfg); i++) ((UBYTE *)&S.cfg)[i] = ((const UBYTE *)cfg)[i];
    for (i = 0; host[i] && i < sizeof(S.hostname) - 1; i++) S.hostname[i] = host[i];
    S.mtu = 1500;
    InitSemaphore(&S.lock);

    /* time for lwIP's timers, from the start (sys_now) */
    for (i = 0; i < sizeof(tr); i++) ((UBYTE *)&tr)[i] = 0;
    if (OpenDevice((STRPTR)TIMERNAME, UNIT_VBLANK, (struct IORequest *)&tr, 0) == 0)
        TimerBase = tr.tr_node.io_Device;          /* kept open: the stack stays */
    rnd_state = sys_now() ^ (ULONG)&S;

    lwip_init();                                    /* the loopback interface, 127.0.0.1 */
    for (i = 0; i < CARD_NS_MAX; i++)
        if (cfg->nameserver[i]) {
            ip_addr_t ns;
            ip_addr_set_ip4_u32(&ns, cfg->nameserver[i]);
            dns_setserver((u8_t)i, &ns);
        }
    direct_sock_init();

    DOSBase = (struct DosLibrary *)OpenLibrary((STRPTR)"dos.library", 37);
    if (!DOSBase) return NULL;
    S.proc = CreateNewProcTags(NP_Entry, (ULONG)proc_main, NP_Name, (ULONG)"OpenSocket Direct",
                               NP_Priority, 5, NP_StackSize, 16384,
                               NP_Input, 0, NP_Output, 0, NP_CloseInput, FALSE, NP_CloseOutput, FALSE,
                               NP_CurrentDir, 0, NP_HomeDir, 0, NP_Cli, FALSE, TAG_DONE);
    S.started = 1;
    return &S;
}

static LONG call(APTR st, ULONG cmd, ULONG a0, ULONG a1, ULONG a2, ULONG a3,
                 const void *tx, ULONG txlen, void *rx, ULONG rxmax, ULONG *rxlen, ULONG *err)
{
    LONG r;
    (void)st;
    direct_lock();
    r = direct_command(cmd, a0, a1, a2, a3, (const UBYTE *)tx, txlen, (UBYTE *)rx, rxmax, rxlen, err);
    direct_unlock();
    return r;
}

static void stop(APTR st) { (void)st; }

static const struct DirectOps ops = { DIRECT_MAGIC, DIRECT_VERSION, start, call, arm, disarm, stop };
static const char rt_name[] = "opensocketdirect";
static const char rt_id[] = "opensocketdirect 1.0 (16.10.2026) OpenSocket's own stack (lwIP 2.2.1)\r\n";
const struct Resident direct_romtag __attribute__((used)) = {
    RTC_MATCHWORD, (struct Resident *)&direct_romtag, (APTR)(&direct_romtag + 1),
    0, 1, NT_UNKNOWN, 0, (char *)rt_name, (char *)rt_id, (APTR)&ops,
};
