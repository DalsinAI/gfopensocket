/* ACTCPTools ifconfig - read-only ACNet interface view. MIT. */
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

int main(void)
{
    struct ACNetworkInterface rows[32];
    struct ACNetworkRequest r;
    LONG n, i;
    char ip[24], mask[24];

    ACNetworkBase = OpenLibrary(ACNETWORK_LIBRARY_NAME, ACNETWORK_LIBRARY_VERSION);
    if (!ACNetworkBase) { printf("ifconfig: gfopensocket.library is not available\n"); return 20; }
    memset(&r, 0, sizeof(r));
    r.command = ACNETWORK_CMD_INTERFACES;
    r.arg[0] = 32;
    r.rx = (UBYTE *)rows;
    r.rxmax = sizeof(rows);
    n = ACNetwork_Call(&r);
    if (n < 0) { printf("ifconfig: GFOpenSocket error %u\n", (unsigned)r.error); CloseLibrary(ACNetworkBase); return 10; }

    for (i = 0; i < n; ++i) {
        struct ACNetworkInterface *p = &rows[i];
        ip4(p->ipv4, ip); ip4(p->netmask, mask);
        printf("%s: %s  mtu %u", p->ifname, (p->flags & 1) ? "UP" : "DOWN", (unsigned)p->mtu);
        if (p->link_mbps) printf("  link %u Mb/s", (unsigned)p->link_mbps);
        printf("\n");
        printf("  inet %s  netmask %s\n", ip, mask);
        printf("  ether %02x:%02x:%02x:%02x:%02x:%02x\n",
               p->mac[0], p->mac[1], p->mac[2], p->mac[3], p->mac[4], p->mac[5]);
        printf("  RX bytes %llu  TX bytes %llu\n",
               (unsigned long long)p->rx_bytes, (unsigned long long)p->tx_bytes);
    }
    CloseLibrary(ACNetworkBase);
    return 0;
}
