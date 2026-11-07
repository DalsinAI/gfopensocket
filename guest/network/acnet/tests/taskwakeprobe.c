/*
 * taskwakeprobe: several tasks of one program waiting on one bsdsocket.library
 * base, as FileZilla's, curl's and Boost.Asio's socket threads do. The opener
 * makes loopback TCP pairs; child processes wait on them with the opener's
 * SocketBase. Each check needs every waiting task woken by its own data or
 * its own timeout. Before bsdsocket.library 4.5 only the last task to arm
 * was woken, and a child's timeout woke the opener. The timing part runs
 * round trips from the opener and from a child, for before/after numbers.
 *   taskwakeprobe [ROUNDS] [owner]   (default 500; owner: only the opener's
 *                                     timings, which an older library can run)
 *   taskwakeprobe [ROUNDS] bench     the wait path alone, no network time:
 *                                     WaitSelect on a socket already readable,
 *                                     and on an empty one with a zero timeout
 * A task found still asleep is a FAIL; the probe then waits for Ctrl-C
 * rather than end under it, so run it with Run and its output in a file.
 * MIT.
 */
#include <exec/types.h>
#include <exec/tasks.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/timer.h>
#include <dos/dostags.h>
#include <devices/timer.h>
#include <proto/bsdsocket.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Library *SocketBase;
struct Device *TimerBase;
static struct timerequest treq;
static ULONG eclock_hz;

#define CHILDREN 3
#define WAIT_SECS 10

/* what one child does, and what it found */
struct Job {
    LONG sock;
    LONG timeout_ms;        /* SO_RCVTIMEO; 0: none */
    LONG rounds;            /* >0: round trips with peer (echoed by the opener) */
    volatile LONG done;
    LONG got;               /* recv's answer */
    LONG ms;                /* how long the wait took */
    double us_per_round;
};
static struct Job jobs[CHILDREN];
static struct Task *parent;

static double now_us(void)
{
    struct EClockVal e;
    ReadEClock(&e);
    return ((double)e.ev_hi * 4294967296.0 + (double)e.ev_lo) * 1000000.0 / (double)eclock_hz;
}

static struct Job *my_job(void)
{
    struct Task *me = FindTask(NULL);
    return (struct Job *)me->tc_UserData;
}

static void child_main(void)
{
    struct Job *j = my_job();
    char b = 0;
    double t0 = now_us();
    if (j->rounds > 0) {
        LONG i;
        for (i = 0; i < j->rounds; i++) {
            if (send(j->sock, (APTR)"p", 1, 0) != 1) { j->got = -2; break; }
            if ((j->got = recv(j->sock, &b, 1, 0)) != 1) break;
        }
        j->us_per_round = (now_us() - t0) / (double)j->rounds;
    } else {
        j->got = recv(j->sock, &b, 1, 0);
    }
    j->ms = (LONG)((now_us() - t0) / 1000.0);
    Forbid();
    j->done = 1;
    Signal(parent, SIGBREAKF_CTRL_F);
}

static int spawn(struct Job *j)
{
    struct Process *p;
    j->done = 0; j->got = -99; j->ms = 0;
    Forbid();
    p = CreateNewProcTags(NP_Entry, (ULONG)child_main, NP_Name, (ULONG)"taskwakeprobe child",
                          NP_StackSize, 16384, NP_Output, 0, NP_Input, 0, NP_CloseOutput, FALSE,
                          NP_CloseInput, FALSE, TAG_DONE);
    if (p) p->pr_Task.tc_UserData = j;
    Permit();
    return p != NULL;
}

/* wait for n jobs, up to secs; 1 if all finished */
static int join(struct Job *js, int n, int secs)
{
    double end = now_us() + secs * 1000000.0;
    for (;;) {
        int i, all = 1;
        for (i = 0; i < n; i++) if (!js[i].done) all = 0;
        if (all) return 1;
        if (now_us() > end) return 0;
        SetSignal(0, SIGBREAKF_CTRL_F);
        Delay(1);
    }
}

static int pair(LONG lsock, struct sockaddr_in *at, LONG *c, LONG *a)
{
    LONG len = sizeof(*at);
    struct sockaddr_in peer;
    *c = socket(AF_INET, SOCK_STREAM, 0);
    if (*c < 0 || connect(*c, (struct sockaddr *)at, sizeof(*at)) < 0) return 0;
    *a = accept(lsock, (struct sockaddr *)&peer, &len);
    return *a >= 0;
}

static void set_timeout(LONG s, LONG ms)
{
    struct timeval tv;
    tv.tv_sec = ms / 1000; tv.tv_usec = (ms % 1000) * 1000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
}

static int passes, failures;
/* tenths, printed as N.N (no floats in libnix's printf) */
static void tenths(char *out, double v) { LONG t = (LONG)(v * 10.0 + 0.5); sprintf(out, "%ld.%ld", (long)(t / 10), (long)(t % 10)); }
static void asleep(void) { printf("a task is still asleep: Ctrl-C ends the probe\n"); fflush(stdout); Wait(SIGBREAKF_CTRL_C); }
static void result(const char *name, int ok, const char *detail)
{
    printf("%-44s %s  %s\n", name, ok ? "PASS" : "FAIL", detail);
    fflush(stdout);
    if (ok) passes++; else failures++;
}

/* owner round trips: c -> a, a -> c, each recv waiting through the library */
static double owner_rounds(LONG c, LONG a, LONG rounds)
{
    char b;
    LONG i;
    double t0 = now_us();
    for (i = 0; i < rounds; i++) {
        if (send(c, (APTR)"p", 1, 0) != 1 || recv(a, &b, 1, 0) != 1) return -1;
        if (send(a, (APTR)"q", 1, 0) != 1 || recv(c, &b, 1, 0) != 1) return -1;
    }
    return (now_us() - t0) / (double)rounds;
}

/* WaitSelect with a timeout on data that comes after a send (the timer is armed each time) */
static double owner_select_rounds(LONG c, LONG a, LONG rounds)
{
    char b;
    LONG i;
    double t0 = now_us();
    for (i = 0; i < rounds; i++) {
        fd_set r;
        struct timeval tv;
        if (send(c, (APTR)"p", 1, 0) != 1) return -1;
        FD_ZERO(&r); FD_SET(a, &r);
        tv.tv_sec = 2; tv.tv_usec = 0;
        if (WaitSelect(a + 1, &r, NULL, NULL, &tv, NULL) != 1) return -1;
        if (recv(a, &b, 1, 0) != 1) return -1;
    }
    return (now_us() - t0) / (double)rounds;
}

int main(int argc, char **argv)
{
    LONG rounds = argc > 1 ? atol(argv[1]) : 500, lsock, i;
    int owner_only = argc > 2 && !strcmp(argv[2], "owner");
    int bench = argc > 2 && !strcmp(argv[2], "bench");
    char t1[16];
    LONG c[CHILDREN + 1], a[CHILDREN + 1];
    struct sockaddr_in at;
    LONG len = sizeof(at);
    char line[96];
    if (rounds < 1) rounds = 1;
    parent = FindTask(NULL);
    if (OpenDevice((STRPTR)TIMERNAME, UNIT_ECLOCK, (struct IORequest *)&treq, 0)) { printf("no timer.device\n"); return 20; }
    TimerBase = treq.tr_node.io_Device;
    { struct EClockVal e; eclock_hz = ReadEClock(&e); }
    if (!(SocketBase = OpenLibrary((STRPTR)"bsdsocket.library", 4))) { printf("no bsdsocket.library\n"); return 20; }
    printf("%s", (char *)SocketBase->lib_IdString);
    lsock = socket(AF_INET, SOCK_STREAM, 0);
    memset(&at, 0, sizeof(at));
    at.sin_family = AF_INET;
    at.sin_addr.s_addr = htonl(0x7f000001);
    at.sin_port = 0;
    if (lsock < 0 || bind(lsock, (struct sockaddr *)&at, sizeof(at)) < 0 || listen(lsock, 8) < 0 ||
        getsockname(lsock, (struct sockaddr *)&at, &len) < 0) { printf("listen failed, errno %ld\n", (long)Errno()); return 20; }
    for (i = 0; i <= CHILDREN; i++)
        if (!pair(lsock, &at, &c[i], &a[i])) { printf("pair %ld failed, errno %ld\n", (long)i, (long)Errno()); return 20; }

    if (bench) {
        /* c[CHILDREN] holds one byte it never reads; c[0] stays empty */
        fd_set r;
        struct timeval tv;
        double t0, us;
        LONG k, bad = 0;
        send(a[CHILDREN], (APTR)"z", 1, 0);
        Delay(5);
        t0 = now_us();
        for (k = 0; k < rounds; k++) {
            FD_ZERO(&r); FD_SET(c[CHILDREN], &r);
            tv.tv_sec = 2; tv.tv_usec = 0;
            if (WaitSelect(c[CHILDREN] + 1, &r, NULL, NULL, &tv, NULL) != 1) bad++;
        }
        us = (now_us() - t0) / (double)rounds;
        tenths(t1, us);
        sprintf(line, "%ld calls, %s us per call", (long)rounds, t1);
        result("WaitSelect, readable at once", !bad, line);
        t0 = now_us();
        for (k = 0; k < rounds; k++) {
            FD_ZERO(&r); FD_SET(c[0], &r);
            tv.tv_sec = 0; tv.tv_usec = 0;
            if (WaitSelect(c[0] + 1, &r, NULL, NULL, &tv, NULL) != 0) bad++;
        }
        us = (now_us() - t0) / (double)rounds;
        tenths(t1, us);
        sprintf(line, "%ld calls, %s us per call", (long)rounds, t1);
        result("WaitSelect, empty, zero timeout", !bad, line);
        goto done;
    }
    if (owner_only) goto timings;

    /* 1: three children each blocked in recv; data for each, in turn */
    for (i = 0; i < CHILDREN; i++) {
        jobs[i].sock = c[i]; jobs[i].timeout_ms = WAIT_SECS * 1000; jobs[i].rounds = 0;
        set_timeout(c[i], WAIT_SECS * 1000);
        if (!spawn(&jobs[i])) { printf("no child\n"); return 20; }
    }
    Delay(25);
    for (i = 0; i < CHILDREN; i++) { send(a[i], (APTR)"x", 1, 0); Delay(5); }
    {
        int all = join(jobs, CHILDREN, WAIT_SECS + 3), ok = all;
        line[0] = 0;
        for (i = 0; i < CHILDREN; i++) {
            char one[24];
            if (!jobs[i].done || jobs[i].got != 1 || jobs[i].ms > 3000) ok = 0;
            sprintf(one, "%s%ld:%s %ldms", i ? ", " : "", (long)i, jobs[i].done ? (jobs[i].got == 1 ? "data" : "no data") : "asleep", (long)jobs[i].ms);
            strcat(line, one);
        }
        result("3 child tasks woken by their own data", ok, line);
        if (!all) { asleep(); return 10; }
    }

    /* 2: the opener and a child wait at once; data for the child only, then for the opener */
    {
        fd_set r;
        struct timeval tv;
        LONG n;
        double t0;
        jobs[0].sock = c[0]; jobs[0].rounds = 0;
        set_timeout(c[0], WAIT_SECS * 1000);
        spawn(&jobs[0]);
        Delay(10);
        send(a[0], (APTR)"y", 1, 0);           /* wakes the child, not the opener's select */
        FD_ZERO(&r); FD_SET(c[CHILDREN], &r);
        tv.tv_sec = 1; tv.tv_usec = 0;
        t0 = now_us();
        n = WaitSelect(c[CHILDREN] + 1, &r, NULL, NULL, &tv, NULL);
        join(jobs, 1, WAIT_SECS + 3);
        sprintf(line, "child %s %ldms, opener select %ld after %ldms", jobs[0].got == 1 ? "data" : "none", (long)jobs[0].ms, (long)n, (long)((now_us() - t0) / 1000.0));
        result("opener and child wait together", jobs[0].done && jobs[0].got == 1 && n == 0, line);
        if (!jobs[0].done) { asleep(); return 10; }
    }

    /* 3: a child's own timeout ends its wait (no data comes) */
    jobs[1].sock = c[1]; jobs[1].rounds = 0;
    set_timeout(c[1], 1000);
    spawn(&jobs[1]);
    {
        int ok = join(&jobs[1], 1, WAIT_SECS);
        sprintf(line, "%s after %ldms", ok ? "timed out" : "still asleep", (long)jobs[1].ms);
        result("child times out by its own timer (1 s)", ok && jobs[1].got < 0 && jobs[1].ms >= 900 && jobs[1].ms < 3000, line);
        if (!ok) { asleep(); return 10; }
    }
    set_timeout(c[1], 0);

    /* timings */
timings:
    {
        double us = owner_rounds(c[CHILDREN], a[CHILDREN], rounds);
        tenths(t1, us);
        sprintf(line, "%ld rounds, %s us per round trip", (long)rounds, t1);
        result("opener recv round trips", us > 0, line);
        us = owner_select_rounds(c[CHILDREN], a[CHILDREN], rounds);
        tenths(t1, us);
        sprintf(line, "%ld rounds, %s us per round", (long)rounds, t1);
        result("opener WaitSelect(2 s) rounds", us > 0, line);
    }
    if (!owner_only) {
        /* a child pings on c[2]; the opener echoes on a[2] */
        char b;
        LONG k, ok = 1;
        jobs[2].sock = c[2]; jobs[2].rounds = rounds;
        set_timeout(c[2], 0);
        spawn(&jobs[2]);
        for (k = 0; k < rounds; k++)
            if (recv(a[2], &b, 1, 0) != 1 || send(a[2], (APTR)"r", 1, 0) != 1) { ok = 0; break; }
        join(&jobs[2], 1, WAIT_SECS);
        tenths(t1, jobs[2].us_per_round);
        sprintf(line, "%ld rounds, %s us per round trip", (long)rounds, t1);
        result("child recv round trips (opener echoes)", ok && jobs[2].done && jobs[2].got == 1, line);
    }

done:
    for (i = 0; i <= CHILDREN; i++) { CloseSocket(c[i]); CloseSocket(a[i]); }
    CloseSocket(lsock);
    printf("taskwakeprobe: %d passed, %d failed\n", passes, failures);
    CloseLibrary(SocketBase);
    CloseDevice((struct IORequest *)&treq);
    return failures ? 10 : 0;
}
