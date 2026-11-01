/* ftp: files to and from an FTP server, in passive mode.
 *   ftp [host [port]]
 * Then: open, user, ls, dir, cd, cdup, pwd, get, put, delete, mkdir, rmdir,
 * rename, size, binary, ascii, quote, close, quit (help lists them).
 * Transfers are binary unless "ascii" is given; ascii turns CR LF into LF
 * coming in and LF into CR LF going out.
 * GFOpenSocket ACTCPTools. MIT. */
#include <exec/types.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <proto/bsdsocket.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include "ostool.h"

static const char version[] __attribute__((used)) = "$VER: ftp 1.0 (4.10.2026) GFOpenSocket";

static LONG ctl = -1;                   /* the control connection */
static struct ost_lines ctl_lines;
static ULONG server_addr;               /* data connections go here, whatever PASV says */
static char server_name[128];
static BOOL binary = TRUE;
static char line[1024], whole[1024], reply_text[1024];
static UBYTE data[8192], conv[16384];

/* ---- the control connection ----------------------------------------------- */

/* The server's reply, all its lines shown: its code, or -1 when the
 * connection is gone. reply_text holds its last line. */
static int reply(void)
{
    int code = -1;
    for (;;) {
        long n = ost_line(&ctl_lines, reply_text, sizeof(reply_text), 60);
        if (n < 0) {
            printf("The connection to %s is gone.\n", server_name);
            CloseSocket(ctl);
            ctl = -1;
            return -1;
        }
        printf("%s\n", reply_text);
        if (n < 3) continue;
        if (code < 0) code = atoi(reply_text);
        if (atoi(reply_text) == code && reply_text[3] == ' ') return code;
    }
}

static int command(const char *fmt, ...)
{
    char buf[600];
    va_list ap;
    if (ctl < 0) { printf("Not connected.\n"); return -1; }
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf) - 2, fmt, ap);
    va_end(ap);
    strcat(buf, "\r\n");
    if (ost_send_str(ctl, buf)) { printf("Could not send to %s.\n", server_name); return -1; }
    return reply();
}

/* A passive data connection: its socket, or -1. */
static LONG passive(void)
{
    unsigned h[4], p[2];
    const char *q;
    char addr[24];
    if (command("PASV") != 227 || !(q = strchr(reply_text, '(')) ||
        sscanf(q + 1, "%u,%u,%u,%u,%u,%u", &h[0], &h[1], &h[2], &h[3], &p[0], &p[1]) != 6) {
        printf("The server would not open a passive connection.\n");
        return -1;
    }
    strcpy(addr, (char *)Inet_NtoA(server_addr));   /* not its own idea of its address: NAT */
    return ost_connect("ftp", addr, (p[0] & 255) * 256 + (p[1] & 255), 20);
}

/* ---- transfers ----------------------------------------------------------------- */

static void rate(long long bytes, struct DateStamp *t0)
{
    struct DateStamp t1;
    long ticks;
    DateStamp(&t1);
    ticks = (t1.ds_Days - t0->ds_Days) * 24 * 60 * 3000 + (t1.ds_Minute - t0->ds_Minute) * 3000 + (t1.ds_Tick - t0->ds_Tick);
    if (ticks < 1) ticks = 1;
    printf("%lld bytes in %ld.%02ld s (%lld KB/s)\n", bytes, ticks / 50, (ticks % 50) * 2, bytes * 50 / ticks / 1024);
}

/* RETR, LIST or NLST into `to`; text: CR LF becomes LF. */
static int fetch(const char *cmd, const char *arg, BPTR to, BOOL text)
{
    struct DateStamp t0;
    long long total = 0;
    LONG d = passive(), n;
    int code;
    BOOL cr = FALSE;
    if (d < 0) return -1;
    code = arg ? command("%s %s", cmd, arg) : command("%s", cmd);
    if (code != 125 && code != 150) { CloseSocket(d); return -1; }
    DateStamp(&t0);
    while ((n = ost_wait_read(d, 60000)) > 0 && (n = recv(d, data, sizeof(data), 0)) > 0) {
        long k = n, i;
        UBYTE *out = data;
        if (text) {
            for (i = k = 0; i < n; ++i) {
                if (cr && data[i] != '\n') conv[k++] = '\r';
                cr = data[i] == '\r';
                if (!cr) conv[k++] = data[i];
            }
            out = conv;
        }
        if (Write(to, out, k) != k) { printf("Writing failed.\n"); break; }
        total += n;
    }
    CloseSocket(d);
    if (n < 0) printf("%s\n", ost_break() ? "Stopped." : "The transfer broke off.");
    code = reply();
    if (code == 226 && strcmp(cmd, "LIST") && strcmp(cmd, "NLST")) rate(total, &t0);
    return code == 226 || code == 250 ? 0 : -1;
}

static int send_file(const char *local, const char *remote)
{
    struct DateStamp t0;
    long long total = 0;
    BPTR fh = Open((STRPTR)local, MODE_OLDFILE);
    LONG d, n;
    int code, ok = 1;
    if (!fh) { printf("Cannot read %s.\n", local); return -1; }
    if ((d = passive()) < 0) { Close(fh); return -1; }
    code = command("STOR %s", remote);
    if (code != 125 && code != 150) { CloseSocket(d); Close(fh); return -1; }
    DateStamp(&t0);
    while ((n = Read(fh, data, sizeof(data))) > 0) {
        long k = n, i;
        UBYTE *out = data;
        if (!binary) {
            for (i = k = 0; i < n; ++i) {
                if (data[i] == '\n') conv[k++] = '\r';
                conv[k++] = data[i];
            }
            out = conv;
        }
        if (ost_send_all(d, out, k)) { printf("Sending failed.\n"); ok = 0; break; }
        total += n;
        if (ost_break()) { printf("Stopped.\n"); ok = 0; break; }
    }
    Close(fh);
    CloseSocket(d);
    code = reply();
    if (code == 226 && ok) rate(total, &t0);
    return code == 226 && ok ? 0 : -1;
}

/* ---- logging in ---------------------------------------------------------------- */

static void ask(const char *prompt, char *out, int max, BOOL hidden)
{
    BPTR in = Input();
    int n = 0;
    printf("%s", prompt);
    fflush(stdout);
    if (!hidden || !IsInteractive(in)) {
        if (!fgets(out, max, stdin)) out[0] = 0;
        out[strcspn(out, "\r\n")] = 0;
        return;
    }
    SetMode(in, 1);
    for (;;) {
        char c;
        if (Read(in, &c, 1) != 1 || c == '\r' || c == '\n') break;
        if ((c == 8 || c == 127) && n) n--;
        else if ((UBYTE)c >= 32 && n < max - 1) out[n++] = c;
    }
    out[n] = 0;
    SetMode(in, 0);
    printf("\n");
}

static void login(const char *name)
{
    char user[64], pass[64];
    int code;
    if (name) snprintf(user, sizeof(user), "%s", name);
    else {
        char prompt[200];
        snprintf(prompt, sizeof(prompt), "Name (%s:anonymous): ", server_name);
        ask(prompt, user, sizeof(user), FALSE);
        if (!user[0]) strcpy(user, "anonymous");
    }
    code = command("USER %s", user);
    if (code == 331) {
        BOOL anon = !strcasecmp(user, "anonymous") || !strcasecmp(user, "ftp");
        ask(anon ? "Password (your e-mail address, or Return): " : "Password: ", pass, sizeof(pass), !anon);
        if (anon && !pass[0]) strcpy(pass, "amiga@opensocket");
        code = command("PASS %s", pass);
        memset(pass, 0, sizeof(pass));
    }
    if (code == 230) {
        if (command("TYPE I") == 200) binary = TRUE;
    } else {
        printf("Not logged in.\n");
    }
}

static void do_close(void)
{
    if (ctl >= 0) {
        command("QUIT");
        if (ctl >= 0) CloseSocket(ctl);
    }
    ctl = -1;
}

static void do_open(const char *host, unsigned port)
{
    do_close();
    if (!ost_resolve("ftp", host, &server_addr)) return;
    if ((ctl = ost_connect("ftp", host, port, 20)) < 0) return;
    snprintf(server_name, sizeof(server_name), "%s", host);
    ost_lines_init(&ctl_lines, ctl);
    if (reply() != 220) { do_close(); return; }
    login(NULL);
}

/* ---- the command line ------------------------------------------------------------ */

/* Splits line into words; "quoted words" keep their spaces. */
static int words(char *p, char **w, int max)
{
    int n = 0;
    while (*p && n < max) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        if (*p == '"') {
            w[n++] = ++p;
            while (*p && *p != '"') p++;
        } else {
            w[n++] = p;
            while (*p && *p != ' ' && *p != '\t') p++;
        }
        if (*p) *p++ = 0;
    }
    return n;
}

static void help(void)
{
    printf("open host [port]   connect           user [name]        log in again\n"
           "ls [path]          names             dir [path]         full listing\n"
           "cd path            change folder     cdup               up one\n"
           "pwd                where am I        size name          a file's size\n"
           "get remote [local] fetch a file      put local [remote] send a file\n"
           "delete name        remove a file     rename from to     rename\n"
           "mkdir name         make a folder     rmdir name         remove a folder\n"
           "binary / ascii     transfer type     quote text         send a raw command\n"
           "close              disconnect        quit               leave ftp\n");
}

static int ftp_main(int argc, char **argv)
{
    char *w[4];
    int n;

    if (argc > 3 || (argc > 1 && (argv[1][0] == '-' || argv[1][0] == '?'))) {
        fprintf(stderr, "Usage: ftp [host [port]]\n");
        return 20;
    }
    if (!ost_open("ftp")) return 20;
    if (argc > 1) do_open(argv[1], argc > 2 ? (unsigned)atol(argv[2]) : 21);

    for (;;) {
        printf("ftp> ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) break;
        line[strcspn(line, "\r\n")] = 0;
        strcpy(whole, line);                        /* words() cuts line up */
        n = words(line, w, 4);
        if (!n) continue;
        if (!strcasecmp(w[0], "quit") || !strcasecmp(w[0], "bye") || !strcasecmp(w[0], "exit")) break;
        else if (!strcasecmp(w[0], "help") || !strcmp(w[0], "?")) help();
        else if (!strcasecmp(w[0], "open")) {
            if (n < 2) printf("open host [port]\n");
            else do_open(w[1], n > 2 ? (unsigned)atol(w[2]) : 21);
        }
        else if (!strcasecmp(w[0], "close")) do_close();
        else if (ctl < 0) printf("Not connected: open host first.\n");
        else if (!strcasecmp(w[0], "user")) login(n > 1 ? w[1] : NULL);
        else if (!strcasecmp(w[0], "ls")) fetch("NLST", n > 1 ? w[1] : NULL, Output(), TRUE);
        else if (!strcasecmp(w[0], "dir")) fetch("LIST", n > 1 ? w[1] : NULL, Output(), TRUE);
        else if (!strcasecmp(w[0], "cd") && n > 1) command("CWD %s", w[1]);
        else if (!strcasecmp(w[0], "cdup")) command("CDUP");
        else if (!strcasecmp(w[0], "pwd")) command("PWD");
        else if (!strcasecmp(w[0], "size") && n > 1) command("SIZE %s", w[1]);
        else if (!strcasecmp(w[0], "delete") && n > 1) command("DELE %s", w[1]);
        else if (!strcasecmp(w[0], "mkdir") && n > 1) command("MKD %s", w[1]);
        else if (!strcasecmp(w[0], "rmdir") && n > 1) command("RMD %s", w[1]);
        else if (!strcasecmp(w[0], "rename") && n > 2) {
            if (command("RNFR %s", w[1]) == 350) command("RNTO %s", w[2]);
        }
        else if (!strcasecmp(w[0], "binary")) { if (command("TYPE I") == 200) binary = TRUE; }
        else if (!strcasecmp(w[0], "ascii")) { if (command("TYPE A") == 200) binary = FALSE; }
        else if (!strcasecmp(w[0], "quote") && n > 1) {
            char *raw = whole + strspn(whole, " \t");   /* everything after the word quote */
            raw += strcspn(raw, " \t");
            command("%s", raw + strspn(raw, " \t"));
        }
        else if (!strcasecmp(w[0], "get") && n > 1) {
            const char *local = n > 2 ? w[2] : (const char *)FilePart((STRPTR)w[1]);
            struct ost_save sv;                     /* a failed fetch keeps the old file */
            if (!ost_save_open(&sv, local)) printf("Cannot write %s.\n", local);
            else ost_save_close(&sv, fetch("RETR", w[1], sv.fh, !binary) == 0, "ftp");
        }
        else if (!strcasecmp(w[0], "put") && n > 1)
            send_file(w[1], n > 2 ? w[2] : (const char *)FilePart((STRPTR)w[1]));
        else printf("?  (help lists the commands)\n");
    }
    do_close();
    ost_close();
    return 0;
}

int main(int argc, char **argv)
{
    return ost_main(ftp_main, argc, argv);
}
