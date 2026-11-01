/* ACNet Roadshow BPF end-to-end probe. MIT. */
#include <exec/types.h>
#include <exec/libraries.h>
#include <proto/exec.h>
#include <libraries/bsdsocket.h>
#include <proto/bsdsocket.h>
#include <utility/tagitem.h>
#include <sys/filio.h>
#include <net/if.h>
#include <net/bpf.h>
#include <stdio.h>
#include <string.h>

struct Library *SocketBase = NULL;
static int failures;
static void check(int ok,const char *name)
{
    printf("%s: %s\n",name,ok?"PASS":"FAIL");
    if(!ok)failures++;
}

int main(void)
{
    ULONG channels=0,dlt=0,buflen=4096,version_ok=0;
    struct TagItem tags[]={{SBTM_GETREF(SBTC_NUM_PACKET_FILTER_CHANNELS),(ULONG)&channels},{TAG_DONE,0}};
    struct bpf_version ver;
    struct ifreq ifr;
    struct bpf_insn insn=BPF_STMT(BPF_RET|BPF_K,0xffff);
    struct bpf_program prog={1,&insn};
    UBYTE frame[60],buffer[4096];
    LONG h,n,waiting;
    struct { LONG secs,micro; } timeout={1,0};
    struct bpf_hdr *hdr;

    SocketBase=OpenLibrary("bsdsocket.library",4);
    if(!SocketBase){printf("OPEN: FAIL\n");return 20;}
    check(SocketBaseTagList(tags)==0,"packet-filter capability query");
    check(channels>=1,"packet-filter channels advertised");

    h=bpf_open(-1);
    check(h>0,"bpf_open");
    if(h<=0){CloseLibrary(SocketBase);return 5;}

    memset(&ver,0,sizeof(ver));
    check(bpf_ioctl(h,BIOCVERSION,&ver)==0,"BIOCVERSION");
    version_ok=(ver.bv_major==BPF_MAJOR_VERSION && ver.bv_minor>=BPF_MINOR_VERSION);
    check(version_ok,"BPF version 1.1 or newer");

    check(bpf_ioctl(h,BIOCGBLEN,&buflen)==0,"BIOCGBLEN");
    check(buflen>=BPF_MINBUFSIZE && buflen<=sizeof(buffer),"buffer length bounded");
    buflen=sizeof(buffer);
    check(bpf_ioctl(h,BIOCSBLEN,&buflen)==0,"BIOCSBLEN");

    memset(&ifr,0,sizeof(ifr)); strcpy(ifr.ifr_name,"gfopensocket0");
    check(bpf_ioctl(h,BIOCSETIF,&ifr)==0,"BIOCSETIF gfopensocket0");
    memset(&ifr,0,sizeof(ifr));
    check(bpf_ioctl(h,BIOCGETIF,&ifr)==0 && strcmp(ifr.ifr_name,"gfopensocket0")==0,"BIOCGETIF");
    check(bpf_ioctl(h,BIOCGDLT,&dlt)==0 && dlt==DLT_EN10MB,"BIOCGDLT Ethernet");
    check(bpf_ioctl(h,BIOCSETF,&prog)==0,"BIOCSETF accept-all");
    check(bpf_ioctl(h,BIOCSRTIMEOUT,&timeout)==0,"BIOCSRTIMEOUT");

    memset(frame,0,sizeof(frame));
    memset(frame,0xff,6);
    frame[6]=0x02;frame[7]=0xda;frame[8]=0x15;frame[11]=0x06;
    frame[12]=0x88;frame[13]=0xb5; /* private/experimental EtherType for the probe */
    frame[14]='A';frame[15]='C';frame[16]='N';frame[17]='E';frame[18]='T';

    n=bpf_write(h,frame,sizeof(frame));
    check(n==(LONG)sizeof(frame),"bpf_write");

    waiting=bpf_data_waiting(h);
    check(waiting==1,"bpf_data_waiting");

    memset(buffer,0,sizeof(buffer));
    n=bpf_read(h,buffer,buflen);
    check(n>0,"bpf_read");
    if(n>0){
        hdr=(struct bpf_hdr *)buffer;
        check(hdr->bh_hdrlen>=sizeof(struct bpf_hdr),"BPF header length");
        check(hdr->bh_caplen==sizeof(frame) && hdr->bh_datalen==sizeof(frame),"BPF capture lengths");
        check(!memcmp(buffer+hdr->bh_hdrlen,frame,sizeof(frame)),"captured injected frame");
    }

    check(bpf_ioctl(h,BIOCFLUSH,NULL)==0,"BIOCFLUSH");
    check(bpf_close(h)==0,"bpf_close");
    CloseLibrary(SocketBase);SocketBase=NULL;
    printf("RESULT: %s (%d failures)\n",failures?"FAIL":"PASS",failures);
    return failures?5:0;
}
