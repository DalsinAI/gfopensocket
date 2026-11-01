/* finger: what a finger server says about a user, or about its users.
 *   finger user@host
 *   finger @host
 * GFOpenSocket ACTCPTools. MIT. */
#include <exec/types.h>
#include <proto/exec.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <proto/bsdsocket.h>
#include <stdio.h>
#include <string.h>

#include "ostool.h"

static const char version[] __attribute__((used)) = "$VER: finger 1.0 (4.10.2026) GFOpenSocket";

static int finger_main(int argc, char **argv)
{
    static char user[128], line[512];
    struct ost_lines lines;
    const char *at;
    LONG s;
    long n;

    if (argc != 2 || !(at = strrchr(argv[1], '@')) || !at[1]) {
        fprintf(stderr, "Usage: finger [user]@host\n");
        return 20;
    }
    if ((size_t)(at - argv[1]) >= sizeof(user)) {
        fprintf(stderr, "finger: the user name is too long\n");
        return 20;
    }
    memcpy(user, argv[1], at - argv[1]);
    user[at - argv[1]] = 0;
    if (!ost_open("finger")) return 20;
    s = ost_connect("finger", at + 1, 79, 15);
    if (s < 0) { ost_close(); return 10; }
    if (ost_send_str(s, user) || ost_send_str(s, "\r\n")) {
        fprintf(stderr, "finger: could not send\n");
    } else {
        ost_lines_init(&lines, s);
        while ((n = ost_line(&lines, line, sizeof(line), 30)) >= 0) printf("%s\n", line);
    }
    CloseSocket(s);
    ost_close();
    return 0;
}

int main(int argc, char **argv)
{
    return ost_main(finger_main, argc, argv);
}
