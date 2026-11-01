/* tftp: a file to or from a TFTP server (binary, 512-byte blocks).
 *   tftp host get remote-file [local-file]
 *   tftp host put local-file [remote-file]
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
#include <string.h>

#include "ostool.h"

static const char version[] __attribute__((used)) = "$VER: tftp 1.0 (4.10.2026) GFOpenSocket";

enum { RRQ = 1, WRQ, DATA, ACK, ERROR };
#define BLOCK   512
#define TRIES   5
#define WAIT_MS 3000

static LONG s = -1;
static struct sockaddr_in server, peer;     /* peer: the server's transfer port, once it answers */
static BOOL have_peer;
static UBYTE out[4 + BLOCK], in[4 + BLOCK + 64];

static UWORD get16(const UBYTE *p) { return (UWORD)(p[0] << 8 | p[1]); }
static void put16(UBYTE *p, UWORD v) { p[0] = v >> 8; p[1] = v; }

static int send_out(long len)
{
    struct sockaddr_in *to = have_peer ? &peer : &server;
    return sendto(s, out, len, 0, (struct sockaddr *)to, sizeof(*to)) == len ? 0 : -1;
}

/* Sends out[] and waits for a packet of `want` for `block`; retries. Its
 * length, or -1 (a message has been printed). */
static long exchange(long len, UWORD want, UWORD block)
{
    int tries;
    for (tries = 0; tries < TRIES; ++tries) {
        if (send_out(len)) { fprintf(stderr, "tftp: could not send (error %ld)\n", (long)Errno()); return -1; }
        for (;;) {
            struct sockaddr_in from;
            socklen_t fl = sizeof(from);
            long n;
            int w = ost_wait_read(s, WAIT_MS);
            if (w < 0) { fprintf(stderr, "tftp: stopped\n"); return -1; }
            if (w == 0) break;                                  /* send again */
            n = recvfrom(s, in, sizeof(in), 0, (struct sockaddr *)&from, &fl);
            if (n < 4 || from.sin_addr.s_addr != server.sin_addr.s_addr) continue;
            if (have_peer && from.sin_port != peer.sin_port) continue;     /* someone else's */
            if (get16(in) == ERROR) {
                in[n < (long)sizeof(in) ? n : (long)sizeof(in) - 1] = 0;
                fprintf(stderr, "tftp: the server says: %s\n", (char *)in + 4);
                return -1;
            }
            if (get16(in) != want || get16(in + 2) != block) continue;     /* a duplicate */
            if (!have_peer) { peer = from; have_peer = TRUE; }
            return n;
        }
    }
    fprintf(stderr, "tftp: the server stopped answering\n");
    return -1;
}

static long request(UWORD op, const char *file)
{
    long len = 2;
    put16(out, op);
    strcpy((char *)out + len, file); len += strlen(file) + 1;
    strcpy((char *)out + len, "octet"); len += 6;
    return len;
}

static int get(const char *remote, const char *local)
{
    struct ost_save sv;                     /* a failed fetch keeps the old file */
    BPTR fh;
    UWORD block = 1;
    long len, n, total = 0;
    if (strlen(remote) > BLOCK - 10) { fprintf(stderr, "tftp: the name is too long\n"); return 10; }
    if (!(fh = ost_save_open(&sv, local))) { fprintf(stderr, "tftp: cannot write %s\n", local); return 10; }
    len = request(RRQ, remote);
    for (;;) {
        n = exchange(len, DATA, block);
        if (n < 0) { ost_save_close(&sv, 0, "tftp"); return 10; }
        if (n > 4 && Write(fh, in + 4, n - 4) != n - 4) {
            fprintf(stderr, "tftp: writing %s failed\n", local);
            ost_save_close(&sv, 0, "tftp"); return 10;
        }
        total += n - 4;
        put16(out, ACK); put16(out + 2, block); len = 4;
        if (n - 4 < BLOCK) { send_out(len); break; }        /* the last block: acknowledge it once */
        block++;
    }
    if (ost_save_close(&sv, 1, "tftp")) return 10;
    printf("Received %s: %ld bytes\n", local, total);
    return 0;
}

static int put(const char *local, const char *remote)
{
    BPTR fh;
    UWORD block = 0;
    long len, n = BLOCK, total = 0;                         /* n: the bytes in the block just sent */
    if (strlen(remote) > BLOCK - 10) { fprintf(stderr, "tftp: the name is too long\n"); return 10; }
    if (!(fh = Open((STRPTR)local, MODE_OLDFILE))) { fprintf(stderr, "tftp: cannot read %s\n", local); return 10; }
    len = request(WRQ, remote);
    for (;;) {
        if (exchange(len, ACK, block) < 0) { Close(fh); return 10; }
        if (block && n < BLOCK) break;                      /* the last block was taken */
        n = Read(fh, out + 4, BLOCK);
        if (n < 0) { fprintf(stderr, "tftp: reading %s failed\n", local); Close(fh); return 10; }
        block++;
        put16(out, DATA); put16(out + 2, block); len = 4 + n;
        total += n;
    }
    Close(fh);
    printf("Sent %s: %ld bytes\n", local, total);
    return 0;
}

static int tftp_main(int argc, char **argv)
{
    ULONG addr;
    int rc = 10;
    const char *mode = argc > 2 ? argv[2] : "";
    if (argc < 4 || argc > 5 || (strcasecmp(mode, "get") && strcasecmp(mode, "put"))) {
        fprintf(stderr, "Usage: tftp host get remote-file [local-file]\n"
                        "       tftp host put local-file [remote-file]\n");
        return 20;
    }
    if (!ost_open("tftp")) return 20;
    if (ost_resolve("tftp", argv[1], &addr) && (s = socket(AF_INET, SOCK_DGRAM, 0)) >= 0) {
        memset(&server, 0, sizeof(server));
        server.sin_len = sizeof(server);
        server.sin_family = AF_INET;
        server.sin_port = htons(69);
        server.sin_addr.s_addr = addr;
        if (!strcasecmp(mode, "get")) rc = get(argv[3], argc > 4 ? argv[4] : (const char *)FilePart((STRPTR)argv[3]));
        else rc = put(argv[3], argc > 4 ? argv[4] : (const char *)FilePart((STRPTR)argv[3]));
        CloseSocket(s);
    }
    ost_close();
    return rc;
}

int main(int argc, char **argv)
{
    return ost_main(tftp_main, argc, argv);
}
