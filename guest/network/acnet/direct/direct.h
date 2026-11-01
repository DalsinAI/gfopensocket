/* GFOpenSocketDirect: GFOpenSocket's own TCP/IP stack (lwIP) over a SANA-II
 * device, as bsdsocket.library's Direct backend sees it.
 *
 * The stack keeps writable globals (lwIP's), so it is built as a hunk
 * image of its own: bsdsocket.library carries the image as data and loads
 * it into RAM (romseg.c) when the card layer chooses Direct, so the library
 * itself stays ROM-clean. The image's first RomTag (rt_Name
 * "gfopensocketdirect") points with rt_Init at its struct DirectOps.
 * MIT. */
#ifndef OPENSOCKET_DIRECT_H
#define OPENSOCKET_DIRECT_H

#include <exec/types.h>
#include <exec/tasks.h>

#define DIRECT_MAGIC    0x4f534431UL   /* "OSD1" */
#define DIRECT_VERSION  1

struct CardConfig;

/* What the Direct backend calls. All but start take the stack start
 * returned; every call is safe from any task. */
struct DirectOps {
    ULONG magic, version;
    /* lwIP up (loopback at once), then the stack's process opens the
     * device and brings the interface up (DHCP when cfg->dhcp). NULL only
     * when memory ran out. cfg is copied. */
    APTR (*start)(const struct CardConfig *cfg);
    /* One HostSocket command (common/protocol/achostsocket.h), answered at
     * once: its result, or -1 with *err set (EAGAIN, EINPROGRESS...). */
    LONG (*call)(APTR st, ULONG cmd, ULONG a0, ULONG a1, ULONG a2, ULONG a3,
                 const void *tx, ULONG txlen, void *rx, ULONG rxmax, ULONG *rxlen, ULONG *err);
    /* From arm until disarm, any news on any socket signals task with mask. */
    void (*arm)(APTR st, struct Task *task, ULONG mask);
    void (*disarm)(APTR st, struct Task *task);
    void (*stop)(APTR st);         /* nothing yet: the stack stays */
};

#endif
