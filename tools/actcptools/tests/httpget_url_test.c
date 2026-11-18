/* Host test of httpget's addresses (src/httpget_url.h):
 *   cc -I tools/actcptools/src -o /tmp/hu tools/actcptools/tests/httpget_url_test.c && /tmp/hu
 * GFOpenSocket ACTCPTools. MIT. */
#include "httpget_url.h"

static int fails, checks;
static void check(const char *what, int ok)
{
    checks++;
    if (!ok) { fails++; printf("FAIL %s\n", what); }
    else printf("ok   %s\n", what);
}

int main(void)
{
    static struct url u, n;
    static char loc[2000], big[HG_URL_MAX + 100];
    size_t i;

    check("https URL: host, port 443, path", parse_url("https://github.com/DalsinAI/gfopenup/releases/download/updates/GFOpenUpdate.index", &u)
          && u.tls && u.port == 443 && !strcmp(u.host, "github.com")
          && !strcmp(u.path, "/DalsinAI/gfopenup/releases/download/updates/GFOpenUpdate.index"));
    check("http URL with a port and no path", parse_url("http://example.org:8080", &u) && !u.tls && u.port == 8080 && !strcmp(u.path, "/"));
    check("a fragment is dropped", parse_url("http://a.b/c#d", &u) && !strcmp(u.path, "/c"));
    check("not http(s): refused", !parse_url("ftp://a.b/c", &u));

    /* GitHub's release-asset redirect, as 18 Nov 2026 saw it: about 1,100 characters */
    strcpy(loc, "https://release-assets.githubusercontent.com/github-production-release-asset/1404405782/"
                "944a0726-24a6-4895-b611-4be1b569ef6c?sp=r&sv=2018-11-09&sr=b&spr=https&se=2026-11-18T04%3A57%3A05Z"
                "&rscd=attachment%3B+filename%3DGFOpenUpdate.index&rsct=application%2Foctet-stream&jwt=");
    for (i = strlen(loc); i < 1150; i++) loc[i] = 'x';
    loc[i] = 0;
    parse_url("https://github.com/DalsinAI/gfopenup/releases/download/updates/GFOpenUpdate.index", &u);
    check("a 1,150-character redirect to another host is followed whole",
          follow(&u, loc, &n) && n.tls && !strcmp(n.host, "release-assets.githubusercontent.com")
          && strlen(n.path) == strlen(loc) - strlen("https://release-assets.githubusercontent.com"));
    check("an absolute path redirect keeps the host", follow(&u, "/other/file", &n) && !strcmp(n.host, "github.com") && !strcmp(n.path, "/other/file"));
    check("a relative redirect is beside the old path", follow(&u, "next.lha", &n)
          && !strcmp(n.path, "/DalsinAI/gfopenup/releases/download/updates/next.lha"));

    memset(big, 'y', sizeof(big) - 1);
    big[sizeof(big) - 1] = 0;
    memcpy(big, "https://h/", 10);
    check("an address longer than HG_URL_MAX is refused, not cut", !parse_url(big, &n));
    check("a relative path longer than HG_URL_MAX is refused", !follow(&u, big + 10, &n));

    printf("%d checks, %d failed\n", checks, fails);
    return fails != 0;
}
