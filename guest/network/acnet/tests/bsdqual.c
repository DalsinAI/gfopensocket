/*
 * ACNet bsdsocket.library qualification harness.
 * Runs inside AmigaOS 3.2.3 against the published bsdsocket ABI.
 * MIT.
 */
#include <exec/types.h>
#include <exec/libraries.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <dos/dostags.h>
#include <proto/bsdsocket.h>
#include <libraries/bsdsocket.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/filio.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <utility/tagitem.h>
#include "acnet_stack.h"

struct Library *SocketBase;

static int passes, failures;

static void result(const char *name, int ok)
{
    printf("%-36s %s\n", name, ok ? "PASS" : "FAIL");
    fflush(stdout);
    if (ok) passes++; else failures++;
}

static void closes(LONG s)
{
    if (s >= 0) CloseSocket(s);
}

static void test_basics(void)
{
    char host[64];
    struct hostent *he;
    struct protoent *pe;
    struct in_addr a;

    result("getdtablesize >= 32", getdtablesize() >= 32);
    result("gethostname", gethostname(host, sizeof(host)) == 0 && host[0] != 0);

    he = gethostbyname("localhost");
    result("gethostbyname localhost", he && he->h_addrtype == AF_INET && he->h_length == 4 && he->h_addr_list && he->h_addr_list[0]);

    if (he && he->h_addr_list && he->h_addr_list[0]) {
        struct hostent *rev = gethostbyaddr(he->h_addr_list[0], 4, AF_INET);
        result("gethostbyaddr localhost", rev != NULL);
    } else result("gethostbyaddr localhost", 0);

    pe = getprotobyname("icmp");
    result("getprotobyname icmp", pe && pe->p_proto == 1);
    pe = getprotobynumber(1);
    result("getprotobynumber icmp", pe && pe->p_proto == 1);
    pe = getprotobyname("tcp");
    result("getprotobyname tcp", pe && pe->p_proto == 6);
    pe = getprotobynumber(17);
    result("getprotobynumber udp", pe && pe->p_proto == 17);

    a.s_addr = inet_addr("127.0.0.1");
    result("inet_addr 127.0.0.1", a.s_addr != (in_addr_t)0xffffffffUL);
    result("Inet_NtoA roundtrip", strcmp(Inet_NtoA(a.s_addr), "127.0.0.1") == 0);
}

static void test_tcp(void)
{
    LONG ls=-1, cs=-1, as=-1, ds=-1;
    struct sockaddr_in bindaddr, peer, local;
    socklen_t alen;
    char buf[16];
    LONG n, avail=0, nb=1;
    fd_set rf;
    struct timeval tv;
    ULONG sigs=0;
    struct timeval setto, getto;
    socklen_t optlen;

    memset(&bindaddr, 0, sizeof(bindaddr));
    bindaddr.sin_len = sizeof(bindaddr);
    bindaddr.sin_family = AF_INET;
    bindaddr.sin_port = htons(23451);
    bindaddr.sin_addr.s_addr = inet_addr("127.0.0.1");

    ls = socket(AF_INET, SOCK_STREAM, 0);
    result("TCP socket listener", ls >= 0);
    if (ls < 0) goto out;

    result("TCP bind", bind(ls, (struct sockaddr *)&bindaddr, sizeof(bindaddr)) == 0);
    result("TCP listen", listen(ls, 2) == 0);

    cs = socket(AF_INET, SOCK_STREAM, 0);
    result("TCP socket client", cs >= 0);
    if (cs < 0) goto out;

    setto.tv_secs=0; setto.tv_micro=250000;
    result("setsockopt SO_RCVTIMEO", setsockopt(cs, SOL_SOCKET, SO_RCVTIMEO, &setto, sizeof(setto)) == 0);
    memset(&getto, 0, sizeof(getto)); optlen=sizeof(getto);
    result("getsockopt SO_RCVTIMEO", getsockopt(cs, SOL_SOCKET, SO_RCVTIMEO, &getto, &optlen) == 0 &&
           getto.tv_secs == setto.tv_secs && getto.tv_micro == setto.tv_micro);

    result("TCP connect", connect(cs, (struct sockaddr *)&bindaddr, sizeof(bindaddr)) == 0);

    FD_ZERO(&rf); FD_SET(ls, &rf);
    tv.tv_secs=0; tv.tv_micro=0; sigs=0;
    result("WaitSelect listener ready", WaitSelect(ls+1, &rf, NULL, NULL, &tv, &sigs) == 1 && FD_ISSET(ls, &rf));

    as = accept(ls, NULL, NULL);
    result("TCP accept", as >= 0);
    if (as < 0) goto out;

    alen=sizeof(local); memset(&local,0,sizeof(local));
    result("getsockname client", getsockname(cs,(struct sockaddr*)&local,&alen)==0 && local.sin_family==AF_INET);
    alen=sizeof(peer); memset(&peer,0,sizeof(peer));
    result("getpeername client", getpeername(cs,(struct sockaddr*)&peer,&alen)==0 && peer.sin_family==AF_INET);

    result("TCP send ping", send(cs, "ping", 4, 0) == 4);

    avail=0;
    result("FIONREAD sees payload", IoctlSocket(as, FIONREAD, &avail) == 0 && avail >= 4);

    FD_ZERO(&rf); FD_SET(as, &rf);
    tv.tv_secs=0; tv.tv_micro=0; sigs=0;
    result("WaitSelect data ready", WaitSelect(as+1, &rf, NULL, NULL, &tv, &sigs) == 1 && FD_ISSET(as, &rf));

    memset(buf,0,sizeof(buf));
    n=recv(as,buf,4,0);
    result("TCP recv ping", n==4 && memcmp(buf,"ping",4)==0);

    FD_ZERO(&rf); FD_SET(as,&rf);
    tv.tv_secs=0; tv.tv_micro=0; sigs=0;
    result("WaitSelect empty poll", WaitSelect(as+1,&rf,NULL,NULL,&tv,&sigs)==0);

    /* A timed wait cut short (here by a signal) must not leave the next timed
     * wait to see its timer as already fired: in 4.1 every later
     * WaitSelect with a timeout returned 0 at once. */
    {
        BYTE sb = AllocSignal(-1);
        struct DateStamp t0, t1;
        LONG ticks, r;
        if (sb >= 0) {
            ULONG m = 1UL << sb;
            SetSignal(m, m);
            FD_ZERO(&rf); FD_SET(as,&rf);
            tv.tv_secs=2; tv.tv_micro=0; sigs=m;
            r = WaitSelect(as+1,&rf,NULL,NULL,&tv,&sigs);
            result("WaitSelect ends on signal", r==0 && sigs==m);
            FD_ZERO(&rf); FD_SET(as,&rf);
            tv.tv_secs=0; tv.tv_micro=300000; sigs=0;
            DateStamp(&t0);
            r = WaitSelect(as+1,&rf,NULL,NULL,&tv,&sigs);
            DateStamp(&t1);
            ticks = (t1.ds_Days-t0.ds_Days)*24*60*60*TICKS_PER_SECOND
                  + (t1.ds_Minute-t0.ds_Minute)*60*TICKS_PER_SECOND
                  + (t1.ds_Tick-t0.ds_Tick);
            result("WaitSelect timeout after cut wait", r==0 && ticks >= TICKS_PER_SECOND/5);
            FreeSignal(sb);
        } else result("WaitSelect ends on signal", 0);
    }

    /* A wait that ends early cancels its timer; the next wait must still
     * last as long as it asks (4 Oct 2026: a stale timer signal made every
     * later WaitSelect return at once). Our own signal ends the first wait
     * after its timer has started. */
    {
        BYTE own = AllocSignal(-1);
        ULONG m = own >= 0 ? 1UL << own : 0;
        struct DateStamp t0, t1;
        LONG ticks, w;
        if (m) {
            SetSignal(m, m);
            FD_ZERO(&rf); FD_SET(as,&rf); tv.tv_secs=2; tv.tv_micro=0; sigs=m;
            w = WaitSelect(as+1,&rf,NULL,NULL,&tv,&sigs);
            result("WaitSelect ends on a signal", w==0 && (sigs&m));
            FD_ZERO(&rf); FD_SET(as,&rf); tv.tv_secs=1; tv.tv_micro=0; sigs=0;
            DateStamp(&t0);
            w = WaitSelect(as+1,&rf,NULL,NULL,&tv,&sigs);
            DateStamp(&t1);
            ticks = ((t1.ds_Days - t0.ds_Days) * 1440 + (t1.ds_Minute - t0.ds_Minute)) * 3000 + (t1.ds_Tick - t0.ds_Tick);
            result("WaitSelect keeps its timeout after an early wait", w==0 && ticks >= 35);
            FreeSignal(own);
        } else result("WaitSelect keeps its timeout after an early wait", 0);
    }

    /* A signal in the break mask ends a blocking call with EINTR and stays set for the program to see, as
     * Roadshow and AmiTCP leave it (12 Oct 2026: 4.2 took it, so ping's CheckSignal() never saw its timer
     * or Ctrl-C, and ping sent one packet and never ended). */
    {
        BYTE own = AllocSignal(-1);
        ULONG m = own >= 0 ? 1UL << own : 0;
        if (m) {
            LONG r, e;
            SocketBaseTags(SBTM_SETVAL(SBTC_BREAKMASK), SIGBREAKF_CTRL_C | m, TAG_END);
            SetSignal(m, m);
            r = recv(as, buf, 4, 0);
            e = Errno();
            result("break signal ends recv with EINTR", r < 0 && e == EINTR);
            result("break signal stays set", (SetSignal(0, m) & m) != 0);
            SocketBaseTags(SBTM_SETVAL(SBTC_BREAKMASK), SIGBREAKF_CTRL_C, TAG_END);
            FreeSignal(own);
        } else result("break signal ends recv with EINTR", 0);
    }

    result("FIONBIO enable", IoctlSocket(cs, FIONBIO, &nb) == 0);
    nb=0;
    result("FIONBIO disable", IoctlSocket(cs, FIONBIO, &nb) == 0);

    ds = Dup2Socket(cs, 20);
    result("Dup2Socket", ds == 20);
    if (ds >= 0) {
        result("duplicate descriptor send", send(ds, "dupe", 4, 0) == 4);
        memset(buf,0,sizeof(buf));
        result("duplicate descriptor recv", recv(as,buf,4,0)==4 && memcmp(buf,"dupe",4)==0);
    }

out:
    closes(ds); closes(as); closes(cs); closes(ls);
}

static void test_udp(void)
{
    LONG rs=-1, ss=-1;
    struct sockaddr_in dst, from;
    socklen_t flen;
    char buf[16];
    LONG avail=0;
    fd_set rf;
    struct timeval tv;
    ULONG sigs=0;

    memset(&dst,0,sizeof(dst));
    dst.sin_len=sizeof(dst);
    dst.sin_family=AF_INET;
    dst.sin_port=htons(23452);
    dst.sin_addr.s_addr = inet_addr("127.0.0.1");

    rs=socket(AF_INET,SOCK_DGRAM,0);
    ss=socket(AF_INET,SOCK_DGRAM,0);
    result("UDP sockets", rs>=0 && ss>=0);
    if(rs<0||ss<0) goto out;

    result("UDP bind", bind(rs,(struct sockaddr*)&dst,sizeof(dst))==0);
    result("UDP sendto", sendto(ss,"udp!",4,0,(struct sockaddr*)&dst,sizeof(dst))==4);

    FD_ZERO(&rf); FD_SET(rs,&rf);
    tv.tv_secs=0; tv.tv_micro=250000; sigs=0;
    result("UDP WaitSelect ready", WaitSelect(rs+1,&rf,NULL,NULL,&tv,&sigs)==1 && FD_ISSET(rs,&rf));

    result("UDP FIONREAD", IoctlSocket(rs,FIONREAD,&avail)==0 && avail>=4);

    memset(&from,0,sizeof(from)); flen=sizeof(from); memset(buf,0,sizeof(buf));
    result("UDP recvfrom", recvfrom(rs,buf,4,0,(struct sockaddr*)&from,&flen)==4 && memcmp(buf,"udp!",4)==0 && from.sin_family==AF_INET);

    /* A datagram larger than the card's transmit buffer is refused with
     * EMSGSIZE (1 Nov 2026: sendto copied it into the library's 16 KB
     * scratch buffer unchecked, and ran over the memory after it). */
    {
        static char big[20000];
        LONG r = sendto(ss,big,sizeof(big),0,(struct sockaddr*)&dst,sizeof(dst));
        LONG e = Errno();
        result("UDP sendto 20000 bytes EMSGSIZE", r<0 && e==EMSGSIZE);
        result("UDP sendto after EMSGSIZE", sendto(ss,"udp!",4,0,(struct sockaddr*)&dst,sizeof(dst))==4);
        memset(buf,0,sizeof(buf)); flen=sizeof(from);
        result("UDP recvfrom after EMSGSIZE", recvfrom(rs,buf,4,0,(struct sockaddr*)&from,&flen)==4 && memcmp(buf,"udp!",4)==0);
    }

    {
        struct iovec siov[2], riov[2];
        struct msghdr smsg, rmsg;
        char a[3] = {'m','s','g'}, b[2] = {'!','!'};
        char ra[2] = {0,0}, rb[4] = {0,0,0,0};

        memset(&smsg,0,sizeof(smsg));
        siov[0].iov_base=a; siov[0].iov_len=3;
        siov[1].iov_base=b; siov[1].iov_len=2;
        smsg.msg_name=&dst; smsg.msg_namelen=sizeof(dst);
        smsg.msg_iov=siov; smsg.msg_iovlen=2;
        result("UDP sendmsg two iov", sendmsg(ss,&smsg,0)==5);

        FD_ZERO(&rf); FD_SET(rs,&rf);
        tv.tv_secs=0; tv.tv_micro=250000; sigs=0;
        result("sendmsg WaitSelect ready", WaitSelect(rs+1,&rf,NULL,NULL,&tv,&sigs)==1 && FD_ISSET(rs,&rf));

        memset(&from,0,sizeof(from));
        memset(&rmsg,0,sizeof(rmsg));
        riov[0].iov_base=ra; riov[0].iov_len=2;
        riov[1].iov_base=rb; riov[1].iov_len=3;
        rmsg.msg_name=&from; rmsg.msg_namelen=sizeof(from);
        rmsg.msg_iov=riov; rmsg.msg_iovlen=2;
        result("UDP recvmsg two iov", recvmsg(rs,&rmsg,0)==5 &&
               ra[0]=='m' && ra[1]=='s' && rb[0]=='g' && rb[1]=='!' && rb[2]=='!' &&
               from.sin_family==AF_INET && rmsg.msg_namelen==16);
    }

    /* MSG_DONTWAIT on a blocking socket returns at once with EWOULDBLOCK
     * (1 Nov 2026: recv and recvfrom waited, here for the receive timeout,
     * as only sendmsg and recvmsg looked at the flag). */
    {
        struct timeval to;
        LONG r, e;
        to.tv_secs=0; to.tv_micro=500000;
        setsockopt(rs, SOL_SOCKET, SO_RCVTIMEO, &to, sizeof(to));
        r = recv(rs, buf, 4, MSG_DONTWAIT);
        e = Errno();
        result("recv MSG_DONTWAIT EWOULDBLOCK", r<0 && e==EWOULDBLOCK);
        flen=sizeof(from);
        r = recvfrom(rs, buf, 4, MSG_DONTWAIT, (struct sockaddr*)&from, &flen);
        e = Errno();
        result("recvfrom MSG_DONTWAIT EWOULDBLOCK", r<0 && e==EWOULDBLOCK);
    }

out:
    closes(ss); closes(rs);
}

/* Another task's traffic: its sends make card events, and the card's event
 * wakes every waiting task on the machine. It arms the card for its socket
 * (a poll that finds nothing), sends itself a datagram, and reads it. */
static struct Task *traffic_parent;
static ULONG traffic_done;
static volatile LONG traffic_stop;

static void traffic_child(void)
{
    struct Library *SocketBase = OpenLibrary("bsdsocket.library", 4);
    if (SocketBase) {
        LONG b = socket(AF_INET,SOCK_DGRAM,0), c = socket(AF_INET,SOCK_DGRAM,0), nb = 1, i;
        struct sockaddr_in a;
        fd_set rf;
        struct timeval z;
        char buf[8];
        memset(&a,0,sizeof(a));
        a.sin_len=sizeof(a); a.sin_family=AF_INET; a.sin_port=htons(23453);
        a.sin_addr.s_addr=inet_addr("127.0.0.1");
        if (b>=0 && c>=0 && bind(b,(struct sockaddr*)&a,sizeof(a))==0 && IoctlSocket(b,FIONBIO,&nb)==0) {
            for (i=0; i<60 && !traffic_stop; i++) {
                FD_ZERO(&rf); FD_SET(b,&rf); z.tv_secs=0; z.tv_micro=0;
                WaitSelect(b+1,&rf,NULL,NULL,&z,NULL);
                sendto(c,"tick",4,0,(struct sockaddr*)&a,sizeof(a));
                Delay(5);
                recv(b,buf,sizeof(buf),0);
            }
        }
        if (c>=0) CloseSocket(c);
        if (b>=0) CloseSocket(b);
        CloseLibrary(SocketBase);
    }
    Forbid();
    Signal(traffic_parent, traffic_done);
}

static LONG ticks_since(const struct DateStamp *t0)
{
    struct DateStamp t1;
    DateStamp(&t1);
    return ((t1.ds_Days-t0->ds_Days)*1440 + (t1.ds_Minute-t0->ds_Minute))*TICKS_PER_SECOND*60 + (t1.ds_Tick-t0->ds_Tick);
}

/* A timeout is a deadline for the whole call, however many card events
 * wake the wait (1 Nov 2026: each event started the timeout again, so with
 * other traffic these waits never ended, and WaitSelect on no sockets ended
 * at the first event). Each wait asks for 1 s while the traffic runs. */
static void test_deadlines(void)
{
    BYTE done = AllocSignal(-1);
    LONG s = -1, r, t;
    struct sockaddr_in a;
    struct DateStamp t0;
    struct timeval tv;
    fd_set rf;
    char buf[4];

    if (done < 0) { result("traffic task", 0); return; }
    traffic_parent = FindTask(NULL); traffic_done = 1UL << done; traffic_stop = 0;
    if (!CreateNewProcTags(NP_Entry,(ULONG)traffic_child, NP_Name,(ULONG)"bsdqual traffic",
                           NP_StackSize,16384, TAG_END)) {
        result("traffic task", 0); FreeSignal(done); return;
    }
    Delay(10);

    tv.tv_secs=1; tv.tv_micro=0;
    DateStamp(&t0);
    r = WaitSelect(0,NULL,NULL,NULL,&tv,NULL);
    t = ticks_since(&t0);
    result("WaitSelect no sockets lasts 1 s", r==0 && t>=45 && t<100);

    memset(&a,0,sizeof(a));
    a.sin_len=sizeof(a); a.sin_family=AF_INET; a.sin_port=htons(23454);
    a.sin_addr.s_addr=inet_addr("127.0.0.1");
    s = socket(AF_INET,SOCK_DGRAM,0);
    if (s>=0 && bind(s,(struct sockaddr*)&a,sizeof(a))==0) {
        FD_ZERO(&rf); FD_SET(s,&rf); tv.tv_secs=1; tv.tv_micro=0;
        DateStamp(&t0);
        r = WaitSelect(s+1,&rf,NULL,NULL,&tv,NULL);
        t = ticks_since(&t0);
        result("WaitSelect timeout under traffic", r==0 && t>=45 && t<100);

        tv.tv_secs=1; tv.tv_micro=0;
        setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv));
        DateStamp(&t0);
        r = recv(s,buf,sizeof(buf),0);
        t = ticks_since(&t0);
        result("SO_RCVTIMEO under traffic", r<0 && t>=45 && t<100);
    } else result("WaitSelect timeout under traffic", 0);

    traffic_stop = 1;
    Wait(traffic_done);
    FreeSignal(done);
    closes(s);
}

static void test_descriptor_handoff(void)
{
    LONG s=-1, id=-1, o=-1;
    s=socket(AF_INET,SOCK_STREAM,0);
    result("handoff source socket", s>=0);
    if(s<0) return;

    id=ReleaseCopyOfSocket(s, UNIQUE_ID);
    result("ReleaseCopyOfSocket", id>0);
    if(id>0) {
        o=ObtainSocket(id,AF_INET,SOCK_STREAM,0);
        result("ObtainSocket copy", o>=0);
    } else result("ObtainSocket copy",0);

    result("original survives copy release", send(s,"",0,0) >= 0 || Errno() != EBADF);
    closes(o); closes(s);
}

static void test_errors(void)
{
    LONG external_errno=0;
    LONG r;
    SetErrnoPtr(&external_errno, sizeof(external_errno));
    r=CloseSocket(127);
    result("bad descriptor rejected", r<0);
    result("bad descriptor errno EBADF", Errno()==EBADF);
    result("SetErrnoPtr propagation", external_errno==EBADF);
    SetErrnoPtr(NULL, sizeof(external_errno));
}

static void test_churn(void)
{
    LONG s;
    int i, ok=1;
    for(i=0;i<128;i++){
        s=socket(AF_INET,SOCK_STREAM,0);
        if(s<0){ok=0;break;}
        if(CloseSocket(s)<0){ok=0;break;}
    }
    result("128 socket open/close cycles", ok);
}

static int bsdqual_main(int argc, char **argv)
{
    printf("OpenSocket bsdsocket.library qualification\n");
    printf("====================================\n");
    fflush(stdout);

    SocketBase=OpenLibrary("bsdsocket.library",4);
    if(!SocketBase) {
        printf("OpenLibrary bsdsocket.library v4 FAIL\n");
        return 20;
    }
    result("OpenLibrary version >= 4", SocketBase->lib_Version >= 4);

    test_basics();
    test_tcp();
    test_udp();
    test_deadlines();
    test_descriptor_handoff();
    test_errors();
    test_churn();

    printf("------------------------------------\n");
    printf("PASS=%d FAIL=%d\n",passes,failures);
    CloseLibrary(SocketBase);
    return failures ? 20 : 0;
}

/* 32 KB: WaitSelect runs about 1.3 KB deep in the library, on this task's
 * stack; a 4 KB Shell stack should not decide a qualification run. */
int main(int argc, char **argv)
{
    return acnet_main_with_stack(bsdqual_main, argc, argv, 32768);
}
