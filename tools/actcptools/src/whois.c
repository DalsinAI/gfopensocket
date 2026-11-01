/* whois: who holds a domain or an address block.
 *   whois aminet.net
 *   whois -h whois.example.net query
 * Without -h it asks IANA, then the server IANA's answer refers to.
 * GFOpenSocket ACTCPTools. MIT. */
#include <exec/types.h>
#include <proto/exec.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <proto/bsdsocket.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "ostool.h"

static const char version[] __attribute__((used)) = "$VER: whois 1.0 (4.10.2026) GFOpenSocket";

/* Asks server; prints the answer; the referral server, if any, into refer. */
static int ask(const char *server, const char *query, char *refer, int refer_max)
{
    struct ost_lines lines;
    static char line[512];
    LONG s = ost_connect("whois", server, 43, 15);
    long n;
    if (refer) refer[0] = 0;
    if (s < 0) return 0;
    if (ost_send_str(s, query) || ost_send_str(s, "\r\n")) {
        fprintf(stderr, "whois: could not send to %s\n", server);
        CloseSocket(s);
        return 0;
    }
    ost_lines_init(&lines, s);
    while ((n = ost_line(&lines, line, sizeof(line), 30)) >= 0) {
        printf("%s\n", line);
        if (refer && !refer[0]) {
            const char *p = line;
            while (*p == ' ') p++;
            if (!strncasecmp(p, "refer:", 6) || !strncasecmp(p, "whois:", 6)) {
                p += 6;
                while (*p == ' ' || *p == '\t') p++;
                strncpy(refer, p, refer_max - 1);
                refer[refer_max - 1] = 0;
            }
        }
    }
    CloseSocket(s);
    if (n == -1 && ost_break()) fprintf(stderr, "whois: stopped\n");
    return 1;
}

static int whois_main(int argc, char **argv)
{
    const char *server = NULL, *query = NULL;
    char refer[128];
    int i, ok;

    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-h") && i + 1 < argc) server = argv[++i];
        else if (!query) query = argv[i];
        else query = NULL, i = argc + 1;
    }
    if (!query || i > argc) {
        fprintf(stderr, "Usage: whois [-h server] domain|address\n");
        return 20;
    }
    if (!ost_open("whois")) return 20;
    if (server) {
        ok = ask(server, query, NULL, 0);
    } else {
        ok = ask("whois.iana.org", query, refer, sizeof(refer));
        if (ok && refer[0] && strcasecmp(refer, "whois.iana.org")) {
            printf("\n-- %s --\n\n", refer);
            ok = ask(refer, query, NULL, 0);
        }
    }
    ost_close();
    return ok ? 0 : 10;
}

int main(int argc, char **argv)
{
    return ost_main(whois_main, argc, argv);
}
