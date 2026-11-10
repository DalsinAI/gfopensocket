/* GFOpenSocketDirect: lwIP's options for GFOpenSocket's own stack over a
 * SANA-II device. lwIP runs without an OS layer (NO_SYS): every call into it
 * holds the stack's semaphore (direct_stack.c), from the stack's process or
 * from a bsdsocket.library caller. MIT (lwIP itself: lwip/COPYING). */
#ifndef OPENSOCKET_LWIPOPTS_H
#define OPENSOCKET_LWIPOPTS_H

#define NO_SYS                      1
#define LWIP_SOCKET                 0
#define LWIP_NETCONN                0
#define LWIP_IPV4                   1
#define LWIP_IPV6                   0
#define LWIP_ARP                    1
#define LWIP_ETHERNET               1
#define LWIP_ICMP                   1
#define LWIP_RAW                    1
#define LWIP_UDP                    1
#define LWIP_TCP                    1
#define LWIP_DHCP                   1
#define LWIP_DHCP_DOES_ACD_CHECK    0
#define LWIP_ACD                    0
#define LWIP_AUTOIP                 0
/* IGMP (multicast: mDNS's 224.0.0.251, the services card's LAN discovery):
 * on unless built with DIRECT_CFLAGS=-DDIRECT_IGMP=0 */
#ifndef DIRECT_IGMP
#define DIRECT_IGMP                 1
#endif
#define LWIP_IGMP                   DIRECT_IGMP
#if LWIP_IGMP
#define LWIP_MULTICAST_TX_OPTIONS   1
#define MEMP_NUM_IGMP_GROUP         16
#define SO_REUSE_RXTOALL            1   /* a group's datagram to every socket bound to its port (SO_REUSEADDR), as BSD */
#endif
#define LWIP_DNS                    1
#define DNS_MAX_SERVERS             3
#define DNS_TABLE_SIZE              8
#define DNS_MAX_NAME_LENGTH         255
#define LWIP_NETIF_HOSTNAME         1
#define LWIP_NETIF_STATUS_CALLBACK  1
#define LWIP_NETIF_LOOPBACK         1
#define LWIP_HAVE_LOOPIF            1
#define LWIP_NETIF_LOOPBACK_MULTITHREADING 0
#define LWIP_LOOPBACK_MAX_PBUFS     0
#define IP_REASSEMBLY               1
#define IP_FRAG                     1
#define SO_REUSE                    1
#define LWIP_TCP_KEEPALIVE          1
#define LWIP_STATS                  0
#define LWIP_DEBUG                  0
#define LWIP_PROVIDE_ERRNO          0
#define LWIP_TIMERS                 1
#define SYS_LIGHTWEIGHT_PROT        0   /* the stack's semaphore guards lwIP */

/* Memory: lwIP's heap and pools come from exec (AllocVec), on demand, so the
 * stack's image keeps no large tables. */
#define MEM_CUSTOM_ALLOCATOR        1
#define MEM_CUSTOM_MALLOC           direct_malloc
#define MEM_CUSTOM_FREE             direct_free
#define MEM_CUSTOM_CALLOC           direct_calloc
#define MEMP_MEM_MALLOC             1
#define MEM_ALIGNMENT               4
#include <stddef.h>
void *direct_malloc(size_t n);
void  direct_free(void *p);
void *direct_calloc(size_t n, size_t m);

#define MEMP_NUM_TCP_PCB            32
#define MEMP_NUM_TCP_PCB_LISTEN     16
#define MEMP_NUM_UDP_PCB            16
#define MEMP_NUM_RAW_PCB            8
#define MEMP_NUM_TCP_SEG            128
#define PBUF_POOL_SIZE              64
#define TCP_MSS                     1460
#define TCP_WND                     (8 * TCP_MSS)
#define TCP_SND_BUF                 (8 * TCP_MSS)
#define TCP_SND_QUEUELEN            (4 * TCP_SND_BUF / TCP_MSS)
#define TCP_LISTEN_BACKLOG          1
#define LWIP_RAND()                 direct_rand()
unsigned long direct_rand(void);

#define LWIP_CHKSUM_ALGORITHM       2
#define ETH_PAD_SIZE                0

#endif
