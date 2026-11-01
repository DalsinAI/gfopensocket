/* ACNet Roadshow read-only compatibility probe. MIT. */
#include <exec/types.h>
#include <exec/libraries.h>
#include <exec/lists.h>
#include <proto/exec.h>
#include <libraries/bsdsocket.h>
#include <proto/bsdsocket.h>
#include <utility/tagitem.h>
#include <sys/socket.h>
#include <net/route.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>

struct Library *SocketBase = NULL;

static int failures;

static void check(int ok, const char *name)
{
    printf("%s: %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) failures++;
}

static LONG count_list(struct List *list)
{
    struct Node *node;
    LONG n = 0;
    if (!list) return -1;
    for (node = list->lh_Head; node->ln_Succ; node = node->ln_Succ) n++;
    return n;
}

static LONG count_routes(struct rt_msghdr *table)
{
    struct rt_msghdr *rtm;
    LONG n = 0;
    if (!table) return -1;
    for (rtm = table; rtm->rtm_msglen > 0;
         rtm = (struct rt_msghdr *)((UBYTE *)rtm + rtm->rtm_msglen)) n++;
    return n;
}

int main(void)
{
    ULONG have_route = 0, have_if = 0, have_status = 0, have_dns = 0;
    struct TagItem caps[] = {
        { SBTM_GETREF(SBTC_HAVE_ROUTING_API), (ULONG)&have_route },
        { SBTM_GETREF(SBTC_HAVE_INTERFACE_API), (ULONG)&have_if },
        { SBTM_GETREF(SBTC_HAVE_STATUS_API), (ULONG)&have_status },
        { SBTM_GETREF(SBTC_HAVE_DNS_API), (ULONG)&have_dns },
        { TAG_DONE, 0 }
    };
    struct List *interfaces = NULL, *dns = NULL;
    struct rt_msghdr *routes = NULL;
    struct Node *node;
    LONG rc;

    SocketBase = OpenLibrary("bsdsocket.library", 4);
    if (!SocketBase) { printf("OPEN: FAIL\n"); return 20; }
    printf("GFOpenSocket Roadshow read-only compatibility probe\n");
    rc = SocketBaseTagList(caps);
    check(rc == 0, "capability query");
    check(have_route == 1, "routing API advertised");
    check(have_if == 1, "interface API advertised");
    check(have_status == 1, "status API advertised");
    check(have_dns == 1, "DNS API advertised");

    {
        ULONG hostid = gethostid();
        check(hostid != 0, "gethostid");
        printf("hostid: %s\n", Inet_NtoA(hostid));
    }

    interfaces = ObtainInterfaceList();
    check(interfaces != NULL, "ObtainInterfaceList");
    if (interfaces) {
        LONG n = count_list(interfaces);
        printf("interfaces: %d\n", (int)n);
        check(n > 0, "interface count");
        for (node = interfaces->lh_Head; node->ln_Succ; node = node->ln_Succ) {
            STRPTR device = NULL;
            LONG mtu = 0, bps = 0, hwbits = 0, state = 0;
            struct sockaddr_in addr, mask;
            SBQUAD_T bytes_in, bytes_out;
            struct TagItem q[] = {
                { IFQ_DeviceName, (ULONG)&device },
                { IFQ_HardwareAddressSize, (ULONG)&hwbits },
                { IFQ_MTU, (ULONG)&mtu },
                { IFQ_BPS, (ULONG)&bps },
                { IFQ_Address, (ULONG)&addr },
                { IFQ_NetMask, (ULONG)&mask },
                { IFQ_State, (ULONG)&state },
                { IFQ_GetBytesIn, (ULONG)&bytes_in },
                { IFQ_GetBytesOut, (ULONG)&bytes_out },
                { TAG_DONE, 0 }
            };
            memset(&addr, 0, sizeof(addr)); memset(&mask, 0, sizeof(mask));
            memset(&bytes_in, 0, sizeof(bytes_in)); memset(&bytes_out, 0, sizeof(bytes_out));
            rc = QueryInterfaceTagList(node->ln_Name, q);
            check(rc == 0, "QueryInterfaceTagList");
            printf("interface %s device=%s addr=%s mtu=%d bps=%d hwbits=%d state=%d\n",
                   (char *)node->ln_Name, device ? (char *)device : "-",
                   (char *)Inet_NtoA(addr.sin_addr.s_addr),
                   (int)mtu, (int)bps, (int)hwbits, (int)state);
        }
        ReleaseInterfaceList(interfaces);
        interfaces = NULL;
    }

    routes = GetRouteInfo(AF_UNSPEC, 0);
    check(routes != NULL, "GetRouteInfo routes");
    if (routes) {
        LONG n = count_routes(routes);
        printf("routes: %d\n", (int)n);
        check(n > 0, "route count");
        FreeRouteInfo(routes); routes = NULL;
    }

    dns = ObtainDomainNameServerList();
    check(dns != NULL, "ObtainDomainNameServerList");
    if (dns) {
        struct DomainNameServerNode *dn;
        LONG n = count_list(dns);
        printf("dns servers: %d\n", (int)n);
        for (dn = (struct DomainNameServerNode *)dns->lh_Head;
             dn->dnsn_MinNode.mln_Succ;
             dn = (struct DomainNameServerNode *)dn->dnsn_MinNode.mln_Succ)
            printf("dns: %s\n", dn->dnsn_Address);
        ReleaseDomainNameServerList(dns); dns = NULL;
    }

    {
        LONG tcp_bytes = GetNetworkStatistics(NETSTATUS_tcp_sockets, NETWORKSTATUS_VERSION, NULL, 0);
        LONG udp_bytes = GetNetworkStatistics(NETSTATUS_udp_sockets, NETWORKSTATUS_VERSION, NULL, 0);
        LONG tcp_stat = GetNetworkStatistics(NETSTATUS_tcp, NETWORKSTATUS_VERSION, NULL, 0);
        check(tcp_bytes >= 0, "TCP socket status API");
        check(udp_bytes >= 0, "UDP socket status API");
        check(tcp_stat > 0, "TCP statistics API");
        printf("status sizes: tcp_sockets=%d udp_sockets=%d tcp=%d\n",
               (int)tcp_bytes, (int)udp_bytes, (int)tcp_stat);
    }

    CloseLibrary(SocketBase);
    SocketBase = NULL;
    printf("RESULT: %s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 5 : 0;
}
