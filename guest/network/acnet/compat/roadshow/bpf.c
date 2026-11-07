/* ACNet Roadshow-compatible Berkeley Packet Filter facade.
 * BPF channels are per bsdsocket.library opener; capture/filtering lives in
 * HostSocket's unprivileged virtual Ethernet provider. MIT. */
#include <exec/types.h>
#include <exec/memory.h>
#include <proto/exec.h>
#include <sys/socket.h>
#include <sys/filio.h>
#include <sys/sockio.h>
#include <netinet/in.h>
#include <net/if.h>
#include <net/bpf.h>
#include <acnetwork.h>
#include "../../library/lib_internal.h"

#define BPF_DEFAULT_BUFSIZE 4096
#define BPF_MAX_TRANSPORT   ACHS_RX_SIZE

struct BPFStatusWire { ULONG queued, received, dropped; };
struct CompatTimeval { LONG secs, micro; };

static struct BPFLocal *bpf_local(struct SocketBase *sb, LONG handle)
{
    LONG i;
    for (i = 0; i < ACNET_BPF_CHANNELS; ++i)
        if (sb->bpf[i].handle == handle) return &sb->bpf[i];
    return NULL;
}

static struct BPFLocal *bpf_free_local(struct SocketBase *sb)
{
    LONG i;
    for (i = 0; i < ACNET_BPF_CHANNELS; ++i)
        if (!sb->bpf[i].handle) return &sb->bpf[i];
    return NULL;
}

static void bpf_clear_local(struct BPFLocal *l)
{
    UBYTE *p = (UBYTE *)l;
    ULONG n = sizeof(*l);
    while (n--) *p++ = 0;
}
static inline void direct_waiter(struct Library *dev, struct ACNWaiter *w, int add)
{
    register struct ACNWaiter *a0 __asm("a0") = w;
    register struct Library *a6 __asm("a6") = dev;
    if (add)
        __asm volatile ("jsr -48(%%a6)" : "+r"(a0) : "r"(a6) :
                        "d0","d1","a1","cc","memory");
    else
        __asm volatile ("jsr -54(%%a6)" : "+r"(a0) : "r"(a6) :
                        "d0","d1","a1","cc","memory");
}

static void notify_update(struct SocketBase *sb)
{
    ULONG mask = 0;
    LONG i;
    for (i = 0; i < ACNET_BPF_CHANNELS; ++i)
        if (sb->bpf[i].handle) mask |= sb->bpf[i].notify_mask;

    if (!mask) {
        if (sb->dev_io) {
            if (sb->dev && sb->sigbit == 0) direct_waiter(sb->dev, &sb->waiter, 0);
            if (sb->dev) CloseDevice((struct IORequest *)sb->dev_io);
            FreeMem(sb->dev_io, sizeof(struct IOStdReq));
            sb->dev_io = NULL; sb->dev = NULL; sb->sigbit = -1;
        }
        return;
    }

    if (!sb->dev_io) {
        sb->dev_io = AllocMem(sizeof(struct IOStdReq), MEMF_PUBLIC | MEMF_CLEAR);
        if (!sb->dev_io) return;
        if (OPENSOCKET_OPEN_DEVICE(sb->dev_io)) {
            FreeMem(sb->dev_io, sizeof(struct IOStdReq)); sb->dev_io = NULL; return;
        }
        sb->dev = (struct Library *)sb->dev_io->io_Device;
        sb->waiter.task = sb->owner;
        sb->waiter.waiting = 1;
        sb->waiter.sigmask = mask;
        direct_waiter(sb->dev, &sb->waiter, 1);
        sb->sigbit = 0; /* direct waiter registered */
    } else {
        Forbid(); sb->waiter.task = sb->owner; sb->waiter.sigmask = mask;
        sb->waiter.waiting = 1; Permit();
    }
}
LONG bsd_bpf_open(struct SocketBase *sb, LONG channel)
{
    struct BPFLocal *l = bpf_free_local(sb);
    LONG h;
    if (!l) return fail(sb, AE_MFILE);
    h = prov_call(sb, ACHS_CMD_BPF_OPEN, (ULONG)channel, 0,0,0,
                  NULL,0,NULL,0,NULL);
    if (h < 0) return fail_provider(sb);
    l->handle = h;
    l->buffer_size = BPF_DEFAULT_BUFSIZE;
    l->timeout.tv_secs = l->timeout.tv_micro = 0;
    l->notify_mask = l->interrupt_mask = 0;
    l->attached = l->immediate = 0;
    set_errno(sb, 0);
    return h;
}

LONG bsd_bpf_close(struct SocketBase *sb, LONG channel)
{
    struct BPFLocal *l = bpf_local(sb, channel);
    LONG r;
    if (!l) return fail(sb, AE_NXIO);
    r = prov_call(sb, ACHS_CMD_BPF_CLOSE, l->handle,0,0,0,NULL,0,NULL,0,NULL);
    if (r < 0) return fail_provider(sb);
    bpf_clear_local(l);
    notify_update(sb);
    set_errno(sb, 0);
    return 0;
}

LONG bsd_bpf_set_notify_mask(struct SocketBase *sb, LONG channel, ULONG mask)
{
    struct BPFLocal *l = bpf_local(sb, channel);
    if (!l) return fail(sb, AE_NXIO);
    l->notify_mask = mask;
    notify_update(sb);
    set_errno(sb, 0);
    return 0;
}

LONG bsd_bpf_set_interrupt_mask(struct SocketBase *sb, LONG channel, ULONG mask)
{
    struct BPFLocal *l = bpf_local(sb, channel);
    if (!l) return fail(sb, AE_NXIO);
    l->interrupt_mask = mask;
    set_errno(sb, 0);
    return 0;
}
static LONG bpf_status(struct SocketBase *sb, struct BPFLocal *l,
                       struct BPFStatusWire *st)
{
    ULONG got = 0;
    LONG r = prov_call(sb, ACHS_CMD_BPF_STATUS, l->handle,0,0,0,
                       NULL,0,st,sizeof(*st),&got);
    if (r < 0) return fail_provider(sb);
    if (got < sizeof(*st)) return fail(sb, AE_IO);
    return 0;
}

LONG bsd_bpf_data_waiting(struct SocketBase *sb, LONG channel)
{
    struct BPFLocal *l = bpf_local(sb, channel);
    struct BPFStatusWire st;
    if (!l) return fail(sb, AE_NXIO);
    if (bpf_status(sb, l, &st) < 0) return -1;
    set_errno(sb, 0);
    return st.queued ? 1 : 0;
}

LONG bsd_bpf_write(struct SocketBase *sb, LONG channel, APTR buffer, LONG len)
{
    struct BPFLocal *l = bpf_local(sb, channel);
    LONG r;
    if (!l) return fail(sb, AE_NXIO);
    if (!buffer || len < 14) return fail(sb, AE_FAULT);
    if (len > 1514) return fail(sb, AE_MSGSIZE);
    r = prov_call(sb, ACHS_CMD_BPF_WRITE, l->handle,0,0,0,
                  buffer,(ULONG)len,NULL,0,NULL);
    if (r < 0) return fail_provider(sb);
    set_errno(sb, 0);
    return r;
}

LONG bsd_bpf_read(struct SocketBase *sb, LONG channel, APTR buffer, LONG len)
{
    struct BPFLocal *l = bpf_local(sb, channel);
    const struct timeval *tv;
    struct Wake wk;
    ULONG got = 0, rxlen = 0;
    LONG r, w;
    if (!l) return fail(sb, AE_NXIO);
    if (!buffer) return fail(sb, AE_FAULT);
    if ((ULONG)len != l->buffer_size) return fail(sb, AE_INVAL);
    tv = (l->timeout.tv_secs || l->timeout.tv_micro) ? &l->timeout : NULL;
    if (wake_begin(sb, &wk) < 0) return -1;
    for (;;) {
        prov_arm(sb, &wk);
        r = prov_call(sb, ACHS_CMD_BPF_READ, l->handle, l->buffer_size,0,0,
                      NULL,0,buffer,l->buffer_size,&rxlen);
        if (r >= 0) { prov_disarm(sb, &wk); set_errno(sb,0); break; }
        if (sb->last_err != AE_AGAIN) { prov_disarm(sb, &wk); r = fail_provider(sb); break; }
        w = wait_armed_event(sb, &wk, tv, l->interrupt_mask, &got);
        if (w < 0) { r = -1; break; }
        if (w == 0 && tv) { set_errno(sb,0); r = 0; break; }
        if (w == 2) { r = fail(sb, AE_INTR); break; }
    }
    wake_end(sb, &wk);
    return r;
}
static int name_is_acnet0(const char *s)
{
    static const char n[] = "gfopensocket0";
    int i;
    if (!s) return 0;
    for (i=0;i<6;i++) if (s[i] != n[i]) return 0;
    return s[6] == 0;
}

static LONG ioctl_ifaddr(struct SocketBase *sb, struct ifreq *ifr)
{
    struct ACNetworkInterface rows[16];
    ULONG got=0; LONG r,i,count; struct sockaddr_in *sin;
    r=prov_call(sb,ACNETWORK_CMD_INTERFACES,16,0,0,0,NULL,0,
                rows,sizeof(rows),&got);
    if(r<0)return fail_provider(sb);
    count=r;if((ULONG)count>got/sizeof(rows[0]))count=got/sizeof(rows[0]);
    for(i=0;i<count;i++) if(rows[i].ipv4[0] && rows[i].ipv4[0]!=127) break;
    if(i>=count)return fail(sb,AE_NXIO);
    sin=(struct sockaddr_in *)&ifr->ifr_addr;
    { UBYTE *p=(UBYTE *)sin; ULONG n=sizeof(*sin); while(n--) *p++=0; }
    sin->sin_len=sizeof(*sin); sin->sin_family=AF_INET;
    CopyMem(rows[i].ipv4,&sin->sin_addr.s_addr,4);
    return 0;
}

LONG bsd_bpf_ioctl(struct SocketBase *sb, LONG channel, ULONG command, APTR buffer)
{
    struct BPFLocal *l=bpf_local(sb,channel); struct BPFStatusWire st;
    if(!l)return fail(sb,AE_NXIO);
    switch(command) {
    case FIONREAD:
        if(!buffer)return fail(sb,AE_FAULT);
        if(bpf_status(sb,l,&st)<0)return -1;
        *(LONG *)buffer=(LONG)st.queued;
        break;
    case BIOCGBLEN:
        if(!buffer)return fail(sb,AE_FAULT);
        *(ULONG *)buffer=l->buffer_size;
        break;
    case BIOCSBLEN:
        if(!buffer)return fail(sb,AE_FAULT);
        { ULONG n=*(ULONG *)buffer;
          if(n<BPF_MINBUFSIZE)n=BPF_MINBUFSIZE;
          if(n>BPF_MAX_TRANSPORT)n=BPF_MAX_TRANSPORT;
          l->buffer_size=n; *(ULONG *)buffer=n;
          if(prov_call(sb,ACHS_CMD_BPF_FLUSH,l->handle,0,0,0,NULL,0,NULL,0,NULL)<0)
              return fail_provider(sb); }
        break;
    case BIOCSETF:
        if(!buffer)return fail(sb,AE_FAULT);
        { struct bpf_program *p=(struct bpf_program *)buffer;
          if(p->bf_len>BPF_MAXINSNS || (p->bf_len && !p->bf_insns))return fail(sb,AE_INVAL);
          if(prov_call(sb,ACHS_CMD_BPF_SETF,l->handle,p->bf_len,0,0,
                       p->bf_insns,p->bf_len*sizeof(struct bpf_insn),NULL,0,NULL)<0)
              return fail_provider(sb); }
        break;
    case BIOCFLUSH:
        if(prov_call(sb,ACHS_CMD_BPF_FLUSH,l->handle,0,0,0,NULL,0,NULL,0,NULL)<0)
            return fail_provider(sb);
        break;
    case BIOCPROMISC:
        /* ACNet's user-mode Ethernet is already an isolated virtual segment. */
        break;
    case BIOCGDLT:
        if(!buffer)return fail(sb,AE_FAULT);
        *(ULONG *)buffer=DLT_EN10MB;
        break;
    case BIOCGETIF:
        if(!buffer)return fail(sb,AE_FAULT);
        { struct ifreq *ifr=(struct ifreq *)buffer; int i;
          for(i=0;i<IFNAMSIZ;i++)ifr->ifr_name[i]=0;
          ifr->ifr_name[0]='a';ifr->ifr_name[1]='c';ifr->ifr_name[2]='n';
          ifr->ifr_name[3]='e';ifr->ifr_name[4]='t';ifr->ifr_name[5]='0'; }
        break;
    case BIOCSETIF:
        if(!buffer)return fail(sb,AE_FAULT);
        if(!name_is_acnet0(((struct ifreq *)buffer)->ifr_name))return fail(sb,AE_NXIO);
        if(prov_call(sb,ACHS_CMD_BPF_ATTACH,l->handle,0,0,0,NULL,0,NULL,0,NULL)<0)
            return fail_provider(sb);
        l->attached=1; break;
    case SIOCGIFADDR:
        if(!buffer)return fail(sb,AE_FAULT);
        if(ioctl_ifaddr(sb,(struct ifreq *)buffer)<0)return -1;
        break;
    case BIOCSRTIMEOUT:
        if(!buffer)return fail(sb,AE_FAULT);
        { struct CompatTimeval *t=(struct CompatTimeval *)buffer;
          if(t->secs<0 || t->micro<0 || t->micro>=1000000)return fail(sb,AE_INVAL);
          l->timeout.tv_secs=t->secs;l->timeout.tv_micro=t->micro; }
        break;
    case BIOCGRTIMEOUT:
        if(!buffer)return fail(sb,AE_FAULT);
        ((struct CompatTimeval *)buffer)->secs=l->timeout.tv_secs;
        ((struct CompatTimeval *)buffer)->micro=l->timeout.tv_micro; break;
    case BIOCGSTATS:
        if(!buffer)return fail(sb,AE_FAULT);
        if(bpf_status(sb,l,&st)<0)return -1;
        ((struct bpf_stat *)buffer)->bs_recv=st.received;
        ((struct bpf_stat *)buffer)->bs_drop=st.dropped; break;
    case BIOCIMMEDIATE:
        if(!buffer)return fail(sb,AE_FAULT);
        l->immediate=*(ULONG *)buffer?1:0;
        break;
    case BIOCVERSION:
        if(!buffer)return fail(sb,AE_FAULT);
        ((struct bpf_version *)buffer)->bv_major=BPF_MAJOR_VERSION;
        ((struct bpf_version *)buffer)->bv_minor=BPF_MINOR_VERSION; break;
    default:
        return fail(sb,AE_NOTTY);
    }
    set_errno(sb,0); return 0;
}

void bpf_close_all(struct SocketBase *sb)
{
    LONG i;
    for(i=0;i<ACNET_BPF_CHANNELS;i++) if(sb->bpf[i].handle) {
        prov_call(sb,ACHS_CMD_BPF_CLOSE,sb->bpf[i].handle,0,0,0,NULL,0,NULL,0,NULL);
        bpf_clear_local(&sb->bpf[i]);
    }
    notify_update(sb);
}
