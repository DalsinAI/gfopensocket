#ifndef ACWIFI_DEVICE_H
#define ACWIFI_DEVICE_H

#include <exec/types.h>
#include <exec/io.h>
#include <acnetwork.h>

#define ACWIFI_DEVICE_NAME    "gfopensocketwifi.device"   /* was acwifi.device (4 Oct 2026) */
#define ACWIFI_DEVICE_VERSION 1

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

#endif
