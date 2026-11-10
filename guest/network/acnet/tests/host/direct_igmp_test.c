/* GFOpenSocketDirect's multicast (IGMP, direct/port/lwipopts.h DIRECT_IGMP;
 * direct/direct_sock.c, mc_sockopt), run on the build machine with lwIP
 * itself: joining mDNS's 224.0.0.251 tells the interface its Ethernet address
 * and sends a membership report; an mDNS answer to the group then reaches the
 * socket, one to a group not joined doesn't; the multicast TTL (1 by default,
 * as BSD), loop and leaving, by IP_DROP_MEMBERSHIP or by closing the socket.
 * amicradle-teams designs/ARexxNet-DESIGN-20261110.md, F2. MIT.
 * Built as direct_ttl_test.c is (the host-tests workflow). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/ip4.h"
#include "lwip/igmp.h"
#include "lwip/inet_chksum.h"
#include "lwip/prot/ip4.h"
#include "lwip/prot/ip.h"
#include "lwip/timeouts.h"

#include "achostsocket.h"
#include "direct_int.h"

struct DirectStack S;
void *direct_malloc(size_t n) { return malloc(n ? n : 1); }
void direct_free(void *p) { free(p); }
void *direct_calloc(size_t n, size_t m) { return calloc(n ? n : 1, m ? m : 1); }
unsigned long direct_rand(void) { static unsigned long r = 12345; return r = r * 1103515245UL + 12345UL; }
void direct_pump(void) { netif_poll_all(); }
u32_t sys_now(void) { return 0; }

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

/* en0: the packets it sends, and the multicast addresses it is told of */
static int sent, last_ttl, last_proto;
static u32_t last_dst;
static int mac_on[256];                 /* by the group's low byte: groups the filter holds */
static err_t en_output(struct netif *n, struct pbuf *p, const ip4_addr_t *to)
{
    const struct ip_hdr *h = (const struct ip_hdr *)p->payload;
    (void)n; (void)to;
    sent++;
    last_ttl = IPH_TTL(h);
    last_proto = IPH_PROTO(h);
    last_dst = lwip_ntohl(ip4_addr_get_u32(&h->dest));
    return ERR_OK;
}
static err_t en_filter(struct netif *n, const ip4_addr_t *g, enum netif_mac_filter_action act)
{
    (void)n;
    mac_on[lwip_ntohl(ip4_addr_get_u32(g)) & 0xff] += act == NETIF_ADD_MAC_FILTER ? 1 : -1;
    return ERR_OK;
}
static err_t en_init(struct netif *n)
{
    n->output = en_output;
    n->mtu = 1500;
    n->flags = NETIF_FLAG_UP | NETIF_FLAG_LINK_UP | NETIF_FLAG_IGMP;
    netif_set_igmp_mac_filter(n, en_filter);
    n->name[0] = 'e'; n->name[1] = 'n';
    return ERR_OK;
}

static LONG cmd(ULONG c, ULONG a0, ULONG a1, ULONG a2, ULONG a3, const void *tx, ULONG txlen,
                void *rx, ULONG rxmax, ULONG *err)
{
    ULONG rxlen;
    return direct_command(c, a0, a1, a2, a3, tx, txlen, rx, rxmax, &rxlen, err);
}
static LONG opt(LONG h, ULONG o, const void *v, ULONG n, ULONG *err) { return cmd(ACHS_CMD_SETSOCKOPT, (ULONG)h, 0, o, 0, v, n, NULL, 0, err); }
static ULONG getopt_ip(LONG h, ULONG o)
{
    ULONG v = 0xdead, err;
    return cmd(ACHS_CMD_GETSOCKOPT, (ULONG)h, 0, o, 4, NULL, 0, &v, 4, &err) < 0 ? 0xffffff00 : v;
}
static LONG memb(LONG h, ULONG o, UBYTE d, ULONG *err)
{
    UBYTE m[8] = { 224, 0, 0, 0, 0, 0, 0, 0 };
    m[3] = d;
    return opt(h, o, m, 8, err);
}
static void sa(UBYTE o[16], UBYTE a, UBYTE b, UBYTE c, UBYTE d, UWORD port)
{
    memset(o, 0, 16);
    o[0] = 16; o[1] = 2; o[2] = (UBYTE)(port >> 8); o[3] = (UBYTE)port;
    o[4] = a; o[5] = b; o[6] = c; o[7] = d;
}

/* an mDNS answer from 10.0.0.9:5353 to 224.0.0.<d>:5353, into en0 */
static void answer_in(UBYTE d)
{
    static const char msg[] = "\0\0\x84\0\0\0\0\1\0\0\0\0";      /* a response header, one answer */
    UBYTE b[20 + 8 + 12];
    struct pbuf *p;
    memset(b, 0, sizeof b);
    b[0] = 0x45; b[3] = sizeof b; b[8] = 255; b[9] = IP_PROTO_UDP;
    b[12] = 10; b[15] = 9; b[16] = 224; b[19] = d;
    {
        u16_t c = inet_chksum(b, 20);
        memcpy(b + 10, &c, 2);
    }
    b[20] = 0x14; b[21] = 0xe9; b[22] = 0x14; b[23] = 0xe9; b[25] = 8 + 12;     /* 5353 to 5353, no checksum */
    memcpy(b + 28, msg, 12);
    p = pbuf_alloc(PBUF_RAW, sizeof b, PBUF_RAM);
    pbuf_take(p, b, sizeof b);
    if (S.en.input(p, &S.en) != ERR_OK) pbuf_free(p);
}

static LONG recv_any(LONG h, UBYTE *from)
{
    UBYTE b[16 + 64];
    ULONG err;
    LONG r = cmd(ACHS_CMD_RECVFROM, (ULONG)h, 64, 0, 0, NULL, 0, b, sizeof b, &err);
    if (r >= 0 && from) memcpy(from, b + 4, 4);
    return r;
}

int main(void)
{
    ip4_addr_t ip, mask, gw;
    UBYTE to[16], pkt[16 + 4], from[4];
    ULONG err, one = 1, v;
    LONG u, u2, t;

    lwip_init();
    IP4_ADDR(&ip, 10, 0, 0, 2); IP4_ADDR(&mask, 255, 255, 255, 0); IP4_ADDR(&gw, 10, 0, 0, 1);
    netif_add(&S.en, &ip, &mask, &gw, NULL, en_init, netif_input);
    netif_set_default(&S.en);
    netif_set_up(&S.en);
    CHECK(mac_on[1] == 1);                                      /* all systems, 224.0.0.1 */

    u = cmd(ACHS_CMD_SOCKET, 2, 2, 0, 0, NULL, 0, NULL, 0, &err);
    CHECK(u >= 0);
    cmd(ACHS_CMD_SETSOCKOPT, (ULONG)u, 0xffff, 4, 0, &one, 4, NULL, 0, &err);
    sa(to, 0, 0, 0, 0, 5353);
    CHECK(cmd(ACHS_CMD_BIND, (ULONG)u, 0, 0, 0, to, 16, NULL, 0, &err) == 0);

    /* joined: the filter has 224.0.0.251, a report goes to the group with TTL 1 */
    sent = 0;
    CHECK(memb(u, 12, 251, &err) == 0);
    CHECK(mac_on[251] == 1);
    CHECK(sent == 1 && last_proto == IP_PROTO_IGMP && last_dst == 0xe00000fbUL && last_ttl == 1);
    err = 0; CHECK(memb(u, 12, 251, &err) == -1 && err == 48);  /* joined already */

    /* an answer to the group arrives; one to 224.0.0.252 doesn't */
    answer_in(251);
    CHECK(recv_any(u, from) == 12 && from[0] == 10 && from[3] == 9);
    answer_in(252);
    err = 0; CHECK(recv_any(u, NULL) < 0);

    /* a second socket on 5353 joins too; the filter counts the group once */
    u2 = cmd(ACHS_CMD_SOCKET, 2, 2, 0, 0, NULL, 0, NULL, 0, &err);
    cmd(ACHS_CMD_SETSOCKOPT, (ULONG)u2, 0xffff, 4, 0, &one, 4, NULL, 0, &err);
    CHECK(cmd(ACHS_CMD_BIND, (ULONG)u2, 0, 0, 0, to, 16, NULL, 0, &err) == 0);
    CHECK(memb(u2, 12, 251, &err) == 0 && mac_on[251] == 1);

    /* sending to the group: TTL 1 by default, IP_MULTICAST_TTL 255 for mDNS, read back; IP_TTL untouched */
    CHECK(getopt_ip(u, 10) == 1 && getopt_ip(u, 11) == 1);
    sa(pkt, 224, 0, 0, 251, 5353);
    memcpy(pkt + 16, "mdns", 4);
    sent = 0;
    CHECK(cmd(ACHS_CMD_SENDTO, (ULONG)u, 0, 0, 0, pkt, 20, NULL, 0, &err) == 4);
    CHECK(sent == 1 && last_ttl == 1 && last_dst == 0xe00000fbUL);
    v = 255; CHECK(opt(u, 10, &v, 4, &err) == 0 && getopt_ip(u, 10) == 255);
    { UBYTE c = 7; CHECK(opt(u, 10, &c, 1, &err) == 0 && getopt_ip(u, 10) == 7); }
    v = 255; opt(u, 10, &v, 4, &err);
    sent = 0;
    CHECK(cmd(ACHS_CMD_SENDTO, (ULONG)u, 0, 0, 0, pkt, 20, NULL, 0, &err) == 4);
    CHECK(sent == 1 && last_ttl == 255);
    CHECK(getopt_ip(u, 4) == IP_DEFAULT_TTL);
    v = 256; err = 0; CHECK(opt(u, 10, &v, 4, &err) == -1 && err == 22);
    v = 0; CHECK(opt(u, 11, &v, 4, &err) == 0 && getopt_ip(u, 11) == 0);
    /* looped back to the other socket while loop is on (it was for u2's sends) */
    sent = 0;
    CHECK(cmd(ACHS_CMD_SENDTO, (ULONG)u2, 0, 0, 0, pkt, 20, NULL, 0, &err) == 4);
    netif_poll_all();
    CHECK(recv_any(u, NULL) == 4);
    while (recv_any(u2, NULL) >= 0)
        ;
    while (recv_any(u, NULL) >= 0)                              /* u's own sends, looped back while its loop was on */
        ;

    /* refused: not a group, a stream socket, a group not joined */
    { UBYTE m[8] = { 10, 0, 0, 9, 0, 0, 0, 0 }; err = 0; CHECK(opt(u, 12, m, 8, &err) == -1 && err == 22); }
    t = cmd(ACHS_CMD_SOCKET, 2, 1, 0, 0, NULL, 0, NULL, 0, &err);
    err = 0; CHECK(memb(t, 12, 251, &err) == -1 && err == 45);
    err = 0; CHECK(memb(u, 13, 250, &err) == -1 && err == 49);

    /* a socket bound to the port that hasn't joined the group gets none of its datagrams (BSD) */
    t = cmd(ACHS_CMD_SOCKET, 2, 2, 0, 0, NULL, 0, NULL, 0, &err);
    cmd(ACHS_CMD_SETSOCKOPT, (ULONG)t, 0xffff, 4, 0, &one, 4, NULL, 0, &err);
    CHECK(cmd(ACHS_CMD_BIND, (ULONG)t, 0, 0, 0, to, 16, NULL, 0, &err) == 0);
    answer_in(251);
    CHECK(recv_any(u, NULL) == 12 && recv_any(u2, NULL) == 12 && recv_any(t, NULL) < 0);

    /* leaving: u drops it (u2 still has it: the filter keeps it, u gets nothing more); u2's close leaves it */
    CHECK(memb(u, 13, 251, &err) == 0 && mac_on[251] == 1);
    answer_in(251);
    CHECK(recv_any(u, NULL) < 0);
    CHECK(recv_any(u2, NULL) == 12);
    sent = 0;
    cmd(ACHS_CMD_CLOSE, (ULONG)u2, 0, 0, 0, NULL, 0, NULL, 0, &err);
    CHECK(mac_on[251] == 0);
    CHECK(sent == 1 && last_proto == IP_PROTO_IGMP && last_dst == 0xe0000002UL);   /* a leave, to all routers */
    answer_in(251);
    CHECK(recv_any(u, NULL) < 0);

    printf("%s direct_igmp_test: join 224.0.0.251 (filter, report), an mDNS answer received, multicast TTL and loop, leave and close\n",
           fails ? "FAIL" : "ok  ");
    return fails != 0;
}
