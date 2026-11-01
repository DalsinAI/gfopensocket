/* ACNet modern compatibility probe for AmigaOS 3.2.3. MIT. */
#include <exec/types.h>
#include <exec/libraries.h>
#include <proto/exec.h>
#include <libraries/bsdsocket.h>
#include <proto/bsdsocket.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>

struct Library *SocketBase = NULL;
static int failures;

static void check(int ok, const char *name)
{
    printf("%s: %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) failures++;
}

int main(void)
{
    struct in_addr addr, parsed;
    char text[32];
    char rbuf[768];
    struct hostent hcopy, *hp;
    LONG he = 0;

    SocketBase = OpenLibrary("bsdsocket.library", 4);
    if (!SocketBase) { printf("OPEN: FAIL\n"); return 20; }
    printf("GFOpenSocket modern compatibility probe\n");
    memset(&addr, 0, sizeof(addr));
    check(inet_aton("192.0.2.1", &addr) == 1, "inet_aton");
    memset(text, 0, sizeof(text));
    check(inet_ntop(AF_INET, &addr, text, sizeof(text)) != NULL &&
          strcmp(text, "192.0.2.1") == 0, "inet_ntop");
    memset(&parsed, 0, sizeof(parsed));
    check(inet_pton(AF_INET, "198.51.100.9", &parsed) == 1,
          "inet_pton");

    memset(&hcopy, 0, sizeof(hcopy));
    memset(rbuf, 0, sizeof(rbuf));
    hp = gethostbyname_r("localhost", &hcopy, rbuf, sizeof(rbuf), &he);
    check(hp != NULL && he == 0 && hp->h_addrtype == AF_INET,
          "gethostbyname_r");

    if (hp && hp->h_addr_list && hp->h_addr_list[0]) {
        struct hostent reverse;
        char reverse_buf[768];
        LONG reverse_he = 0;
        memset(&reverse, 0, sizeof(reverse));
        memset(reverse_buf, 0, sizeof(reverse_buf));
        check(gethostbyaddr_r(hp->h_addr_list[0], 4, AF_INET, &reverse,
                              reverse_buf, sizeof(reverse_buf), &reverse_he) != NULL &&
              reverse_he == 0, "gethostbyaddr_r");
    } else check(0, "gethostbyaddr_r");
    {
        struct addrinfo hints, *ai = NULL;
        char host[NI_MAXHOST], serv[NI_MAXSERV];
        LONG rc;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;
        hints.ai_flags = AI_CANONNAME;

        rc = getaddrinfo("localhost", "80", &hints, &ai);
        check(rc == 0 && ai != NULL && ai->ai_family == AF_INET &&
              ai->ai_socktype == SOCK_STREAM, "getaddrinfo");

        if (ai) {
            memset(host, 0, sizeof(host));
            memset(serv, 0, sizeof(serv));
            rc = getnameinfo(ai->ai_addr, ai->ai_addrlen, host, sizeof(host),
                             serv, sizeof(serv), NI_NUMERICHOST | NI_NUMERICSERV);
            check(rc == 0 && host[0] != 0 && strcmp(serv, "80") == 0,
                  "getnameinfo");
            printf("numeric endpoint: %s:%s\n", host, serv);
            freeaddrinfo(ai);
        } else check(0, "getnameinfo");
    }

    check(gai_strerror(EAI_NONAME) != NULL &&
          gai_strerror(EAI_NONAME)[0] != 0, "gai_strerror");

    CloseLibrary(SocketBase);
    SocketBase = NULL;
    printf("RESULT: %s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 5 : 0;
}
