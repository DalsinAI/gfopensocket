/* GFOpenSocketDirect: the HostSocket commands (common/protocol/achostsocket.h)
 * answered by lwIP's raw API, so bsdsocket.library's front end works the
 * same over Direct as over the bridge card. Nothing here blocks: a call
 * that would wait answers EAGAIN or EINPROGRESS, and the front end waits
 * for the next event (direct_stack.c wakes it) and asks again.
 *
 * Called with the stack's lock held. Errno values are the Amiga's (BSD).
 * Socket addresses are the Amiga's struct sockaddr_in: 16 bytes of len,
 * family, port, address, zero. MIT. */
#include "lwip/tcp.h"
#include "lwip/udp.h"
#include "lwip/raw.h"
#include "lwip/dns.h"
#include "lwip/dhcp.h"
#include "lwip/igmp.h"
#include "lwip/ip_addr.h"
#include "lwip/pbuf.h"

#include <exec/types.h>
#include <proto/exec.h>

#include "achostsocket.h"
#include "acnetwork.h"
#include "direct_int.h"

enum {
    E_BADF = 9, E_NOMEM = 12, E_INVAL = 22, E_MFILE = 24, E_PIPE = 32, E_AGAIN = 35,
    E_INPROGRESS = 36, E_ALREADY = 37, E_NOTSOCK = 38, E_DESTADDRREQ = 39, E_MSGSIZE = 40,
    E_PROTOTYPE = 41, E_PROTONOSUPPORT = 43, E_OPNOTSUPP = 45, E_AFNOSUPPORT = 47,
    E_ADDRINUSE = 48, E_ADDRNOTAVAIL = 49, E_NETDOWN = 50, E_NETUNREACH = 51,
    E_CONNABORTED = 53, E_CONNRESET = 54, E_NOBUFS = 55, E_ISCONN = 56, E_NOTCONN = 57,
    E_TIMEDOUT = 60, E_CONNREFUSED = 61, E_HOSTUNREACH = 65, E_NOSYS = 78
};
#define H_HOST_NOT_FOUND 1
#define SOCK_STREAM 1
#define SOCK_DGRAM  2
#define SOCK_RAW    3

#define NSOCK  64
#define NDGRAM 16
#define NACC   8
#define NTICK  8
#define NMEMB  4                                     /* multicast groups a socket joins, at most */

struct DSock {
    UBYTE used, type, listening, connecting;
    UBYTE connected, eof, shut_wr, hidden;      /* hidden: accepted, not yet ACCEPTed */
    LONG err;                                    /* the socket's pending error (SO_ERROR) */
    union { struct tcp_pcb *tcp; struct udp_pcb *udp; struct raw_pcb *raw; void *any; } pcb;
    struct pbuf *rx;                             /* a stream's data */
    struct { struct pbuf *p; ULONG addr; UWORD port; } dq[NDGRAM];
    UBYTE dqh, dqn, acn, pad;
    UBYTE acq[NACC];                             /* accepted handles waiting */
#if LWIP_IGMP
    ULONG memb[NMEMB][2];                        /* IP_ADD_MEMBERSHIP's groups and interfaces (network order), left at close */
#endif
};

struct Ticket { UBYTE used, state; ULONG addr; char name[128]; };   /* state 0 run, 1 done, 2 failed */

static struct DSock socks[NSOCK];
static struct Ticket tickets[NTICK];

void direct_sock_init(void) { }

static LONG e_of(err_t e)
{
    switch (e) {
    case ERR_MEM: case ERR_BUF: return E_NOBUFS;
    case ERR_TIMEOUT: return E_TIMEDOUT;
    case ERR_RTE: return E_HOSTUNREACH;
    case ERR_INPROGRESS: return E_INPROGRESS;
    case ERR_WOULDBLOCK: return E_AGAIN;
    case ERR_USE: return E_ADDRINUSE;
    case ERR_ALREADY: return E_ALREADY;
    case ERR_ISCONN: return E_ISCONN;
    case ERR_CONN: case ERR_CLSD: return E_NOTCONN;
    case ERR_IF: return E_NETUNREACH;
    case ERR_ABRT: return E_CONNABORTED;
    case ERR_RST: return E_CONNRESET;
    default: return E_INVAL;
    }
}

static struct DSock *sock_of(ULONG h) { return (h >= 1 && h <= NSOCK && socks[h - 1].used) ? &socks[h - 1] : NULL; }
static ULONG handle_of(struct DSock *s) { return (ULONG)(s - socks) + 1; }

static struct DSock *sock_new(UBYTE type)
{
    int i, j;
    for (i = 0; i < NSOCK; i++)
        if (!socks[i].used) {
            UBYTE *p = (UBYTE *)&socks[i];
            for (j = 0; j < (int)sizeof(struct DSock); j++) p[j] = 0;
            socks[i].used = 1;
            socks[i].type = type;
            return &socks[i];
        }
    return NULL;
}

static void sa_out(UBYTE *o, const ip_addr_t *ip, u16_t port)
{
    ULONG a = ip ? ip_addr_get_ip4_u32(ip) : 0;
    int i;
    for (i = 0; i < 16; i++) o[i] = 0;
    o[0] = 16; o[1] = 2;
    o[2] = (UBYTE)(port >> 8); o[3] = (UBYTE)port;
    CopyMem(&a, o + 4, 4);
}

static int sa_in(const UBYTE *t, ULONG n, ip_addr_t *ip, u16_t *port)
{
    ULONG a;
    if (n < 8) return 0;
    *port = (u16_t)(t[2] << 8 | t[3]);
    CopyMem((APTR)(t + 4), &a, 4);
    ip_addr_set_ip4_u32(ip, a);
    return 1;
}

/* ---- lwIP's callbacks: each is news for the waiters -------------------- */

static err_t tcp_recv_cb(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
    struct DSock *s = arg;
    (void)pcb; (void)err;
    S.news = 1;
    if (!s) { if (p) pbuf_free(p); return ERR_OK; }
    if (!p) { s->eof = 1; return ERR_OK; }
    if (s->rx) pbuf_cat(s->rx, p); else s->rx = p;
    return ERR_OK;
}

static err_t tcp_sent_cb(void *arg, struct tcp_pcb *pcb, u16_t len)
{
    (void)arg; (void)pcb; (void)len;
    S.news = 1;
    return ERR_OK;
}

static void tcp_err_cb(void *arg, err_t err)
{
    struct DSock *s = arg;
    S.news = 1;
    if (!s) return;
    s->pcb.tcp = NULL;                       /* lwIP has freed it */
    s->err = err == ERR_RST && s->connecting ? E_CONNREFUSED : e_of(err);
    s->connecting = 0;
    s->connected = 0;
    s->eof = 1;
}

static err_t tcp_connected_cb(void *arg, struct tcp_pcb *pcb, err_t err)
{
    struct DSock *s = arg;
    (void)pcb;
    S.news = 1;
    if (s) { s->connecting = 0; s->connected = err == ERR_OK; if (err) s->err = e_of(err); }
    return ERR_OK;
}

static void tcp_hook(struct DSock *s, struct tcp_pcb *pcb)
{
    tcp_arg(pcb, s);
    tcp_recv(pcb, tcp_recv_cb);
    tcp_sent(pcb, tcp_sent_cb);
    tcp_err(pcb, tcp_err_cb);
}

static err_t tcp_accept_cb(void *arg, struct tcp_pcb *pcb, err_t err)
{
    struct DSock *l = arg, *s;
    S.news = 1;
    if (err != ERR_OK || !pcb) return ERR_VAL;
    if (!l || l->acn >= NACC || !(s = sock_new(SOCK_STREAM))) { tcp_abort(pcb); return ERR_ABRT; }
    s->pcb.tcp = pcb;
    pcb->ttl = l->pcb.tcp ? l->pcb.tcp->ttl : pcb->ttl;   /* IP_TTL is inherited, as on BSD */
    s->connected = 1;
    s->hidden = 1;
    tcp_hook(s, pcb);
    l->acq[l->acn++] = (UBYTE)handle_of(s);
    return ERR_OK;
}

static void dgram_push(struct DSock *s, struct pbuf *p, const ip_addr_t *addr, u16_t port)
{
    int at;
    S.news = 1;
    if (s->dqn >= NDGRAM) { pbuf_free(p); return; }
    at = (s->dqh + s->dqn) % NDGRAM;
    s->dq[at].p = p;
    s->dq[at].addr = addr ? ip_addr_get_ip4_u32(addr) : 0;
    s->dq[at].port = port;
    s->dqn++;
}

#if LWIP_IGMP
/* A datagram to a group reaches only the sockets that joined it, as on BSD
 * (lwIP hands it to every socket bound to the port) */
static int joined(const struct DSock *s)
{
    int i;
    u32_t g = ip4_addr_get_u32(ip4_current_dest_addr());
    if (!ip4_addr_ismulticast(ip4_current_dest_addr())) return 1;
    for (i = 0; i < NMEMB; i++) if (s->memb[i][0] == g) return 1;
    return 0;
}
#endif

static void udp_recv_cb(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port)
{
    (void)pcb;
#if LWIP_IGMP
    if (arg && !joined(arg)) { pbuf_free(p); return; }
#endif
    if (arg) dgram_push(arg, p, addr, port); else pbuf_free(p);
}

/* A raw socket gets a copy (the IP header first, as BSD's do); the
 * stack goes on with the packet, so ICMP echo is still answered. */
static u8_t raw_recv_cb(void *arg, struct raw_pcb *pcb, struct pbuf *p, const ip_addr_t *addr)
{
    struct pbuf *c;
    (void)pcb;
#if LWIP_IGMP
    if (arg && !joined(arg)) return 0;
#endif
    if (arg && (c = pbuf_clone(PBUF_RAW, PBUF_RAM, p)) != NULL) dgram_push(arg, c, addr, 0);
    return 0;
}

static void dns_cb(const char *name, const ip_addr_t *ip, void *arg)
{
    struct Ticket *t = arg;
    (void)name;
    S.news = 1;
    if (!t->used) return;
    if (ip) { t->addr = ip_addr_get_ip4_u32(ip); t->state = 1; } else t->state = 2;
}

/* ---- the commands ------------------------------------------------------ */

static void sock_free(struct DSock *s)
{
    int i;
    if (s->type == SOCK_STREAM) {
        for (i = 0; i < s->acn; i++) {
            struct DSock *a = sock_of(s->acq[i]);
            if (a) sock_free(a);
        }
        if (s->pcb.tcp) {
            struct tcp_pcb *pcb = s->pcb.tcp;
            tcp_arg(pcb, NULL);
            if (!s->listening) { tcp_recv(pcb, NULL); tcp_sent(pcb, NULL); tcp_err(pcb, NULL); }
            else tcp_accept(pcb, NULL);
            if (tcp_close(pcb) != ERR_OK) tcp_abort(pcb);
        }
        if (s->rx) pbuf_free(s->rx);
    } else {
#if LWIP_IGMP
        for (i = 0; i < NMEMB; i++)
            if (s->memb[i][0]) {
                ip4_addr_t g, f;
                ip4_addr_set_u32(&g, s->memb[i][0]);
                ip4_addr_set_u32(&f, s->memb[i][1]);
                igmp_leavegroup(&f, &g);
            }
#endif
        if (s->pcb.any) { if (s->type == SOCK_DGRAM) udp_remove(s->pcb.udp); else raw_remove(s->pcb.raw); }
        for (i = 0; i < s->dqn; i++) pbuf_free(s->dq[(s->dqh + i) % NDGRAM].p);
    }
    s->used = 0;
}

static LONG c_socket(ULONG domain, ULONG type, ULONG proto, ULONG *err)
{
    struct DSock *s;
    if (domain != 2) { *err = E_AFNOSUPPORT; return -1; }
    if (type != SOCK_STREAM && type != SOCK_DGRAM && type != SOCK_RAW) { *err = E_PROTOTYPE; return -1; }
    if (!(s = sock_new((UBYTE)type))) { *err = E_MFILE; return -1; }
    if (type == SOCK_STREAM) {
        if ((s->pcb.tcp = tcp_new()) != NULL) tcp_hook(s, s->pcb.tcp);
    } else if (type == SOCK_DGRAM) {
        if ((s->pcb.udp = udp_new()) != NULL) {
            udp_recv(s->pcb.udp, udp_recv_cb, s);
#if LWIP_IGMP
            udp_set_flags(s->pcb.udp, UDP_FLAGS_MULTICAST_LOOP);   /* IP_MULTICAST_LOOP on, as on BSD */
            udp_set_multicast_ttl(s->pcb.udp, 1);                  /* and IP_MULTICAST_TTL 1: on this LAN only */
#endif
        }
    } else {
        if ((s->pcb.raw = raw_new((u8_t)(proto ? proto : 1))) != NULL) raw_recv(s->pcb.raw, raw_recv_cb, s);
    }
    if (!s->pcb.any) { s->used = 0; *err = E_NOBUFS; return -1; }
    return (LONG)handle_of(s);
}

static LONG c_bind(struct DSock *s, const UBYTE *tx, ULONG txlen, ULONG *err)
{
    ip_addr_t ip; u16_t port; err_t e;
    if (!sa_in(tx, txlen, &ip, &port)) { *err = E_INVAL; return -1; }
    if (!s->pcb.any) { *err = E_INVAL; return -1; }
    if (s->type == SOCK_STREAM) e = tcp_bind(s->pcb.tcp, &ip, port);
    else if (s->type == SOCK_DGRAM) e = udp_bind(s->pcb.udp, &ip, port);
    else e = raw_bind(s->pcb.raw, &ip);
    if (e != ERR_OK) { *err = e == ERR_USE ? E_ADDRINUSE : e_of(e); return -1; }
    return 0;
}

static LONG c_listen(struct DSock *s, ULONG backlog, ULONG *err)
{
    struct tcp_pcb *l;
    if (s->type != SOCK_STREAM) { *err = E_OPNOTSUPP; return -1; }
    if (s->listening) return 0;
    if (!s->pcb.tcp || s->connected || s->connecting) { *err = E_INVAL; return -1; }
    if (!s->pcb.tcp->local_port && tcp_bind(s->pcb.tcp, IP_ANY_TYPE, 0) != ERR_OK) { *err = E_ADDRINUSE; return -1; }
    l = tcp_listen_with_backlog(s->pcb.tcp, (u8_t)(backlog > 0 && backlog < 255 ? backlog : 8));
    if (!l) { *err = E_NOBUFS; return -1; }
    s->pcb.tcp = l;
    s->listening = 1;
    tcp_arg(l, s);
    tcp_accept(l, tcp_accept_cb);
    return 0;
}

static LONG c_accept(struct DSock *s, UBYTE *rx, ULONG rxmax, ULONG *rxlen, ULONG *err)
{
    struct DSock *a;
    UBYTE sa[16];
    int i;
    if (!s->listening) { *err = E_INVAL; return -1; }
    if (!s->acn) { *err = s->pcb.tcp ? E_AGAIN : E_CONNABORTED; return -1; }
    a = sock_of(s->acq[0]);
    for (i = 1; i < s->acn; i++) s->acq[i - 1] = s->acq[i];
    s->acn--;
    if (!a) { *err = E_CONNABORTED; return -1; }
    a->hidden = 0;
    /* A peer that reset before this ACCEPT has had its pcb freed by lwIP
     * (tcp_err_cb NULLed it; freeing it gave its backlog slot back). It is
     * still handed out, as BSD stacks do: its first read gives ECONNRESET. */
    if (s->pcb.tcp && a->pcb.tcp) tcp_backlog_accepted(a->pcb.tcp);
    sa_out(sa, a->pcb.tcp ? &a->pcb.tcp->remote_ip : NULL, a->pcb.tcp ? a->pcb.tcp->remote_port : 0);
    if (rx) CopyMem(sa, rx, rxmax < 16 ? rxmax : 16);
    if (rxlen) *rxlen = 16;
    return (LONG)handle_of(a);
}

static LONG c_connect(struct DSock *s, const UBYTE *tx, ULONG txlen, ULONG *err)
{
    ip_addr_t ip; u16_t port; err_t e;
    if (!sa_in(tx, txlen, &ip, &port)) { *err = E_INVAL; return -1; }
    if (s->type == SOCK_DGRAM) { e = udp_connect(s->pcb.udp, &ip, port); if (e) { *err = e_of(e); return -1; } return 0; }
    if (s->type == SOCK_RAW) { raw_connect(s->pcb.raw, &ip); return 0; }
    if (s->connected) { *err = E_ISCONN; return -1; }
    if (s->connecting) { *err = E_ALREADY; return -1; }
    if (!s->pcb.tcp) { LONG x = s->err ? s->err : E_INVAL; s->err = 0; *err = x; return -1; }
    s->err = 0;
    e = tcp_connect(s->pcb.tcp, &ip, port, tcp_connected_cb);
    if (e != ERR_OK) { *err = e_of(e); return -1; }
    s->connecting = 1;
    direct_pump();                          /* over loopback it is done at once */
    if (s->connected) return 0;
    if (!s->connecting && s->err) { LONG x = s->err; s->err = 0; *err = x; return -1; }
    *err = E_INPROGRESS;
    return -1;
}

static LONG dgram_send(struct DSock *s, const UBYTE *data, ULONG n, const ip_addr_t *ip, u16_t port, ULONG *err)
{
    struct pbuf *p;
    err_t e;
    if (n > 65000) { *err = E_MSGSIZE; return -1; }
    p = pbuf_alloc(s->type == SOCK_DGRAM ? PBUF_TRANSPORT : PBUF_IP, (u16_t)n, PBUF_RAM);
    if (!p) { *err = E_NOBUFS; return -1; }
    if (n) pbuf_take(p, data, (u16_t)n);
    if (s->type == SOCK_DGRAM) e = ip ? udp_sendto(s->pcb.udp, p, ip, port) : udp_send(s->pcb.udp, p);
    else e = ip ? raw_sendto(s->pcb.raw, p, ip) : raw_send(s->pcb.raw, p);
    pbuf_free(p);
    if (e != ERR_OK) { *err = e == ERR_RTE ? E_HOSTUNREACH : e_of(e); return -1; }
    return (LONG)n;
}

static LONG c_send(struct DSock *s, const UBYTE *data, ULONG n, ULONG *err)
{
    ULONG room;
    err_t e;
    if (s->type != SOCK_STREAM) {
        if (s->type == SOCK_DGRAM && !s->pcb.udp->remote_port) { *err = E_DESTADDRREQ; return -1; }
        return dgram_send(s, data, n, NULL, 0, err);
    }
    if (s->err) { *err = s->err; s->err = 0; return -1; }
    if (!s->connected || !s->pcb.tcp) { *err = s->eof ? E_PIPE : E_NOTCONN; return -1; }
    if (s->shut_wr) { *err = E_PIPE; return -1; }
    if (!n) return 0;
    room = tcp_sndbuf(s->pcb.tcp);
    if (!room || tcp_sndqueuelen(s->pcb.tcp) >= TCP_SND_QUEUELEN - 4) { *err = E_AGAIN; return -1; }
    if (n > room) n = room;
    if (n > 0xffff) n = 0xffff;
    e = tcp_write(s->pcb.tcp, data, (u16_t)n, TCP_WRITE_FLAG_COPY);
    if (e == ERR_MEM) { *err = E_AGAIN; return -1; }
    if (e != ERR_OK) { *err = e_of(e); return -1; }
    tcp_output(s->pcb.tcp);
    return (LONG)n;
}

static LONG c_recv(struct DSock *s, ULONG max, ULONG flags, UBYTE *rx, ULONG rxmax, ULONG *rxlen,
                   UBYTE *sa, ULONG *err)
{
    ULONG n;
    if (max > rxmax) max = rxmax;
    if (s->type == SOCK_STREAM) {
        if (s->rx) {
            n = s->rx->tot_len;
            if (n > max) n = max;
            if (n) pbuf_copy_partial(s->rx, rx, (u16_t)n, 0);
            if (!(flags & 0x2)) {                       /* not MSG_PEEK */
                s->rx = pbuf_free_header(s->rx, (u16_t)n);
                if (s->pcb.tcp) tcp_recved(s->pcb.tcp, (u16_t)n);
            }
            if (sa) sa_out(sa, s->pcb.tcp ? &s->pcb.tcp->remote_ip : NULL, s->pcb.tcp ? s->pcb.tcp->remote_port : 0);
            if (rxlen) *rxlen = n;
            return (LONG)n;
        }
        if (s->err) { *err = s->err; s->err = 0; return -1; }
        if (s->eof) { if (rxlen) *rxlen = 0; return 0; }
        if (!s->connected) { *err = E_NOTCONN; return -1; }
        *err = E_AGAIN;
        return -1;
    }
    if (!s->dqn) { *err = E_AGAIN; return -1; }
    {
        struct pbuf *p = s->dq[s->dqh].p;
        ip_addr_t from;
        n = p->tot_len;
        if (n > max) n = max;
        if (n) pbuf_copy_partial(p, rx, (u16_t)n, 0);
        ip_addr_set_ip4_u32(&from, s->dq[s->dqh].addr);
        if (sa) sa_out(sa, &from, s->dq[s->dqh].port);
        if (!(flags & 0x2)) {
            pbuf_free(p);
            s->dqh = (UBYTE)((s->dqh + 1) % NDGRAM);
            s->dqn--;
        }
        if (rxlen) *rxlen = n;
        return (LONG)n;
    }
}

static ULONG revents(struct DSock *s)
{
    ULONG r = 0;
    if (!s) return ACHS_POLLNVAL;
    if (s->type == SOCK_STREAM) {
        if (s->listening) return s->acn ? ACHS_POLLIN : (s->pcb.tcp ? 0 : ACHS_POLLERR);
        if (s->rx || s->eof) r |= ACHS_POLLIN;
        if (s->err) r |= ACHS_POLLERR | ACHS_POLLOUT | ACHS_POLLIN;
        if (s->connected && s->pcb.tcp && tcp_sndbuf(s->pcb.tcp)) r |= ACHS_POLLOUT;
        if (s->eof && !s->rx) r |= ACHS_POLLHUP;
        return r;
    }
    if (s->dqn) r |= ACHS_POLLIN;
    return r | ACHS_POLLOUT;
}

static LONG c_poll(ULONG count, const UBYTE *tx, ULONG txlen, UBYTE *rx, ULONG rxmax, ULONG *rxlen)
{
    ULONG i, ready = 0;
    for (i = 0; i < count && (i + 1) * 8 <= txlen && (i + 1) * 4 <= rxmax; i++) {
        ULONG h, ev, r;
        CopyMem((APTR)(tx + i * 8), &h, 4);
        CopyMem((APTR)(tx + i * 8 + 4), &ev, 4);
        r = revents(sock_of(h));
        r &= ev | ACHS_POLLERR | ACHS_POLLHUP | ACHS_POLLNVAL;
        if (r) ready++;
        CopyMem(&r, rx + i * 4, 4);
    }
    if (rxlen) *rxlen = i * 4;
    return (LONG)ready;
}

#define SOL_IP_LEVEL 0                              /* IPPROTO_IP */
#define OPT_IP_TTL   4                              /* IP_TTL, as the Amiga's netinet/in.h */

#define OPT_IP_MULTICAST_IF    9                    /* the Amiga's netinet/in.h, as BSD's */
#define OPT_IP_MULTICAST_TTL   10
#define OPT_IP_MULTICAST_LOOP  11
#define OPT_IP_ADD_MEMBERSHIP  12
#define OPT_IP_DROP_MEMBERSHIP 13

#if LWIP_IGMP
/* Multicast: a group joined (ip_mreq: the group, then the interface's
 * address or INADDR_ANY), its TTL, loop and interface. Each refused when it
 * can't be done, never taken and ignored. */
static LONG mc_sockopt(struct DSock *s, ULONG opt, const UBYTE *tx, ULONG txlen, ULONG *err)
{
    ULONG v = 0;
    int i;
    if (!s->pcb.any || s->type == SOCK_STREAM) { *err = E_OPNOTSUPP; return -1; }
    if (opt == OPT_IP_ADD_MEMBERSHIP || opt == OPT_IP_DROP_MEMBERSHIP) {
        ULONG g, f;
        ip4_addr_t ga, fa;
        if (txlen < 8) { *err = E_INVAL; return -1; }
        CopyMem((APTR)tx, &g, 4);
        CopyMem((APTR)(tx + 4), &f, 4);
        ip4_addr_set_u32(&ga, g);
        ip4_addr_set_u32(&fa, f);
        if (!ip4_addr_ismulticast(&ga)) { *err = E_INVAL; return -1; }
        for (i = 0; i < NMEMB && !(s->memb[i][0] == g && s->memb[i][1] == f); i++) ;
        if (opt == OPT_IP_DROP_MEMBERSHIP) {
            if (i == NMEMB) { *err = E_ADDRNOTAVAIL; return -1; }
            igmp_leavegroup(&fa, &ga);
            s->memb[i][0] = s->memb[i][1] = 0;
            return 0;
        }
        if (i < NMEMB) { *err = E_ADDRINUSE; return -1; }   /* this socket has it already */
        for (i = 0; i < NMEMB && s->memb[i][0]; i++) ;
        if (i == NMEMB) { *err = E_NOBUFS; return -1; }
        if (igmp_joingroup(&fa, &ga) != ERR_OK) { *err = E_ADDRNOTAVAIL; return -1; }
        s->memb[i][0] = g;
        s->memb[i][1] = f;
        return 0;
    }
    if (s->type != SOCK_DGRAM) { *err = E_OPNOTSUPP; return -1; }
    if (txlen >= 4) CopyMem((APTR)tx, &v, 4);
    else if (txlen >= 1 && opt != OPT_IP_MULTICAST_IF) v = tx[0];     /* a u_char, as BSD takes too */
    else { *err = E_INVAL; return -1; }
    if (opt == OPT_IP_MULTICAST_TTL) {
        if (v > 255 && v != 0xffffffffUL) { *err = E_INVAL; return -1; }
        udp_set_multicast_ttl(s->pcb.udp, (u8_t)(v == 0xffffffffUL ? 1 : v));   /* -1: BSD's default, 1 */
    } else if (opt == OPT_IP_MULTICAST_LOOP) {
        if (v) udp_set_flags(s->pcb.udp, UDP_FLAGS_MULTICAST_LOOP); else udp_clear_flags(s->pcb.udp, UDP_FLAGS_MULTICAST_LOOP);
    } else {
        ip4_addr_t fa;
        ip4_addr_set_u32(&fa, v);
        udp_set_multicast_netif_addr(s->pcb.udp, &fa);
    }
    return 0;
}
#endif

static LONG c_sockopt(struct DSock *s, int set, ULONG level, ULONG opt, const UBYTE *tx, ULONG txlen,
                      UBYTE *rx, ULONG rxmax, ULONG *rxlen, ULONG *err)
{
    ULONG v = 0;
    if (set) {
        if (txlen >= 4) CopyMem((APTR)tx, &v, 4);
        if (level == SOL_IP_LEVEL && opt == OPT_IP_TTL) {
            /* IP_TTL: 1 to 255 on every packet the socket sends, -1 the
             * default; anything else refused, never taken and ignored. A
             * listening socket's is its accepted connections' too. */
            if (txlen < 4 || !s->pcb.any || (v != 0xffffffffUL && (v < 1 || v > 255))) { *err = E_INVAL; return -1; }
            ((struct ip_pcb *)s->pcb.any)->ttl = (u8_t)(v == 0xffffffffUL ? IP_DEFAULT_TTL : v);
#if LWIP_IGMP
        } else if (level == SOL_IP_LEVEL && opt >= OPT_IP_MULTICAST_IF && opt <= OPT_IP_DROP_MEMBERSHIP) {
            return mc_sockopt(s, opt, tx, txlen, err);
#endif
        } else if (level == 0xffff && s->pcb.any) {
            u8_t f = opt == 0x0004 ? SOF_REUSEADDR : opt == 0x0008 ? SOF_KEEPALIVE : opt == 0x0020 ? SOF_BROADCAST : 0;
            if (f) { if (v) ip_set_option((struct ip_pcb *)s->pcb.any, f); else ip_reset_option((struct ip_pcb *)s->pcb.any, f); }
        } else if (level == 6 && opt == 1 && s->type == SOCK_STREAM && s->pcb.tcp && !s->listening) {
            if (v) tcp_nagle_disable(s->pcb.tcp); else tcp_nagle_enable(s->pcb.tcp);
        }
        return 0;                                   /* the rest: taken, and left as they are */
    }
    if (level == 0xffff) {
        if (opt == 0x1007) { v = (ULONG)s->err; s->err = 0; }
        else if (opt == 0x1008) v = s->type;
        else if (opt == 0x1001 || opt == 0x1002) v = 8 * 1460;
        else if (opt == 0x0002) v = s->listening;
        else if (s->pcb.any && (opt == 0x0004 || opt == 0x0008 || opt == 0x0020))
            v = ip_get_option((struct ip_pcb *)s->pcb.any, opt == 0x0004 ? SOF_REUSEADDR : opt == 0x0008 ? SOF_KEEPALIVE : SOF_BROADCAST) ? 1 : 0;
    } else if (level == 6 && opt == 1 && s->type == SOCK_STREAM && s->pcb.tcp && !s->listening) {
        v = tcp_nagle_disabled(s->pcb.tcp) ? 1 : 0;
    } else if (level == SOL_IP_LEVEL && opt == OPT_IP_TTL && s->pcb.any) {
        v = ((struct ip_pcb *)s->pcb.any)->ttl;
#if LWIP_IGMP
    } else if (level == SOL_IP_LEVEL && s->type == SOCK_DGRAM && s->pcb.udp && opt == OPT_IP_MULTICAST_TTL) {
        v = udp_get_multicast_ttl(s->pcb.udp);
    } else if (level == SOL_IP_LEVEL && s->type == SOCK_DGRAM && s->pcb.udp && opt == OPT_IP_MULTICAST_LOOP) {
        v = udp_is_flag_set(s->pcb.udp, UDP_FLAGS_MULTICAST_LOOP) ? 1 : 0;
    } else if (level == SOL_IP_LEVEL && s->type == SOCK_DGRAM && s->pcb.udp && opt == OPT_IP_MULTICAST_IF) {
        v = ip4_addr_get_u32(udp_get_multicast_netif_addr(s->pcb.udp));
#endif
    }
    if (rx && rxmax >= 4) CopyMem(&v, rx, 4);
    if (rxlen) *rxlen = rxmax >= 4 ? 4 : 0;
    return 0;
}

static LONG c_name(struct DSock *s, int peer, UBYTE *rx, ULONG rxmax, ULONG *rxlen, ULONG *err)
{
    UBYTE sa[16];
    ip_addr_t *ip = NULL; u16_t port = 0;
    if (s->pcb.any) {
        if (s->type == SOCK_STREAM) {
            if (peer && !s->connected) { *err = E_NOTCONN; return -1; }
            ip = peer ? &s->pcb.tcp->remote_ip : &s->pcb.tcp->local_ip;
            port = peer ? s->pcb.tcp->remote_port : s->pcb.tcp->local_port;
        } else if (s->type == SOCK_DGRAM) {
            if (peer && !s->pcb.udp->remote_port) { *err = E_NOTCONN; return -1; }
            ip = peer ? &s->pcb.udp->remote_ip : &s->pcb.udp->local_ip;
            port = peer ? s->pcb.udp->remote_port : s->pcb.udp->local_port;
        } else ip = peer ? &s->pcb.raw->remote_ip : &s->pcb.raw->local_ip;
    } else if (peer) { *err = E_NOTCONN; return -1; }
    sa_out(sa, ip, port);
    if (rx) CopyMem(sa, rx, rxmax < 16 ? rxmax : 16);
    if (rxlen) *rxlen = 16;
    return 0;
}

/* ---- names ------------------------------------------------------------- */

static int scopy(char *d, const UBYTE *s, ULONG n, ULONG max)
{
    ULONG i;
    for (i = 0; i < n && i < max - 1 && s[i]; i++) d[i] = (char)s[i];
    d[i] = 0;
    return (int)i;
}

static int lc_is(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) { char c = *a; if (c >= 'A' && c <= 'Z') c += 32; if (c != *b) return 0; }
    return !*a && !*b;
}

static int dotted(char *o, ULONG a)
{
    int i, n = 0;
    for (i = 0; i < 4; i++) {
        UBYTE b = (UBYTE)(a >> (24 - 8 * i));
        if (b >= 100) o[n++] = (char)('0' + b / 100);
        if (b >= 10) o[n++] = (char)('0' + b / 10 % 10);
        o[n++] = (char)('0' + b % 10);
        if (i < 3) o[n++] = '.';
    }
    o[n] = 0;
    return n;
}

static LONG c_resolve(int byaddr, const UBYTE *tx, ULONG txlen, ULONG *err)
{
    struct Ticket *t = NULL;
    int i;
    for (i = 0; i < NTICK; i++) if (!tickets[i].used) { t = &tickets[i]; break; }
    if (!t) { *err = 2; return -1; }                     /* TRY_AGAIN */
    t->used = 1; t->state = 0; t->addr = 0;
    if (byaddr) {
        ULONG a;
        if (txlen < 4) { t->used = 0; *err = H_HOST_NOT_FOUND; return -1; }
        CopyMem((APTR)tx, &a, 4);
        t->addr = a;
        if ((a >> 24) == 127) { const char *l = "localhost"; for (i = 0; l[i]; i++) t->name[i] = l[i]; t->name[i] = 0; }
        else dotted(t->name, a);
        t->state = 1;
    } else {
        ip_addr_t ip;
        err_t e;
        scopy(t->name, tx, txlen, sizeof(t->name));
        if (lc_is(t->name, "localhost")) { t->addr = 0x7f000001UL; t->state = 1; }
        else if (ip4addr_aton(t->name, ip_2_ip4(&ip))) { t->addr = ip_addr_get_ip4_u32(&ip); t->state = 1; }
        else {
            e = dns_gethostbyname(t->name, &ip, dns_cb, t);
            if (e == ERR_OK) { t->addr = ip_addr_get_ip4_u32(&ip); t->state = 1; }
            else if (e != ERR_INPROGRESS) t->state = 2;
        }
    }
    return (LONG)(t - tickets) + 1;
}

static LONG c_answer(ULONG ticket, UBYTE *rx, ULONG rxmax, ULONG *rxlen, ULONG *err)
{
    struct Ticket *t = (ticket >= 1 && ticket <= NTICK && tickets[ticket - 1].used) ? &tickets[ticket - 1] : NULL;
    ULONG n = 0, i;
    if (!t) { *err = H_HOST_NOT_FOUND; return -1; }
    if (t->state == 0) { *err = E_INPROGRESS; return -1; }
    t->used = 0;
    if (t->state == 2) { *err = H_HOST_NOT_FOUND; return -1; }
    for (i = 0; t->name[i] && n + 1 < rxmax; i++) rx[n++] = (UBYTE)t->name[i];
    if (n + 5 > rxmax) { *err = 3; return -1; }
    rx[n++] = 0;
    CopyMem(&t->addr, rx + n, 4);
    n += 4;
    if (rxlen) *rxlen = n;
    return 1;
}

static const struct { const char *name; UWORD port; UBYTE udp; } services[] = {
    { "echo", 7, 2 }, { "discard", 9, 2 }, { "daytime", 13, 2 }, { "ftp-data", 20, 0 }, { "ftp", 21, 0 },
    { "ssh", 22, 0 }, { "telnet", 23, 0 }, { "smtp", 25, 0 }, { "time", 37, 2 }, { "whois", 43, 0 },
    { "domain", 53, 2 }, { "tftp", 69, 1 }, { "gopher", 70, 0 }, { "finger", 79, 0 }, { "http", 80, 0 },
    { "pop3", 110, 0 }, { "sunrpc", 111, 2 }, { "auth", 113, 0 }, { "nntp", 119, 0 }, { "ntp", 123, 1 },
    { "imap", 143, 0 }, { "snmp", 161, 1 }, { "irc", 194, 0 }, { "https", 443, 0 }, { "printer", 515, 0 },
    { "imaps", 993, 0 }, { "pop3s", 995, 0 }, { NULL, 0, 0 }
};

static LONG c_service(ULONG port, const UBYTE *tx, ULONG txlen, UBYTE *rx, ULONG rxmax, ULONG *rxlen, ULONG *err)
{
    char name[32], proto[8];
    int i, n = scopy(name, tx, txlen, sizeof(name)), udp;
    scopy(proto, tx + n + 1 < tx + txlen ? tx + n + 1 : (const UBYTE *)"", txlen > (ULONG)n + 1 ? txlen - n - 1 : 0, sizeof(proto));
    udp = lc_is(proto, "udp");
    for (i = 0; services[i].name; i++) {
        if (proto[0] && ((services[i].udp == 1 && !udp) || (services[i].udp == 0 && udp))) continue;
        if (port ? services[i].port == port : lc_is(name, services[i].name)) {
            ULONG k = 0;
            const char *s = services[i].name;
            while (s[k] && k + 1 < rxmax) { rx[k] = (UBYTE)s[k]; k++; }
            if (rxmax) rx[k++] = 0;
            if (rxlen) *rxlen = k;
            return services[i].port;
        }
    }
    *err = H_HOST_NOT_FOUND;
    return -1;
}

static void put_if(struct ACNetworkInterface *r, ULONG index, struct netif *ni, const char *name)
{
    UBYTE *p = (UBYTE *)r;
    ULONG i, a = ip4_addr_get_u32(netif_ip4_addr(ni)), m = ip4_addr_get_u32(netif_ip4_netmask(ni));
    for (i = 0; i < sizeof(*r); i++) p[i] = 0;
    r->ifindex = index;
    r->flags = (netif_is_up(ni) ? 1 : 0) | (netif_is_link_up(ni) ? 0x40 : 0);
    CopyMem(&a, r->ipv4, 4);
    CopyMem(&m, r->netmask, 4);
    if (ni->hwaddr_len == 6) CopyMem(ni->hwaddr, r->mac, 6);
    r->mtu = ni->mtu ? ni->mtu : 1500;
    r->link_mbps = index == 2 ? 100 : 0;
    if (index == 2) { r->rx_bytes = S.rx_bytes; r->tx_bytes = S.tx_bytes; }
    for (i = 0; name[i] && i < 15; i++) r->ifname[i] = name[i];
}

LONG direct_command(ULONG cmd, ULONG a0, ULONG a1, ULONG a2, ULONG a3,
                    const UBYTE *tx, ULONG txlen, UBYTE *rx, ULONG rxmax, ULONG *rxlen, ULONG *err)
{
    struct DSock *s = NULL;
    (void)a3;
    if (rxlen) *rxlen = 0;
    *err = 0;
    switch (cmd) {
    case ACHS_CMD_SOCKET: case ACHS_CMD_POLL: case ACHS_CMD_RESOLVE: case ACHS_CMD_RESOLVE_ADDR:
    case ACHS_CMD_ANSWER: case ACHS_CMD_SERVICE: case ACHS_CMD_HOSTNAME: case ACHS_CMD_HELLO:
    case ACHS_CMD_STATUS: case ACHS_CMD_INTERFACES: case ACHS_CMD_DNS_SERVERS: case ACHS_CMD_ROUTES:
    case ACHS_CMD_NEIGHBOURS: case ACHS_CMD_SOCKETS: case ACHS_CMD_NET_BY_NAME: case ACHS_CMD_NET_BY_ADDR:
    case ACHS_CMD_SET_ONLINE: case ACHS_CMD_LOG:
        break;
    default:
        if (cmd >= ACHS_CMD_CLOSE && cmd <= ACHS_CMD_PENDING) {
            if (!(s = sock_of(a0)) || s->hidden) { *err = E_BADF; return -1; }
        }
    }
    switch (cmd) {
    case ACHS_CMD_HELLO: return (LONG)ACHS_VERSION;
    case ACHS_CMD_SOCKET: return c_socket(a0, a1, a2, err);
    case ACHS_CMD_CLOSE: sock_free(s); return 0;
    case ACHS_CMD_BIND: return c_bind(s, tx, txlen, err);
    case ACHS_CMD_LISTEN: return c_listen(s, a1, err);
    case ACHS_CMD_ACCEPT: return c_accept(s, rx, rxmax, rxlen, err);
    case ACHS_CMD_CONNECT: return c_connect(s, tx, txlen, err);
    case ACHS_CMD_SEND: return c_send(s, tx, txlen, err);
    case ACHS_CMD_SENDTO: {
        ip_addr_t ip; u16_t port;
        if (s->type == SOCK_STREAM) return c_send(s, tx + 16, txlen >= 16 ? txlen - 16 : 0, err);
        if (txlen < 16 || !sa_in(tx, 16, &ip, &port)) { *err = E_INVAL; return -1; }
        return dgram_send(s, tx + 16, txlen - 16, &ip, port, err);
    }
    case ACHS_CMD_RECV: return c_recv(s, a1, a2, rx, rxmax, rxlen, NULL, err);
    case ACHS_CMD_RECVFROM: {
        LONG r;
        if (rxmax < 16) { *err = E_INVAL; return -1; }
        r = c_recv(s, a1, a2, rx + 16, rxmax - 16, rxlen, rx, err);
        if (r >= 0 && rxlen) *rxlen += 16;
        return r;
    }
    case ACHS_CMD_SHUTDOWN:
        if (s->type != SOCK_STREAM) return 0;
        if (!s->connected || !s->pcb.tcp) { *err = E_NOTCONN; return -1; }
        if (a1 != 0) s->shut_wr = 1;
        tcp_shutdown(s->pcb.tcp, a1 != 1, a1 != 0);
        if (a1 != 1) s->eof = 1;
        return 0;
    case ACHS_CMD_SETSOCKOPT: return c_sockopt(s, 1, a1, a2, tx, txlen, NULL, 0, NULL, err);
    case ACHS_CMD_GETSOCKOPT: return c_sockopt(s, 0, a1, a2, NULL, 0, rx, a3 < rxmax ? a3 : rxmax, rxlen, err);
    case ACHS_CMD_GETSOCKNAME: return c_name(s, 0, rx, rxmax, rxlen, err);
    case ACHS_CMD_GETPEERNAME: return c_name(s, 1, rx, rxmax, rxlen, err);
    case ACHS_CMD_POLL: return c_poll(a0, tx, txlen, rx, rxmax, rxlen);
    case ACHS_CMD_PENDING:
        if (s->type == SOCK_STREAM) return s->rx ? s->rx->tot_len : 0;
        return s->dqn ? s->dq[s->dqh].p->tot_len : 0;
    case ACHS_CMD_RESOLVE: return c_resolve(0, tx, txlen, err);
    case ACHS_CMD_RESOLVE_ADDR: return c_resolve(1, tx, txlen, err);
    case ACHS_CMD_ANSWER: return c_answer(a0, rx, rxmax, rxlen, err);
    case ACHS_CMD_SERVICE: return c_service(a0, tx, txlen, rx, rxmax, rxlen, err);
    case ACHS_CMD_HOSTNAME: {
        ULONG i;
        for (i = 0; S.hostname[i] && i + 1 < rxmax; i++) rx[i] = (UBYTE)S.hostname[i];
        if (rxmax) rx[i++] = 0;
        if (rxlen) *rxlen = i;
        return 0;
    }
    case ACHS_CMD_STATUS: {
        ULONG st[10], i;
        for (i = 0; i < 10; i++) st[i] = 0;
        st[0] = (S.online ? ACNETWORK_STATE_CARD : 0) |
                (S.en_added && !ip4_addr_isany_val(*netif_ip4_addr(&S.en)) ? ACNETWORK_STATE_ONLINE : 0);
        st[1] = S.online ? 100 : 0;
        st[5] = S.rx_bytes; st[7] = S.tx_bytes;
        CopyMem(st, rx, rxmax < 40 ? rxmax : 40);
        if (rxlen) *rxlen = rxmax < 40 ? rxmax : 40;
        return 0;
    }
    case ACHS_CMD_INTERFACES: {
        struct ACNetworkInterface r;
        ULONG n = 0, max = a0;
        struct netif *lo = netif_find("lo0");
        if (lo && n < max && (n + 1) * sizeof(r) <= rxmax) { put_if(&r, 1, lo, "lo0"); CopyMem(&r, rx, sizeof(r)); n++; }
        if (S.en_added && n < max && (n + 1) * sizeof(r) <= rxmax) {
            put_if(&r, 2, &S.en, S.cfg.ifname[0] ? S.cfg.ifname : "gfopensocket0");
            CopyMem(&r, rx + n * sizeof(r), sizeof(r)); n++;
        }
        if (rxlen) *rxlen = n * sizeof(r);
        return (LONG)n;
    }
    case ACHS_CMD_DNS_SERVERS: {
        ULONG n = 0, i;
        for (i = 0; i < DNS_MAX_SERVERS && n < a0 && (n + 1) * 4 <= rxmax; i++) {
            const ip_addr_t *ns = dns_getserver((u8_t)i);
            ULONG a = ip_addr_get_ip4_u32(ns);
            if (a) { CopyMem(&a, rx + n * 4, 4); n++; }
        }
        if (rxlen) *rxlen = n * 4;
        return (LONG)n;
    }
    case ACHS_CMD_ROUTES: case ACHS_CMD_NEIGHBOURS: case ACHS_CMD_SOCKETS: case ACHS_CMD_LOG:
        return 0;
    case ACHS_CMD_SET_ONLINE:
        return S.online ? ACNETWORK_STATE_CARD | ACNETWORK_STATE_ONLINE : 0;
    case ACHS_CMD_NET_BY_NAME: case ACHS_CMD_NET_BY_ADDR:
        *err = H_HOST_NOT_FOUND;
        return -1;
    default:
        *err = E_NOSYS;
        return -1;
    }
}
