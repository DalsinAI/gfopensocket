/* acwifi.device: ACNet Wi-Fi control for AmigaOS 3.x.
 * Association remains host-owned. The device is a client of acnetwork.library;
 * it never touches the product-6 ACNet card directly. MIT. */
#include <exec/types.h>
#include <exec/resident.h>
#include <exec/devices.h>
#include <exec/errors.h>
#include <exec/execbase.h>
#include <proto/exec.h>
#include <dos/dos.h>

#include <acnetwork.h>
#include "acwifi_device.h"

#define REG(r, decl) register decl __asm(#r)
#define AE_INTR       4
#define AE_INVAL      22
#define AE_INPROGRESS 36
#define AE_NETDOWN    50

struct ACWiFiBase {
    struct Library lib;
    UWORD pad;
    BPTR seglist;
};

struct ExecBase *SysBase;

/* Must remain first for the bare Amiga executable layout. */
int start(void) { return -1; }

static const char dev_name[] = ACWIFI_DEVICE_NAME;
static const char dev_id[] = "gfopensocketwifi.device 1.1 (9.11.2026) GFOpenSocket (AmiCradle)\r\n";
static const char ver[] __attribute__((used)) =
    "$VER: gfopensocketwifi.device 1.1 (9.11.2026) GFOpenSocket (AmiCradle)";
static LONG net_call(struct Library *lib, struct ACNetworkRequest *request)
{
    register struct ACNetworkRequest *a0 __asm("a0") = request;
    register struct Library *a6 __asm("a6") = lib;
    register LONG d0 __asm("d0");
    __asm volatile ("jsr -30(%%a6)" : "=r"(d0), "+r"(a0) : "r"(a6)
                    : "d1", "a1", "cc", "memory");
    return d0;
}

static void net_arm(struct Library *lib)
{
    register struct Library *a6 __asm("a6") = lib;
    __asm volatile ("jsr -36(%%a6)" : : "r"(a6)
                    : "d0", "d1", "a0", "a1", "cc", "memory");
}

static void net_disarm(struct Library *lib)
{
    register struct Library *a6 __asm("a6") = lib;
    __asm volatile ("jsr -42(%%a6)" : : "r"(a6)
                    : "d0", "d1", "a0", "a1", "cc", "memory");
}

static ULONG net_signalmask(struct Library *lib)
{
    register struct Library *a6 __asm("a6") = lib;
    register ULONG d0 __asm("d0");
    __asm volatile ("jsr -48(%%a6)" : "=r"(d0) : "r"(a6)
                    : "d1", "a0", "a1", "cc", "memory");
    return d0;
}

static ULONG bounded_len(const char *s, ULONG maximum)
{
    ULONG n = 0;
    if (!s) return 0;
    while (n < maximum && s[n]) ++n;
    return n;
}
static ULONG native_command(ULONG command)
{
    switch (command) {
        case ACWIFI_SCAN:   return ACNETWORK_CMD_WIFI_SCAN;
        case ACWIFI_STATUS: return ACNETWORK_CMD_WIFI_STATUS;
        case ACWIFI_JOIN:   return ACNETWORK_CMD_WIFI_JOIN;
        case ACWIFI_LEAVE:  return ACNETWORK_CMD_WIFI_LEAVE;
        case ACWIFI_FORGET: return ACNETWORK_CMD_WIFI_FORGET;
        default: return 0;
    }
}

/* One host command run as the Wi-Fi ones are: sent, which gives a ticket, then WIFI_ANSWER until it is
 * done, waiting on the library's signal; Ctrl-C cancels. Returns the answer's result (rx gets its data,
 * *rxlen its length), or -1 with *error. */
static LONG run_ticket(ULONG command, const UBYTE *tx, ULONG txlen, UBYTE *rx, ULONG rxmax,
                       ULONG *error, ULONG *rxlen)
{
    struct Library *net;
    struct ACNetworkRequest req;
    ULONG ticket, mask, sigs;
    LONG rc;

    *error = AE_INVAL;
    if (rxlen) *rxlen = 0;
    net = OpenLibrary(ACNETWORK_LIBRARY_NAME, ACNETWORK_LIBRARY_VERSION);
    if (!net) { *error = AE_NETDOWN; return -1; }
    mask = net_signalmask(net);
    if (!mask) { CloseLibrary(net); *error = AE_NETDOWN; return -1; }
    req.command = command;
    req.arg[0] = req.arg[1] = req.arg[2] = req.arg[3] = 0;
    req.tx = tx;
    req.txlen = txlen;
    req.rx = NULL;
    req.rxmax = 0;
    req.result = -1;
    req.error = 0;
    req.rxlen = 0;
    rc = net_call(net, &req);
    if (rc < 0) { *error = req.error; CloseLibrary(net); return -1; }
    ticket = (ULONG)rc;
    for (;;) {
        net_arm(net);
        req.command = ACNETWORK_CMD_WIFI_ANSWER;
        req.arg[0] = ticket;
        req.arg[1] = req.arg[2] = req.arg[3] = 0;
        req.tx = NULL;
        req.txlen = 0;
        req.rx = rx;
        req.rxmax = rx ? rxmax : 0;
        req.result = -1;
        req.error = 0;
        req.rxlen = 0;
        rc = net_call(net, &req);
        if (rc >= 0) {
            net_disarm(net);
            SetSignal(0, mask);
            *error = 0;
            if (rxlen) *rxlen = req.rxlen;
            CloseLibrary(net);
            return rc;
        }
        if (req.error != AE_INPROGRESS) {
            net_disarm(net);
            *error = req.error;
            CloseLibrary(net);
            return -1;
        }
        sigs = Wait(mask | SIGBREAKF_CTRL_C);
        net_disarm(net);
        if (sigs & SIGBREAKF_CTRL_C) {
            req.command = ACNETWORK_CMD_WIFI_CANCEL;
            req.arg[0] = ticket;
            req.rx = NULL;
            req.rxmax = 0;
            req.tx = NULL;
            req.txlen = 0;
            net_call(net, &req);
            *error = AE_INTR;
            CloseLibrary(net);
            return -1;
        }
    }
}

/* 1.1: join with a key, and the network settings (ACWiFiCall2). */
static LONG run_call2(struct ACWiFiCall2 *call)
{
    ULONG command, txlen = 0, i;
    LONG rc;

    if (!call || call->size < sizeof(struct ACWiFiCall2)) return -1;
    call->result = -1;
    call->error = AE_INVAL;
    call->rxlen = 0;
    switch (call->command) {
        case ACWIFI_JOINKEY: command = ACNETWORK_CMD_WIFI_JOINKEY; break;
        case ACWIFI_NET_GET: command = ACNETWORK_CMD_NET_GET; break;
        case ACWIFI_NET_SET: command = ACNETWORK_CMD_NET_SET; break;
        default: return -1;
    }
    if (call->command == ACWIFI_JOINKEY || call->command == ACWIFI_NET_SET) {
        /* SSID\0KEY\0 (two NULs, at most 33 + 65 bytes), or text\0 (at most 8 KB) */
        ULONG need = call->command == ACWIFI_JOINKEY ? 2 : 1, seen = 0, limit = call->command == ACWIFI_JOINKEY ? 98 : 8192;
        if (!call->tx || !call->txlen || call->txlen > limit) return -1;
        for (i = 0; i < call->txlen && seen < need; ++i) if (!call->tx[i]) seen++;
        if (seen < need) return -1;
        txlen = i;
    }
    if (call->command == ACWIFI_NET_GET && (!call->rx || call->rxmax < 2)) return -1;
    rc = run_ticket(command, call->tx, txlen, call->command == ACWIFI_NET_GET ? call->rx : NULL,
                    call->command == ACWIFI_NET_GET ? call->rxmax - 1 : 0, &call->error, &call->rxlen);
    if (call->command == ACWIFI_NET_GET && rc >= 0) {
        if (call->rxlen >= call->rxmax) call->rxlen = call->rxmax - 1;
        call->rx[call->rxlen] = 0;
    }
    call->result = rc;
    return rc;
}

static LONG run_call(struct ACWiFiCall *call)
{
    struct Library *net;
    struct ACNetworkRequest req;
    ULONG command, ticket, mask, sigs;
    ULONG txlen = 0;
    LONG rc;

    if (!call) return -1;
    call->result = -1;
    call->error = AE_INVAL;
    command = native_command(call->command);
    if (!command) return -1;

    if (call->command == ACWIFI_JOIN || call->command == ACWIFI_FORGET) {
        txlen = bounded_len(call->ssid, 33);
        if (!txlen || txlen > 32 || call->ssid[txlen]) return -1;
        txlen++;
    }

    net = OpenLibrary(ACNETWORK_LIBRARY_NAME, ACNETWORK_LIBRARY_VERSION);
    if (!net) { call->error = AE_NETDOWN; return -1; }
    mask = net_signalmask(net);
    if (!mask) { CloseLibrary(net); call->error = AE_NETDOWN; return -1; }
    req.command = command;
    req.arg[0] = req.arg[1] = req.arg[2] = req.arg[3] = 0;
    req.tx = (const UBYTE *)call->ssid;
    req.txlen = txlen;
    req.rx = NULL;
    req.rxmax = 0;
    req.result = -1;
    req.error = 0;
    req.rxlen = 0;
    rc = net_call(net, &req);
    if (rc < 0) {
        call->error = req.error;
        CloseLibrary(net);
        return -1;
    }
    ticket = (ULONG)rc;

    for (;;) {
        net_arm(net);
        req.command = ACNETWORK_CMD_WIFI_ANSWER;
        req.arg[0] = ticket;
        req.arg[1] = req.arg[2] = req.arg[3] = 0;
        req.tx = NULL;
        req.txlen = 0;
        req.rx = (UBYTE *)call->networks;
        req.rxmax = call->networks ? call->capacity * sizeof(struct ACWiFiNetwork) : 0;
        req.result = -1;
        req.error = 0;
        req.rxlen = 0;
        rc = net_call(net, &req);
        if (rc >= 0) {
            net_disarm(net);
            SetSignal(0, mask);
            call->result = rc;
            if ((call->command == ACWIFI_SCAN || call->command == ACWIFI_STATUS) &&
                call->result > (LONG)call->capacity)
                call->result = (LONG)call->capacity;
            call->error = 0;
            CloseLibrary(net);
            return call->result;
        }
        if (req.error != AE_INPROGRESS) {
            net_disarm(net);
            call->error = req.error;
            CloseLibrary(net);
            return -1;
        }

        sigs = Wait(mask | SIGBREAKF_CTRL_C);
        net_disarm(net);
        if (sigs & SIGBREAKF_CTRL_C) {
            req.command = ACNETWORK_CMD_WIFI_CANCEL;
            req.arg[0] = ticket;
            req.rx = NULL;
            req.rxmax = 0;
            req.tx = NULL;
            req.txlen = 0;
            net_call(net, &req);
            call->error = AE_INTR;
            CloseLibrary(net);
            return -1;
        }
    }
}

static BPTR dev_expunge(REG(a6, struct ACWiFiBase *b))
{
    if (b->lib.lib_OpenCnt) { b->lib.lib_Flags |= LIBF_DELEXP; return 0; }
    {
        BPTR seg = b->seglist;
        Remove(&b->lib.lib_Node);
        FreeMem((UBYTE *)b - b->lib.lib_NegSize,
                b->lib.lib_NegSize + b->lib.lib_PosSize);
        return seg;
    }
}

static LONG dev_open(REG(a1, struct IORequest *io), REG(d0, ULONG unit),
                     REG(d1, ULONG flags), REG(a6, struct ACWiFiBase *b))
{
    (void)flags;
    if (unit != 0) { io->io_Error = IOERR_OPENFAIL; return IOERR_OPENFAIL; }
    b->lib.lib_OpenCnt++;
    b->lib.lib_Flags &= ~LIBF_DELEXP;
    io->io_Device = (struct Device *)b;
    io->io_Unit = NULL;
    io->io_Error = 0;
    io->io_Message.mn_Node.ln_Type = NT_REPLYMSG;
    return 0;
}
static BPTR dev_close(REG(a1, struct IORequest *io), REG(a6, struct ACWiFiBase *b))
{
    io->io_Device = (struct Device *)-1;
    io->io_Unit = (struct Unit *)-1;
    if (--b->lib.lib_OpenCnt == 0 && (b->lib.lib_Flags & LIBF_DELEXP))
        return dev_expunge(b);
    return 0;
}

static ULONG dev_null(void) { return 0; }

static LONG acw_call(REG(a0, struct ACWiFiCall *call), REG(a6, struct ACWiFiBase *b))
{
    (void)b;
    return run_call(call);
}

static LONG acw_call2(REG(a0, struct ACWiFiCall2 *call), REG(a6, struct ACWiFiBase *b))
{
    (void)b;
    return run_call2(call);
}

static void dev_beginio(REG(a1, struct IORequest *raw), REG(a6, struct ACWiFiBase *b))
{
    struct IOStdReq *io = (struct IOStdReq *)raw;
    (void)b;
    io->io_Message.mn_Node.ln_Type = NT_MESSAGE;
    io->io_Error = 0;
    io->io_Actual = 0;
    if (io->io_Command == ACWIFI_IO_CALL && io->io_Data &&
        io->io_Length >= sizeof(struct ACWiFiCall)) {
        run_call((struct ACWiFiCall *)io->io_Data);
        io->io_Actual = sizeof(struct ACWiFiCall);
    } else if (io->io_Command == ACWIFI_IO_CALL2 && io->io_Data &&
               io->io_Length >= sizeof(struct ACWiFiCall2)) {
        run_call2((struct ACWiFiCall2 *)io->io_Data);
        io->io_Actual = sizeof(struct ACWiFiCall2);
    } else {
        io->io_Error = IOERR_NOCMD;
    }
    if (!(io->io_Flags & IOF_QUICK)) ReplyMsg(&io->io_Message);
}

static LONG dev_abortio(REG(a1, struct IORequest *io), REG(a6, struct ACWiFiBase *b))
{
    (void)io; (void)b;
    return IOERR_NOCMD;
}
static struct ACWiFiBase *dev_init(REG(d0, struct ACWiFiBase *b),
                                   REG(a0, BPTR seglist),
                                   REG(a6, struct ExecBase *sys))
{
    SysBase = sys;
    b->seglist = seglist;
    b->lib.lib_Node.ln_Type = NT_DEVICE;
    b->lib.lib_Node.ln_Name = (char *)dev_name;
    b->lib.lib_Flags = LIBF_SUMUSED | LIBF_CHANGED;
    b->lib.lib_Version = ACWIFI_DEVICE_VERSION;
    b->lib.lib_Revision = ACWIFI_DEVICE_REVISION;
    b->lib.lib_IdString = (APTR)dev_id;
    return b;
}

static const APTR dev_vectors[] = {
    (APTR)dev_open, (APTR)dev_close, (APTR)dev_expunge, (APTR)dev_null,
    (APTR)dev_beginio, (APTR)dev_abortio,
    (APTR)acw_call,
    (APTR)acw_call2,
    (APTR)-1
};

static const struct {
    ULONG size;
    const APTR *vectors;
    APTR data;
    APTR init;
} dev_inittable = {
    sizeof(struct ACWiFiBase), dev_vectors, NULL, (APTR)dev_init
};

const struct Resident dev_romtag = {
    RTC_MATCHWORD, (struct Resident *)&dev_romtag, (APTR)(&dev_romtag + 1),
    RTF_AUTOINIT, ACWIFI_DEVICE_VERSION, NT_DEVICE, 0,
    (char *)dev_name, (char *)dev_id, (APTR)&dev_inittable
};
