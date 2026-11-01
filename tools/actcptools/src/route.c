/* ACTCPTools route - read-only ACNet IPv4 route view. MIT. */
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
    struct ACNetworkRoute rows[64];
    struct ACNetworkRequest r;
    LONG n, i;
    char dst[24], gw[24], mask[24];

    ACNetworkBase = OpenLibrary(ACNETWORK_LIBRARY_NAME, ACNETWORK_LIBRARY_VERSION);
    if (!ACNetworkBase) { printf("route: gfopensocket.library is not available\n"); return 20; }
    memset(&r, 0, sizeof(r));
    r.command = ACNETWORK_CMD_ROUTES;
    r.arg[0] = 64;
    r.rx = (UBYTE *)rows;
    r.rxmax = sizeof(rows);
    n = ACNetwork_Call(&r);
    if (n < 0) { printf("route: GFOpenSocket error %u\n", (unsigned)r.error); CloseLibrary(ACNetworkBase); return 10; }

    printf("Destination       Gateway           Netmask           Flags Metric Iface\n");
    for (i = 0; i < n; ++i) {
        ip4(rows[i].destination, dst); ip4(rows[i].gateway, gw); ip4(rows[i].netmask, mask);
        printf("%-17s %-17s %-17s %04x  %-5u %s\n",
               dst, gw, mask, (unsigned)rows[i].flags, (unsigned)rows[i].metric, rows[i].ifname);
    }
    CloseLibrary(ACNetworkBase);
    return 0;
}
