/* ACTCPTools netstat - ACNet socket/status view. MIT. */
#include <exec/types.h>
#include <exec/libraries.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <stdio.h>
#include <string.h>
#include <acnetwork.h>
#include <proto/acnetwork.h>

struct Library *ACNetworkBase = NULL;

static void ip4(const UBYTE a[4], char *out)
{
    sprintf(out, "%u.%u.%u.%u", (unsigned)a[0], (unsigned)a[1], (unsigned)a[2], (unsigned)a[3]);
}

static const char *state_name(ULONG state)
{
    switch (state) {
        case ACNETWORK_SOCKET_BOUND: return "BOUND";
        case ACNETWORK_SOCKET_LISTEN: return "LISTEN";
        case ACNETWORK_SOCKET_CONNECTED: return "ESTABLISHED";
        default: return "OPEN";
    }
}

int main(void)
{
    struct ACNetworkSocket rows[64];
    struct ACNetworkStatus st;
    struct ACNetworkRequest r;
    LONG n, i;
    char lip[24], rip[24];

    ACNetworkBase = OpenLibrary(ACNETWORK_LIBRARY_NAME, ACNETWORK_LIBRARY_VERSION);
    if (!ACNetworkBase) { printf("netstat: gfopensocket.library is not available\n"); return 20; }

    memset(&r, 0, sizeof(r));
    r.command = ACNETWORK_CMD_STATUS;
    r.rx = (UBYTE *)&st;
    r.rxmax = sizeof(st);
    if (ACNetwork_Call(&r) >= 0) {
        printf("GFOpenSocket: %s, link %u Mb/s, sockets %u\n",
               (st.state & ACNETWORK_STATE_ONLINE) ? "online" : "offline", (unsigned)st.link_mbps, (unsigned)st.sockets);
        printf("Traffic: in %llu  out %llu  connects %u  refused %u\n\n",
               (unsigned long long)st.bytes_in, (unsigned long long)st.bytes_out, (unsigned)st.connects, (unsigned)st.refused);
    }

    memset(&r, 0, sizeof(r));
    r.command = ACNETWORK_CMD_SOCKETS;
    r.arg[0] = 64;
    r.rx = (UBYTE *)rows;
    r.rxmax = sizeof(rows);
    n = ACNetwork_Call(&r);
    if (n < 0) { printf("netstat: GFOpenSocket error %u\n", (unsigned)r.error); CloseLibrary(ACNetworkBase); return 10; }

    printf("Proto  Local Address          Foreign Address        State\n");
    for (i = 0; i < n; ++i) {
        const char *proto = rows[i].protocol == 6 ? "tcp" : (rows[i].protocol == 17 ? "udp" : "raw");
        ip4(rows[i].local_ipv4, lip); ip4(rows[i].remote_ipv4, rip);
        printf("%-5s  %-15s:%-5u  %-15s:%-5u  %s\n",
               proto, lip, rows[i].local_port, rip, rows[i].remote_port, state_name(rows[i].state));
    }
    CloseLibrary(ACNetworkBase);
    return 0;
}
