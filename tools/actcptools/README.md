# ACTCPTools — AmigaOS 3.2.3 network utilities

ACTCPTools is GFOpenSocket's classic-Amiga command-line tool set (GFOpenSocket was ACNet until 4 October 2026). The tools install in `SYS:Tools/GFOpenSocket/`; `GFOpenSocket` (was `acnetctl`) installs in `C:`.

## Contract

The application-compatibility tools are ordinary AmigaOS programs using the published `bsdsocket.library` API:

`hostname/resolve/ping/traceroute/arp -> bsdsocket.library -> gfopensocket.library -> gfopensocket.device -> GFOpenSocket card -> Linux HostSocket`

The GFOpenSocket administration tools use the public `gfopensocket.library` API directly (it was `acnetwork.library`):

`ifconfig/route/netstat/GFOpenSocket -> gfopensocket.library -> gfopensocket.device -> GFOpenSocket card -> Linux HostSocket`

No ACTCPTool calls HostSocket, the device's private vectors, or AmiCradle host APIs directly.

## Current port status

| Tool | OS3 build | ACNet runtime | Notes |
| --- | --- | --- | --- |
| `hostname` | PASS | PASS | End-to-end through ACNet `gethostname()`. |
| `resolve` | PASS | PASS | Forward lookup through ACNet name service. |
| `ping` | PASS | PASS | End-to-end through ACNet's constrained ICMP compatibility path. |
| `traceroute` | PASS | PASS | Raw-UDP guest probes are translated through Linux UDP/error-queue ICMP. |
| `arp` | PASS | PASS (read-only) | `arp -a -n` uses Roadshow `GetRouteInfo()` compatibility over ACNet neighbour enumeration. |
| `ifconfig` | PASS | PASS (read-only) | Native ACNet interface/address/link/counter view. |
| `route` | PASS | PASS (read-only) | Native ACNet IPv4 route view. |
| `netstat` | PASS | PASS (read-only) | Native ACNet state/counters and this-instance socket inventory. |
| `GFOpenSocket` (was `acnetctl`) | PASS | PASS | Native status and soft online/offline control; Cradle remains authoritative. |
| `host` | PASS | not yet run | `host name` and `host address`: forward and reverse lookups, aliases. |
| `whois` | PASS | not yet run | Asks IANA, then the registry it refers to; `-h server` asks one directly. |
| `finger` | PASS | not yet run | `finger user@host`, `finger @host`. |
| `telnet` | PASS | not yet run | VT100 terminal type, window size 80×24, server echo; Ctrl-] closes, Ctrl-C goes to the other machine. |
| `ftp` | PASS | not yet run | Interactive, passive mode only (data goes to the control connection's address, which survives NAT); binary by default, `ascii` converts line ends. |
| `tftp` | PASS | not yet run | `get` and `put`, binary, 512-byte blocks, retries. |
| `sntp` | PASS | not yet run | The time from pool.ntp.org (or a given server) and the Amiga clock's error; `SET` sets the clock, `SAVE` the battery-backed one too. Integer arithmetic, no FPU. |
| `httpget` | PASS | PASS (19 Oct 2026, lab) | http:// and https:// through GFOpenTLS (`gfopentls.library`): TLS 1.2 or later, certificate and name checked. Follows redirects, chunked transfers. |
| `nc` | PASS | not yet run | Connect, listen (ports from 1024: the host refuses lower ones), UDP, and `-z` port scans. |

All binaries target 68040 and AmigaOS 3.2.3. The nine newest (`host` to `nc`) share `src/ostool.c`: opening the library, lookups, a connect that gives up after a while, Ctrl-C, and a 16 KB stack of their own (`guest/common/os3/acnet_stack.c`). They use only the published bsdsocket.library interface, so they also run over other Amiga TCP/IP stacks.
## Building

Use the same placed OS 3.2.3 NDK and m68k-amigaos GCC stove as ACNet:

```sh
STOVE=/path/to/os32-gcc16 ./tools/actcptools/build.sh
```

The build does not fetch an NDK or a networking SDK. `httpget`'s https:// is GFOpenTLS: its API headers are in `third_party/opentls` (from DalsinAI/gfopentls main e7c0e2d, MIT), and `gfopentls.library` is opened at run time (GFOpenUp's GFOpenTLS part puts it in LIBS:). No AmiSSL SDK is needed or used.

## Porting changes

`ping` and `traceroute` started from Olaf Barthel's classic-Amiga ports of the 4.4BSD-Lite2 utilities shipped with the NDK networking material.

The AmiCradle port removes their floating-point RTT calculations and uses integer microseconds instead. This avoids a dependency on `mathieeedoubbas.library` and keeps the tools suitable for a plain 68020 guest.

`hostname`, `resolve`, `GFOpenSocket`, `ifconfig`, `route`, `netstat` and the nine newest (`host`, `whois`, `finger`, `telnet`, `ftp`, `tftp`, `sntp`, `httpget`, `nc`) are GFOpenSocket programs (MIT, like the rest of GFOpenSocket) written for this package; none is derived from another implementation.

`arp` currently remains source-compatible with the supplied Amiga port, but ACNet intentionally has no mutable guest ARP table in HostSocket mode.