#ifndef ACWIFI_DEVICE_H
#define ACWIFI_DEVICE_H

#include <exec/types.h>
#include <exec/io.h>
#include <acnetwork.h>

#define ACWIFI_DEVICE_NAME    "gfopensocketwifi.device"   /* was acwifi.device (4 Oct 2026) */
#define ACWIFI_DEVICE_VERSION 1
#define ACWIFI_DEVICE_REVISION 1   /* 1.1: ACWiFiCall2 (JOINKEY, NET_GET, NET_SET): check lib_Revision >= 1 */

#define ACWIFI_SCAN   1
#define ACWIFI_STATUS 2
#define ACWIFI_JOIN   3
#define ACWIFI_LEAVE  4
#define ACWIFI_FORGET 5

/* Synchronous control request used by ACW_Call().
 * For SCAN/STATUS, networks/capacity receive ACWiFiNetwork records.
 * JOIN/FORGET take ssid. LEAVE has no payload.
 * result is record count for SCAN/STATUS and zero for successful mutations.
 * error is an Amiga network errno; zero means success. */
struct ACWiFiCall {
    ULONG command;
    const char *ssid;
    struct ACWiFiNetwork *networks;
    ULONG capacity;
    LONG result;
    ULONG error;
};

/* Optional standard-device facade: io_Data points at struct ACWiFiCall. */
#define ACWIFI_IO_CALL (CMD_NONSTD + 0)

/* Private vector after the standard device vectors. */
#define ACWIFI_LVO_CALL (-42)

/* 1.1 (9 Nov 2026, amicradle docs/design/Design-Network-Passthrough.md): the network from the Amiga.
 * JOINKEY joins a network the host may not know yet: tx is SSID\0KEY\0 (KEY empty for an open network;
 * 8-63 characters or 64 hex digits). NET_GET answers the settings as text (no secret) into rx.
 * NET_SET sends amicradle-network.txt's lines (tx, NUL-ended). On a native card the Amiga runs the
 * card's network; on a PC, NET_SET answers EPERM and the Wi-Fi needs Cradle's switch.
 * The device keeps no copy of tx: the caller clears its key once the call returns. */
#define ACWIFI_JOINKEY 6
#define ACWIFI_NET_GET 7
#define ACWIFI_NET_SET 8

struct ACWiFiCall2 {
    ULONG size;               /* sizeof(struct ACWiFiCall2) */
    ULONG command;            /* ACWIFI_JOINKEY, ACWIFI_NET_GET or ACWIFI_NET_SET */
    const UBYTE *tx;
    ULONG txlen;
    UBYTE *rx;
    ULONG rxmax;
    LONG result;              /* NET_GET: the text's length; else 0 */
    ULONG error;              /* an Amiga network errno; 0 is success */
    ULONG rxlen;
};

#define ACWIFI_IO_CALL2 (CMD_NONSTD + 1)
#define ACWIFI_LVO_CALL2 (-48)

#endif
