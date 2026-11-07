/* ACNet bsdsocket.library: DNS, hostname and service lookups. MIT. */
#include <exec/types.h>
#include <proto/exec.h>
#include <netdb.h>
#include <netinet/in.h>
#include <acnetwork.h>
#include "lib_internal.h"

static ULONG slen(const char*s,ULONG max){ULONG n=0;if(!s)return 0;while(n<max&&s[n])n++;return n;}
static void scopy(char*d,const char*s,ULONG max){ULONG n=slen(s,max?max-1:0);if(max){if(n)CopyMem((APTR)s,d,n);d[n]=0;}}
void names_close(struct SocketBase *sb){(void)sb;}

static struct hostent *answer(struct SocketBase *sb,struct Wake *wk,LONG ticket,UBYTE *x);
static struct hostent *resolve(struct SocketBase *sb,ULONG cmd,const void*tx,ULONG txlen)
{
 UBYTE *x=scratch(sb);struct Wake wk;struct hostent *h;LONG ticket=prov_call(sb,cmd,0,0,0,0,tx,txlen,NULL,0,NULL);if(ticket<0){set_herrno(sb,(LONG)sb->last_err);return NULL;}
 if(wake_begin(sb,&wk)<0){set_herrno(sb,AH_TRY_AGAIN);return NULL;}h=answer(sb,&wk,ticket,x);wake_end(sb,&wk);return h;
}
static struct hostent *answer(struct SocketBase *sb,struct Wake *wk,LONG ticket,UBYTE *x)
{
 ULONG n=0;
 /* armed before each ANSWER: the lookup can finish (and its event come) between an
    "in progress" answer and the wait; an armed waiter gets the signal then */
 for(;;){prov_arm(sb,wk);LONG r=prov_call(sb,ACHS_CMD_ANSWER,ticket,0,0,0,NULL,0,x,SCRATCH_SIZE,&n);if(r>=0||sb->last_err!=AE_INPROGRESS)prov_disarm(sb,wk);if(r>=0){ULONG i=0,j=0;while(i<n&&x[i])i++;if(i>=n){set_herrno(sb,AH_NO_RECOVERY);return NULL;}scopy(sb->hname,(char*)x,sizeof(sb->hname));i++;while(i+4<=n&&j<NAME_MAX_ADDRS){CopyMem(x+i,&sb->haddr[j],4);sb->haddrs[j]=(char*)&sb->haddr[j];j++;i+=4;}sb->haddrs[j]=NULL;sb->hnull[0]=NULL;sb->hent.h_name=sb->hname;sb->hent.h_aliases=(STRPTR*)sb->hnull;sb->hent.h_addrtype=AF_INET;sb->hent.h_length=4;sb->hent.h_addr_list=sb->haddrs;set_herrno(sb,0);return &sb->hent;}
   if(sb->last_err!=AE_INPROGRESS){set_herrno(sb,(LONG)sb->last_err);return NULL;}if(wait_event(sb,wk)<0){set_herrno(sb,AH_TRY_AGAIN);return NULL;}}
}
struct hostent *bsd_gethostbyname(struct SocketBase *sb,STRPTR name){if(!name){set_herrno(sb,AH_HOST_NOT_FOUND);return NULL;}return resolve(sb,ACHS_CMD_RESOLVE,name,slen(name,255)+1);}
struct hostent *bsd_gethostbyaddr(struct SocketBase *sb,STRPTR addr,LONG len,LONG type){if(!addr||len!=4||type!=AF_INET){set_herrno(sb,AH_NO_RECOVERY);return NULL;}return resolve(sb,ACHS_CMD_RESOLVE_ADDR,addr,4);}

static struct servent *service(struct SocketBase *sb,STRPTR name,LONG port,STRPTR proto)
{
 UBYTE tx[128],rx[64];ULONG n=0,p=0,k=0;if(name){while(name[k]&&p<62)tx[p++]=name[k++];}tx[p++]=0;k=0;if(proto){while(proto[k]&&p<126)tx[p++]=proto[k++];}tx[p++]=0;
 LONG r=prov_call(sb,ACHS_CMD_SERVICE,(ULONG)port,sizeof(rx),0,0,tx,p,rx,sizeof(rx),&n);if(r<0)return NULL;scopy(sb->sname,(char*)rx,sizeof(sb->sname));scopy(sb->sproto,proto?(char*)proto:"",sizeof(sb->sproto));sb->sent.s_name=sb->sname;sb->sent.s_aliases=(STRPTR*)sb->hnull;sb->sent.s_port=(int)r;sb->sent.s_proto=sb->sproto;return &sb->sent;
}

static struct netent *network_lookup(struct SocketBase *sb, ULONG cmd, STRPTR name, in_addr_t net)
{
 UBYTE rx[96]; ULONG n=0,i=0; LONG r; const void *tx=NULL; ULONG txlen=0;
 if(name){tx=name;txlen=slen((char*)name,63)+1;}
 r=prov_call(sb,cmd,(ULONG)net,0,0,0,tx,txlen,rx,sizeof(rx),&n);
 if(r<0){set_herrno(sb,(LONG)sb->last_err);return NULL;}
 while(i<n&&rx[i])i++;
 if(i>=n||i+5>n){set_herrno(sb,AH_NO_RECOVERY);return NULL;}
 scopy(sb->nname,(char*)rx,sizeof(sb->nname));
 sb->naliases[0]=NULL;
 sb->nent.n_name=sb->nname;
 sb->nent.n_aliases=(STRPTR*)sb->naliases;
 sb->nent.n_addrtype=AF_INET;
 CopyMem(rx+i+1,&sb->nent.n_net,4);
 set_herrno(sb,0);
 return &sb->nent;
}
struct netent *bsd_getnetbyname(struct SocketBase *sb,STRPTR name)
{
 if(!name){set_herrno(sb,AH_HOST_NOT_FOUND);return NULL;}
 return network_lookup(sb,ACHS_CMD_NET_BY_NAME,name,0);
}
struct netent *bsd_getnetbyaddr(struct SocketBase *sb,in_addr_t net,LONG type)
{
 if(type!=AF_INET){set_herrno(sb,AH_NO_RECOVERY);return NULL;}
 return network_lookup(sb,ACHS_CMD_NET_BY_ADDR,NULL,net);
}

struct servent *bsd_getservbyname(struct SocketBase *sb,STRPTR name,STRPTR proto){return service(sb,name,0,proto);}
struct servent *bsd_getservbyport(struct SocketBase *sb,LONG port,STRPTR proto){return service(sb,NULL,port,proto);}

struct protoent *bsd_getprotobyname(struct SocketBase *sb,STRPTR name){if(!name)return NULL;LONG p=0;if((name[0]=='i'||name[0]=='I')&&(name[1]=='c'||name[1]=='C')&&(name[2]=='m'||name[2]=='M')&&(name[3]=='p'||name[3]=='P')&&!name[4])p=1;else if((name[0]=='t'||name[0]=='T')&&(name[1]=='c'||name[1]=='C')&&(name[2]=='p'||name[2]=='P')&&!name[3])p=6;else if((name[0]=='u'||name[0]=='U')&&(name[1]=='d'||name[1]=='D')&&(name[2]=='p'||name[2]=='P')&&!name[3])p=17;else return NULL;sb->pent.p_name=p==1?(char*)"icmp":p==6?(char*)"tcp":(char*)"udp";sb->pent.p_aliases=(STRPTR*)sb->hnull;sb->pent.p_proto=p;return &sb->pent;}
struct protoent *bsd_getprotobynumber(struct SocketBase *sb,LONG p){return p==1?bsd_getprotobyname(sb,(STRPTR)"icmp"):p==6?bsd_getprotobyname(sb,(STRPTR)"tcp"):p==17?bsd_getprotobyname(sb,(STRPTR)"udp"):NULL;}
LONG bsd_gethostname(struct SocketBase *sb,STRPTR name,LONG namelen){UBYTE rx[64];ULONG n=0;if(!name||namelen<=0)return fail(sb,AE_FAULT);LONG r=prov_call(sb,ACHS_CMD_HOSTNAME,0,0,0,0,NULL,0,rx,sizeof(rx),&n);if(r<0)return fail_provider(sb);scopy(name,(char*)rx,namelen);return 0;}

in_addr_t bsd_gethostid(struct SocketBase *sb){struct ACNetworkInterface rows[16];ULONG n=0,id=0;LONG r=prov_call(sb,ACNETWORK_CMD_INTERFACES,16,0,0,0,NULL,0,rows,sizeof(rows),&n);LONG i,count;if(r<0){fail_provider(sb);return 0;}count=r;if((ULONG)count>n/sizeof(rows[0]))count=n/sizeof(rows[0]);for(i=0;i<count;i++){if(rows[i].ipv4[0]&&rows[i].ipv4[0]!=127){CopyMem(rows[i].ipv4,&id,4);set_errno(sb,0);return id;}}if(count>0)CopyMem(rows[0].ipv4,&id,4);set_errno(sb,0);return id;}
