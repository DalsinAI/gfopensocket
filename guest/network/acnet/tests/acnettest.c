#include <exec/types.h>
#include <exec/libraries.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/bsdsocket.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

struct Library *SocketBase;
static int fail(const char *s){PutStr((STRPTR)"GFOpenSocket: FAIL ");PutStr((STRPTR)s);PutStr((STRPTR)"\n");return 20;}

int main(void)
{
    char host[64], buf[16];
    LONG ls=-1, cs=-1, as=-1;
    struct sockaddr_in a;
    struct hostent *he;
    SocketBase=OpenLibrary("bsdsocket.library",4);
    if(!SocketBase)return fail("OpenLibrary");
    PutStr((STRPTR)"GFOpenSocket: library open\n");
    if(gethostname((STRPTR)host,sizeof(host)))return fail("gethostname");
    PutStr((STRPTR)"GFOpenSocket: hostname=");PutStr((STRPTR)host);PutStr((STRPTR)"\n");
    he=gethostbyname((STRPTR)"localhost");
    if(!he||he->h_addrtype!=AF_INET||he->h_length!=4)return fail("gethostbyname localhost");
    PutStr((STRPTR)"GFOpenSocket: DNS localhost OK\n");
    ls=socket(AF_INET,SOCK_STREAM,0);if(ls<0)return fail("listen socket");
    a.sin_len=sizeof(a);a.sin_family=AF_INET;a.sin_port=12345;a.sin_addr.s_addr=0x7f000001UL;
    if(bind(ls,(struct sockaddr*)&a,sizeof(a))<0)return fail("bind");
    if(listen(ls,2)<0)return fail("listen");
    cs=socket(AF_INET,SOCK_STREAM,0);if(cs<0)return fail("client socket");
    if(connect(cs,(struct sockaddr*)&a,sizeof(a))<0)return fail("connect");
    as=accept(ls,NULL,NULL);if(as<0)return fail("accept");
    if(send(cs,(APTR)"ping",4,0)!=4)return fail("send");
    if(recv(as,(APTR)buf,4,0)!=4)return fail("recv");
    if(buf[0]!='p'||buf[1]!='i'||buf[2]!='n'||buf[3]!='g')return fail("payload");
    PutStr((STRPTR)"GFOpenSocket: TCP loopback ping OK\n");
    if(as>=0)CloseSocket(as);
    if(cs>=0)CloseSocket(cs);
    if(ls>=0)CloseSocket(ls);
    CloseLibrary(SocketBase);
    PutStr((STRPTR)"GFOpenSocket: PASS\n");
    return 0;
}
