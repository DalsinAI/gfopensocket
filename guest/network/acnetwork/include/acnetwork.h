#ifndef ACNETWORK_H
#define ACNETWORK_H

#include <exec/types.h>

#define ACNETWORK_LIBRARY_NAME "gfopensocket.library"   /* was acnetwork.library (4 Oct 2026) */
#define ACNETWORK_LIBRARY_VERSION 1

#define ACNETWORK_STATE_CARD   0x01
#define ACNETWORK_STATE_ONLINE 0x02

#define ACNETWORK_CMD_STATUS       0x02
#define ACNETWORK_CMD_HOSTNAME     0x34
#define ACNETWORK_CMD_NEIGHBOURS   0x35
#define ACNETWORK_CMD_INTERFACES   0x36
#define ACNETWORK_CMD_ROUTES       0x37
#define ACNETWORK_CMD_SOCKETS      0x38
#define ACNETWORK_CMD_SET_ONLINE   0x39
#define ACNETWORK_CMD_DNS_SERVERS  0x3a
#define ACNETWORK_CMD_LOG          0x3d
#define ACNETWORK_CMD_WIFI_SCAN    0x40
#define ACNETWORK_CMD_WIFI_STATUS  0x41
#define ACNETWORK_CMD_WIFI_JOIN    0x42
#define ACNETWORK_CMD_WIFI_LEAVE   0x43
#define ACNETWORK_CMD_WIFI_FORGET  0x44
#define ACNETWORK_CMD_WIFI_ANSWER  0x45
#define ACNETWORK_CMD_WIFI_CANCEL  0x46
#define ACNETWORK_CMD_PACKET_INFO    0x50
#define ACNETWORK_CMD_PACKET_ONLINE  0x51
#define ACNETWORK_CMD_PACKET_OFFLINE 0x52
#define ACNETWORK_CMD_PACKET_SEND    0x53
#define ACNETWORK_CMD_PACKET_RECV    0x54
#define ACNETWORK_CMD_PACKET_STATS   0x55

#define ACNETWORK_NEIGH_VALID      0x01
#define ACNETWORK_SOCKET_OPEN      0
#define ACNETWORK_SOCKET_BOUND     1
#define ACNETWORK_SOCKET_LISTEN    2
#define ACNETWORK_SOCKET_CONNECTED 3

struct ACNetworkStatus {
    ULONG state;
    ULONG link_mbps;
    ULONG sockets;
    ULONG reserved;
    unsigned long long bytes_in;
    unsigned long long bytes_out;
    ULONG connects;
    ULONG refused;
};
typedef char ACNetworkStatus_size_must_be_40[(sizeof(struct ACNetworkStatus) == 40) ? 1 : -1];

struct ACNetworkNeighbour {
    UBYTE ipv4[4];
    UBYTE mac[6];
    UBYTE state;
    UBYTE reserved;
    ULONG ifindex;
    char ifname[16];
};
typedef char ACNetworkNeighbour_size_must_be_32[(sizeof(struct ACNetworkNeighbour) == 32) ? 1 : -1];

struct ACNetworkInterface {
    ULONG ifindex;
    ULONG flags;
    UBYTE ipv4[4];
    UBYTE netmask[4];
    UBYTE mac[6];
    UBYTE reserved0[2];
    ULONG mtu;
    ULONG link_mbps;
    unsigned long long rx_bytes;
    unsigned long long tx_bytes;
    char ifname[16];
};
typedef char ACNetworkInterface_size_must_be_64[(sizeof(struct ACNetworkInterface) == 64) ? 1 : -1];

struct ACNetworkRoute {
    UBYTE destination[4];
    UBYTE gateway[4];
    UBYTE netmask[4];
    ULONG ifindex;
    ULONG flags;
    ULONG metric;
    char ifname[16];
};
typedef char ACNetworkRoute_size_must_be_40[(sizeof(struct ACNetworkRoute) == 40) ? 1 : -1];

struct ACNetworkSocket {
    ULONG handle;
    ULONG kind;
    ULONG protocol;
    ULONG state;
    UBYTE local_ipv4[4];
    UWORD local_port;
    UBYTE reserved0[2];
    UBYTE remote_ipv4[4];
    UWORD remote_port;
    UBYTE reserved1[2];
    char label[16];
};
typedef char ACNetworkSocket_size_must_be_48[(sizeof(struct ACNetworkSocket) == 48) ? 1 : -1];

#define ACNETWORK_LOG_INFO  1
#define ACNETWORK_LOG_WARN  2
#define ACNETWORK_LOG_ERROR 3

struct ACNetworkLogEntry {
    ULONG sequence;
    ULONG seconds;
    ULONG level;
    char text[84];
};
typedef char ACNetworkLogEntry_size_must_be_96[(sizeof(struct ACNetworkLogEntry) == 96) ? 1 : -1];

#define ACWIFI_FLAG_KNOWN   0x01
#define ACWIFI_FLAG_ACTIVE  0x02
#define ACWIFI_FLAG_SECURED 0x04
#define ACWIFI_SECURITY_OPEN  0
#define ACWIFI_SECURITY_WEP   1
#define ACWIFI_SECURITY_WPA   2
#define ACWIFI_SECURITY_WPA2  3
#define ACWIFI_SECURITY_WPA3  4
#define ACWIFI_SECURITY_OTHER 5

struct ACWiFiNetwork {
    char ssid[33];
    UBYTE bssid[6];
    UBYTE signal;
    UBYTE security;
    UBYTE flags;
    UBYTE reserved0[2];
    ULONG channel;
    ULONG rate_mbps;
    UBYTE reserved1[12];
};
typedef char ACWiFiNetwork_size_must_be_64[(sizeof(struct ACWiFiNetwork) == 64) ? 1 : -1];

#define ACPACKET_FLAG_ONLINE   0x01
#define ACPACKET_FLAG_IPV4     0x02
#define ACPACKET_FLAG_IPV6     0x04
#define ACPACKET_FLAG_USERMODE 0x08

struct ACPacketInfo {
    UBYTE mac[6];
    UBYTE reserved0[2];
    ULONG mtu;
    ULONG bps;
    ULONG flags;
    ULONG rx_queued;
    ULONG tx_packets;
    ULONG rx_packets;
};
typedef char ACPacketInfo_size_must_be_32[(sizeof(struct ACPacketInfo) == 32) ? 1 : -1];

struct ACPacketStats {
    ULONG tx_packets;
    ULONG rx_packets;
    ULONG tx_bytes;
    ULONG rx_bytes;
    ULONG tx_dropped;
    ULONG rx_dropped;
};
typedef char ACPacketStats_size_must_be_24[(sizeof(struct ACPacketStats) == 24) ? 1 : -1];

struct ACNetworkRequest {
    ULONG command;
    ULONG arg[4];
    const UBYTE *tx;
    ULONG txlen;
    UBYTE *rx;
    ULONG rxmax;
    LONG result;
    ULONG error;
    ULONG rxlen;
};

#define ACNETWORK_LVO_CALL       (-30)
#define ACNETWORK_LVO_ARM        (-36)
#define ACNETWORK_LVO_DISARM     (-42)
#define ACNETWORK_LVO_SIGNALMASK (-48)
#define ACNETWORK_LVO_LASTERROR  (-54)
#define ACNETWORK_LVO_STATE      (-60)

#endif
