/* GFOpenSocketDirect's IP_TTL (direct/direct_sock.c, c_sockopt), run on the
 * build machine with lwIP itself: set, read back, refused out of range, on
 * every packet a UDP, TCP or raw socket sends, and inherited by a listening
 * socket's accepted connections. ARexxNet's boundary relies on TTL 1 and
 * reads it back (amicradle-teams designs/ARexxNet-DESIGN-20261110.md, F1).
 * MIT. Built by the host-tests workflow:
 *   cc -Iinclude -Idirect_arch -I../../direct/port -I../../direct/lwip/src/include -I../../direct \
 *      -I../../library -I../../../../common/protocol -I../../../acnetwork/include \
 *      direct_ttl_test.c ../../direct/direct_sock.c <lwIP's core, as direct/build.sh> */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/ip4.h"
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
static u32_t now_ms;
u32_t sys_now(void) { return now_ms; }

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

/* en0: every IPv4 packet it sends, its TTL and protocol kept */
static int sent, last_ttl, last_proto;
static err_t en_output(struct netif *n, struct pbuf *p, const ip4_addr_t *to)
{
    const struct ip_hdr *h = (const struct ip_hdr *)p->payload;
    (void)n; (void)to;
    sent++;
    last_ttl = IPH_TTL(h);
    last_proto = IPH_PROTO(h);
    return ERR_OK;
}
static err_t en_init(struct netif *n)
{
    n->output = en_output;
    n->mtu = 1500;
    n->flags = NETIF_FLAG_UP | NETIF_FLAG_LINK_UP;
    n->name[0] = 'e'; n->name[1] = 'n';
    return ERR_OK;
}

static LONG cmd(ULONG c, ULONG a0, ULONG a1, ULONG a2, ULONG a3, const void *tx, ULONG txlen,
                void *rx, ULONG rxmax, ULONG *err)
{
    ULONG rxlen;
    return direct_command(c, a0, a1, a2, a3, tx, txlen, rx, rxmax, &rxlen, err);
}
static LONG set_ttl(LONG h, ULONG v, ULONG *err) { return cmd(ACHS_CMD_SETSOCKOPT, (ULONG)h, 0, 4, 0, &v, 4, NULL, 0, err); }
static ULONG get_ttl(LONG h)
{
    ULONG v = 0xdead, err;
    LONG r = cmd(ACHS_CMD_GETSOCKOPT, (ULONG)h, 0, 4, 4, NULL, 0, &v, 4, &err);
    return r < 0 ? 0xffffff00 : v;
}
static void sa(UBYTE o[16], UBYTE a, UBYTE b, UBYTE c, UBYTE d, UWORD port)
{
    memset(o, 0, 16);
    o[0] = 16; o[1] = 2; o[2] = (UBYTE)(port >> 8); o[3] = (UBYTE)port;
    o[4] = a; o[5] = b; o[6] = c; o[7] = d;
}
static void settle(void) { int i; for (i = 0; i < 20; i++) { netif_poll_all(); sys_check_timeouts(); } }

int main(void)
{
    ip4_addr_t ip, mask, gw;
    UBYTE to[16], pkt[24 + 4];
    ULONG err;
    LONG u, t, r, l, c, a;

    lwip_init();
    IP4_ADDR(&ip, 10, 0, 0, 2); IP4_ADDR(&mask, 255, 255, 255, 0); IP4_ADDR(&gw, 10, 0, 0, 1);
    netif_add(&S.en, &ip, &mask, &gw, NULL, en_init, netif_input);
    netif_set_default(&S.en);
    netif_set_up(&S.en);

    /* UDP: the default, then 1, read back and on the wire */
    u = cmd(ACHS_CMD_SOCKET, 2, 2, 0, 0, NULL, 0, NULL, 0, &err);
    CHECK(u >= 0);
    CHECK(get_ttl(u) == IP_DEFAULT_TTL);
    CHECK(set_ttl(u, 1, &err) == 0);
    CHECK(get_ttl(u) == 1);
    sa(pkt, 10, 0, 0, 9, 9);
    memcpy(pkt + 16, "ttl!", 4);
    sent = 0;
    CHECK(cmd(ACHS_CMD_SENDTO, (ULONG)u, 0, 0, 0, pkt, 20, NULL, 0, &err) == 4);
    CHECK(sent == 1 && last_ttl == 1 && last_proto == IP_PROTO_UDP);
    /* out of range: refused with EINVAL, the TTL as it was */
    err = 0; CHECK(set_ttl(u, 0, &err) == -1 && err == 22);
    err = 0; CHECK(set_ttl(u, 256, &err) == -1 && err == 22);
    err = 0; CHECK(cmd(ACHS_CMD_SETSOCKOPT, (ULONG)u, 0, 4, 0, "\1", 1, NULL, 0, &err) == -1 && err == 22);
    CHECK(get_ttl(u) == 1);
    /* -1: the default again */
    CHECK(set_ttl(u, 0xffffffffUL, &err) == 0 && get_ttl(u) == IP_DEFAULT_TTL);
    CHECK(set_ttl(u, 64, &err) == 0 && get_ttl(u) == 64);
    sent = 0;
    CHECK(cmd(ACHS_CMD_SENDTO, (ULONG)u, 0, 0, 0, pkt, 20, NULL, 0, &err) == 4);
    CHECK(sent == 1 && last_ttl == 64);

    /* TCP: the SYN carries it */
    t = cmd(ACHS_CMD_SOCKET, 2, 1, 0, 0, NULL, 0, NULL, 0, &err);
    CHECK(t >= 0 && set_ttl(t, 1, &err) == 0 && get_ttl(t) == 1);
    sa(to, 10, 0, 0, 9, 80);
    sent = 0;
    cmd(ACHS_CMD_CONNECT, (ULONG)t, 0, 0, 0, to, 16, NULL, 0, &err);
    CHECK(sent >= 1 && last_ttl == 1 && last_proto == IP_PROTO_TCP);

    /* raw: its packets too */
    r = cmd(ACHS_CMD_SOCKET, 2, 3, 1, 0, NULL, 0, NULL, 0, &err);
    CHECK(r >= 0 && set_ttl(r, 2, &err) == 0 && get_ttl(r) == 2);
    sa(pkt, 10, 0, 0, 9, 0);
    memcpy(pkt + 16, "\x08\0\xf7\xff\0\0\0\0", 8);  /* an ICMP echo request */
    sent = 0;
    cmd(ACHS_CMD_SENDTO, (ULONG)r, 0, 0, 0, pkt, 24, NULL, 0, &err);
    CHECK(sent == 1 && last_ttl == 2 && last_proto == IP_PROTO_ICMP);

    /* a listening socket's TTL, set before and after LISTEN, is its accepted connections' */
    l = cmd(ACHS_CMD_SOCKET, 2, 1, 0, 0, NULL, 0, NULL, 0, &err);
    sa(to, 127, 0, 0, 1, 7001);
    CHECK(l >= 0 && cmd(ACHS_CMD_BIND, (ULONG)l, 0, 0, 0, to, 16, NULL, 0, &err) == 0);
    CHECK(set_ttl(l, 3, &err) == 0);
    CHECK(cmd(ACHS_CMD_LISTEN, (ULONG)l, 4, 0, 0, NULL, 0, NULL, 0, &err) == 0);
    CHECK(get_ttl(l) == 3);
    CHECK(set_ttl(l, 1, &err) == 0 && get_ttl(l) == 1);
    c = cmd(ACHS_CMD_SOCKET, 2, 1, 0, 0, NULL, 0, NULL, 0, &err);
    cmd(ACHS_CMD_CONNECT, (ULONG)c, 0, 0, 0, to, 16, NULL, 0, &err);
    settle();
    a = cmd(ACHS_CMD_ACCEPT, (ULONG)l, 0, 0, 0, NULL, 0, pkt, 16, &err);
    CHECK(a >= 0);
    if (a >= 0) CHECK(get_ttl(a) == 1);
    CHECK(get_ttl(c) == IP_DEFAULT_TTL);

    printf("%s direct_ttl_test: IP_TTL set, read back, refused out of range, on UDP, TCP and raw packets, inherited by accept\n",
           fails ? "FAIL" : "ok  ");
    return fails != 0;
}
