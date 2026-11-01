/* The card layer's configuration lines (library/card_parse.c), run on the
 * build machine: GFOpenSocket's own ENV:GFOpenSocket/Interface, a Roadshow
 * DEVS:NetInterfaces/ file (its documented keywords) and
 * DEVS:Internet/name_resolution. MIT.
 *   cc -Iinclude -I../../library card_parse_test.c ../../library/card_parse.c */
#include <stdio.h>
#include <string.h>
#include <exec/types.h>
#include "card.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
#define IP(a, b, c, d) (((ULONG)(a) << 24) | ((ULONG)(b) << 16) | ((ULONG)(c) << 8) | (ULONG)(d))

static LONG run(struct CardConfig *cfg, const char *text, int ours)
{
    memset(cfg, 0, sizeof(*cfg));
    return card_parse(cfg, text, (LONG)strlen(text), ours);
}

int main(void)
{
    struct CardConfig c;
    LONG got;

    got = run(&c, "# GFOpenSocket\nbackend=host\n", 1);
    CHECK(got == GOT_BACKEND && c.backend == BACKEND_HOST);

    got = run(&c, "Backend = Direct\r\nDEVICE=prism2.device\nunit=1\naddress=192.168.1.20\n"
                  "netmask=255.255.255.0\ngateway=192.168.1.1\nnameserver 1.1.1.1\nnameserver=8.8.8.8\n", 1);
    CHECK(got == (GOT_BACKEND | GOT_DEVICE) && c.backend == BACKEND_DIRECT);
    CHECK(!strcmp(c.device, "prism2.device") && c.unit == 1 && !c.dhcp);
    CHECK(c.address == IP(192, 168, 1, 20) && c.netmask == IP(255, 255, 255, 0) && c.gateway == IP(192, 168, 1, 1));
    CHECK(c.nameserver[0] == IP(1, 1, 1, 1) && c.nameserver[1] == IP(8, 8, 8, 8) && !c.nameserver[2]);

    got = run(&c, "backend=none\n", 1);
    CHECK(got == GOT_BACKEND && c.backend == BACKEND_NONE);
    got = run(&c, "backend=sideways\n", 1);
    CHECK(got == 0);
    got = run(&c, "\n# nothing\n; nothing\n", 1);
    CHECK(got == 0);

    /* a Roadshow interface file: device, unit, configure=dhcp; backend and
     * gateway are GFOpenSocket's keywords, not read from it */
    got = run(&c, "device=a2065.device\nunit=0\nconfigure=dhcp\ndowngoesoffline=yes\nbackend=host\ngateway=10.0.0.1\n", 0);
    CHECK(got == GOT_DEVICE && !strcmp(c.device, "a2065.device") && c.dhcp && c.backend == BACKEND_NONE && !c.gateway);
    got = run(&c, "device=\"DEVS:Networks/plipbox.device\"\naddress=10.0.0.5\nnetmask=255.0.0.0", 0);
    CHECK(got == GOT_DEVICE && !strcmp(c.device, "DEVS:Networks/plipbox.device"));
    CHECK(c.address == IP(10, 0, 0, 5) && c.netmask == IP(255, 0, 0, 0) && !c.dhcp);
    got = run(&c, "device=x.device\naddress=dhcp\n", 0);
    CHECK(c.dhcp);

    /* name_resolution */
    run(&c, "domain example.org\nnameserver 192.168.0.1\nnameserver 192.168.0.1\nnameserver 9.9.9.9\n"
            "nameserver 1.0.0.1\nnameserver 4.4.4.4\n", 0);
    CHECK(c.nameserver[0] == IP(192, 168, 0, 1) && c.nameserver[1] == IP(9, 9, 9, 9) && c.nameserver[2] == IP(1, 0, 0, 1));

    /* addresses that are not addresses */
    run(&c, "address=256.1.1.1\nnetmask=1.2.3\ngateway=1.2.3.4.5\n", 1);
    CHECK(!c.address && !c.netmask && !c.gateway);

    /* a long device name is cut, not overrun */
    {
        char line[400];
        strcpy(line, "device=");
        memset(line + 7, 'd', 300);
        line[307] = 0;
        run(&c, line, 1);
        CHECK(strlen(c.device) == CARD_DEVICE_MAX - 1);
    }

    /* version strings: a RomTag's IdString, and a file's $VER after "$VER: " */
    CHECK(card_version("gfopensocket.device 1.1 (16.10.2026) GFOpenSocket\r\n", 42) == (1UL << 16 | 1));
    CHECK(card_version("gfopensocket.library 1.12 (x)", 29) == (1UL << 16 | 12));
    CHECK(card_version("gfopensocket.device 2", 21) == 0);
    CHECK(card_version("gfopensocket.device", 19) == 0);
    CHECK(card_version("gfopensocket.device 3.0", 23) == (3UL << 16));

    printf("card_parse: %s (%d failed)\n", fails ? "FAIL" : "PASS", fails);
    return fails != 0;
}
