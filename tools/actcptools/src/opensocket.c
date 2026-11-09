/* ACTCPTools acnetctl - native ACNet control/status. MIT.
 * 9 Nov 2026: the network from the Amiga (amicradle docs/design/Design-Network-Passthrough.md):
 *   GFOpenSocket wifi join SSID [KEY password]   a network, new or known
 *   GFOpenSocket net get                          this machine's network settings (no secret)
 *   GFOpenSocket net set FILE                     amicradle-network.txt's lines (a native card only)
 * through gfopensocketwifi.device 1.1. */
#include <exec/types.h>
#include <exec/libraries.h>
#include <exec/io.h>
#include <exec/devices.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <stdio.h>
#include <string.h>
#include <acnetwork.h>
#include <proto/acnetwork.h>
#include <acwifi_device.h>

struct Library *ACNetworkBase = NULL;

static int show_status(void)
{
    struct ACNetworkStatus st;
    struct ACNetworkRequest r;
    memset(&r, 0, sizeof(r));
    memset(&st, 0, sizeof(st));
    r.command = ACNETWORK_CMD_STATUS;
    r.rx = (UBYTE *)&st;
    r.rxmax = sizeof(st);
    if (ACNetwork_Call(&r) < 0) {
        printf("GFOpenSocket: error %u\n", (unsigned)r.error);
        return 10;
    }
    printf("GFOpenSocket %s, link %u Mb/s, sockets %u, in %llu, out %llu\n",
           (st.state & ACNETWORK_STATE_ONLINE) ? "online" : "offline",
           (unsigned)st.link_mbps, (unsigned)st.sockets,
           (unsigned long long)st.bytes_in, (unsigned long long)st.bytes_out);
    return 0;
}

static const char *why(ULONG e)
{
    switch (e) {
        case 1: return "not allowed (on a PC, Cradle's Network panel decides)";
        case 22: return "not accepted (GFOpenSocket net get shows why on its last= line)";
        case 50: return "the network is off in Cradle";
        case 78: return "this machine can't apply network settings";
        case 4: return "interrupted";
        default: return "failed";
    }
}

/* gfopensocketwifi.device 1.1's ACWiFiCall2. Returns the result, or -1 having said why. */
static LONG call2(ULONG command, const UBYTE *tx, ULONG txlen, UBYTE *rx, ULONG rxmax)
{
    struct MsgPort *port = CreateMsgPort();
    struct IOStdReq *io = port ? (struct IOStdReq *)CreateIORequest(port, sizeof(*io)) : NULL;
    struct ACWiFiCall2 c;
    LONG rc = -1;
    if (!io || OpenDevice((STRPTR)ACWIFI_DEVICE_NAME, 0, (struct IORequest *)io, 0)) {
        printf("GFOpenSocket: %s is not installed\n", ACWIFI_DEVICE_NAME);
    } else {
        if (io->io_Device->dd_Library.lib_Revision < ACWIFI_DEVICE_REVISION &&
            io->io_Device->dd_Library.lib_Version <= ACWIFI_DEVICE_VERSION) {
            printf("GFOpenSocket: %s is older than 1.1\n", ACWIFI_DEVICE_NAME);
        } else {
            memset(&c, 0, sizeof(c));
            c.size = sizeof(c);
            c.command = command;
            c.tx = tx;
            c.txlen = txlen;
            c.rx = rx;
            c.rxmax = rxmax;
            io->io_Command = ACWIFI_IO_CALL2;
            io->io_Data = &c;
            io->io_Length = sizeof(c);
            DoIO((struct IORequest *)io);
            if (c.error) printf("GFOpenSocket: %s (error %lu)\n", why(c.error), (unsigned long)c.error);
            else rc = c.result;
        }
        CloseDevice((struct IORequest *)io);
    }
    if (io) DeleteIORequest((struct IORequest *)io);
    if (port) DeleteMsgPort(port);
    return rc;
}

static int wifi_join(const char *ssid, const char *key)
{
    UBYTE tx[33 + 65];
    ULONG sl = strlen(ssid), kl = key ? strlen(key) : 0;
    LONG rc;
    if (!sl || sl > 32 || kl > 64 || (kl && kl < 8)) {
        printf("GFOpenSocket: a network name is 1 to 32 bytes, a password 8 to 63 characters (or 64 hex digits)\n");
        return 10;
    }
    memcpy(tx, ssid, sl + 1);
    memcpy(tx + sl + 1, kl ? key : "", kl + 1);
    rc = call2(ACWIFI_JOINKEY, tx, sl + kl + 2, NULL, 0);
    memset(tx, 0, sizeof tx);
    if (rc < 0) return 10;
    printf("GFOpenSocket: joining %s\n", ssid);
    return 0;
}

static int net_get(void)
{
    static UBYTE text[4096];
    if (call2(ACWIFI_NET_GET, NULL, 0, text, sizeof text) < 0) return 10;
    printf("%s", (char *)text);
    return 0;
}

static int net_set(const char *file)
{
    static UBYTE text[8192];
    BPTR fh = Open((STRPTR)file, MODE_OLDFILE);
    LONG n;
    if (!fh) { printf("GFOpenSocket: can't open %s\n", file); return 10; }
    n = Read(fh, text, sizeof text - 1);
    Close(fh);
    if (n <= 0) { printf("GFOpenSocket: %s is empty\n", file); return 10; }
    text[n] = 0;
    if (call2(ACWIFI_NET_SET, text, (ULONG)n + 1, NULL, 0) < 0) return 10;
    printf("GFOpenSocket: sent; the card applies it in a moment\n");
    return 0;
}

int main(int argc, char **argv)
{
    struct ACNetworkRequest r;
    int rc;
    if (argc >= 4 && !strcmp(argv[1], "wifi") && !strcmp(argv[2], "join")) {
        const char *key = argc >= 6 && !strcmp(argv[4], "KEY") ? argv[5] : NULL;
        return wifi_join(argv[3], key);
    }
    if (argc >= 3 && !strcmp(argv[1], "net") && !strcmp(argv[2], "get")) return net_get();
    if (argc >= 4 && !strcmp(argv[1], "net") && !strcmp(argv[2], "set")) return net_set(argv[3]);

    ACNetworkBase = OpenLibrary(ACNETWORK_LIBRARY_NAME, ACNETWORK_LIBRARY_VERSION);
    if (!ACNetworkBase) { printf("GFOpenSocket: gfopensocket.library is not available\n"); return 20; }

    if (argc > 1 && (!strcmp(argv[1], "online") || !strcmp(argv[1], "offline"))) {
        memset(&r, 0, sizeof(r));
        r.command = ACNETWORK_CMD_SET_ONLINE;
        r.arg[0] = !strcmp(argv[1], "online") ? 1 : 0;
        if (ACNetwork_Call(&r) < 0) {
            printf("GFOpenSocket: error %u\n", (unsigned)r.error);
            CloseLibrary(ACNetworkBase);
            return 10;
        }
    } else if (argc > 1 && strcmp(argv[1], "status")) {
        printf("Usage: GFOpenSocket [status|online|offline]\n"
               "       GFOpenSocket wifi join SSID [KEY password]\n"
               "       GFOpenSocket net get\n"
               "       GFOpenSocket net set FILE\n");
        CloseLibrary(ACNetworkBase);
        return 5;
    }

    rc = show_status();
    CloseLibrary(ACNetworkBase);
    return rc;
}
