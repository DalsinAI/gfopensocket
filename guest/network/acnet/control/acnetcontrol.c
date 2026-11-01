/*
 * RETIRED (4 Oct 2026): ACNet is now GFOpenSocket, and AmigaOS 3.x programs are
 * GadTools or MUI. This ReAction Commodity is no longer built. It stays as the
 * reference for the pages GFOpenSocketControl (acnetcontrol_gt.c) still has to
 * gain: live control, DNS/TCP diagnostics, the event log, Wi-Fi. Delete it
 * once GFOpenSocketControl has them.
 */
/*
 * ACNetControl - the ACNet ReAction Commodity for AmigaOS 3.2.3.
 *
 * First-light UI: native five-page control/status application.  Network
 * operation does not depend on this program; closing the window merely hides
 * the Commodity.
 *
 * MIT.
 */
#include <exec/types.h>
#include <exec/io.h>
#include <exec/libraries.h>
#include <exec/ports.h>
#include <dos/dos.h>
#include <string.h>
#include <intuition/intuition.h>
#include <libraries/commodities.h>
#include <reaction/reaction.h>
#include <reaction/reaction_macros.h>
#include <gadgets/button.h>
#include <gadgets/clicktab.h>
#include <gadgets/layout.h>
#include <gadgets/string.h>
#include <images/label.h>
#include <classes/window.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdlib.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/utility.h>
#include <proto/commodities.h>
#include <proto/button.h>
#include <proto/clicktab.h>
#include <proto/layout.h>
#include <proto/string.h>
#include <proto/label.h>
#include <proto/window.h>
#include <proto/bsdsocket.h>
#include <clib/alib_protos.h>
#include <stdio.h>

#include <acnetwork.h>
#include <proto/acnetwork.h>
#include <acwifi_device.h>
#include "../include/acnet_device.h"

/* Strong definitions stop libnix libstubs.a from supplying its own class
 * bases and pre-main autoinit names (notably window.library and
 * gadgets/label.gadget, which are not present in stock OS 3.2.3 ReAction). */
struct Library *CxBase = NULL;
struct Library *ButtonBase = NULL;
struct Library *ClickTabBase = NULL;
struct Library *LabelBase = NULL;
struct Library *LayoutBase = NULL;
struct Library *StringBase = NULL;
struct Library *WindowBase = NULL;
struct Library *ACNetworkBase = NULL;
struct Library *SocketBase = NULL;

enum {
    GID_TABS = 1,
    GID_REFRESH,
    GID_GO_OFFLINE,
    GID_WIFI_RESCAN,
    GID_WIFI_JOIN,
    GID_WIFI_LEAVE,
    GID_WIFI_FORGET,
    GID_DIAG_DNS,
    GID_DIAG_CONNECT,
    GID_DIAG_INTERNET,
    GID_DIAG_COPY,
    GID_LOG_CLEAR,
    GID_LOG_SAVE
};

#define HOTKEY_ID 0xAC01

static struct MsgPort *cx_port;
static CxObj *broker;
static Object *win_obj;
static struct Window *window;
static struct List tabs;
static struct MsgPort *dev_port;
static struct IOStdReq *dev_req;
static struct MsgPort *wifi_port;
static struct IOStdReq *wifi_req;
static ULONG acnet_state;
static struct ACNetworkStatus live_status;
static BOOL have_live_status;
static struct ACNetworkInterface live_interface;
static BOOL have_live_interface;
static struct ACNetworkRoute live_default_route;
static BOOL have_live_route;
static struct ACNetworkSocket live_sockets[6];
static LONG live_socket_count;
static UBYTE live_dns[4];
static BOOL have_live_dns;
static char live_hostname[64];

static char status_network[64];
static char status_card[96];
static char status_bottom[96];
static char status_hostname[80];
static char status_ip[64];
static char status_mask[64];
static char status_gateway[64];
static char status_dns[64];
static char status_link[64];
static char status_interface[64];
static char status_sockets[64];
static char status_in[64];
static char status_out[64];
static char status_connects[64];
static char status_refused[64];
static char status_toggle[32];
static char conn_lines[6][96];
static char log_lines[6][96] = { "No ACNet events yet.", "", "", "", "", "" };
static ULONG live_log_seq;
static char wifi_lines[6][96] = { "Press Rescan to list host-visible Wi-Fi networks.", "", "", "", "", "" };
static char wifi_result[96] = "Wi-Fi control follows the Cradle Host Wi-Fi control switch.";
static char wifi_ssid[33] = "";
static Object *wifi_ssid_obj;
static Object *diag_name_obj;
static Object *diag_host_obj;
static Object *diag_port_obj;
static char diag_name[64] = "localhost";
static char diag_host[64] = "example.com";
static char diag_port[8] = "80";
static char diag_result[128] = "Ready.";

static ULONG acn_state_call(struct Device *dev)
{
    register struct Device *a6 __asm("a6") = dev;
    register ULONG d0 __asm("d0");
    __asm volatile ("jsr -60(a6)"
                    : "=r"(d0)
                    : "r"(a6)
                    : "d1", "a0", "a1", "cc", "memory");
    return d0;
}

static LONG acw_control_call(struct Device *dev, struct ACWiFiCall *call)
{
    register struct Device *a6 __asm("a6") = dev;
    register struct ACWiFiCall *a0 __asm("a0") = call;
    register LONG d0 __asm("d0");
    __asm volatile ("jsr -42(%%a6)" : "=r"(d0), "+r"(a0) : "r"(a6)
                    : "d1", "a1", "cc", "memory");
    return d0;
}

static void read_acnet_state(void)
{
    acnet_state = 0;
    dev_port = CreateMsgPort();
    if (!dev_port) return;
    dev_req = (struct IOStdReq *)CreateIORequest(dev_port, sizeof(*dev_req));
    if (!dev_req) return;

    if (OPENSOCKET_OPEN_DEVICE(dev_req) == 0) {
        acnet_state = acn_state_call(dev_req->io_Device);
    }

    wifi_port = CreateMsgPort();
    if (!wifi_port) return;
    wifi_req = (struct IOStdReq *)CreateIORequest(wifi_port, sizeof(*wifi_req));
    if (!wifi_req) return;
    if (OpenDevice(ACWIFI_DEVICE_NAME, 0, (struct IORequest *)wifi_req, 0) != 0) {
        DeleteIORequest((struct IORequest *)wifi_req);
        DeleteMsgPort(wifi_port);
        wifi_req = NULL;
        wifi_port = NULL;
    }
}

static void close_acnet_state(void)
{
    if (wifi_req) {
        if (wifi_req->io_Device) CloseDevice((struct IORequest *)wifi_req);
        DeleteIORequest((struct IORequest *)wifi_req);
        wifi_req = NULL;
    }
    if (wifi_port) {
        DeleteMsgPort(wifi_port);
        wifi_port = NULL;
    }
    if (dev_req) {
        if (dev_req->io_Device) CloseDevice((struct IORequest *)dev_req);
        DeleteIORequest((struct IORequest *)dev_req);
        dev_req = NULL;
    }
    if (dev_port) {
        DeleteMsgPort(dev_port);
        dev_port = NULL;
    }
}

static void ip4_text(const UBYTE a[4], char *out)
{
    sprintf(out, "%u.%u.%u.%u",
            (unsigned)a[0], (unsigned)a[1], (unsigned)a[2], (unsigned)a[3]);
}

static BOOL ip4_is_zero(const UBYTE a[4])
{
    return a[0] == 0 && a[1] == 0 && a[2] == 0 && a[3] == 0;
}

static LONG ac_call2(ULONG command, ULONG arg0, ULONG arg1, APTR rx, ULONG rxmax, ULONG *rxlen)
{
    struct ACNetworkRequest r;
    LONG result;
    if (!ACNetworkBase) return -1;
    memset(&r, 0, sizeof(r));
    r.command = command;
    r.arg[0] = arg0;
    r.arg[1] = arg1;
    r.rx = (UBYTE *)rx;
    r.rxmax = rxmax;
    result = ACNetwork_Call(&r);
    if (rxlen) *rxlen = r.rxlen;
    return result;
}

static LONG ac_call(ULONG command, ULONG arg0, APTR rx, ULONG rxmax, ULONG *rxlen)
{
    return ac_call2(command, arg0, 0, rx, rxmax, rxlen);
}

static void push_log_line(const struct ACNetworkLogEntry *entry)
{
    LONG i;
    const char *level = entry->level == ACNETWORK_LOG_ERROR ? "ERR" :
                        entry->level == ACNETWORK_LOG_WARN ? "WARN" : "INFO";
    if (live_log_seq == 0 && !strcmp(log_lines[0], "No ACNet events yet."))
        for (i = 0; i < 6; ++i) log_lines[i][0] = 0;
    for (i = 0; i < 5; ++i) strcpy(log_lines[i], log_lines[i + 1]);
    sprintf(log_lines[5], "%lu %-4s %s", (unsigned long)entry->sequence, level, entry->text);
    live_log_seq = entry->sequence;
}

static void read_live_log(void)
{
    struct ACNetworkLogEntry rows[32];
    ULONG rxlen = 0;
    LONG n, i, count;
    if (!ACNetworkBase) return;
    n = ac_call2(ACNETWORK_CMD_LOG, live_log_seq, 32, rows, sizeof(rows), &rxlen);
    if (n <= 0) return;
    count = n;
    if ((ULONG)count > rxlen / sizeof(rows[0])) count = rxlen / sizeof(rows[0]);
    for (i = 0; i < count; ++i) push_log_line(&rows[i]);
}

static void read_live_data(void)
{
    struct ACNetworkInterface ifs[16];
    struct ACNetworkRoute routes[32];
    UBYTE dns[16];
    LONG n, i;
    ULONG rxlen = 0;

    have_live_status = have_live_interface = have_live_route = have_live_dns = FALSE;
    live_socket_count = 0;
    memset(&live_status, 0, sizeof(live_status));
    memset(&live_interface, 0, sizeof(live_interface));
    memset(&live_default_route, 0, sizeof(live_default_route));
    memset(live_dns, 0, sizeof(live_dns));
    live_hostname[0] = 0;

    if (!ACNetworkBase) return;

    if (ac_call(ACNETWORK_CMD_STATUS, 0, &live_status, sizeof(live_status), &rxlen) >= 0 &&
        rxlen >= sizeof(live_status))
        have_live_status = TRUE;

    rxlen = 0;
    if (ac_call(ACNETWORK_CMD_HOSTNAME, 0, live_hostname, sizeof(live_hostname) - 1, &rxlen) >= 0) {
        ULONG end = rxlen < sizeof(live_hostname) ? rxlen : sizeof(live_hostname) - 1;
        live_hostname[end] = 0;
    }

    n = ac_call(ACNETWORK_CMD_INTERFACES, 16, ifs, sizeof(ifs), NULL);
    if (n > 0) {
        live_interface = ifs[0];
        have_live_interface = TRUE;
        for (i = 0; i < n; ++i) {
            if (!ip4_is_zero(ifs[i].ipv4) && ifs[i].ipv4[0] != 127) {
                live_interface = ifs[i];
                break;
            }
        }
    }

    n = ac_call(ACNETWORK_CMD_ROUTES, 32, routes, sizeof(routes), NULL);
    for (i = 0; i < n; ++i) {
        if (ip4_is_zero(routes[i].destination) && ip4_is_zero(routes[i].netmask)) {
            live_default_route = routes[i];
            have_live_route = TRUE;
            break;
        }
    }

    n = ac_call(ACNETWORK_CMD_DNS_SERVERS, 4, dns, sizeof(dns), NULL);
    if (n > 0) {
        CopyMem(dns, live_dns, 4);
        have_live_dns = TRUE;
    }

    n = ac_call(ACNETWORK_CMD_SOCKETS, 6, live_sockets, sizeof(live_sockets), NULL);
    if (n > 0) live_socket_count = n > 6 ? 6 : n;

    read_live_log();
}

static const char *socket_state_name(ULONG state)
{
    switch (state) {
        case ACNETWORK_SOCKET_BOUND: return "BOUND";
        case ACNETWORK_SOCKET_LISTEN: return "LISTEN";
        case ACNETWORK_SOCKET_CONNECTED: return "ESTABLISHED";
        default: return "OPEN";
    }
}

static void make_connection_strings(void)
{
    LONG i;
    for (i = 0; i < 6; ++i) strcpy(conn_lines[i], i == 0 ? "No open sockets." : "");
    for (i = 0; i < live_socket_count && i < 6; ++i) {
        char lip[24], rip[24];
        const char *proto = live_sockets[i].protocol == 6 ? "TCP" :
                            (live_sockets[i].protocol == 17 ? "UDP" : "RAW");
        ip4_text(live_sockets[i].local_ipv4, lip);
        ip4_text(live_sockets[i].remote_ipv4, rip);
        sprintf(conn_lines[i], "%s %s:%u -> %s:%u  %s",
                proto, lip, (unsigned)live_sockets[i].local_port,
                rip, (unsigned)live_sockets[i].remote_port,
                socket_state_name(live_sockets[i].state));
    }
}

static void make_status_strings(void)
{
    ULONG state = have_live_status ? live_status.state : acnet_state;
    char ip[24], mask[24], gateway[24], dns[24];

    if (!(state & ACNETWORK_STATE_CARD)) {
        strcpy(status_network, "Network       No ACNet card");
        strcpy(status_card,    "Card          Not present");
        strcpy(status_bottom,  "No ACNet card - enable Network in Cradle and reboot");
    } else if (!(state & ACNETWORK_STATE_ONLINE)) {
        strcpy(status_network, "Network       Offline");
        strcpy(status_card,    "Card          ACNet - AmiCradle Team product 6 - HostSocket");
        strcpy(status_bottom,  "ACNet is offline - use Go online or enable Network in Cradle");
    } else {
        strcpy(status_network, "Network       Online");
        strcpy(status_card,    "Card          ACNet - AmiCradle Team product 6 - HostSocket");
        strcpy(status_bottom,  "Live ACNet status - Refresh updates counters and connections");
    }

    sprintf(status_hostname, "Hostname      %s", live_hostname[0] ? live_hostname : "(unavailable)");
    if (have_live_interface) {
        ip4_text(live_interface.ipv4, ip);
        ip4_text(live_interface.netmask, mask);
        sprintf(status_ip, "IP Address    %s", ip);
        sprintf(status_mask, "Subnet Mask   %s", mask);
        sprintf(status_interface, "Interface     %s", live_interface.ifname);
    } else {
        strcpy(status_ip, "IP Address    unavailable");
        strcpy(status_mask, "Subnet Mask   unavailable");
        strcpy(status_interface, "Interface     unavailable");
    }

    if (have_live_route) {
        ip4_text(live_default_route.gateway, gateway);
        sprintf(status_gateway, "Gateway       %s", gateway);
    } else {
        strcpy(status_gateway, "Gateway       unavailable");
    }

    if (have_live_dns) {
        ip4_text(live_dns, dns);
        sprintf(status_dns, "DNS Server    %s", dns);
    } else {
        strcpy(status_dns, "DNS Server    unavailable");
    }

    if (have_live_status) {
        sprintf(status_link, "Host link     %u Mb/s", (unsigned)live_status.link_mbps);
        sprintf(status_sockets, "Open Sockets  %u", (unsigned)live_status.sockets);
        sprintf(status_in, "Total In      %llu bytes", (unsigned long long)live_status.bytes_in);
        sprintf(status_out, "Total Out     %llu bytes", (unsigned long long)live_status.bytes_out);
        sprintf(status_connects, "Connects      %u", (unsigned)live_status.connects);
        sprintf(status_refused, "Refused       %u", (unsigned)live_status.refused);
    } else {
        strcpy(status_link, "Host link     unavailable");
        strcpy(status_sockets, "Open Sockets  unavailable");
        strcpy(status_in, "Total In      unavailable");
        strcpy(status_out, "Total Out     unavailable");
        strcpy(status_connects, "Connects      unavailable");
        strcpy(status_refused, "Refused       unavailable");
    }

    strcpy(status_toggle, (state & ACNETWORK_STATE_ONLINE) ? "Go offline" : "Go online");
    make_connection_strings();
}

static BOOL make_tabs(void)
{
    static STRPTR names[] = {
        "Status", "Wi-Fi", "Connections", "Diagnostics", "Log", NULL
    };
    struct Node *node;
    LONG i;

    NewList(&tabs);
    for (i = 0; names[i]; ++i) {
        node = (struct Node *)AllocClickTabNode(
            TNA_Text, names[i],
            TNA_Number, i,
            TNA_Enabled, TRUE,
            TNA_Spacing, 6,
            TAG_DONE);
        if (!node) return FALSE;
        AddTail(&tabs, node);
    }
    return TRUE;
}

static void free_tabs(void)
{
    struct Node *node;
    while ((node = RemHead(&tabs)) != NULL) FreeClickTabNode(node);
}

#define LINE(txt)     LAYOUT_AddImage, LabelObject, LABEL_Text, (ULONG)(txt), LabelEnd,     CHILD_WeightedHeight, 0

static Object *status_page(void)
{
    return HGroupObject,
        LAYOUT_SpaceOuter, TRUE,
        LAYOUT_SpaceInner, TRUE,

        LAYOUT_AddChild, VGroupObject,
            LAYOUT_BevelStyle, BVS_GROUP,
            LAYOUT_Label, "Network Status",
            LINE(status_network),
            LINE(status_card),
            LINE("Library       bsdsocket.library 4.x (ACNet)"),
            LINE(status_hostname),
            LINE(status_ip),
            LINE(status_mask),
            LINE(status_gateway),
            LINE(status_dns),
            LAYOUT_AddChild, HGroupObject,
                LAYOUT_AddChild, ButtonObject,
                    GA_ID, GID_REFRESH,
                    GA_Text, "Refresh",
                    GA_RelVerify, TRUE,
                ButtonEnd,
                LAYOUT_AddChild, ButtonObject,
                    GA_ID, GID_GO_OFFLINE,
                    GA_Text, status_toggle,
                    GA_RelVerify, TRUE,
                    GA_Disabled, ACNetworkBase ? FALSE : TRUE,
                ButtonEnd,
            LayoutEnd,
            CHILD_WeightedHeight, 0,
        LayoutEnd,

        LAYOUT_AddChild, VGroupObject,
            LAYOUT_BevelStyle, BVS_GROUP,
            LAYOUT_Label, "Traffic / Host (PC)",
            LINE(status_link),
            LINE(status_interface),
            LINE(status_sockets),
            LINE(status_in),
            LINE(status_out),
            LINE(status_connects),
            LINE(status_refused),
            LINE(""),
            LINE("Counters are per ACNet runtime session."),
        LayoutEnd,
    LayoutEnd;
}

static Object *wifi_page(void)
{
    return VGroupObject,
        LAYOUT_SpaceOuter, TRUE,
        LAYOUT_BevelStyle, BVS_GROUP,
        LAYOUT_Label, "Host Wi-Fi Control",
        LINE(wifi_result),
        LINE(wifi_lines[0]),
        LINE(wifi_lines[1]),
        LINE(wifi_lines[2]),
        LINE(wifi_lines[3]),
        LINE(wifi_lines[4]),
        LINE(wifi_lines[5]),
        LAYOUT_AddChild, ButtonObject,
            GA_ID, GID_WIFI_RESCAN,
            GA_Text, "Rescan",
            GA_RelVerify, TRUE,
            GA_Disabled, wifi_req ? FALSE : TRUE,
        ButtonEnd,
        CHILD_WeightedHeight, 0,
        LAYOUT_AddChild, HGroupObject,
            LINE("Known SSID"),
            LAYOUT_AddChild, wifi_ssid_obj = StringObject,
                STRINGA_TextVal, wifi_ssid,
                STRINGA_MaxChars, sizeof(wifi_ssid) - 1,
                GA_TabCycle, TRUE,
            StringEnd,
            LAYOUT_AddChild, ButtonObject,
                GA_ID, GID_WIFI_JOIN,
                GA_Text, "Join...",
                GA_RelVerify, TRUE,
                GA_Disabled, wifi_req ? FALSE : TRUE,
            ButtonEnd,
            LAYOUT_AddChild, ButtonObject,
                GA_ID, GID_WIFI_FORGET,
                GA_Text, "Forget...",
                GA_RelVerify, TRUE,
                GA_Disabled, wifi_req ? FALSE : TRUE,
            ButtonEnd,
        LayoutEnd,
        CHILD_WeightedHeight, 0,
        LAYOUT_AddChild, ButtonObject,
            GA_ID, GID_WIFI_LEAVE,
            GA_Text, "Disconnect host Wi-Fi...",
            GA_RelVerify, TRUE,
            GA_Disabled, wifi_req ? FALSE : TRUE,
        ButtonEnd,
        CHILD_WeightedHeight, 0,
        LINE("Only host-known Wi-Fi profiles may be joined; no password crosses into the Amiga."),
    LayoutEnd;
}

static Object *connections_page(void)
{
    return VGroupObject,
        LAYOUT_SpaceOuter, TRUE,
        LAYOUT_BevelStyle, BVS_GROUP,
        LAYOUT_Label, "Connections",
        LINE("Open sockets owned by this ACNet instance"),
        LINE("Protocol / local -> remote / state"),
        LINE(conn_lines[0]),
        LINE(conn_lines[1]),
        LINE(conn_lines[2]),
        LINE(conn_lines[3]),
        LINE(conn_lines[4]),
        LINE(conn_lines[5]),
        LINE("Read-only. Use netstat for the complete list."),
    LayoutEnd;
}


static void copy_string_field(Object *obj, char *out, ULONG size)
{
    ULONG value = 0;
    STRPTR text = NULL;
    if (!out || size == 0) return;
    if (obj && GetAttr(STRINGA_TextVal, obj, &value)) text = (STRPTR)value;
    if (!text) text = (STRPTR)"";
    strncpy(out, (char *)text, size - 1);
    out[size - 1] = 0;
}

static const char *wifi_security_name(UBYTE security)
{
    switch (security) {
        case ACWIFI_SECURITY_OPEN: return "open";
        case ACWIFI_SECURITY_WEP: return "WEP";
        case ACWIFI_SECURITY_WPA: return "WPA";
        case ACWIFI_SECURITY_WPA2: return "WPA2";
        case ACWIFI_SECURITY_WPA3: return "WPA3";
        default: return "secured";
    }
}

static void format_wifi_rows(const struct ACWiFiNetwork *rows, LONG count)
{
    LONG i;
    for (i = 0; i < 6; ++i) wifi_lines[i][0] = 0;
    if (count <= 0) {
        strcpy(wifi_lines[0], "No Wi-Fi networks returned.");
        return;
    }
    if (count > 6) count = 6;
    for (i = 0; i < count; ++i) {
        sprintf(wifi_lines[i], "%c %-32s %3u%% ch %-3lu %-5s %s",
                (rows[i].flags & ACWIFI_FLAG_ACTIVE) ? '*' : ' ',
                rows[i].ssid,
                (unsigned)rows[i].signal,
                (unsigned long)rows[i].channel,
                wifi_security_name(rows[i].security),
                (rows[i].flags & ACWIFI_FLAG_KNOWN) ? "known" : "");
    }
}

static LONG run_wifi_call(ULONG command, const char *ssid,
                          struct ACWiFiNetwork *rows, ULONG capacity)
{
    struct ACWiFiCall call;
    if (!wifi_req || !wifi_req->io_Device) {
        strcpy(wifi_result, "acwifi.device is not installed.");
        return -1;
    }
    memset(&call, 0, sizeof(call));
    call.command = command;
    call.ssid = ssid;
    call.networks = rows;
    call.capacity = capacity;
    acw_control_call(wifi_req->io_Device, &call);
    if (call.error == 1)
        strcpy(wifi_result, "Host Wi-Fi control is disabled in Cradle.");
    else if (call.error == 50)
        strcpy(wifi_result, "ACNet is offline in Cradle.");
    else if (call.error == 4)
        strcpy(wifi_result, "Wi-Fi operation interrupted.");
    else if (call.error)
        sprintf(wifi_result, "Wi-Fi operation failed (errno %lu).", (unsigned long)call.error);
    return call.error ? -1 : call.result;
}

static void run_wifi_scan(void)
{
    struct ACWiFiNetwork rows[6];
    LONG count = run_wifi_call(ACWIFI_SCAN, NULL, rows, 6);
    if (count >= 0) {
        format_wifi_rows(rows, count);
        sprintf(wifi_result, "Wi-Fi scan complete: %ld network%s.", (long)count, count == 1 ? "" : "s");
    }
}

static void run_wifi_status(void)
{
    struct ACWiFiNetwork row;
    LONG count = run_wifi_call(ACWIFI_STATUS, NULL, &row, 1);
    if (count > 0) {
        format_wifi_rows(&row, 1);
        sprintf(wifi_result, "Host Wi-Fi is associated with %s.", row.ssid);
    } else if (count == 0) {
        strcpy(wifi_result, "Host Wi-Fi is not currently associated.");
    }
}

static BOOL confirm_wifi_change(const char *text)
{
    struct EasyStruct easy = {
        sizeof(struct EasyStruct), 0,
        (STRPTR)"ACNetControl - Host Wi-Fi",
        (STRPTR)text,
        (STRPTR)"Proceed|Cancel"
    };
    return EasyRequestArgs(window, &easy, NULL, NULL) == 1;
}

static void read_wifi_ssid(void)
{
    copy_string_field(wifi_ssid_obj, wifi_ssid, sizeof(wifi_ssid));
}

static void run_wifi_join(void)
{
    read_wifi_ssid();
    if (!wifi_ssid[0]) { strcpy(wifi_result, "Enter a known SSID first."); return; }
    if (!confirm_wifi_change("Joining this profile changes the host PC's Wi-Fi connection."))
        return;
    if (run_wifi_call(ACWIFI_JOIN, wifi_ssid, NULL, 0) >= 0) {
        sprintf(wifi_result, "Joined host Wi-Fi profile %s.", wifi_ssid);
        run_wifi_status();
    }
}

static void run_wifi_leave(void)
{
    if (!confirm_wifi_change("Disconnecting Wi-Fi changes the host PC's network connection."))
        return;
    if (run_wifi_call(ACWIFI_LEAVE, NULL, NULL, 0) >= 0)
        strcpy(wifi_result, "Host Wi-Fi disconnected.");
}

static void run_wifi_forget(void)
{
    read_wifi_ssid();
    if (!wifi_ssid[0]) { strcpy(wifi_result, "Enter a known SSID first."); return; }
    if (!confirm_wifi_change("Forgetting this profile removes it from the host PC."))
        return;
    if (run_wifi_call(ACWIFI_FORGET, wifi_ssid, NULL, 0) >= 0) {
        sprintf(wifi_result, "Forgot host Wi-Fi profile %s.", wifi_ssid);
        run_wifi_scan();
    }
}

static void read_diag_fields(void)
{
    copy_string_field(diag_name_obj, diag_name, sizeof(diag_name));
    copy_string_field(diag_host_obj, diag_host, sizeof(diag_host));
    copy_string_field(diag_port_obj, diag_port, sizeof(diag_port));
}

static void run_dns_lookup(void)
{
    struct hostent *he;
    in_addr_t addr;
    STRPTR text;
    read_diag_fields();
    if (!SocketBase) {
        strcpy(diag_result, "DNS: bsdsocket.library unavailable");
        return;
    }
    he = gethostbyname((STRPTR)diag_name);
    if (!he || !he->h_addr || he->h_length < 4) {
        sprintf(diag_result, "DNS: %s lookup failed", diag_name);
        return;
    }
    CopyMem(he->h_addr, &addr, 4);
    text = Inet_NtoA(addr);
    sprintf(diag_result, "DNS: %s -> %s", diag_name, text ? (char *)text : "(unknown)");
}

static void run_tcp_test(void)
{
    struct hostent *he;
    struct sockaddr_in sa;
    LONG s, port;
    read_diag_fields();
    port = atol(diag_port);
    if (port < 1 || port > 65535) {
        strcpy(diag_result, "TCP: port must be 1..65535");
        return;
    }
    if (!SocketBase) {
        strcpy(diag_result, "TCP: bsdsocket.library unavailable");
        return;
    }
    he = gethostbyname((STRPTR)diag_host);
    if (!he || !he->h_addr || he->h_length < 4) {
        sprintf(diag_result, "TCP: %s lookup failed", diag_host);
        return;
    }
    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        strcpy(diag_result, "TCP: could not create socket");
        return;
    }
    memset(&sa, 0, sizeof(sa));
    sa.sin_len = sizeof(sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons((UWORD)port);
    CopyMem(he->h_addr, &sa.sin_addr.s_addr, 4);
    if (connect(s, (struct sockaddr *)&sa, sizeof(sa)) == 0)
        sprintf(diag_result, "TCP: %s:%ld connected", diag_host, (long)port);
    else
        sprintf(diag_result, "TCP: %s:%ld failed", diag_host, (long)port);
    CloseSocket(s);
}

static Object *diagnostics_page(void)
{
    return VGroupObject,
        LAYOUT_SpaceOuter, TRUE,
        LAYOUT_BevelStyle, BVS_GROUP,
        LAYOUT_Label, "Diagnostics",
        LINE("Diagnostics use the public bsdsocket.library API."),
        LAYOUT_AddChild, HGroupObject,
            LINE("Name"),
            LAYOUT_AddChild, diag_name_obj = StringObject,
                STRINGA_TextVal, diag_name,
                STRINGA_MaxChars, sizeof(diag_name) - 1,
                GA_TabCycle, TRUE,
            StringEnd,
            LAYOUT_AddChild, ButtonObject,
                GA_ID, GID_DIAG_DNS,
                GA_Text, "Lookup",
                GA_RelVerify, TRUE,
                GA_Disabled, SocketBase ? FALSE : TRUE,
            ButtonEnd,
        LayoutEnd,
        CHILD_WeightedHeight, 0,
        LAYOUT_AddChild, HGroupObject,
            LINE("Host"),
            LAYOUT_AddChild, diag_host_obj = StringObject,
                STRINGA_TextVal, diag_host,
                STRINGA_MaxChars, sizeof(diag_host) - 1,
                GA_TabCycle, TRUE,
            StringEnd,
            LINE("Port"),
            LAYOUT_AddChild, diag_port_obj = StringObject,
                STRINGA_TextVal, diag_port,
                STRINGA_MaxChars, sizeof(diag_port) - 1,
                GA_TabCycle, TRUE,
            StringEnd,
            LAYOUT_AddChild, ButtonObject,
                GA_ID, GID_DIAG_CONNECT,
                GA_Text, "Test TCP",
                GA_RelVerify, TRUE,
                GA_Disabled, SocketBase ? FALSE : TRUE,
            ButtonEnd,
        LayoutEnd,
        CHILD_WeightedHeight, 0,
        LINE(diag_result),
        LAYOUT_AddChild, ButtonObject,
            GA_ID, GID_DIAG_COPY,
            GA_Text, "Save report to RAM:",
            GA_RelVerify, TRUE,
            GA_Disabled, ACNetworkBase ? FALSE : TRUE,
        ButtonEnd,
        CHILD_WeightedHeight, 0,
    LayoutEnd;
}

static Object *log_page(void)
{
    return VGroupObject,
        LAYOUT_SpaceOuter, TRUE,
        LAYOUT_BevelStyle, BVS_GROUP,
        LAYOUT_Label, "ACNet Event Log",
        LINE(log_lines[0]),
        LINE(log_lines[1]),
        LINE(log_lines[2]),
        LINE(log_lines[3]),
        LINE(log_lines[4]),
        LINE(log_lines[5]),
        LAYOUT_AddChild, HGroupObject,
            LAYOUT_AddChild, ButtonObject,
                GA_ID, GID_REFRESH,
                GA_Text, "Refresh",
                GA_RelVerify, TRUE,
            ButtonEnd,
            LAYOUT_AddChild, ButtonObject,
                GA_ID, GID_LOG_CLEAR,
                GA_Text, "Clear view",
                GA_RelVerify, TRUE,
            ButtonEnd,
            LAYOUT_AddChild, ButtonObject,
                GA_ID, GID_LOG_SAVE,
                GA_Text, "Save RAM:ACNetLog.txt",
                GA_RelVerify, TRUE,
            ButtonEnd,
        LayoutEnd,
        CHILD_WeightedHeight, 0,
    LayoutEnd;
}

static BOOL create_window_object(void)
{
    Object *pages = NULL;

    win_obj = WindowObject,
        WA_Title, "ACNetControl",
        WA_ScreenTitle, "ACNet - AmiCradle networking",
        WA_Activate, TRUE,
        WA_DepthGadget, TRUE,
        WA_DragBar, TRUE,
        WA_CloseGadget, TRUE,
        WA_SizeGadget, TRUE,
        WA_SmartRefresh, TRUE,
        WINDOW_Position, WPOS_CENTERSCREEN,
        WINDOW_ParentGroup, VGroupObject,
            LAYOUT_SpaceOuter, TRUE,
            LAYOUT_DeferLayout, TRUE,

            LAYOUT_AddImage, LabelObject,
                LABEL_Text, "ACNetControl - Network Configuration for AmiCradle",
            LabelEnd,
            CHILD_WeightedHeight, 0,

            LAYOUT_AddChild, ClickTabObject,
                GA_ID, GID_TABS,
                GA_RelVerify, TRUE,
                CLICKTAB_Labels, &tabs,
                CLICKTAB_Current, 0,
                CLICKTAB_PageGroup, pages = PageObject,
                    LAYOUT_DeferLayout, TRUE,
                    PAGE_Add, status_page(),
                    PAGE_Add, wifi_page(),
                    PAGE_Add, connections_page(),
                    PAGE_Add, diagnostics_page(),
                    PAGE_Add, log_page(),
                PageEnd,
            ClickTabEnd,

            LAYOUT_AddImage, LabelObject,
                LABEL_Text, status_bottom,
            LabelEnd,
            CHILD_WeightedHeight, 0,
        LayoutEnd,
    EndWindow;

    return win_obj != NULL;
}

static void hide_window(void)
{
    if (window && win_obj) {
        DoMethod(win_obj, WM_CLOSE);
        window = NULL;
    }
}

static void show_window(void)
{
    if (!window && win_obj) window = (struct Window *)RA_OpenWindow(win_obj);
    if (window) {
        WindowToFront(window);
        ActivateWindow(window);
    }
}

static BOOL set_soft_online(BOOL online)
{
    struct ACNetworkRequest r;
    if (!ACNetworkBase) return FALSE;
    memset(&r, 0, sizeof(r));
    r.command = ACNETWORK_CMD_SET_ONLINE;
    r.arg[0] = online ? 1 : 0;
    return ACNetwork_Call(&r) >= 0;
}

static void save_report(void)
{
    BPTR fh;
    LONG i;
    fh = Open("RAM:ACNetReport.txt", MODE_NEWFILE);
    if (!fh) return;
    FPuts(fh, "ACNetControl report\n");
    FPuts(fh, status_network); FPuts(fh, "\n");
    FPuts(fh, status_card); FPuts(fh, "\n");
    FPuts(fh, status_hostname); FPuts(fh, "\n");
    FPuts(fh, status_ip); FPuts(fh, "\n");
    FPuts(fh, status_mask); FPuts(fh, "\n");
    FPuts(fh, status_gateway); FPuts(fh, "\n");
    FPuts(fh, status_dns); FPuts(fh, "\n");
    FPuts(fh, status_link); FPuts(fh, "\n");
    FPuts(fh, status_interface); FPuts(fh, "\n");
    FPuts(fh, status_sockets); FPuts(fh, "\n");
    FPuts(fh, status_in); FPuts(fh, "\n");
    FPuts(fh, status_out); FPuts(fh, "\n");
    FPuts(fh, status_connects); FPuts(fh, "\n");
    FPuts(fh, status_refused); FPuts(fh, "\n\nConnections:\n");
    for (i = 0; i < 6; ++i) {
        if (conn_lines[i][0]) { FPuts(fh, conn_lines[i]); FPuts(fh, "\n"); }
    }
    FPuts(fh, "\nEvent log:\n");
    for (i = 0; i < 6; ++i) {
        if (log_lines[i][0]) { FPuts(fh, log_lines[i]); FPuts(fh, "\n"); }
    }
    Close(fh);
}

static void save_log(void)
{
    BPTR fh;
    LONG i;
    fh = Open("RAM:ACNetLog.txt", MODE_NEWFILE);
    if (!fh) return;
    FPuts(fh, "ACNet event log view\n");
    for (i = 0; i < 6; ++i) {
        if (log_lines[i][0]) { FPuts(fh, log_lines[i]); FPuts(fh, "\n"); }
    }
    Close(fh);
}

static void clear_log_view(void)
{
    LONG i;
    for (i = 0; i < 6; ++i) log_lines[i][0] = 0;
    strcpy(log_lines[0], "Log view cleared. New events will appear here.");
}

static BOOL rebuild_window(void)
{
    BOOL reopen = window != NULL;
    hide_window();
    if (win_obj) {
        DisposeObject(win_obj);
        win_obj = NULL;
    }
    read_live_data();
    make_status_strings();
    if (!create_window_object()) return FALSE;
    if (reopen) show_window();
    return TRUE;
}

static BOOL open_bases(void)
{
    CxBase       = OpenLibrary("commodities.library", 37);
    WindowBase   = OpenLibrary("window.class", 0);
    LayoutBase   = OpenLibrary("gadgets/layout.gadget", 0);
    ClickTabBase = OpenLibrary("gadgets/clicktab.gadget", 0);
    StringBase   = OpenLibrary("gadgets/string.gadget", 0);
    LabelBase    = OpenLibrary("images/label.image", 0);
    ButtonBase   = OpenLibrary("gadgets/button.gadget", 0);
    ACNetworkBase = OpenLibrary(ACNETWORK_LIBRARY_NAME, ACNETWORK_LIBRARY_VERSION);
    SocketBase    = OpenLibrary("bsdsocket.library", 4);
    return CxBase && WindowBase && LayoutBase && ClickTabBase && StringBase && LabelBase && ButtonBase;
}

static void close_bases(void)
{
    if (SocketBase) CloseLibrary(SocketBase);
    if (ACNetworkBase) CloseLibrary(ACNetworkBase);
    if (ButtonBase)   CloseLibrary(ButtonBase);
    if (LabelBase)    CloseLibrary(LabelBase);
    if (StringBase)   CloseLibrary(StringBase);
    if (ClickTabBase) CloseLibrary(ClickTabBase);
    if (LayoutBase)   CloseLibrary(LayoutBase);
    if (WindowBase)   CloseLibrary(WindowBase);
    if (CxBase)       CloseLibrary(CxBase);
}

static BOOL create_broker(void)
{
    struct NewBroker nb;
    CxObj *hotkey;
    LONG err = 0;

    cx_port = CreateMsgPort();
    if (!cx_port) return FALSE;

    nb.nb_Version = NB_VERSION;
    nb.nb_Name = "ACNetControl";
    nb.nb_Title = "ACNet Network Control";
    nb.nb_Descr = "Status and controls for AmiCradle ACNet";
    nb.nb_Unique = NBU_UNIQUE | NBU_NOTIFY;
    nb.nb_Flags = COF_SHOW_HIDE;
    nb.nb_Pri = 0;
    nb.nb_Port = cx_port;
    nb.nb_ReservedChannel = 0;

    broker = CxBroker(&nb, &err);
    if (!broker) return FALSE;

    hotkey = HotKey("ctrl alt n", cx_port, HOTKEY_ID);
    if (hotkey) AttachCxObj(broker, hotkey);
    ActivateCxObj(broker, 1);
    return TRUE;
}

static void destroy_broker(void)
{
    if (broker) {
        DeleteCxObjAll(broker);
        broker = NULL;
    }
    if (cx_port) {
        struct Message *m;
        while ((m = GetMsg(cx_port)) != NULL) ReplyMsg(m);
        DeleteMsgPort(cx_port);
        cx_port = NULL;
    }
}

static BOOL handle_cx_messages(void)
{
    CxMsg *msg;
    BOOL running = TRUE;

    while ((msg = (CxMsg *)GetMsg(cx_port)) != NULL) {
        ULONG type = CxMsgType(msg);
        ULONG id = CxMsgID(msg);

        if (type == CXM_COMMAND) {
            switch (id) {
                case CXCMD_APPEAR:
                case CXCMD_UNIQUE:
                    show_window();
                    break;
                case CXCMD_DISAPPEAR:
                    hide_window();
                    break;
                case CXCMD_ENABLE:
                    ActivateCxObj(broker, 1);
                    break;
                case CXCMD_DISABLE:
                    ActivateCxObj(broker, 0);
                    break;
                case CXCMD_KILL:
                    running = FALSE;
                    break;
            }
        } else if (id == HOTKEY_ID) {
            show_window();
        }
        ReplyMsg((struct Message *)msg);
    }
    return running;
}

int main(void)
{
    ULONG winsig = 0, sigs;
    BOOL running = TRUE;
    UWORD code = 0;

    if (!open_bases()) {
        PutStr("ACNetControl: required ReAction/Commodity classes are unavailable.\n");
        close_bases();
        return 20;
    }

    read_acnet_state();
    read_live_data();
    make_status_strings();

    if (!make_tabs() || !create_window_object() || !create_broker()) {
        PutStr("ACNetControl: could not initialise.\n");
        destroy_broker();
        if (win_obj) DisposeObject(win_obj);
        free_tabs();
        close_acnet_state();
        close_bases();
        return 20;
    }

    show_window();

    while (running) {
        winsig = 0;
        if (window) GetAttr(WINDOW_SigMask, win_obj, &winsig);
        sigs = Wait(winsig | (1UL << cx_port->mp_SigBit) | SIGBREAKF_CTRL_C);

        if (sigs & SIGBREAKF_CTRL_C) running = FALSE;
        if (sigs & (1UL << cx_port->mp_SigBit)) running = handle_cx_messages();

        if (running && window && (sigs & winsig)) {
            ULONG result;
            BOOL refresh_ui = FALSE;
            while ((result = RA_HandleInput(win_obj, &code)) != WMHI_LASTMSG) {
                switch (result & WMHI_CLASSMASK) {
                    case WMHI_CLOSEWINDOW:
                        hide_window();
                        break;
                    case WMHI_GADGETUP:
                        switch (result & WMHI_GADGETMASK) {
                            case GID_REFRESH:
                                refresh_ui = TRUE;
                                break;
                            case GID_GO_OFFLINE:
                                set_soft_online(!(have_live_status &&
                                                  (live_status.state & ACNETWORK_STATE_ONLINE)));
                                refresh_ui = TRUE;
                                break;
                            case GID_WIFI_RESCAN:
                                run_wifi_scan();
                                refresh_ui = TRUE;
                                break;
                            case GID_WIFI_JOIN:
                                run_wifi_join();
                                refresh_ui = TRUE;
                                break;
                            case GID_WIFI_LEAVE:
                                run_wifi_leave();
                                refresh_ui = TRUE;
                                break;
                            case GID_WIFI_FORGET:
                                run_wifi_forget();
                                refresh_ui = TRUE;
                                break;
                            case GID_DIAG_DNS:
                                run_dns_lookup();
                                refresh_ui = TRUE;
                                break;
                            case GID_DIAG_CONNECT:
                                run_tcp_test();
                                refresh_ui = TRUE;
                                break;
                            case GID_DIAG_COPY:
                                save_report();
                                break;
                            case GID_LOG_CLEAR:
                                clear_log_view();
                                refresh_ui = TRUE;
                                break;
                            case GID_LOG_SAVE:
                                save_log();
                                break;
                        }
                        break;
                }
            }
            if (refresh_ui && !rebuild_window()) running = FALSE;
        }
    }

    hide_window();
    destroy_broker();
    if (win_obj) DisposeObject(win_obj);
    free_tabs();
    close_acnet_state();
    close_bases();
    return 0;
}
