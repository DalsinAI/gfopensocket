/*
 * GFOpenSocketControl's core (acnetcontrol_core.h): the live pages. What the
 * retired ReAction ACNetControl did, without the toolkit: status, traffic,
 * connections, the event log, the PC's Wi-Fi, and the diagnostics.
 *
 * gfopensocket.library gives the status, interfaces, routes, DNS servers,
 * sockets and log; gfopensocketwifi.device the Wi-Fi; bsdsocket.library the
 * diagnostics. Each is opened only when there, so the Commodity also runs
 * on an Amiga without the card and says so.
 *
 * MIT.
 */
#include <exec/types.h>
#include <exec/io.h>
#include <exec/memory.h>
#include <exec/tasks.h>
#include <exec/lists.h>
#include <exec/nodes.h>
#include <dos/dos.h>
#include <libraries/commodities.h>
#include <libraries/bsdsocket.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/filio.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/commodities.h>
#include <proto/bsdsocket.h>
#include <clib/alib_protos.h>

#include <acnetwork.h>
#include <proto/acnetwork.h>
#include <acwifi_device.h>
#include "../include/acnet_device.h"
#include "acnetcontrol_core.h"

/* Strong definitions: ours win over libnix's auto-open stubs (-fno-common). */
struct Library *CxBase = NULL;
struct Library *ACNetworkBase = NULL;
struct Library *SocketBase = NULL;

#define HOTKEY_ID 0xAC01
#define LINE 96

/* ---- the text the pages show ---------------------------------------------- */

static char st_network[LINE], st_card[LINE], st_host[LINE], st_iface[LINE], st_ip[LINE],
            st_mask[LINE], st_gateway[LINE], st_dns[LINE];
static char tr_link[LINE], tr_sockets[LINE], tr_in[LINE], tr_out[LINE], tr_connects[LINE],
            tr_refused[LINE];
static char toggle_label[24] = "_Go offline...";
static char wifi_result[LINE] = "Rescan lists the networks Cradle can see.";
static char diag_result[LINE] = "Ready.";
static char status_bar[LINE];

static char wifi_ssid[33] = "";
static char diag_name[64] = "aminet.net";
static char diag_host[64] = "aminet.net";
static char diag_port[8] = "80";

/* A list's rows: nodes over a ring of text lines. */
#define CONN_ROWS 32
#define LOG_ROWS  64
#define WIFI_ROWS 16

struct Rows {
    struct List list;
    struct Node *node;
    char (*text)[LINE];
    int max, count;
};
static struct Node conn_node[CONN_ROWS], log_node[LOG_ROWS], wifi_node[WIFI_ROWS];
static char conn_text[CONN_ROWS][LINE], log_text[LOG_ROWS][LINE], wifi_text[WIFI_ROWS][LINE];
static struct Rows conn_rows = { { 0 }, conn_node, conn_text, CONN_ROWS, 0 };
static struct Rows log_rows = { { 0 }, log_node, log_text, LOG_ROWS, 0 };
static struct Rows wifi_rows = { { 0 }, wifi_node, wifi_text, WIFI_ROWS, 0 };
static char wifi_row_ssid[WIFI_ROWS][33];

const ACNCPage acnc_page[ACNC_PAGES] = {
    { "Status", 2, {
        { "Network",
          { st_network, st_card, "Library       bsdsocket.library (GFOpenSocket)",
            st_host, st_iface, st_ip, st_mask, st_gateway, st_dns, NULL },
          { { 0 } }, 0, 0, 0,
          { { GID_REFRESH, "_Refresh", FALSE }, { GID_GO_OFFLINE, toggle_label, FALSE }, { 0 } }, TRUE },
        { "Traffic",
          { tr_link, tr_sockets, tr_in, tr_out, tr_connects, tr_refused, NULL },
          { { 0 } }, 0, 0, 0, { { 0 } }, FALSE } } },
    { "Wi-Fi", 1, {
        { "Cradle's Wi-Fi",
          { "Joins only networks Cradle already knows;",
            "passphrases never pass through the Amiga.",
            "Cradle's Network setting must include Wi-Fi.",
            wifi_result, NULL },
          { { GID_F_WIFI_SSID, "Network", wifi_ssid, sizeof(wifi_ssid), 24 }, { 0 } },
          GID_L_WIFI, 6, 56,
          { { GID_WIFI_RESCAN, "Re_scan", FALSE }, { GID_WIFI_JOIN, "_Join...", FALSE },
            { GID_WIFI_LEAVE, "_Leave...", FALSE }, { GID_WIFI_FORGET, "_Forget...", FALSE } }, TRUE } } },
    { "Connections", 1, {
        { "Open sockets on this Amiga",
          { "Proto  Local                  Remote                 State", NULL },
          { { 0 } }, GID_L_CONN, 8, 64,
          { { GID_REFRESH, "_Refresh", FALSE }, { 0 } }, TRUE } } },
    { "Diagnostics", 1, {
        { "Diagnostics",
          { "These reach outside services only when you ask.", diag_result, NULL },
          { { GID_F_DIAG_NAME, "Name", diag_name, sizeof(diag_name), 28 },
            { GID_F_DIAG_HOST, "Host", diag_host, sizeof(diag_host), 28 },
            { GID_F_DIAG_PORT, "Port", diag_port, sizeof(diag_port), 6 } },
          0, 0, 0,
          { { GID_DIAG_DNS, "Look _up name", FALSE }, { GID_DIAG_CONNECT, "_Test host", FALSE },
            { GID_DIAG_INTERNET, "_Internet?", FALSE }, { GID_DIAG_COPY, "Sa_ve report", FALSE } }, TRUE } } },
    { "Log", 1, {
        { "GFOpenSocket event log",
          { "Online and offline, DNS failures and refusals, newest last.", NULL },
          { { 0 } }, GID_L_LOG, 10, 70,
          { { GID_REFRESH, "_Refresh", FALSE }, { GID_LOG_CLEAR, "_Clear view", FALSE },
            { GID_LOG_SAVE, "_Save log", FALSE }, { 0 } }, TRUE } } },
};

/* ---- lists ------------------------------------------------------------------ */

static void rows_init(struct Rows *r)
{
    NewList(&r->list);
    r->count = 0;
}

static void rows_link(struct Rows *r)
{
    int i;
    NewList(&r->list);
    for (i = 0; i < r->count; ++i) {
        r->node[i].ln_Name = r->text[i];
        AddTail(&r->list, &r->node[i]);
    }
}

/* Adds a row; a full list drops its oldest. */
static void rows_add(struct Rows *r, const char *text)
{
    if (r->count == r->max) {
        memmove(r->text[0], r->text[1], (r->max - 1) * LINE);
        r->count--;
    }
    strncpy(r->text[r->count], text, LINE - 1);
    r->text[r->count][LINE - 1] = 0;
    r->count++;
}

struct List *acnc_list(ULONG list_id)
{
    struct Rows *r = list_id == GID_L_CONN ? &conn_rows : list_id == GID_L_LOG ? &log_rows :
                     list_id == GID_L_WIFI ? &wifi_rows : NULL;
    if (!r) return NULL;
    rows_link(r);
    return &r->list;
}

/* ---- gfopensocket.library ---------------------------------------------------- */

static LONG os_call(ULONG command, ULONG arg0, ULONG arg1, APTR rx, ULONG rxmax, ULONG *rxlen)
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

static void ip4_text(const UBYTE a[4], char *out)
{
    sprintf(out, "%u.%u.%u.%u", (unsigned)a[0], (unsigned)a[1], (unsigned)a[2], (unsigned)a[3]);
}

static BOOL ip4_is_zero(const UBYTE a[4])
{
    return !(a[0] | a[1] | a[2] | a[3]);
}

static ULONG card_state(void)
{
    /* Without gfopensocket.library: ask the device itself, as first light did. */
    struct MsgPort *port = CreateMsgPort();
    struct IOStdReq *req = port ? (struct IOStdReq *)CreateIORequest(port, sizeof(*req)) : NULL;
    ULONG state = 0;
    if (req && OPENSOCKET_OPEN_DEVICE(req) == 0) {
        register struct Device *a6 __asm("a6") = req->io_Device;
        register ULONG d0 __asm("d0");
        __asm volatile ("jsr -60(a6)" : "=r"(d0) : "r"(a6) : "d1", "a0", "a1", "cc", "memory");
        state = d0;
        CloseDevice((struct IORequest *)req);
    }
    if (req) DeleteIORequest((struct IORequest *)req);
    if (port) DeleteMsgPort(port);
    return state;
}

static ULONG log_seq;
static ULONG online_state;

static const char *socket_state_name(ULONG state)
{
    switch (state) {
        case ACNETWORK_SOCKET_BOUND: return "BOUND";
        case ACNETWORK_SOCKET_LISTEN: return "LISTEN";
        case ACNETWORK_SOCKET_CONNECTED: return "ESTABLISHED";
        default: return "OPEN";
    }
}

static void read_log(void)
{
    struct ACNetworkLogEntry rows[16];
    ULONG rxlen = 0;
    LONG n, i;
    for (;;) {
        n = os_call(ACNETWORK_CMD_LOG, log_seq, 16, rows, sizeof(rows), &rxlen);
        if (n <= 0) return;
        if ((ULONG)n > rxlen / sizeof(rows[0])) n = rxlen / sizeof(rows[0]);
        for (i = 0; i < n; ++i) {
            char line[LINE];
            const char *level = rows[i].level == ACNETWORK_LOG_ERROR ? "ERR " :
                                rows[i].level == ACNETWORK_LOG_WARN ? "WARN" : "INFO";
            rows[i].text[sizeof(rows[i].text) - 1] = 0;
            sprintf(line, "%5lu %s %s", (unsigned long)rows[i].sequence, level, rows[i].text);
            rows_add(&log_rows, line);
            log_seq = rows[i].sequence;
        }
        if (n < 16) return;
    }
}

static void read_sockets(void)
{
    struct ACNetworkSocket s[CONN_ROWS];
    LONG n, i;
    conn_rows.count = 0;
    n = os_call(ACNETWORK_CMD_SOCKETS, CONN_ROWS, 0, s, sizeof(s), NULL);
    if (n > CONN_ROWS) n = CONN_ROWS;
    for (i = 0; i < n; ++i) {
        char lip[16], rip[16], line[LINE];
        const char *proto = s[i].protocol == 6 ? "TCP" : s[i].protocol == 17 ? "UDP" : "RAW";
        ip4_text(s[i].local_ipv4, lip);
        ip4_text(s[i].remote_ipv4, rip);
        sprintf(line, "%-5s  %15s:%-5u  %15s:%-5u  %s", proto, lip, (unsigned)s[i].local_port,
                rip, (unsigned)s[i].remote_port, socket_state_name(s[i].state));
        rows_add(&conn_rows, line);
    }
    if (n <= 0) rows_add(&conn_rows, ACNetworkBase ? "No open sockets." : "gfopensocket.library is not installed.");
}

/* Everything the Status page and the status bar say. */
static void read_status(void)
{
    struct ACNetworkStatus st;
    struct ACNetworkInterface ifs[8];
    struct ACNetworkRoute routes[16];
    UBYTE dns[16];
    char host[64], a[16], b[16];
    ULONG rxlen = 0;
    LONG n, i;
    BOOL have_st;

    memset(&st, 0, sizeof(st));
    have_st = os_call(ACNETWORK_CMD_STATUS, 0, 0, &st, sizeof(st), &rxlen) >= 0 && rxlen >= sizeof(st);
    online_state = have_st ? st.state : card_state();

    if (!(online_state & ACNETWORK_STATE_CARD)) {
        strcpy(st_network, "Network       No GFOpenSocket card");
        strcpy(st_card,    "Card          Not present");
        strcpy(status_bar, "No GFOpenSocket card - turn Network on in Cradle and reboot");
    } else if (!(online_state & ACNETWORK_STATE_ONLINE)) {
        strcpy(st_network, "Network       Offline");
        strcpy(st_card,    "Card          GFOpenSocket - AmigaChrome Team product 6 - HostSocket");
        strcpy(status_bar, "Offline - Go online, or turn Network on in Cradle");
    } else {
        strcpy(st_network, "Network       Online");
        strcpy(st_card,    "Card          GFOpenSocket - AmigaChrome Team product 6 - HostSocket");
        strcpy(status_bar, ACNetworkBase ? "Online" : "Online - gfopensocket.library is not installed");
    }
    strcpy(toggle_label, (online_state & ACNETWORK_STATE_ONLINE) ? "_Go offline..." : "_Go online");

    host[0] = 0;
    rxlen = 0;
    if (os_call(ACNETWORK_CMD_HOSTNAME, 0, 0, host, sizeof(host) - 1, &rxlen) >= 0)
        host[rxlen < sizeof(host) ? rxlen : sizeof(host) - 1] = 0;
    sprintf(st_host, "Hostname      %s", host[0] ? host : "unavailable");

    n = os_call(ACNETWORK_CMD_INTERFACES, 8, 0, ifs, sizeof(ifs), NULL);
    for (i = 0; i < n && i < 8; ++i)                 /* the first that is not loopback */
        if (!ip4_is_zero(ifs[i].ipv4) && ifs[i].ipv4[0] != 127) break;
    if (n > 0) {
        if (i >= n || i >= 8) i = 0;
        ifs[i].ifname[sizeof(ifs[i].ifname) - 1] = 0;
        ip4_text(ifs[i].ipv4, a);
        ip4_text(ifs[i].netmask, b);
        sprintf(st_iface, "Interface     %s", ifs[i].ifname);
        sprintf(st_ip,    "IP Address    %s", a);
        sprintf(st_mask,  "Subnet Mask   %s", b);
    } else {
        strcpy(st_iface, "Interface     unavailable");
        strcpy(st_ip,    "IP Address    unavailable");
        strcpy(st_mask,  "Subnet Mask   unavailable");
    }

    strcpy(st_gateway, "Gateway       unavailable");
    n = os_call(ACNETWORK_CMD_ROUTES, 16, 0, routes, sizeof(routes), NULL);
    for (i = 0; i < n && i < 16; ++i)
        if (ip4_is_zero(routes[i].destination) && ip4_is_zero(routes[i].netmask)) {
            ip4_text(routes[i].gateway, a);
            sprintf(st_gateway, "Gateway       %s", a);
            break;
        }

    if (os_call(ACNETWORK_CMD_DNS_SERVERS, 4, 0, dns, sizeof(dns), NULL) > 0) {
        ip4_text(dns, a);
        sprintf(st_dns, "DNS Server    %s", a);
    } else {
        strcpy(st_dns, "DNS Server    unavailable");
    }

    if (have_st) {
        sprintf(tr_link,     "Link speed    %lu Mb/s", (unsigned long)st.link_mbps);
        sprintf(tr_sockets,  "Open sockets  %lu", (unsigned long)st.sockets);
        sprintf(tr_in,       "Total in      %llu bytes", st.bytes_in);
        sprintf(tr_out,      "Total out     %llu bytes", st.bytes_out);
        sprintf(tr_connects, "Connects      %lu", (unsigned long)st.connects);
        sprintf(tr_refused,  "Refused       %lu", (unsigned long)st.refused);
    } else {
        strcpy(tr_link,     "Link speed    unavailable");
        strcpy(tr_sockets,  "Open sockets  unavailable");
        strcpy(tr_in,       "Total in      unavailable");
        strcpy(tr_out,      "Total out     unavailable");
        strcpy(tr_connects, "Connects      unavailable");
        strcpy(tr_refused,  "Refused       unavailable");
    }
}

void acnc_open(void)
{
    rows_init(&conn_rows);
    rows_init(&log_rows);
    rows_init(&wifi_rows);
    ACNetworkBase = OpenLibrary(ACNETWORK_LIBRARY_NAME, ACNETWORK_LIBRARY_VERSION);
    if (!ACNetworkBase) rows_add(&log_rows, "gfopensocket.library is not installed: no event log.");
    acnc_refresh(NULL);
}

void acnc_close(void)
{
    if (ACNetworkBase) CloseLibrary(ACNetworkBase);
    ACNetworkBase = NULL;
}

/* A sum over everything the pages show, to tell whether a refresh changed it. */
static ULONG shown_sum(void)
{
    const char *lines[] = { st_network, st_card, st_host, st_iface, st_ip, st_mask, st_gateway, st_dns,
                            tr_link, tr_sockets, tr_in, tr_out, tr_connects, tr_refused, status_bar, NULL };
    ULONG h = 5381;
    const char *p;
    int i;
    for (i = 0; lines[i]; ++i)
        for (p = lines[i]; *p; ++p) h = h * 33 + (UBYTE)*p;
    for (i = 0; i < conn_rows.count; ++i)
        for (p = conn_rows.text[i]; *p; ++p) h = h * 33 + (UBYTE)*p;
    return h * 33 + log_rows.count * 7 + log_seq;
}

BOOL acnc_refresh(BOOL *state_changed)
{
    ULONG before = shown_sum(), old_state = online_state;
    read_status();
    read_sockets();
    read_log();
    if (state_changed) *state_changed = online_state != old_state;
    return shown_sum() != before;
}

const char *acnc_status_line(void)
{
    return status_bar;
}

/* ---- the PC's Wi-Fi ---------------------------------------------------------- */

static const char *security_name(UBYTE security)
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

static LONG wifi_call(ULONG command, const char *ssid, struct ACWiFiNetwork *rows, ULONG capacity)
{
    struct MsgPort *port = CreateMsgPort();
    struct IOStdReq *req = port ? (struct IOStdReq *)CreateIORequest(port, sizeof(*req)) : NULL;
    struct ACWiFiCall call;
    LONG result = -1;

    if (!req || OpenDevice((STRPTR)ACWIFI_DEVICE_NAME, 0, (struct IORequest *)req, 0)) {
        strcpy(wifi_result, "gfopensocketwifi.device is not installed.");
    } else {
        register struct Device *a6 __asm("a6") = req->io_Device;
        register struct ACWiFiCall *a0 __asm("a0") = &call;
        register LONG d0 __asm("d0");
        memset(&call, 0, sizeof(call));
        call.command = command;
        call.ssid = ssid;
        call.networks = rows;
        call.capacity = capacity;
        __asm volatile ("jsr -42(%%a6)" : "=r"(d0), "+r"(a0) : "r"(a6) : "d1", "a1", "cc", "memory");
        (void)d0;
        if (call.error == 1) strcpy(wifi_result, "Wi-Fi control is off in Cradle.");
        else if (call.error == 50) strcpy(wifi_result, "The network is off in Cradle.");
        else if (call.error == 4) strcpy(wifi_result, "The Wi-Fi request was interrupted.");
        else if (call.error) sprintf(wifi_result, "The Wi-Fi request failed (error %lu).", (unsigned long)call.error);
        else result = call.result;
        CloseDevice((struct IORequest *)req);
    }
    if (req) DeleteIORequest((struct IORequest *)req);
    if (port) DeleteMsgPort(port);
    return result;
}

static void wifi_show(const struct ACWiFiNetwork *n, LONG count)
{
    LONG i;
    wifi_rows.count = 0;
    for (i = 0; i < count && i < WIFI_ROWS; ++i) {
        char line[LINE];
        sprintf(line, "%c %-32.32s %3u%%  ch %-3lu %-5s %s", (n[i].flags & ACWIFI_FLAG_ACTIVE) ? '*' : ' ',
                n[i].ssid, (unsigned)n[i].signal, (unsigned long)n[i].channel,
                security_name(n[i].security), (n[i].flags & ACWIFI_FLAG_KNOWN) ? "known" : "");
        rows_add(&wifi_rows, line);
        /* at most 32 bytes of the device's ssid[33], which needn't end in 0 */
        snprintf(wifi_row_ssid[wifi_rows.count - 1], sizeof wifi_row_ssid[0], "%.32s", n[i].ssid);
    }
}

static void wifi_scan(void)
{
    static struct ACWiFiNetwork n[WIFI_ROWS];
    LONG count = wifi_call(ACWIFI_SCAN, NULL, n, WIFI_ROWS);
    if (count < 0) return;
    wifi_show(n, count);
    sprintf(wifi_result, "%ld network%s. * is the one in use.", (long)count, count == 1 ? "" : "s");
}

BOOL acnc_pick(ULONG list_id, UWORD row)
{
    if (list_id != GID_L_WIFI || row >= wifi_rows.count) return FALSE;
    strcpy(wifi_ssid, wifi_row_ssid[row]);
    return TRUE;
}

/* ---- diagnostics (bsdsocket.library) ----------------------------------------- */

static BOOL open_sockets(char *result, const char *what)
{
    SocketBase = OpenLibrary("bsdsocket.library", 4);
    if (!SocketBase) sprintf(result, "%s: no bsdsocket.library.", what);
    return SocketBase != NULL;
}

static void close_sockets(void)
{
    if (SocketBase) CloseLibrary(SocketBase);
    SocketBase = NULL;
}

static BOOL lookup(const char *name, ULONG *addr, char *result, const char *what)
{
    struct hostent *he = gethostbyname((STRPTR)name);
    if (!he || !he->h_addr || he->h_length < 4) {
        sprintf(result, "%s: %s was not found.", what, name);
        return FALSE;
    }
    CopyMem(he->h_addr, addr, 4);
    return TRUE;
}

static void dns_lookup(void)
{
    ULONG addr;
    if (!diag_name[0]) { strcpy(diag_result, "Look up: type a name first."); return; }
    if (!open_sockets(diag_result, "Look up")) return;
    if (lookup(diag_name, &addr, diag_result, "Look up"))
        sprintf(diag_result, "%s is %s", diag_name, (char *)Inet_NtoA(addr));
    close_sockets();
}

/* A TCP connect, given up after 5 seconds. */
static void tcp_test(const char *host, const char *port_text, const char *what)
{
    struct sockaddr_in sa;
    struct timeval tv;
    fd_set wf;
    ULONG addr, sigs = 0;
    LONG s, port = atol(port_text), on = 1, err = 0, n;
    socklen_t len = sizeof(err);

    if (!host[0]) { sprintf(diag_result, "%s: type a host first.", what); return; }
    if (port < 1 || port > 65535) { sprintf(diag_result, "%s: the port is 1 to 65535.", what); return; }
    if (!open_sockets(diag_result, what)) return;
    if (!lookup(host, &addr, diag_result, what)) { close_sockets(); return; }
    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) { sprintf(diag_result, "%s: no socket (error %ld).", what, (long)Errno()); close_sockets(); return; }
    memset(&sa, 0, sizeof(sa));
    sa.sin_len = sizeof(sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons((UWORD)port);
    sa.sin_addr.s_addr = addr;
    IoctlSocket(s, FIONBIO, (char *)&on);
    if (connect(s, (struct sockaddr *)&sa, sizeof(sa)) < 0 && Errno() != EINPROGRESS) {
        err = Errno();
    } else {
        FD_ZERO(&wf);
        FD_SET(s, &wf);
        tv.tv_sec = 5;
        tv.tv_usec = 0;
        n = WaitSelect(s + 1, NULL, &wf, NULL, &tv, &sigs);
        if (n == 0) err = ETIMEDOUT;
        else if (n < 0) err = Errno();
        else if (getsockopt(s, SOL_SOCKET, SO_ERROR, &err, &len) < 0) err = Errno();
    }
    if (!err) sprintf(diag_result, "%s: %s port %ld answers.", what, host, (long)port);
    else if (err == ETIMEDOUT) sprintf(diag_result, "%s: %s port %ld: no answer in 5 s.", what, host, (long)port);
    else if (err == ECONNREFUSED) sprintf(diag_result, "%s: %s port %ld refused.", what, host, (long)port);
    else sprintf(diag_result, "%s: %s port %ld failed (error %ld).", what, host, (long)port, (long)err);
    CloseSocket(s);
    close_sockets();
}

/* ---- saving -------------------------------------------------------------------- */

static void put(BPTR fh, const char *s)
{
    FPuts(fh, (STRPTR)s);
    FPutC(fh, '\n');
}

static void save_report(void)
{
    const char *path = "RAM:GFOpenSocketReport.txt";
    BPTR fh = Open((STRPTR)path, MODE_NEWFILE);
    int i;
    if (!fh) { sprintf(diag_result, "Could not write %s.", path); return; }
    put(fh, "GFOpenSocketControl report");
    put(fh, "");
    for (i = 0; acnc_page[0].group[0].line[i]; ++i) put(fh, acnc_page[0].group[0].line[i]);
    for (i = 0; acnc_page[0].group[1].line[i]; ++i) put(fh, acnc_page[0].group[1].line[i]);
    put(fh, "");
    put(fh, "Connections:");
    for (i = 0; i < conn_rows.count; ++i) put(fh, conn_rows.text[i]);
    put(fh, "");
    put(fh, "Event log:");
    for (i = 0; i < log_rows.count; ++i) put(fh, log_rows.text[i]);
    Close(fh);
    sprintf(diag_result, "Saved the report as %s.", path);
}

static void save_log(void)
{
    const char *path = "RAM:GFOpenSocketLog.txt";
    BPTR fh = Open((STRPTR)path, MODE_NEWFILE);
    int i;
    if (!fh) { sprintf(status_bar, "Could not write %s", path); return; }
    put(fh, "GFOpenSocket event log");
    for (i = 0; i < log_rows.count; ++i) put(fh, log_rows.text[i]);
    Close(fh);
    sprintf(status_bar, "Saved the log as %s", path);
}

/* ---- the buttons ----------------------------------------------------------------- */

BOOL acnc_press(ULONG id, BOOL (*confirm)(const char *text))
{
    struct ACWiFiNetwork n;
    LONG count;

    switch (id) {
        case GID_REFRESH:
            break;
        case GID_GO_OFFLINE:
            if (!ACNetworkBase) { strcpy(status_bar, "gfopensocket.library is not installed"); return TRUE; }
            if (online_state & ACNETWORK_STATE_ONLINE) {
                if (!confirm("Take this Amiga offline?\nEvery program's connections close.")) return FALSE;
                os_call(ACNETWORK_CMD_SET_ONLINE, 0, 0, NULL, 0, NULL);
            } else {
                os_call(ACNETWORK_CMD_SET_ONLINE, 1, 0, NULL, 0, NULL);
            }
            break;
        case GID_WIFI_RESCAN:
            wifi_scan();
            return TRUE;
        case GID_WIFI_JOIN:
            if (!wifi_ssid[0]) { strcpy(wifi_result, "Pick or type a network first."); return TRUE; }
            if (!confirm("Join this network?\nIt changes Cradle's own Wi-Fi connection.")) return FALSE;
            if (wifi_call(ACWIFI_JOIN, wifi_ssid, NULL, 0) >= 0) {
                count = wifi_call(ACWIFI_STATUS, NULL, &n, 1);
                if (count > 0) sprintf(wifi_result, "Cradle is on %s.", n.ssid);
                else sprintf(wifi_result, "Asked Cradle to join %s.", wifi_ssid);
            }
            return TRUE;
        case GID_WIFI_LEAVE:
            if (!confirm("Disconnect Cradle's Wi-Fi?\nIt changes Cradle's own network connection.")) return FALSE;
            if (wifi_call(ACWIFI_LEAVE, NULL, NULL, 0) >= 0) strcpy(wifi_result, "Cradle's Wi-Fi is disconnected.");
            return TRUE;
        case GID_WIFI_FORGET:
            if (!wifi_ssid[0]) { strcpy(wifi_result, "Pick or type a network first."); return TRUE; }
            if (!confirm("Forget this network?\nCradle removes its saved profile.")) return FALSE;
            if (wifi_call(ACWIFI_FORGET, wifi_ssid, NULL, 0) >= 0) {
                sprintf(wifi_result, "Cradle forgot %s.", wifi_ssid);
                wifi_scan();
            }
            return TRUE;
        case GID_DIAG_DNS:
            dns_lookup();
            return TRUE;
        case GID_DIAG_CONNECT:
            tcp_test(diag_host, diag_port, "Test");
            return TRUE;
        case GID_DIAG_INTERNET:
            tcp_test("aminet.net", "80", "Internet");
            return TRUE;
        case GID_DIAG_COPY:
            acnc_refresh(NULL);
            save_report();
            return TRUE;
        case GID_LOG_CLEAR:
            log_rows.count = 0;
            return TRUE;
        case GID_LOG_SAVE:
            save_log();
            return TRUE;
        default:
            return FALSE;
    }
    acnc_refresh(NULL);
    return TRUE;
}

/* ---- the commodity --------------------------------------------------------------- */

static struct MsgPort *cx_port;
static CxObj *broker;

int acnc_broker_open(void)
{
    struct NewBroker nb;
    CxObj *hotkey;
    LONG err = 0;

    CxBase = OpenLibrary("commodities.library", 37);
    if (!CxBase) return 0;
    cx_port = CreateMsgPort();
    if (!cx_port) return 0;

    memset(&nb, 0, sizeof(nb));
    nb.nb_Version = NB_VERSION;
    nb.nb_Name = "GFOpenSocketControl";
    nb.nb_Title = "GFOpenSocket Network Control";
    nb.nb_Descr = "Status and controls for GFOpenSocket";
    nb.nb_Unique = NBU_UNIQUE | NBU_NOTIFY;
    nb.nb_Flags = COF_SHOW_HIDE;
    nb.nb_Pri = 0;
    nb.nb_Port = cx_port;

    broker = CxBroker(&nb, &err);
    if (!broker) return err == CBERR_DUP ? ACNC_ALREADY_RUNNING : 0;

    hotkey = HotKey(ACNC_HOTKEY, cx_port, HOTKEY_ID);
    if (hotkey) AttachCxObj(broker, hotkey);
    ActivateCxObj(broker, 1);
    return 1;
}

void acnc_broker_close(void)
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
    if (CxBase) {
        CloseLibrary(CxBase);
        CxBase = NULL;
    }
}

ULONG acnc_broker_signal(void)
{
    return broker ? 1UL << cx_port->mp_SigBit : 0;
}

BOOL acnc_broker_handle(void (*show)(void), void (*hide)(void))
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
                    show();
                    break;
                case CXCMD_DISAPPEAR:
                    hide();
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
            show();
        }
        ReplyMsg((struct Message *)msg);
    }
    return running;
}

/* Static, not on the stack: nothing may be read from the old stack's frame
 * between the two StackSwap calls. The body takes no arguments, so nothing
 * is left to pop after the swap back. */
static struct StackSwapStruct swap;
static int (*volatile swap_body)(void);
static volatile int swap_rc;

int acnc_main_with_stack(int (*body)(void), unsigned long bytes)
{
    struct Task *me = FindTask(NULL);
    unsigned long have = (unsigned long)me->tc_SPUpper - (unsigned long)me->tc_SPLower;
    APTR lower;

    if (have >= bytes || !(lower = AllocVec(bytes, MEMF_ANY)))
        return body();
    swap.stk_Lower = lower;
    swap.stk_Upper = (ULONG)lower + bytes;
    swap.stk_Pointer = (APTR)swap.stk_Upper;
    swap_body = body;
    StackSwap(&swap);
    swap_rc = swap_body();
    StackSwap(&swap);
    FreeVec(lower);
    return swap_rc;
}
