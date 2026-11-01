/* sntp: the time from a time server, and how far this Amiga's clock is off.
 *   sntp                    asks pool.ntp.org
 *   sntp time.example.net
 *   sntp SET                also sets the Amiga's clock
 *   sntp SET SAVE           and its battery-backed clock
 * The Amiga's clock is local time: the offset from UTC comes from Locale
 * prefs (AmigaOS 2.1 and later), else UTC is assumed.
 * GFOpenSocket ACTCPTools. MIT. */
#include <exec/types.h>
#include <exec/io.h>
#include <devices/timer.h>
#include <libraries/locale.h>
#include <utility/date.h>
#include <resources/battclock.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/timer.h>
#include <proto/locale.h>
#include <proto/utility.h>
#include <proto/battclock.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <proto/bsdsocket.h>
#include <stdio.h>
#include <string.h>

#include "ostool.h"

static const char version[] __attribute__((used)) = "$VER: sntp 1.0 (4.10.2026) GFOpenSocket";

struct Device *TimerBase;
struct LocaleBase *LocaleBase;
struct Library *BattClockBase;

#define NTP_TO_AMIGA 2461449600UL       /* 1900-01-01 to 1978-01-01, in seconds */

typedef long long us_t;                 /* microseconds since 1978-01-01 UTC */

static struct MsgPort *tport;
static struct timerequest *treq;

static LONG gmt_minutes;                /* minutes west of UTC, from Locale */

static us_t now_utc(void)
{
    struct timeval tv;
    GetSysTime(&tv);
    return ((us_t)tv.tv_secs + (us_t)gmt_minutes * 60) * 1000000 + tv.tv_micro;
}

static us_t ntp_to_us(const UBYTE *p)
{
    ULONG secs = (ULONG)p[0] << 24 | (ULONG)p[1] << 16 | (ULONG)p[2] << 8 | p[3];
    ULONG frac = (ULONG)p[4] << 24 | (ULONG)p[5] << 16 | (ULONG)p[6] << 8 | p[7];
    return (us_t)(secs - NTP_TO_AMIGA) * 1000000 + (us_t)(((unsigned long long)frac * 1000000) >> 32);
}

static void put_ntp(UBYTE *p, us_t t)
{
    ULONG secs = (ULONG)(t / 1000000) + NTP_TO_AMIGA;
    ULONG frac = (ULONG)((((unsigned long long)(t % 1000000)) << 32) / 1000000);
    p[0] = secs >> 24; p[1] = secs >> 16; p[2] = secs >> 8; p[3] = secs;
    p[4] = frac >> 24; p[5] = frac >> 16; p[6] = frac >> 8; p[7] = frac;
}

static void show_time(const char *what, ULONG secs)
{
    static const char *mon[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    struct ClockData cd;
    Amiga2Date(secs, &cd);
    printf("%s%02u-%s-%04u %02u:%02u:%02u", what, (unsigned)cd.mday, mon[(cd.month - 1) % 12],
           (unsigned)cd.year, (unsigned)cd.hour, (unsigned)cd.min, (unsigned)cd.sec);
}

static void show_offset(us_t off)
{
    us_t a = off < 0 ? -off : off;
    if (a < 1000) printf("This Amiga's clock is right (within 1 ms).\n");
    else printf("This Amiga's clock is %lld.%03lld s %s.\n", a / 1000000, (a % 1000000) / 1000, off < 0 ? "fast" : "slow");
}

static int sntp_main(int argc, char **argv)
{
    const char *server = "pool.ntp.org";
    BOOL set = FALSE, save = FALSE;
    struct sockaddr_in sa;
    struct Locale *loc;
    UBYTE pkt[48];
    ULONG addr;
    LONG s = -1;
    us_t t1 = 0, t4 = 0, t2, t3, off = 0;
    int i, tries, got = 0, rc = 10;

    for (i = 1; i < argc; ++i) {
        if (!strcasecmp(argv[i], "SET")) set = TRUE;
        else if (!strcasecmp(argv[i], "SAVE")) save = set = TRUE;
        else if (argv[i][0] == '-' || argv[i][0] == '?') { server = NULL; break; }
        else server = argv[i];
    }
    if (!server) {
        fprintf(stderr, "Usage: sntp [server] [SET [SAVE]]\n");
        return 20;
    }

    tport = CreateMsgPort();
    treq = tport ? (struct timerequest *)CreateIORequest(tport, sizeof(*treq)) : NULL;
    if (!treq || OpenDevice((STRPTR)TIMERNAME, UNIT_VBLANK, (struct IORequest *)treq, 0)) {
        fprintf(stderr, "sntp: no timer.device\n");
        goto out;
    }
    TimerBase = treq->tr_node.io_Device;
    if ((LocaleBase = (struct LocaleBase *)OpenLibrary("locale.library", 38)) && (loc = OpenLocale(NULL))) {
        gmt_minutes = loc->loc_GMTOffset;
        CloseLocale(loc);
    } else {
        printf("No Locale: taking this Amiga's clock to be UTC.\n");
    }

    if (!ost_open("sntp")) goto out;
    if (!ost_resolve("sntp", server, &addr)) goto out;
    s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) { fprintf(stderr, "sntp: no socket (error %ld)\n", (long)Errno()); goto out; }
    memset(&sa, 0, sizeof(sa));
    sa.sin_len = sizeof(sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(123);
    sa.sin_addr.s_addr = addr;

    for (tries = 0; tries < 3 && !got; ++tries) {
        memset(pkt, 0, sizeof(pkt));
        pkt[0] = 0x23;                              /* no leap warning, version 4, client */
        t1 = now_utc();
        put_ntp(pkt + 40, t1);                      /* our transmit time; the server echoes it */
        if (sendto(s, pkt, sizeof(pkt), 0, (struct sockaddr *)&sa, sizeof(sa)) != sizeof(pkt)) {
            fprintf(stderr, "sntp: could not send (error %ld)\n", (long)Errno());
            goto out;
        }
        for (;;) {
            UBYTE in[48];
            int w = ost_wait_read(s, 3000);
            LONG n;
            if (w < 0) { fprintf(stderr, "sntp: stopped\n"); goto out; }
            if (w == 0) break;                      /* try again */
            n = recv(s, in, sizeof(in), 0);
            t4 = now_utc();
            if (n < 48 || (in[0] & 7) != 4 || memcmp(in + 24, pkt + 40, 8)) continue;   /* not our answer */
            if (in[1] == 0) {
                fprintf(stderr, "sntp: %s asks us to go away (%.4s)\n", server, (char *)in + 12);
                goto out;
            }
            memcpy(pkt, in, 48);
            got = 1;
            break;
        }
    }
    if (!got) { fprintf(stderr, "sntp: no answer from %s\n", server); goto out; }

    t2 = ntp_to_us(pkt + 32);                       /* the server received */
    t3 = ntp_to_us(pkt + 40);                       /* the server sent */
    off = ((t2 - t1) + (t3 - t4)) / 2;
    show_time("Server time  ", (ULONG)((t4 + off) / 1000000));
    printf(" UTC  (%s, stratum %u, round trip %lld ms)\n", server, (unsigned)pkt[1], ((t4 - t1) - (t3 - t2)) / 1000);
    show_offset(off);
    rc = 0;

    if (set) {
        us_t local = t4 + off - (us_t)gmt_minutes * 60 * 1000000 + (now_utc() - t4);
        treq->tr_node.io_Command = TR_SETSYSTIME;
        treq->tr_time.tv_secs = (ULONG)(local / 1000000);
        treq->tr_time.tv_micro = (ULONG)(local % 1000000);
        if (DoIO((struct IORequest *)treq)) { fprintf(stderr, "sntp: the clock could not be set\n"); rc = 10; }
        else show_time("Set the clock to ", treq->tr_time.tv_secs), printf(" (local)\n");
        if (save && rc == 0) {
            if ((BattClockBase = (struct Library *)OpenResource((STRPTR)BATTCLOCKNAME))) {
                WriteBattClock(treq->tr_time.tv_secs);
                printf("Saved it in the battery-backed clock.\n");
            } else {
                printf("This Amiga has no battery-backed clock to save it in.\n");
            }
        }
    }

out:
    if (s >= 0) CloseSocket(s);
    ost_close();
    if (LocaleBase) CloseLibrary((struct Library *)LocaleBase);
    if (treq) {
        if (TimerBase) CloseDevice((struct IORequest *)treq);
        DeleteIORequest((struct IORequest *)treq);
    }
    if (tport) DeleteMsgPort(tport);
    return rc;
}

int main(int argc, char **argv)
{
    return ost_main(sntp_main, argc, argv);
}
