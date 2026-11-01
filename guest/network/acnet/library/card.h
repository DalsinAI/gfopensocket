#ifndef OPENSOCKET_CARD_H
#define OPENSOCKET_CARD_H

/* GFOpenSocket's card layer: which backend answers the socket calls, chosen
 * once, at the library's init (its first OpenLibrary, made by ramlib after
 * DOS), from configuration on disk and the machine's cards. MIT. */
#include <exec/types.h>

#define BACKEND_NONE    0   /* no card, no device: the init keeps nothing */
#define BACKEND_HOST    1   /* the GFOpenSocket card: calls go to the host's sockets */
#define BACKEND_DIRECT  2   /* GFOpenSocket's own stack over a SANA-II device */

/* Where the choice came from (for status and the tests). */
#define CHOSEN_BY_CONFIG    1   /* ENV:GFOpenSocket/Interface (or ENVARC:) */
#define CHOSEN_BY_CARD      2   /* the GFOpenSocket card is fitted */
#define CHOSEN_BY_ROADSHOW  3   /* a Roadshow interface file, DEVS:NetInterfaces/ */
#define CHOSEN_BY_DRIVER    4   /* a SANA-II driver in DEVS:Networks/, with no file naming it */

#define CARD_DEVICE_MAX 108
#define CARD_NS_MAX     3

/* The interface the Direct backend drives, read from the configuration.
 * Addresses are IPv4 in network order; 0 is "not set". */
struct CardConfig {
    UBYTE backend, chosen_by, dhcp, pad;
    UBYTE lib_disk, dev_disk, pad2[2];  /* Host: 1 when the disk's gfopensocket.library
                                         * (device) is newer than the ROM's, and is used */
    LONG  unit;
    char  device[CARD_DEVICE_MAX];   /* a SANA-II device, as OpenDevice takes it */
    char  ifname[32];                /* the interface file's name (Roadshow), or "gfopensocket0" */
    ULONG address, netmask, gateway;
    ULONG nameserver[CARD_NS_MAX];
};

/* Fills cfg and returns its backend. Called from the init only. */
LONG card_choose(struct CardConfig *cfg);

/* card_parse.c: one file's n bytes of lines into cfg. ours: GFOpenSocket's
 * own file (all its keywords); else a Roadshow file (only the documented
 * interface keywords and nameserver). What it found: */
#define GOT_BACKEND 1
#define GOT_DEVICE  2
LONG card_parse(struct CardConfig *cfg, const char *b, LONG n, int ours);
int  card_key_is(const char *k, LONG klen, const char *want);   /* want in lower case */
void card_copy(char *to, LONG max, const char *s, LONG len);
ULONG card_version(const char *s, LONG n);   /* "name V.R": V << 16 | R, or 0 */

#endif
