/* ACTCPTools acnetctl - native ACNet control/status. MIT. */
#include <exec/types.h>
#include <exec/libraries.h>
#include <proto/exec.h>
#include <stdio.h>
#include <string.h>
#include <acnetwork.h>
#include <proto/acnetwork.h>

struct Library *ACNetworkBase = NULL;

static int show_status(void)
{
    struct ACNetworkStatus st;
    struct ACNetworkRequest r;
    memset(&r, 0, sizeof(r));
    memset(&st, 0, sizeof(st));
    r.command = ACNETWORK_CMD_STATUS;
    r.rx = (UBYTE *)&st;
    r.rxmax = sizeof(st);
    if (ACNetwork_Call(&r) < 0) {
        printf("GFOpenSocket: error %u\n", (unsigned)r.error);
        return 10;
    }
    printf("GFOpenSocket %s, link %u Mb/s, sockets %u, in %llu, out %llu\n",
           (st.state & ACNETWORK_STATE_ONLINE) ? "online" : "offline",
           (unsigned)st.link_mbps, (unsigned)st.sockets,
           (unsigned long long)st.bytes_in, (unsigned long long)st.bytes_out);
    return 0;
}

int main(int argc, char **argv)
{
    struct ACNetworkRequest r;
    int rc;
    ACNetworkBase = OpenLibrary(ACNETWORK_LIBRARY_NAME, ACNETWORK_LIBRARY_VERSION);
    if (!ACNetworkBase) { printf("GFOpenSocket: gfopensocket.library is not available\n"); return 20; }

    if (argc > 1 && (!strcmp(argv[1], "online") || !strcmp(argv[1], "offline"))) {
        memset(&r, 0, sizeof(r));
        r.command = ACNETWORK_CMD_SET_ONLINE;
        r.arg[0] = !strcmp(argv[1], "online") ? 1 : 0;
        if (ACNetwork_Call(&r) < 0) {
            printf("GFOpenSocket: error %u\n", (unsigned)r.error);
            CloseLibrary(ACNetworkBase);
            return 10;
        }
    } else if (argc > 1 && strcmp(argv[1], "status")) {
        printf("Usage: GFOpenSocket [status|online|offline]\n");
        CloseLibrary(ACNetworkBase);
        return 5;
    }

    rc = show_status();
    CloseLibrary(ACNetworkBase);
    return rc;
}
