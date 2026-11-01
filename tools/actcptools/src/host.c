/* host: look a name up, or an address back up to its name.
 *   host aminet.net
 *   host 1.2.3.4
 * GFOpenSocket ACTCPTools. MIT. */
#include <exec/types.h>
#include <proto/exec.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <proto/bsdsocket.h>
#include <stdio.h>
#include <string.h>

#include "ostool.h"

static const char version[] __attribute__((used)) = "$VER: host 1.0 (4.10.2026) GFOpenSocket";

static int host_main(int argc, char **argv)
{
    struct hostent *he;
    LONG a;
    char **p;
    int rc = 0;

    if (argc != 2 || argv[1][0] == '-' || argv[1][0] == '?') {
        fprintf(stderr, "Usage: host name|address\n");
        return 20;
    }
    if (!ost_open("host")) return 20;
    a = inet_addr((STRPTR)argv[1]);
    if (a != -1) {
        he = gethostbyaddr((STRPTR)&a, 4, AF_INET);
        if (he && he->h_name) printf("%s is %s\n", argv[1], (char *)he->h_name);
        else { printf("%s has no name\n", argv[1]); rc = 5; }
    } else {
        he = gethostbyname((STRPTR)argv[1]);
        if (!he || he->h_addrtype != AF_INET || he->h_length != 4) {
            fprintf(stderr, "host: %s was not found\n", argv[1]);
            rc = 10;
        } else {
            if (he->h_name && strcmp((char *)he->h_name, argv[1]))
                printf("%s is another name for %s\n", argv[1], (char *)he->h_name);
            for (p = (char **)he->h_aliases; p && *p; ++p)
                if (strcmp(*p, argv[1])) printf("%s is also called %s\n", (char *)he->h_name, *p);
            for (p = (char **)he->h_addr_list; p && *p; ++p) {
                LONG addr;
                memcpy(&addr, *p, 4);
                printf("%s has address %s\n", he->h_name ? (char *)he->h_name : argv[1], (char *)Inet_NtoA(addr));
            }
        }
    }
    ost_close();
    return rc;
}

int main(int argc, char **argv)
{
    return ost_main(host_main, argc, argv);
}
