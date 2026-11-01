/* Read-only Roadshow interface/DNS/status compatibility over acnetwork.library.
 * The native ACNet control plane remains authoritative. MIT. */
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/lists.h>
#include <proto/exec.h>
#include <utility/tagitem.h>
#include <dos/dos.h>
#include <devices/sana2.h>
#include <libraries/bsdsocket.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp_fsm.h>
#include <netinet/icmp_var.h>
#include <netinet/igmp_var.h>
#include <netinet/ip_var.h>
#include <netinet/ip_mroute.h>
#include <netinet/tcp_var.h>
#include <netinet/udp_var.h>
#include <net/route.h>
#include <sys/mbuf.h>

#include <acnetwork.h>
#include "../../library/lib_internal.h"

#define ACNET_COMPAT_IF_MAX 16
#define ACNET_COMPAT_SOCKET_MAX 128
#define ACNET_COMPAT_DNS_MAX 8
static const char acnet_device_name[] = OPENSOCKET_DEVICE_PATH;

static int ascii_equal(const char *a, const char *b)
{
    UBYTE ca, cb;
    if (!a || !b) return 0;
    for (;;) {
        ca = (UBYTE)*a++; cb = (UBYTE)*b++;
        if (ca >= 'A' && ca <= 'Z') ca += 'a' - 'A';
        if (cb >= 'A' && cb <= 'Z') cb += 'a' - 'A';
        if (ca != cb) return 0;
        if (!ca) return 1;
    }
}

static void zero_bytes(APTR p, ULONG n)
{
    UBYTE *q = (UBYTE *)p;
    while (n--) *q++ = 0;
}

static void copy_name(char *dst, const char *src, ULONG max)
{
    ULONG i = 0;
    if (!max) return;
    while (i + 1 < max && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

static struct List *list_alloc(ULONG payload)
{
    ULONG total = sizeof(ULONG) + sizeof(struct List) + payload;
    ULONG *raw = AllocMem(total, MEMF_PUBLIC | MEMF_CLEAR);
    struct List *list;
    if (!raw) return NULL;
    raw[0] = total;
    list = (struct List *)(raw + 1);
    list->lh_Head = (struct Node *)&list->lh_Tail;
    list->lh_Tail = NULL;
    list->lh_TailPred = (struct Node *)&list->lh_Head;
    list->lh_Type = 0;
    list->l_pad = 0;
    return list;
}

static void list_free(struct List *list)
{
    ULONG *raw;
    if (!list) return;
    raw = ((ULONG *)list) - 1;
    FreeMem(raw, raw[0]);
}

static LONG load_interfaces(struct SocketBase *sb, struct ACNetworkInterface *rows, LONG max)
{
    ULONG rxlen = 0;
    LONG n = prov_call(sb, ACNETWORK_CMD_INTERFACES, max, 0, 0, 0,
                       NULL, 0, rows, max * sizeof(*rows), &rxlen);
    if (n < 0) return fail_provider(sb);
    if ((ULONG)n > rxlen / sizeof(*rows)) n = rxlen / sizeof(*rows);
    return n;
}

static int select_interface(struct ACNetworkInterface *rows, LONG n, struct ACNetworkInterface *out)
{
    LONG i;
    if (n <= 0) return 0;
    *out = rows[0];
    for (i = 0; i < n; ++i) {
        if ((rows[i].ipv4[0] != 0) && (rows[i].ipv4[0] != 127)) { *out = rows[i]; break; }
    }
    return 1;
}

static void fill_sin(struct sockaddr_in *sin, const UBYTE ip[4])
{
    zero_bytes(sin, sizeof(*sin));
    sin->sin_len = sizeof(*sin);
    sin->sin_family = AF_INET;
    CopyMem((APTR)ip, &sin->sin_addr.s_addr, 4);
}

static void fill_quad(SBQUAD_T *q, unsigned long long v)
{
    q->sbq_High = (ULONG)(v >> 32);
    q->sbq_Low = (ULONG)v;
}

static struct TagItem *next_tag(struct TagItem **cursor)
{
    struct TagItem *ti = *cursor;
    while (ti) {
        switch (ti->ti_Tag) {
        case TAG_DONE: *cursor = NULL; return NULL;
        case TAG_IGNORE: ti++; break;
        case TAG_MORE: ti = (struct TagItem *)ti->ti_Data; break;
        case TAG_SKIP: ti += ti->ti_Data + 1; break;
        default: *cursor = ti + 1; return ti;
        }
    }
    *cursor = NULL;
    return NULL;
}

struct List *bsd_ObtainInterfaceList(struct SocketBase *sb)
{
    struct ACNetworkInterface rows[ACNET_COMPAT_IF_MAX], selected;
    struct List *list;
    struct Node *node;
    char *name;
    LONG n = load_interfaces(sb, rows, ACNET_COMPAT_IF_MAX);
    if (n < 0) return NULL;
    if (!select_interface(rows, n, &selected)) {
        list = list_alloc(0);
        if (!list) set_errno(sb, AE_NOMEM); else set_errno(sb, 0);
        return list;
    }
    list = list_alloc(sizeof(struct Node) + 16);
    if (!list) { set_errno(sb, AE_NOMEM); return NULL; }
    node = (struct Node *)(list + 1);
    name = (char *)(node + 1);
    copy_name(name, "gfopensocket0", 16);
    node->ln_Name = (STRPTR)name;
    AddTail(list, node);
    set_errno(sb, 0);
    return list;
}

VOID bsd_ReleaseInterfaceList(struct SocketBase *sb, struct List *list)
{
    (void)sb;
    list_free(list);
}

static LONG load_dns(struct SocketBase *sb, UBYTE dns[ACNET_COMPAT_DNS_MAX][4])
{
    ULONG rxlen = 0;
    LONG n = prov_call(sb, ACNETWORK_CMD_DNS_SERVERS, ACNET_COMPAT_DNS_MAX, 0, 0, 0,
                       NULL, 0, dns, ACNET_COMPAT_DNS_MAX * 4, &rxlen);
    if (n < 0) return fail_provider(sb);
    if ((ULONG)n > rxlen / 4) n = rxlen / 4;
    return n;
}

LONG bsd_QueryInterfaceTagList(struct SocketBase *sb, STRPTR name, struct TagItem *tags)
{
    struct ACNetworkInterface rows[ACNET_COMPAT_IF_MAX], selected;
    UBYTE dns[ACNET_COMPAT_DNS_MAX][4];
    LONG n, dnsn;
    struct TagItem *cursor = tags, *ti;
    if (!ascii_equal((char *)name, "gfopensocket0")) return fail(sb, AE_INVAL);
    n = load_interfaces(sb, rows, ACNET_COMPAT_IF_MAX);
    if (n < 0) return -1;
    if (!select_interface(rows, n, &selected)) return fail(sb, AE_INVAL);
    dnsn = load_dns(sb, dns);
    if (dnsn < 0) dnsn = 0;

    while ((ti = next_tag(&cursor)) != NULL) {
        APTR out = (APTR)ti->ti_Data;
        UBYTE broadcast[4];
        ULONG bps;
        LONG i;
        if (!out) return fail(sb, AE_FAULT);
        switch (ti->ti_Tag) {
        case IFQ_DeviceName:
            *(STRPTR *)out = (STRPTR)acnet_device_name;
            break;
        case IFQ_DeviceUnit:
            *(LONG *)out = 0;
            break;
        case IFQ_HardwareAddressSize:
            *(LONG *)out = 48;
            break;
        case IFQ_HardwareAddress:
            CopyMem(selected.mac, out, 6);
            break;
        case IFQ_MTU:
        case IFQ_HardwareMTU:
            *(LONG *)out = (LONG)selected.mtu;
            break;
        case IFQ_BPS:
            bps = selected.link_mbps > 4294 ? 0xffffffffUL : selected.link_mbps * 1000000UL;
            *(LONG *)out = (LONG)bps;
            break;
        case IFQ_HardwareType:
            *(LONG *)out = S2WireType_Ethernet;
            break;
        case IFQ_PacketsReceived:
        case IFQ_PacketsSent:
        case IFQ_BadData:
        case IFQ_Overruns:
        case IFQ_UnknownTypes:
        case IFQ_NumReadRequests:
        case IFQ_MaxReadRequests:
        case IFQ_NumReadRequestsPending:
        case IFQ_NumWriteRequests:
        case IFQ_MaxWriteRequests:
        case IFQ_NumWriteRequestsPending:
        case IFQ_GetDebugMode:
        case IFQ_OutputDrops:
        case IFQ_InputDrops:
        case IFQ_OutputErrors:
        case IFQ_InputErrors:
        case IFQ_OutputMulticasts:
        case IFQ_InputMulticasts:
        case IFQ_IPDrops:
        case IFQ_ARPDrops:
            *(LONG *)out = 0;
            break;
        case IFQ_LastStart:
            zero_bytes(out, sizeof(struct timeval));
            break;
        case IFQ_Address:
            fill_sin((struct sockaddr_in *)out, selected.ipv4);
            break;
        case IFQ_DestinationAddress:
            zero_bytes(out, sizeof(struct sockaddr_in));
            ((struct sockaddr_in *)out)->sin_len = sizeof(struct sockaddr_in);
            ((struct sockaddr_in *)out)->sin_family = AF_INET;
            break;
        case IFQ_BroadcastAddress:
            for (i = 0; i < 4; ++i)
                broadcast[i] = selected.ipv4[i] | (UBYTE)~selected.netmask[i];
            fill_sin((struct sockaddr_in *)out, broadcast);
            break;
        case IFQ_NetMask:
            fill_sin((struct sockaddr_in *)out, selected.netmask);
            break;
        case IFQ_Metric:
            *(LONG *)out = 0;
            break;
        case IFQ_State:
            *(LONG *)out = (selected.flags & 1) ? SM_Up : SM_Down;
            break;
        case IFQ_AddressBindType:
            *(LONG *)out = IFABT_Unknown;
            break;
        case IFQ_AddressLeaseExpires:
            zero_bytes(out, sizeof(struct DateStamp));
            break;
        case IFQ_PrimaryDNSAddress:
            if (dnsn > 0) fill_sin((struct sockaddr_in *)out, dns[0]);
            else {
                UBYTE zero[4] = {0,0,0,0};
                fill_sin((struct sockaddr_in *)out, zero);
            }
            break;
        case IFQ_SecondaryDNSAddress:
            if (dnsn > 1) fill_sin((struct sockaddr_in *)out, dns[1]);
            else {
                UBYTE zero[4] = {0,0,0,0};
                fill_sin((struct sockaddr_in *)out, zero);
            }
            break;
        case IFQ_GetBytesIn:
            fill_quad((SBQUAD_T *)out, selected.rx_bytes);
            break;
        case IFQ_GetBytesOut:
            fill_quad((SBQUAD_T *)out, selected.tx_bytes);
            break;
        case IFQ_GetSANA2CopyStats:
            zero_bytes(out, sizeof(struct SANA2CopyStats));
            break;
        default:
            return fail(sb, AE_INVAL);
        }
    }
    set_errno(sb, 0);
    return 0;
}

static char *append_octet(char *p, UBYTE v)
{
    if (v >= 100) { *p++ = '0' + v / 100; v %= 100; *p++ = '0' + v / 10; }
    else if (v >= 10) *p++ = '0' + v / 10;
    *p++ = '0' + v % 10;
    return p;
}

static void ipv4_string(const UBYTE ip[4], char out[16])
{
    LONG i;
    char *p = out;
    for (i = 0; i < 4; ++i) {
        if (i) *p++ = '.';
        p = append_octet(p, ip[i]);
    }
    *p = 0;
}

struct List *bsd_ObtainDomainNameServerList(struct SocketBase *sb)
{
    UBYTE dns[ACNET_COMPAT_DNS_MAX][4];
    LONG n = load_dns(sb, dns), i;
    ULONG each = sizeof(struct DomainNameServerNode) + 16;
    struct List *list;
    UBYTE *cursor;
    if (n < 0) return NULL;
    list = list_alloc((ULONG)n * each);
    if (!list) { set_errno(sb, AE_NOMEM); return NULL; }
    cursor = (UBYTE *)(list + 1);
    for (i = 0; i < n; ++i) {
        struct DomainNameServerNode *node = (struct DomainNameServerNode *)cursor;
        char *address = (char *)(node + 1);
        node->dnsn_Size = sizeof(*node);
        node->dnsn_Address = (STRPTR)address;
        node->dnsn_UseCount = 1;
        ipv4_string(dns[i], address);
        AddTail(list, (struct Node *)node);
        cursor += each;
    }
    set_errno(sb, 0);
    return list;
}

VOID bsd_ReleaseDomainNameServerList(struct SocketBase *sb, struct List *list)
{
    (void)sb;
    list_free(list);
}

static LONG copy_stat(struct SocketBase *sb, APTR src, LONG needed, APTR dst, LONG size)
{
    LONG amount;
    if (size < 0) return fail(sb, AE_INVAL);
    if (!dst) { set_errno(sb, 0); return needed; }
    amount = size < needed ? size : needed;
    if (amount > 0) CopyMem(src, dst, amount);
    set_errno(sb, 0);
    return amount;
}

static LONG load_sockets(struct SocketBase *sb, struct ACNetworkSocket *rows, LONG max)
{
    ULONG rxlen = 0;
    LONG n = prov_call(sb, ACNETWORK_CMD_SOCKETS, max, 0, 0, 0,
                       NULL, 0, rows, max * sizeof(*rows), &rxlen);
    if (n < 0) return fail_provider(sb);
    if ((ULONG)n > rxlen / sizeof(*rows)) n = rxlen / sizeof(*rows);
    return n;
}

static LONG socket_stats(struct SocketBase *sb, LONG type, APTR destination, LONG size)
{
    struct ACNetworkSocket rows[ACNET_COMPAT_SOCKET_MAX];
    struct protocol_connection_data *out = (struct protocol_connection_data *)destination;
    LONG n = load_sockets(sb, rows, ACNET_COMPAT_SOCKET_MAX);
    LONG i, count = 0, capacity;
    UBYTE want_kind = type == NETSTATUS_tcp_sockets ? SOCK_STREAM : SOCK_DGRAM;
    if (n < 0) return -1;
    for (i = 0; i < n; ++i) if (rows[i].kind == want_kind) count++;
    if (!destination) { set_errno(sb, 0); return count * sizeof(*out); }
    if (size < 0) return fail(sb, AE_INVAL);
    capacity = size / sizeof(*out);
    count = 0;
    for (i = 0; i < n && count < capacity; ++i) {
        struct ACNetworkSocket *s = &rows[i];
        struct protocol_connection_data *p;
        if (s->kind != want_kind) continue;
        p = &out[count++];
        zero_bytes(p, sizeof(*p));
        CopyMem(s->remote_ipv4, &p->pcd_foreign_address.s_addr, 4);
        p->pcd_foreign_port = s->remote_port;
        CopyMem(s->local_ipv4, &p->pcd_local_address.s_addr, 4);
        p->pcd_local_port = s->local_port;
        p->pcd_tcp_state = -1;
        if (want_kind == SOCK_STREAM) {
            if (s->state == ACNETWORK_SOCKET_LISTEN) p->pcd_tcp_state = TCPS_LISTEN;
            else if (s->state == ACNETWORK_SOCKET_CONNECTED) p->pcd_tcp_state = TCPS_ESTABLISHED;
        }
    }
    set_errno(sb, 0);
    return count * sizeof(*out);
}

union ACNetCompatStats {
    struct icmpstat icmp;
    struct igmpstat igmp;
    struct ipstat ip;
    struct mbstat mb;
    struct mrtstat mrt;
    struct rtstat rt;
    struct tcpstat tcp;
    struct udpstat udp;
};

LONG bsd_GetNetworkStatistics(struct SocketBase *sb, LONG type, LONG version, APTR destination, LONG size)
{
    union ACNetCompatStats stats;
    struct ACNetworkStatus status;
    APTR src = NULL;
    LONG needed = 0;
    ULONG rxlen = 0;

    if (version != NETWORKSTATUS_VERSION) return fail(sb, AE_INVAL);
    if (type == NETSTATUS_tcp_sockets || type == NETSTATUS_udp_sockets)
        return socket_stats(sb, type, destination, size);

    zero_bytes(&stats, sizeof(stats));
    switch (type) {
    case NETSTATUS_icmp: src = &stats.icmp; needed = sizeof(stats.icmp); break;
    case NETSTATUS_igmp: src = &stats.igmp; needed = sizeof(stats.igmp); break;
    case NETSTATUS_ip: src = &stats.ip; needed = sizeof(stats.ip); break;
    case NETSTATUS_mb: src = &stats.mb; needed = sizeof(stats.mb); break;
    case NETSTATUS_mrt: src = &stats.mrt; needed = sizeof(stats.mrt); break;
    case NETSTATUS_rt: src = &stats.rt; needed = sizeof(stats.rt); break;
    case NETSTATUS_tcp:
        src = &stats.tcp; needed = sizeof(stats.tcp);
        zero_bytes(&status, sizeof(status));
        if (prov_call(sb, ACNETWORK_CMD_STATUS, 0, 0, 0, 0, NULL, 0,
                      &status, sizeof(status), &rxlen) >= 0 && rxlen >= sizeof(status))
            stats.tcp.tcps_connects = status.connects;
        break;
    case NETSTATUS_udp: src = &stats.udp; needed = sizeof(stats.udp); break;
    default: return fail(sb, AE_INVAL);
    }
    return copy_stat(sb, src, needed, destination, size);
}
