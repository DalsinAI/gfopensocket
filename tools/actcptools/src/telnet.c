/* telnet: a terminal session on another machine.
 *   telnet host [port]
 * Ctrl-] closes the connection; Ctrl-C goes to the other machine. The
 * terminal type is VT100 (the Amiga's console speaks most of it); the
 * cursor keys are sent as VT100 keys, Backspace as DEL.
 * GFOpenSocket ACTCPTools. MIT. */
#include <exec/types.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <proto/bsdsocket.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ostool.h"

static const char version[] __attribute__((used)) = "$VER: telnet 1.0 (4.10.2026) GFOpenSocket";

enum { SE = 240, SB = 250, WILL = 251, WONT = 252, DO = 253, DONT = 254, IAC = 255 };
enum { OPT_ECHO = 1, OPT_SGA = 3, OPT_TTYPE = 24, OPT_NAWS = 31 };
#define QUIT_KEY 0x1d                   /* Ctrl-] */

static LONG s;
static BOOL remote_echo;
static UBYTE net[2048], key[256], show[2048];

static void send3(UBYTE a, UBYTE b, UBYTE c)
{
    UBYTE x[3];
    x[0] = a; x[1] = b; x[2] = c;
    ost_send_all(s, x, 3);
}

/* Answers the other side's option requests: echo and suppress-go-ahead
 * from them; terminal type and window size from us; nothing else. */
static void option(UBYTE verb, UBYTE opt)
{
    static UBYTE asked_do[256], asked_will[256];
    if (verb == WILL) {
        BOOL ok = opt == OPT_ECHO || opt == OPT_SGA;
        if (opt == OPT_ECHO) remote_echo = TRUE;
        if (!asked_do[opt]) send3(IAC, ok ? DO : DONT, opt);
        asked_do[opt] = 1;
    } else if (verb == WONT) {
        if (opt == OPT_ECHO) remote_echo = FALSE;
        asked_do[opt] = 0;
    } else if (verb == DO) {
        BOOL ok = opt == OPT_TTYPE || opt == OPT_NAWS || opt == OPT_SGA;
        if (!asked_will[opt]) send3(IAC, ok ? WILL : WONT, opt);
        asked_will[opt] = 1;
        if (opt == OPT_NAWS) {
            static const UBYTE naws[] = { IAC, SB, OPT_NAWS, 0, 80, 0, 24, IAC, SE };
            ost_send_all(s, naws, sizeof(naws));
        }
    } else if (verb == DONT) {
        asked_will[opt] = 0;
    }
}

static void suboption(const UBYTE *sb, long len)
{
    if (len >= 2 && sb[0] == OPT_TTYPE && sb[1] == 1) {          /* SEND: we are a VT100 */
        static const UBYTE is[] = { IAC, SB, OPT_TTYPE, 0, 'V', 'T', '1', '0', '0', IAC, SE };
        ost_send_all(s, is, sizeof(is));
    }
}

/* What came from the network: commands handled, the rest shown. */
static void from_net(const UBYTE *p, long n)
{
    static int state;                       /* 0 data, 1 IAC, 2 verb, 3 SB, 4 SB IAC; 5 after CR */
    static UBYTE verb, sb[64];
    static long sbn;
    long k = 0, i;
    for (i = 0; i < n; ++i) {
        UBYTE c = p[i];
        switch (state) {
            case 5:
                state = 0;
                if (c == 0) continue;               /* CR NUL is a bare CR */
                /* fall through */
            case 0:
                if (c == IAC) state = 1;
                else { show[k++] = c; if (c == '\r') state = 5; }
                break;
            case 1:
                if (c == IAC) { show[k++] = c; state = 0; }
                else if (c >= WILL) { verb = c; state = 2; }
                else if (c == SB) { sbn = 0; state = 3; }
                else state = 0;
                break;
            case 2:
                option(verb, c);
                state = 0;
                break;
            case 3:
                if (c == IAC) state = 4;
                else if (sbn < (long)sizeof(sb)) sb[sbn++] = c;
                break;
            case 4:
                if (c == SE) { suboption(sb, sbn); state = 0; }
                else { if (sbn < (long)sizeof(sb)) sb[sbn++] = c; state = 3; }
                break;
        }
    }
    if (k) Write(Output(), show, k);
}

/* What was typed: 1 when the quit key was pressed. */
static int from_keys(const UBYTE *p, long n)
{
    UBYTE out[512];
    long k = 0, i;
    for (i = 0; i < n && k < (long)sizeof(out) - 4; ++i) {
        UBYTE c = p[i];
        if (c == QUIT_KEY) return 1;
        if (c == '\r') {
            out[k++] = '\r'; out[k++] = '\n';
            if (!remote_echo) Write(Output(), "\n", 1);
        } else if (c == 0x9b) {                 /* the console's CSI: VT100's ESC [ */
            out[k++] = 0x1b; out[k++] = '[';
        } else if (c == 0x08 || c == 0x7f) {
            out[k++] = 0x7f;
            if (!remote_echo) Write(Output(), "\b \b", 3);
        } else if (c == IAC) {
            out[k++] = IAC; out[k++] = IAC;
        } else {
            out[k++] = c;
            if (!remote_echo) Write(Output(), &c, 1);
        }
    }
    ost_send_all(s, out, k);
    return 0;
}

static int telnet_main(int argc, char **argv)
{
    BPTR in = Input();
    BOOL raw;
    unsigned port = argc > 2 ? (unsigned)atol(argv[2]) : 23;
    int rc = 0;

    if (argc < 2 || argc > 3 || !port || argv[1][0] == '-' || argv[1][0] == '?') {
        fprintf(stderr, "Usage: telnet host [port]\n");
        return 20;
    }
    if (!IsInteractive(in)) { fprintf(stderr, "telnet: needs a console\n"); return 20; }
    if (!ost_open("telnet")) return 20;
    if ((s = ost_connect("telnet", argv[1], port, 20)) < 0) { ost_close(); return 10; }
    printf("Connected to %s. Ctrl-] closes the connection.\n", argv[1]);
    fflush(stdout);
    raw = SetMode(in, 1);

    for (;;) {
        struct timeval tv;
        fd_set rf;
        ULONG sigs = SIGBREAKF_CTRL_C;
        LONG n;
        FD_ZERO(&rf);
        FD_SET(s, &rf);
        tv.tv_sec = 0;
        tv.tv_usec = 50000;
        n = WaitSelect(s + 1, &rf, NULL, NULL, &tv, &sigs);
        if (n < 0) { rc = 10; break; }
        if (n == 0 && (sigs & SIGBREAKF_CTRL_C)) ost_send_all(s, "\003", 1);   /* Ctrl-C: theirs, not ours */
        if (n > 0) {
            n = recv(s, net, sizeof(net), 0);
            if (n <= 0) break;                      /* the other side closed */
            from_net(net, n);
        }
        while (WaitForChar(in, 0)) {
            n = Read(in, key, sizeof(key));
            if (n <= 0) break;
            if (from_keys(key, n)) goto closed;
        }
    }
closed:
    if (raw) SetMode(in, 0);
    printf("\nConnection to %s closed.\n", argv[1]);
    CloseSocket(s);
    ost_close();
    return rc;
}

int main(int argc, char **argv)
{
    return ost_main(telnet_main, argc, argv);
}
