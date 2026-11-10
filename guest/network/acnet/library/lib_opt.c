/* ACNet bsdsocket.library: options and ioctl. MIT. */
#include <exec/types.h>
#include <proto/exec.h>
#include <sys/socket.h>
#include <sys/filio.h>
#include <sys/sockio.h>
#include "lib_internal.h"

LONG bsd_setsockopt(struct SocketBase *sb,LONG s,LONG level,LONG opt,APTR val,socklen_t len)
{
 struct FD*f=fd_get(sb,s); if(!f)return -1;
 if(level==A_SOL_SOCKET&&opt==A_SO_RCVTIMEO&&len>=sizeof(struct timeval)){f->rcvtimeo=*(struct timeval*)val;return 0;}
 if(level==A_SOL_SOCKET&&opt==A_SO_SNDTIMEO&&len>=sizeof(struct timeval)){f->sndtimeo=*(struct timeval*)val;return 0;}
 LONG r=prov_call(sb,ACHS_CMD_SETSOCKOPT,f->handle,level,opt,0,val,len,NULL,0,NULL); return r<0?fail_provider(sb):r;
}
LONG bsd_getsockopt(struct SocketBase *sb,LONG s,LONG level,LONG opt,APTR val,socklen_t *len)
{
 struct FD*f=fd_get(sb,s); ULONG n=0; if(!f||!len)return fail(sb,AE_FAULT);
 if(level==A_SOL_SOCKET&&opt==A_SO_RCVTIMEO){if(*len<sizeof(struct timeval))return fail(sb,AE_INVAL);*(struct timeval*)val=f->rcvtimeo;*len=sizeof(struct timeval);return 0;}
 if(level==A_SOL_SOCKET&&opt==A_SO_SNDTIMEO){if(*len<sizeof(struct timeval))return fail(sb,AE_INVAL);*(struct timeval*)val=f->sndtimeo;*len=sizeof(struct timeval);return 0;}
 LONG r=prov_call(sb,ACHS_CMD_GETSOCKOPT,f->handle,level,opt,*len,NULL,0,val,*len,&n); if(r<0)return fail_provider(sb);*len=(socklen_t)n;return 0;
}
static LONG getname(struct SocketBase*sb,LONG s,ULONG cmd,struct sockaddr*name,socklen_t*len){struct FD*f=fd_get(sb,s);UBYTE rx[16];ULONG n=0;if(!f||!len)return fail(sb,AE_FAULT);LONG r=prov_call(sb,cmd,f->handle,0,0,0,NULL,0,rx,sizeof(rx),&n);if(r<0)return fail_provider(sb);ULONG m=*len<n?*len:n;if(name)CopyMem(rx,name,m);*len=n;return 0;}
LONG bsd_getsockname(struct SocketBase*sb,LONG s,struct sockaddr*n,socklen_t*l){return getname(sb,s,ACHS_CMD_GETSOCKNAME,n,l);}
LONG bsd_getpeername(struct SocketBase*sb,LONG s,struct sockaddr*n,socklen_t*l){return getname(sb,s,ACHS_CMD_GETPEERNAME,n,l);}
/* IoctlSocket(): the requests POSIX ports make (Boost.Asio's FIONBIO, and
 * FIONREAD, FIOASYNC, SIOCATMARK). A descriptor that isn't open is EBADF
 * and a NULL argument EFAULT; before 4.6 both were EFAULT, so a bad
 * descriptor looked like a bad pointer (Team 2, aMule, 10 Nov 2026).
 * FIOASYNC: no SIGIO here (GetSocketEvents has none), so off is taken and
 * on is refused rather than silently ignored. SIOCATMARK: the stack keeps
 * no urgent data apart, so never at the mark. Anything else EOPNOTSUPP. */
LONG bsd_IoctlSocket(struct SocketBase*sb,LONG s,ULONG req,APTR argp)
{
 struct FD*f=fd_get(sb,s);
 if(!f)return -1;
 if(!argp)return fail(sb,AE_FAULT);
 switch(req){
 case FIONBIO: f->nonblock=(*(LONG*)argp)!=0; return 0;
 case FIONREAD:{LONG r=prov_call(sb,ACHS_CMD_PENDING,f->handle,0,0,0,NULL,0,NULL,0,NULL);if(r<0)return fail_provider(sb);*(LONG*)argp=r;return 0;}
 case FIOASYNC: if(*(LONG*)argp)return fail(sb,AE_OPNOTSUPP); f->async=0; return 0;
 case SIOCATMARK: *(LONG*)argp=0; return 0;
 }
 return fail(sb,AE_OPNOTSUPP);
}
