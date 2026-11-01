#ifndef AMICRADLE_ACHOSTSOCKET_H
#define AMICRADLE_ACHOSTSOCKET_H

/*
 * ACNet, the network card, and its HostSocket v1 block: the Amiga's sockets,
 * carried out by the Linux host.
 *
 * The card is a 64 KiB Zorro II AutoConfig I/O board: manufacturer AmiCradle Team
 * (0xDA15; 2011 for boot ROMs from before it, as ACBridge), product 6, the
 * number main reserved for networking. It is fitted while the instance's
 * Network switch is on, from the next reset. HostSocket's block starts at the
 * card's base; the top half ($8000-$FFFF) is kept for the packet rings of a
 * later SANA-II side. Design: docs/architecture/ACNET_DESIGN.md.
 *
 * A command is synchronous: writing COMMAND runs it on the host, and RESULT,
 * ERRNO and RXLEN are set when the write returns. Host sockets never block;
 * the guest library waits for readiness with POLL, which also arms the host
 * to raise EVENT when a socket it named becomes ready, and INT2 when
 * IRQ_ENABLE is set. Callers hold Forbid() from the first argument write to
 * the last read of the receive buffer: the block has one set of arguments.
 *
 * All registers and buffers are big-endian to the 68k. Socket addresses are
 * the Amiga's struct sockaddr_in (16 bytes: len, family, port, address, zero).
 * Errno values are the Amiga's (BSD) numbers; socket levels and options are
 * the Amiga's too: the host maps them.
 */

#include "amicradle_zorro.h"
#define ACNET_MANUFACTURER        AMICRADLE_MANUFACTURER
#define ACNET_MANUFACTURER_OLD    AMICRADLE_MANUFACTURER_OLD   /* looked for too */
#define ACNET_PRODUCT             6
#define ACNET_APERTURE_SIZE       0x10000UL

#define ACHS_SIGNATURE            0x41434831UL /* "ACH1" */
#define ACHS_VERSION              0x00010000UL

#define ACHS_REG_SIGNATURE        0x00
#define ACHS_REG_VERSION          0x04
#define ACHS_REG_STATE            0x08   /* ACHS_STATE_* (read) */
#define ACHS_REG_COMMAND          0x10   /* write: runs the command */
#define ACHS_REG_ARG0             0x14
#define ACHS_REG_ARG1             0x18
#define ACHS_REG_ARG2             0x1c
#define ACHS_REG_ARG3             0x20
#define ACHS_REG_RESULT           0x24   /* the call's return value; -1 with ERRNO on failure */
#define ACHS_REG_ERRNO            0x28
#define ACHS_REG_TXLEN            0x30   /* bytes of the transmit buffer the command takes */
#define ACHS_REG_RXLEN            0x34   /* bytes the command left in the receive buffer */
#define ACHS_REG_IRQ_ENABLE       0x40   /* 1: a pending event raises INT2 (PORTS) */
#define ACHS_REG_EVENT            0x44   /* read: nonzero while an event is pending; write anything: acknowledge */
#define ACHS_REG_EVENT_COUNT      0x48   /* events posted since reset */

#define ACHS_TX                   0x0100UL   /* guest -> host, from the card's base */
#define ACHS_TX_SIZE              0x3f00UL
#define ACHS_RX                   0x4000UL   /* host -> guest */
#define ACHS_RX_SIZE              0x4000UL

#define ACHS_STATE_PRESENT        0x01   /* the host service answers */
#define ACHS_STATE_ONLINE         0x02   /* this instance's network is switched on */

/* Commands. ARGn as listed; "tx" and "rx" are the buffers. */
#define ACHS_CMD_HELLO            0x01   /* ARG0 1: the device has just started (the host drops the last
                                            boot's sockets) -> version; rx: ULONG state, ULONG host link Mb/s */
#define ACHS_CMD_STATUS           0x02   /* rx: ULONG state, ULONG link Mb/s, ULONG sockets, ULONG 0,
                                            UQUAD bytes in, UQUAD bytes out, ULONG connects, ULONG refused */
#define ACHS_CMD_SOCKET           0x10   /* domain, type, protocol -> handle */
#define ACHS_CMD_CLOSE            0x11   /* handle */
#define ACHS_CMD_BIND             0x12   /* handle; tx: sockaddr */
#define ACHS_CMD_LISTEN           0x13   /* handle, backlog */
#define ACHS_CMD_ACCEPT           0x14   /* handle -> new handle; rx: peer sockaddr */
#define ACHS_CMD_CONNECT          0x15   /* handle; tx: sockaddr -> 0, or -1 EINPROGRESS */
#define ACHS_CMD_SEND             0x16   /* handle, flags; tx: data -> bytes taken */
#define ACHS_CMD_RECV             0x17   /* handle, max, flags -> bytes (0: end); rx: data */
#define ACHS_CMD_SENDTO           0x18   /* handle, flags; tx: sockaddr then data -> bytes */
#define ACHS_CMD_RECVFROM         0x19   /* handle, max, flags -> bytes; rx: sockaddr then data */
#define ACHS_CMD_SHUTDOWN         0x1a   /* handle, how */
#define ACHS_CMD_SETSOCKOPT       0x1b   /* handle, level, option; tx: value */
#define ACHS_CMD_GETSOCKOPT       0x1c   /* handle, level, option, max -> length; rx: value */
#define ACHS_CMD_GETSOCKNAME      0x1d   /* handle; rx: sockaddr */
#define ACHS_CMD_GETPEERNAME      0x1e   /* handle; rx: sockaddr */
#define ACHS_CMD_POLL             0x1f   /* count; tx: count x {handle, events} -> ready count; rx: count x revents */
#define ACHS_CMD_PENDING          0x20   /* handle -> bytes waiting to be read (FIONREAD) */
#define ACHS_CMD_RESOLVE          0x30   /* tx: host name -> ticket (the answer comes later, with an event) */
#define ACHS_CMD_RESOLVE_ADDR     0x31   /* tx: 4-byte address -> ticket */
#define ACHS_CMD_ANSWER           0x32   /* ticket -> address count, or -1 EINPROGRESS while it runs;
                                            rx: name\0 then count x 4-byte address; ERRNO holds h_errno on failure */
#define ACHS_CMD_SERVICE          0x33   /* port (0: by name), max; tx: name\0proto\0 -> port; rx: name\0 */
#define ACHS_CMD_HOSTNAME         0x34   /* rx: the instance's host name\0 */
#define ACHS_CMD_NEIGHBOURS       0x35   /* a0=max records; rx: ACNetworkNeighbour records */
#define ACHS_CMD_INTERFACES       0x36   /* a0=max records; rx: ACNetworkInterface records */
#define ACHS_CMD_ROUTES           0x37   /* a0=max records; rx: ACNetworkRoute records */
#define ACHS_CMD_SOCKETS          0x38   /* a0=max records; rx: ACNetworkSocket records */
#define ACHS_CMD_SET_ONLINE       0x39   /* a0=0 soft-offline, 1 online; -> state */
#define ACHS_CMD_DNS_SERVERS      0x3a   /* a0=max addresses; rx: packed IPv4 addresses */
#define ACHS_CMD_NET_BY_NAME      0x3b   /* tx=name\0; rx=canonical\0 + IPv4 network */
#define ACHS_CMD_NET_BY_ADDR      0x3c   /* a0=host-order IPv4 network; same rx */
#define ACHS_CMD_LOG              0x3d   /* a0=sequence already seen, a1=max records; rx: ACNetworkLogEntry records */
#define ACHS_CMD_WIFI_SCAN        0x40   /* -> async ticket; answer: ACWiFiNetwork records */
#define ACHS_CMD_WIFI_STATUS      0x41   /* -> async ticket; answer: active ACWiFiNetwork, or 0 records */
#define ACHS_CMD_WIFI_JOIN        0x42   /* tx=known SSID\0 -> async ticket */
#define ACHS_CMD_WIFI_LEAVE       0x43   /* -> async ticket */
#define ACHS_CMD_WIFI_FORGET      0x44   /* tx=known SSID\0 -> async ticket */
#define ACHS_CMD_WIFI_ANSWER      0x45   /* a0=ticket -> result/rx, EINPROGRESS until done */
#define ACHS_CMD_WIFI_CANCEL      0x46   /* a0=ticket; forget/cancel outstanding job */
#define ACHS_CMD_PACKET_INFO      0x50   /* rx: ACPacketInfo */
#define ACHS_CMD_PACKET_ONLINE    0x51   /* a0: flags; start user-mode Ethernet provider */
#define ACHS_CMD_PACKET_OFFLINE   0x52   /* stop packet provider */
#define ACHS_CMD_PACKET_SEND      0x53   /* tx: raw Ethernet frame -> bytes accepted */
#define ACHS_CMD_PACKET_RECV      0x54   /* a0=max bytes -> frame, EAGAIN when empty */
#define ACHS_CMD_PACKET_STATS     0x55   /* rx: ACPacketStats */
#define ACHS_CMD_BPF_OPEN         0x60   /* a0=channel or -1 -> handle */
#define ACHS_CMD_BPF_CLOSE        0x61   /* a0=handle */
#define ACHS_CMD_BPF_READ         0x62   /* a0=handle, a1=max bytes -> packed BPF records */
#define ACHS_CMD_BPF_WRITE        0x63   /* a0=handle; tx=raw Ethernet frame */
#define ACHS_CMD_BPF_SETF         0x64   /* a0=handle, a1=instruction count; tx=8-byte BPF instructions */
#define ACHS_CMD_BPF_FLUSH        0x65   /* a0=handle */
#define ACHS_CMD_BPF_ATTACH       0x66   /* a0=handle; attach to ACNet Ethernet */
#define ACHS_CMD_BPF_STATUS       0x67   /* a0=handle; rx: queued bytes, received, dropped */

/* POLL events (poll(2)'s values). */
#define ACHS_POLLIN               0x0001
#define ACHS_POLLPRI              0x0002
#define ACHS_POLLOUT              0x0004
#define ACHS_POLLERR              0x0008
#define ACHS_POLLHUP              0x0010
#define ACHS_POLLNVAL             0x0020

#endif
