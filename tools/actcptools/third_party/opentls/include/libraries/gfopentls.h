#ifndef LIBRARIES_OPENTLS_H
#define LIBRARIES_OPENTLS_H
/*
 * gfopentls.library: a TLS client for AmigaOS 3.x.
 * MIT licensed and free. Copyright (c) 2026 AmiCradle Team.
 *
 * API version 1, frozen 9 October 2026. Later versions only add: new
 * functions at the end of the table, new constants, and new fields at the
 * end of struct OTContextConfig (its Size field says which fields a caller
 * filled in). Nothing here changes meaning.
 *
 * The functions are in <proto/gfopentls.h>; docs/AutoDocs-GFOpenTLS.md
 * describes each one. In short:
 *
 *   ctx  = OT_NewContext(&config, &err);          once per program
 *   conn = OT_NewConnection(ctx, "host.name", &err);
 *   OT_SetSocket(conn, fd, SocketBase);           a connected bsdsocket socket
 *   OT_SetALPN(conn, "http/1.1");                 optional
 *   OT_SetSession(conn, session);                 optional: resume a session
 *   if (OT_Handshake(conn) == OTERR_OK) {
 *       OT_Write(conn, ...); OT_Read(conn, ...);
 *       OT_Close(conn);                           sends close_notify
 *   }
 *   OT_FreeConnection(conn);                      the socket stays open
 *   OT_FreeContext(ctx);
 *
 * FTPS data connections resume the control connection's session:
 *
 *   s = OT_GetSession(control);
 *   OT_SetSession(data, s);
 *   OT_FreeSession(s);
 *   OT_Handshake(data);                           OT_SessionResumed(data) is 1
 */

#include <exec/types.h>
#include <utility/hooks.h>

#define OPENTLSLIB_NAME     "gfopentls.library"
#define OPENTLSLIB_VERSION  1

/* Protocol versions, as on the wire. */
#define OT_TLS10 0x0301
#define OT_TLS11 0x0302
#define OT_TLS12 0x0303
#define OT_TLS13 0x0304

/* Error codes. Every call that answers a LONG answers OTERR_OK (0), a
 * count or length (>= 0), or one of these. OT_ErrorString() names them;
 * OT_GetErrorText() explains the last one on a connection. */
#define OTERR_OK              0
#define OTERR_ARGS          (-1)   /* a NULL pointer, a bad size, a bad mode */
#define OTERR_NOMEM         (-2)
#define OTERR_IO            (-3)   /* the socket or I/O hook failed */
#define OTERR_CLOSED        (-4)   /* the peer closed without close_notify */
#define OTERR_WOULDBLOCK    (-5)   /* non-blocking socket: call again later */
#define OTERR_PROTOCOL      (-6)   /* the peer broke the protocol */
#define OTERR_VERSION       (-7)   /* no protocol version in common */
#define OTERR_CIPHER        (-8)   /* no cipher suite or curve in common */
#define OTERR_ALERT         (-9)   /* the peer sent a fatal alert (OT_GetErrorDetail) */
#define OTERR_UNTRUSTED     (-10)  /* the chain does not end at a trusted CA */
#define OTERR_HOSTNAME      (-11)  /* the certificate is not for this host */
#define OTERR_EXPIRED       (-12)  /* the certificate is expired or not yet valid */
#define OTERR_BADCERT       (-13)  /* malformed, bad signature or unsupported key */
#define OTERR_TRUSTSTORE    (-14)  /* no trust store could be read */
#define OTERR_STATE         (-15)  /* the call is out of order (e.g. read before handshake) */
#define OTERR_RANDOM        (-16)  /* no entropy */
#define OTERR_UNSUPPORTED   (-17)  /* asked for something this version lacks */
#define OTERR_INTERNAL      (-18)
#define OTERR_PINNED        (-19)  /* a pinned certificate did not match */

/* OT_SetVerify() modes. */
#define OTV_FULL        0   /* chain to a trusted CA, dates and host name (default) */
#define OTV_NO_HOSTNAME 1   /* chain and dates; any host name */
#define OTV_NONE        2   /* no checks: encryption without authentication */
#define OTV_PINNED_ONLY 3   /* only a certificate given to OT_PinCertificate() */

/* struct OTContextConfig Flags. */
#define OTCF_NO_SYSTEM_TRUST  (1UL << 0)  /* only CAFile and OT_AddTrust*() */
#define OTCF_NO_OFFLOAD       (1UL << 1)  /* all maths as 68k code (measuring) */
#define OTCF_SMALL_BUFFERS    (1UL << 2)  /* ~17 KB per connection, not ~33 KB */

struct OTContextConfig {
    ULONG Size;          /* sizeof(struct OTContextConfig) */
    ULONG Flags;         /* OTCF_* */
    LONG  MinVersion;    /* OT_TLS* ; 0: OT_TLS12 */
    LONG  MaxVersion;    /* OT_TLS* ; 0: the highest this library has (OT_TLS13 since 1.3) */
    STRPTR CAFile;       /* PEM bundle to trust as well; NULL: none */
    STRPTR ALPN;         /* default ALPN list, comma-separated; NULL: none */
};

/* struct OTIOMessage: what an I/O hook (OT_SetIOHook) is called with.
 * The hook answers the number of bytes moved (> 0), 0 at end of stream,
 * OTERR_WOULDBLOCK, or another negative number for an error. */
#define OTIO_READ   1
#define OTIO_WRITE  2

struct OTIOMessage {
    ULONG MethodID;      /* OTIO_READ or OTIO_WRITE */
    APTR  Buffer;
    LONG  Length;
};

/* OT_Accelerated() bits: which maths the x86 or ARM64 cores do. */
#define OTACC_HASH      (1UL << 0)
#define OTACC_AES       (1UL << 1)
#define OTACC_CHACHA    (1UL << 2)
#define OTACC_X25519    (1UL << 3)
#define OTACC_RSA       (1UL << 4)
#define OTACC_ECDSA     (1UL << 5)

#define OT_FINGERPRINT_SIZE 32   /* SHA-256 of the certificate's DER */

struct OTContext;      /* opaque */
struct OTConnection;   /* opaque */
struct OTSession;      /* opaque */

#endif
