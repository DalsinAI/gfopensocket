/* ACNet bsdsocket.library: provider readiness, timeouts and WaitSelect. MIT. */
#include <exec/types.h>
#include <proto/exec.h>
#include <devices/timer.h>
#include <sys/socket.h>
#include "lib_internal.h"

static LONG timer_open(struct SocketBase *sb,struct STimer *t){if(t->open)return 0;t->port=CreateMsgPort();if(!t->port)return fail(sb,AE_NOMEM);t->req=(struct timerequest*)CreateIORequest(t->port,sizeof(*t->req));if(!t->req){DeleteMsgPort(t->port);t->port=NULL;return fail(sb,AE_NOMEM);}if(OpenDevice(TIMERNAME,UNIT_MICROHZ,(struct IORequest*)t->req,0)){DeleteIORequest((struct IORequest*)t->req);DeleteMsgPort(t->port);t->req=NULL;t->port=NULL;return fail(sb,AE_NOMEM);}t->open=1;return 0;}
static void timer_shut(struct STimer *t){if(!t->open)return;if(!CheckIO((struct IORequest*)t->req)){AbortIO((struct IORequest*)t->req);WaitIO((struct IORequest*)t->req);SetSignal(0,1UL<<t->port->mp_SigBit);}CloseDevice((struct IORequest*)t->req);DeleteIORequest((struct IORequest*)t->req);DeleteMsgPort(t->port);t->req=NULL;t->port=NULL;t->open=0;}
void timer_close(struct SocketBase *sb){timer_shut(&sb->tm);}
/* The timer port's signal bit is not a reliable "timer fired" flag: AbortIO()
 * replies the request at once, which signals the port, and WaitIO() then finds
 * it already replied and leaves the bit set. Clear it before every request and
 * after every cancel, and judge expiry by io_Error, not by the signal. */
static ULONG timer_start(struct SocketBase *sb,struct STimer *t,const struct timeval *tv){ULONG m;if(!tv)return 0;if(timer_open(sb,t)<0)return 0;m=1UL<<t->port->mp_SigBit;SetSignal(0,m);t->req->tr_node.io_Command=TR_ADDREQUEST;t->req->tr_time=*tv;SendIO((struct IORequest*)t->req);return m;}
/* 1 if the request ran its full time, 0 if it was aborted. */
static LONG timer_cancel(struct STimer *t){if(!t->open)return 0;if(!CheckIO((struct IORequest*)t->req))AbortIO((struct IORequest*)t->req);WaitIO((struct IORequest*)t->req);SetSignal(0,1UL<<t->port->mp_SigBit);return t->req->tr_node.io_Error==0;}

/* The system time, read through the wait's own timer request (a ROM
 * library keeps no TimerBase). */
static LONG timer_now(struct SocketBase *sb,struct STimer *t,struct timeval *tv){if(timer_open(sb,t)<0)return -1;t->req->tr_node.io_Command=TR_GETSYSTIME;DoIO((struct IORequest*)t->req);*tv=t->req->tr_time;return 0;}
/* Take the time from t0 to t1 off what is left of a timeout, down to 0. A
 * clock set back while the wait ran counts as no time. */
static void timer_left(struct timeval *left,const struct timeval *t0,const struct timeval *t1){LONG s=(LONG)(t1->tv_secs-t0->tv_secs),u=(LONG)t1->tv_micro-(LONG)t0->tv_micro;if(u<0){u+=1000000;s--;}if(s<0)return;if((ULONG)s>left->tv_secs||((ULONG)s==left->tv_secs&&(ULONG)u>=left->tv_micro)){left->tv_secs=0;left->tv_micro=0;return;}left->tv_secs-=s;if(left->tv_micro<(ULONG)u){left->tv_micro+=1000000;left->tv_secs--;}left->tv_micro-=u;}

/* A wait's wake-up, for the task that calls. The opener's own task waits
 * with the base's signal and timer, as before. Another task of its program
 * calling with the same base (FileZilla's, curl's or Boost.Asio's socket
 * threads) gets a timer of its own and, where the provider can, a signal of
 * its own, for this wait only (4.5, 7 Nov 2026): before, all tasks armed
 * the base's one waiter and shared its timer, so only the last task to arm
 * was woken, the others slept on, and their timeouts woke the opener. */
LONG wake_begin(struct SocketBase *sb,struct Wake *wk)
{
 wk->task=FindTask(NULL);wk->mask=sb->provider_sigmask;wk->tm=&sb->tm;wk->sig=-1;
 if(wk->task==sb->owner)return 0;
 wk->own.port=NULL;wk->own.req=NULL;wk->own.open=0;wk->tm=&wk->own;
 if(!prov_own_waiter(sb))return 0;
 if((wk->sig=AllocSignal(-1))<0)return fail(sb,AE_NOMEM);
 wk->mask=1UL<<wk->sig;prov_add(sb,wk);return 0;
}
void wake_end(struct SocketBase *sb,struct Wake *wk)
{
 if(wk->tm==&wk->own)timer_shut(&wk->own);
 if(wk->sig>=0){prov_rem(sb,wk);SetSignal(0,wk->mask);FreeSignal(wk->sig);wk->sig=-1;}
}

/* The card's event wakes every waiting task on the machine, whichever socket
 * it was for, so one wait may poll many times. The timeout is a deadline for
 * the whole call: each round waits only for what is left of it (1 Nov 2026:
 * each round started the full timeout again, so while other programs had
 * traffic, WaitSelect, SO_RCVTIMEO and SO_SNDTIMEO never ran out, and a
 * WaitSelect on no sockets ended, as if timed out, at the first event). */
static LONG wait_poll(struct SocketBase *sb,struct Wake *wk,ULONG *items,ULONG *revents,LONG n,const struct timeval *tv,ULONG extra,ULONG *got_extra){ULONG rxlen=0,tmask,sigs;LONG ready,expired;struct timeval left,t0,t1;struct STimer *t=wk->tm;if(tv){left.tv_secs=tv->tv_secs+tv->tv_micro/1000000;left.tv_micro=tv->tv_micro%1000000;}for(;;){prov_arm(sb,wk);ready=prov_call(sb,ACHS_CMD_POLL,n,0,0,0,items,n*8,revents,n*4,&rxlen);if(ready<0){prov_disarm(sb,wk);return fail_provider(sb);}if(ready>0){prov_disarm(sb,wk);return ready;}if(tv&&left.tv_secs==0&&left.tv_micro==0){prov_disarm(sb,wk);return 0;}if(tv&&timer_now(sb,t,&t0)<0){prov_disarm(sb,wk);return -1;}tmask=timer_start(sb,t,tv?&left:NULL);if(tv&&!tmask){prov_disarm(sb,wk);return -1;}sigs=Wait(wk->mask|tmask|extra|sb->sigintr);prov_disarm(sb,wk);expired=tmask?timer_cancel(t):0;if(sigs&sb->sigintr){SetSignal(sigs&sb->sigintr,sigs&sb->sigintr);return fail(sb,AE_INTR);}if(sigs&extra){if(got_extra)*got_extra=sigs&extra;return 0;}if(expired)return 0;if(tv){if(timer_now(sb,t,&t1)<0)return -1;timer_left(&left,&t0,&t1);}/* a card event: poll again, for what is left of the timeout */}}
LONG wait_items(struct SocketBase *sb,ULONG *items,ULONG *revents,LONG n,const struct timeval *tv,ULONG extra,ULONG *got_extra){struct Wake wk;LONG r;if(got_extra)*got_extra=0;if(wake_begin(sb,&wk)<0)return -1;r=wait_poll(sb,&wk,items,revents,n,tv,extra,got_extra);wake_end(sb,&wk);return r;}
LONG wait_ready(struct SocketBase *sb,LONG handle,ULONG events,const struct timeval *tv){ULONG items[2]={handle,events},rev=0;LONG r=wait_items(sb,items,&rev,1,(tv&&(tv->tv_secs||tv->tv_micro))?tv:NULL,0,NULL);if(r<=0)return r;return (LONG)rev;}
/* the next provider event, for a caller that asks again after it (a name lookup's answer) */
LONG wait_event(struct SocketBase *sb,struct Wake *wk){prov_arm(sb,wk);return wait_armed_event(sb,wk,NULL,0,NULL)<0?-1:0;}

/* A break signal (SBTC_BREAKMASK, Ctrl-C unless the program sets it) ends a wait with EINTR and is set again
 * after Wait() took it, as Roadshow and AmiTCP leave it: the program checks it after EINTR (ping's
 * CheckSignal() of its timer and of Ctrl-C). 4.3: before, the signal was eaten, so ping sent one packet and
 * then waited for ever, deaf to Ctrl-C too. */
/* Wait after the caller has already armed the provider, preserving the classic
 * arm -> poll -> sleep pattern without losing an event between poll and Wait().
 * 1=provider event, 0=timeout, 2=extra signal, -1=interrupt/error. */
LONG wait_armed_event(struct SocketBase *sb,struct Wake *wk,const struct timeval *tv,ULONG extra,ULONG *got_extra)
{
 ULONG tmask=0,sigs;LONG expired;if(got_extra)*got_extra=0;
 if(tv){tmask=timer_start(sb,wk->tm,tv);if(!tmask){prov_disarm(sb,wk);return -1;}}
 sigs=Wait(wk->mask|tmask|extra|sb->sigintr);prov_disarm(sb,wk);
 expired=tmask?timer_cancel(wk->tm):0;
 if(sigs&sb->sigintr){SetSignal(sigs&sb->sigintr,sigs&sb->sigintr);return fail(sb,AE_INTR);}
 if(sigs&extra){if(got_extra)*got_extra=sigs&extra;return 2;}
 if(expired)return 0;
 return (sigs&wk->mask)?1:0;
}

LONG bsd_WaitSelect(struct SocketBase *sb,LONG nfds,APTR rf,APTR wf,APTR ef,struct timeval *tv,ULONG *signals)
{
 fd_set *r=(fd_set*)rf,*w=(fd_set*)wf,*e=(fd_set*)ef;ULONG items[FD_SETSIZE*2],rev[FD_SETSIZE],extra=signals?*signals:0,got=0;LONG map[FD_SETSIZE];UBYTE want[FD_SETSIZE];LONG n=0,fd,rc,ready=0;
 if(nfds<0)return fail(sb,AE_INVAL);
 if(nfds>FD_SETSIZE)nfds=FD_SETSIZE;
 for(fd=0;fd<nfds;fd++){ULONG ev=0;if(r&&FD_ISSET(fd,r))ev|=ACHS_POLLIN;if(w&&FD_ISSET(fd,w))ev|=ACHS_POLLOUT;if(e&&FD_ISSET(fd,e))ev|=ACHS_POLLPRI;if(!ev)continue;struct FD*f=fd_get(sb,fd);if(!f)return -1;map[n]=fd;want[n]=(UBYTE)ev;items[n*2]=f->handle;items[n*2+1]=ev;rev[n]=0;n++;}
 if(r)FD_ZERO(r);
 if(w)FD_ZERO(w);
 if(e)FD_ZERO(e);
 rc=wait_items(sb,items,rev,n,tv,extra,&got);if(signals)*signals=got;if(rc<=0)return rc;
 for(fd=0;fd<n;fd++){int any=0;if(r&&(want[fd]&ACHS_POLLIN)&&(rev[fd]&(ACHS_POLLIN|ACHS_POLLHUP|ACHS_POLLERR))){FD_SET(map[fd],r);any=1;}if(w&&(want[fd]&ACHS_POLLOUT)&&(rev[fd]&(ACHS_POLLOUT|ACHS_POLLERR))){FD_SET(map[fd],w);any=1;}if(e&&(want[fd]&ACHS_POLLPRI)&&(rev[fd]&(ACHS_POLLPRI|ACHS_POLLERR))){FD_SET(map[fd],e);any=1;}if(any)ready++;}
 return ready;
}
