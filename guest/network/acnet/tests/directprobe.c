/* directprobe: GFOpenSocketDirect end to end, through bsdsocket.library:
 * the interface's address (DHCP), a DNS lookup, a TCP connect and an HTTP
 * request. Prints PASS or FAIL for each.
 *   directprobe [host [port [path]]]     (default: example.com 80 /)
 * MIT. */
#include <exec/types.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/bsdsocket.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

struct Library *SocketBase;
static int fails;

static void result(const char *what, int ok) { printf("%-30s %s\n", what, ok ? "PASS" : "FAIL"); if (!ok) fails++; }

int main(int argc, char **argv)
{
    const char *host = argc > 1 ? argv[1] : "example.com";
    int port = argc > 2 ? atoi(argv[2]) : 80;
    const char *path = argc > 3 ? argv[3] : "/";
    ULONG id = 0;
    int i;
    struct hostent *he;
    struct sockaddr_in sa;
    LONG s, n, total = 0;
    char buf[512];

    setvbuf(stdout, NULL, _IONBF, 0);
    if (!(SocketBase = OpenLibrary("bsdsocket.library", 4))) { printf("no bsdsocket.library\n"); return 20; }
    for (i = 0; i < 60; i++) {                      /* up to 15 s for DHCP */
        id = gethostid();
        if (id && (id >> 24) != 127) break;
        Delay(12);
    }
    printf("address %u.%u.%u.%u after %d.%02d s\n", id >> 24, (id >> 16) & 255, (id >> 8) & 255, id & 255,
           i / 4, (i % 4) * 25);
    result("address (DHCP)", id && (id >> 24) != 127);

    he = gethostbyname((STRPTR)host);
    if (he) {
        ULONG a = *(ULONG *)he->h_addr_list[0];
        printf("%s is %u.%u.%u.%u\n", host, a >> 24, (a >> 16) & 255, (a >> 8) & 255, a & 255);
    }
    result("DNS lookup", he != NULL);
    if (!he) goto out;

    memset(&sa, 0, sizeof(sa));
    sa.sin_len = sizeof(sa);
    sa.sin_family = AF_INET;
    sa.sin_port = port;
    memcpy(&sa.sin_addr, he->h_addr_list[0], 4);
    s = socket(AF_INET, SOCK_STREAM, 0);
    result("TCP connect", s >= 0 && connect(s, (struct sockaddr *)&sa, sizeof(sa)) == 0);
    if (s < 0) goto out;
    n = sprintf(buf, "GET %s HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n\r\n", path, host);
    result("TCP send", send(s, buf, n, 0) == n);
    while ((n = recv(s, buf, sizeof(buf) - 1, 0)) > 0) {
        if (!total) { buf[n] = 0; { char *e = strchr(buf, '\r'); if (e) *e = 0; } printf("reply: %s\n", buf); }
        total += n;
    }
    printf("received %ld bytes\n", total);
    result("TCP receive", total > 0 && n == 0);
    CloseSocket(s);
out:
    printf("RESULT: %s (%d failed)\n", fails ? "FAIL" : "PASS", fails);
    CloseLibrary(SocketBase);
    return fails ? 5 : 0;
}
