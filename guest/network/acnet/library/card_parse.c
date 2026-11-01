/* GFOpenSocket's card layer: the configuration files' lines (card.c reads
 * the files). Plain C with no Amiga calls, so tests/host/card_parse_test.c
 * runs it on the build machine. MIT. */
#include <exec/types.h>

#include "card.h"

static int lc(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

static int is_space(int c) { return c == ' ' || c == '\t' || c == '\r'; }

/* The keyword k (klen bytes) is want, in any case. */
int card_key_is(const char *k, LONG klen, const char *want)
{
    LONG i;
    for (i = 0; i < klen; i++)
        if (!want[i] || lc(k[i]) != want[i]) return 0;
    return want[i] == 0;
}

/* A dotted IPv4 address, in network order (as the Amiga is big-endian, the
 * first byte highest); 0 when it isn't one. */
static ULONG parse_ip(const char *s, LONG len)
{
    ULONG ip = 0, part = 0;
    LONG i, dots = 0, digits = 0;
    for (i = 0; i < len; i++) {
        char c = s[i];
        if (c >= '0' && c <= '9') {
            part = part * 10 + (c - '0');
            if (part > 255 || ++digits > 3) return 0;
        } else if (c == '.' && digits) {
            ip = (ip << 8) | part;
            part = 0; digits = 0;
            if (++dots > 3) return 0;
        } else return 0;
    }
    if (dots != 3 || !digits) return 0;
    return (ip << 8) | part;
}

static LONG parse_long(const char *s, LONG len)
{
    LONG v = 0, i;
    for (i = 0; i < len && s[i] >= '0' && s[i] <= '9'; i++) v = v * 10 + (s[i] - '0');
    return v;
}

void card_copy(char *to, LONG max, const char *s, LONG len)
{
    LONG i;
    if (len > max - 1) len = max - 1;
    for (i = 0; i < len; i++) to[i] = s[i];
    to[len] = 0;
}

static void add_ns(struct CardConfig *cfg, ULONG ip)
{
    LONG i;
    if (!ip) return;
    for (i = 0; i < CARD_NS_MAX; i++) {
        if (cfg->nameserver[i] == ip) return;
        if (!cfg->nameserver[i]) { cfg->nameserver[i] = ip; return; }
    }
}

/* One file's lines into cfg. ours: GFOpenSocket's own file (all keywords);
 * else a Roadshow file (its documented interface keywords and nameserver). */
LONG card_parse(struct CardConfig *cfg, const char *b, LONG n, int ours)
{
    LONG i = 0, got = 0;
    while (i < n) {
        LONG ks, ke, vs, ve, eol = i;
        while (eol < n && b[eol] != '\n') eol++;
        while (i < eol && is_space(b[i])) i++;
        if (i == eol || b[i] == '#' || b[i] == ';') { i = eol + 1; continue; }
        ks = i;
        while (i < eol && b[i] != '=' && !is_space(b[i])) i++;
        ke = i;
        while (i < eol && (is_space(b[i]) || b[i] == '=')) i++;
        vs = i;
        ve = eol;
        while (ve > vs && is_space(b[ve - 1])) ve--;
        if (ve - vs >= 2 && b[vs] == '"' && b[ve - 1] == '"') { vs++; ve--; }
        {
            const char *k = b + ks, *v = b + vs;
            LONG kl = ke - ks, vl = ve - vs;
            if (ours && card_key_is(k, kl, "backend")) {
                if (card_key_is(v, vl, "host")) cfg->backend = BACKEND_HOST;
                else if (card_key_is(v, vl, "direct")) cfg->backend = BACKEND_DIRECT;
                else if (card_key_is(v, vl, "none") || card_key_is(v, vl, "off")) cfg->backend = BACKEND_NONE;
                else { i = eol + 1; continue; }
                got |= GOT_BACKEND;
            } else if (card_key_is(k, kl, "device") && vl) {
                card_copy(cfg->device, CARD_DEVICE_MAX, v, vl);
                got |= GOT_DEVICE;
            } else if (card_key_is(k, kl, "unit")) {
                cfg->unit = parse_long(v, vl);
            } else if (card_key_is(k, kl, "configure")) {
                cfg->dhcp = card_key_is(v, vl, "dhcp");
            } else if (card_key_is(k, kl, "address")) {
                if (card_key_is(v, vl, "dhcp")) cfg->dhcp = 1;
                else cfg->address = parse_ip(v, vl);
            } else if (card_key_is(k, kl, "netmask")) {
                cfg->netmask = parse_ip(v, vl);
            } else if (ours && card_key_is(k, kl, "gateway")) {
                cfg->gateway = parse_ip(v, vl);
            } else if (card_key_is(k, kl, "nameserver")) {
                add_ns(cfg, parse_ip(v, vl));
            }
        }
        i = eol + 1;
    }
    return got;
}


/* A version string's "name V.R": V << 16 | R, or 0 when s (n bytes) does
 * not hold one. s starts at the name, as a RomTag's IdString does, or as a
 * "$VER: " string does after its "$VER: ". */
ULONG card_version(const char *s, LONG n)
{
    LONG i = 0;
    ULONG v = 0, r = 0;
    int dv = 0, dr = 0;
    while (i < n && is_space(s[i])) i++;
    while (i < n && s[i] && !is_space(s[i])) i++;     /* the name */
    while (i < n && is_space(s[i])) i++;
    for (; i < n && s[i] >= '0' && s[i] <= '9' && dv < 5; i++, dv++) v = v * 10 + (s[i] - '0');
    if (!dv || i >= n || s[i] != '.') return 0;
    for (i++; i < n && s[i] >= '0' && s[i] <= '9' && dr < 5; i++, dr++) r = r * 10 + (s[i] - '0');
    if (!dr || v > 0xffff || r > 0xffff) return 0;
    return v << 16 | r;
}
