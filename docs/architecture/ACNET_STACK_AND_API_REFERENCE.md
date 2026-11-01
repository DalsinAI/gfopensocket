# ACNet Stack and API Reference

**Status:** Complete-stack implementation reference; consolidated live qualification pending
**Date:** 3 October 2026
**Guest branch:** `feature/complete-stack-20261003`
**Host/runtime branch:** `net/complete-stack-20261003`
**Created by:** AmiCradle Team, in collaboration with Thufir Hawat.

## 1. Scope

ACNet is AmiCradle's independent networking subsystem for AmigaOS 3.2.3. It provides a classic `bsdsocket.library` application ABI, an AmiCradle-native `acnetwork.library`, `acnet.device`, a SANA-II packet facade, Wi-Fi control, BPF packet capture and the ACNet virtual Zorro-II board.

Roadshow and AmiTCP are compatibility targets, not dependencies. HostSocket is ACNet's current provider/backend for host networking. Linux supplies low-level TCP/IP execution; ACNet owns the guest ABI, descriptors, waits/signals, errors, DNS compatibility, state/control model, packet interface and policy.
## 2. Architecture

```text
Classic Amiga application        ACNet-native application / ACNetControl
          |                                      |
   bsdsocket.library                       acnetwork.library
          |                                      ^
          +------------> acnetwork.library ------+
                              |
                         acnet.device
                    socket/control | SANA-II
                              |
                    ACNet Zorro-II board
                              |
                         HostSocket
                    /          |          \
              host sockets   Wi-Fi     packet/BPF
                    \          |          /
                    Linux + user-mode libslirp
```

The application socket path does not require SANA-II. Packet-oriented software can use the SANA-II face of `acnet.device`; packet capture uses the Roadshow-compatible BPF facade without stealing frames from SANA-II readers.
## 3. Components and ownership

| Component | Role |
| --- | --- |
| `bsdsocket.library` 4.x | Classic BSD socket facade; 46/46 core application vectors implemented. |
| Roadshow compatibility tail | Optional legacy/status APIs in physical slots 57-139; unimplemented slots fail safely. |
| `acnetwork.library` 1.0 | Native per-opener ACNet session/control API. |
| `acnet.device` 1.0 | ACNet board driver, event source and SANA-II packet device. |
| `acwifi.device` 1.0 | Wi-Fi control facade using the native ACNet session API. |
| ACNet card | AmiCradle Team manufacturer $DA15, product 6, 64 KiB Zorro-II AutoConfig device. |
| HostSocket | Per-instance host service: sockets, DNS, state, Wi-Fi, packet provider and BPF. |
| ACNetControl | ReAction Commodity for status, connections, Wi-Fi, diagnostics and event log. |
| ACTCPTools | hostname, resolve, ping, traceroute, arp, ifconfig, route, netstat and acnetctl. |

Each `OpenLibrary("bsdsocket.library")` receives its own descriptor/error/wait state. Each `acnetwork.library` opener receives its own `acnet.device` session and Exec signal.
## 4. Classic bsdsocket.library core ABI

ACNet owns `guest/network/acnet/abi/bsdsocket-v4.json`. The default build does not read a third-party SFD. Slots 1-46 are application vectors; slots 47-56 remain reserved. All 46 application vectors are implemented.

| Slots | Routines |
| --- | --- |
| 1-5 | `socket`, `bind`, `listen`, `accept`, `connect` |
| 6-10 | `sendto`, `send`, `recvfrom`, `recv`, `shutdown` |
| 11-16 | `setsockopt`, `getsockopt`, `getsockname`, `getpeername`, `IoctlSocket`, `CloseSocket` |
| 17-19 | `WaitSelect`, `SetSocketSignals`, `getdtablesize` |
| 20-24 | `ObtainSocket`, `ReleaseSocket`, `ReleaseCopyOfSocket`, `Errno`, `SetErrnoPtr` |
| 25-30 | `Inet_NtoA`, `inet_addr`, `Inet_LnaOf`, `Inet_NetOf`, `Inet_MakeAddr`, `inet_network` |
| 31-38 | host, network, service and protocol database lookups |
| 39-46 | `vsyslog`, `Dup2Socket`, `sendmsg`, `recvmsg`, `gethostname`, `gethostid`, `SocketBaseTagList`, `GetSocketEvents` |

Blocking calls use provider arm → poll → Exec `Wait()` semantics. A provider event cannot be lost between the poll and the sleep. Break signals return `EINTR`; timed waits use `timer.device`.
### 4.1 Socket semantics

Version 1 supports IPv4 TCP and UDP, private guest loopback, non-blocking sockets, normal socket options, `FIONREAD`, `FIONBIO`, scatter/gather `sendmsg/recvmsg`, descriptor duplication and socket transfer between tasks.

General unrestricted raw sockets are not exposed. `SOCK_RAW/IPPROTO_ICMP` is a constrained compatibility personality using Linux's unprivileged datagram ICMP facility. Traceroute raw-UDP/IP probes are translated to Linux UDP plus the error queue, with classic ICMP Time Exceeded data synthesized back to the caller.

The host PC's own addresses are deliberately not a shortcut into host services. Guest `127.0.0.1` refers to the same ACNet instance's private loopback listeners.

### 4.2 Name and resolver APIs

Classic host/service/protocol/network lookups are implemented. The compatibility tail also provides IPv4 `inet_aton`, `inet_pton`, `inet_ntop`, re-entrant host lookups, `getaddrinfo`, `freeaddrinfo`, `gai_strerror` and `getnameinfo`.

The target AmigaOS 3.2/Roadshow NDK has no public `AF_INET6` / `sockaddr_in6` socket ABI. Therefore these compatibility calls truthfully implement the target's IPv4 semantics rather than inventing an incompatible classic IPv6 ABI.
## 5. Roadshow-shaped compatibility tail

Physical vector slots 57-139 are a permanent compatibility/safety envelope. Unknown or deliberately unsupported calls return the appropriate safe failure instead of jumping beyond the library. New ACNet-native functionality should use `acnetwork.library`, not consume these legacy slots.

Implemented compatibility vectors are:

| Slots | Implemented API |
| --- | --- |
| 57-64 | BPF: open, close, read, write, notify mask, interrupt mask, ioctl, data-waiting |
| 68-69 | `FreeRouteInfo`, `GetRouteInfo` for IPv4 routes and ARP/LLINFO |
| 72-74 | interface list acquire/release/query |
| 81 | `GetNetworkStatistics` |
| 84-85 | DNS server list acquire/release |
| 95-99 | `inet_aton`, `inet_ntop`, `inet_pton`, `In_LocalAddr`, `In_CanForward` |
| 119-120 | `gethostbyname_r`, `gethostbyaddr_r` |
| 130-133 | `freeaddrinfo`, `getaddrinfo`, `gai_strerror`, `getnameinfo` |

Route/interface/DNS mutation, mbuf internals, Roadshow global-data access, IP-filter internals and server-private APIs remain guards unless real application evidence justifies a safe adapter.
## 6. BPF and libpcap compatibility

ACNet exposes eight BPF channels per `bsdsocket.library` opener and advertises them through `SBTC_NUM_PACKET_FILTER_CHANNELS`.

Current BPF contract:

- interface name: `acnet0`;
- data-link type: `DLT_EN10MB`;
- filter VM: classic BPF 1.1, maximum 512 instructions;
- capture: both virtual Ethernet receive and transmit directions;
- injection: `bpf_write()` sends a raw Ethernet frame into the ACNet packet provider;
- queues are bounded; overflow increments the drop counter;
- SANA-II and BPF share one packet provider but have independent ownership/lifetime;
- promiscuous-mode ioctl succeeds as a no-op because ACNet is already an isolated virtual Ethernet segment;
- BPF never grants host raw-NIC or raw-socket privilege.

Supported ioctls include `FIONREAD`, `BIOCGBLEN`, `BIOCSBLEN`, `BIOCSETF`, `BIOCFLUSH`, `BIOCPROMISC`, `BIOCGDLT`, `BIOCGETIF`, `BIOCSETIF`, `SIOCGIFADDR`, `BIOCSRTIMEOUT`, `BIOCGRTIMEOUT`, `BIOCGSTATS`, `BIOCIMMEDIATE` and `BIOCVERSION`.

The Roadshow NDK's real AmigaOS libpcap 0.8.1 source has been cross-built against this ABI into a 140 KiB m68k `libpcap.a`. The supplied tcpdump 3.8.1 source proceeds through the same libpcap/BPF path but its archive is missing `rpc/pmap_prot.h`, blocking an unrelated SunRPC printer module; that is not an ACNet BPF failure.
## 7. acnetwork.library native API

`acnetwork.library` version 1 is the stable ACNet-native boundary. Its public vectors are:

| LVO | API | Purpose |
| ---: | --- | --- |
| -30 | `ACNetwork_Call(request)` | Execute one native ACNet request. |
| -36 | `ACNetwork_Arm()` | Arm this opener for the next provider event. |
| -42 | `ACNetwork_Disarm()` | Stop waiting for provider events. |
| -48 | `ACNetwork_SignalMask()` | Return this opener's Exec signal mask. |
| -54 | `ACNetwork_LastError()` | Return the most recent provider error. |
| -60 | `ACNetwork_State()` | Return card/online state bits. |

`struct ACNetworkRequest` contains a command, four integer arguments, bounded transmit/receive buffers, result, error and returned length. No host pointer or file descriptor crosses the ABI.

Native fixed-format records include `ACNetworkStatus` (40 bytes), `ACNetworkNeighbour` (32), `ACNetworkInterface` (64), `ACNetworkRoute` (40), `ACNetworkSocket` (48), `ACNetworkLogEntry` (96), `ACWiFiNetwork` (64), `ACPacketInfo` (32) and `ACPacketStats` (24).
### 7.1 Native command surface

| Command | Function |
| --- | --- |
| $02 | status/counters |
| $34 | instance hostname |
| $35 | neighbours |
| $36 | interfaces |
| $37 | routes |
| $38 | this-instance sockets |
| $39 | soft online/offline |
| $3A | DNS servers |
| $3D | bounded event log |
| $40-$46 | asynchronous Wi-Fi scan/status/join/leave/forget/answer/cancel |
| $50-$55 | packet info, online/offline, send, receive and statistics |

The network-database provider additionally uses $3B/$3C internally for network-name/address lookup.

Soft online/offline is subordinate to Cradle's Network switch: guest software cannot override a host-disabled network.
## 8. acnet.device

`acnet.device` is both the card/control driver and the SANA-II packet face.

Private vectors after the six standard device vectors are:

- `ACN_Call` (-42): one bounded HostSocket command;
- `ACN_AddWaiter` (-48): register a task/signal waiter;
- `ACN_RemWaiter` (-54): remove it;
- `ACN_State` (-60): card/online state.

The SANA-II `BeginIO` implementation supports `CMD_READ`, `CMD_WRITE`, `CMD_FLUSH`, `S2_DEVICEQUERY`, station/configuration queries, multicast address calls, `S2_MULTICAST`, `S2_BROADCAST`, packet-type tracking/statistics, global/special statistics, `S2_ONEVENT`, `S2_READORPHAN`, `S2_ONLINE`, `S2_OFFLINE` and `NSCMD_DEVICEQUERY`.

A worker task services the user-mode packet provider and queued reads. Raw SANA-II packets include the Ethernet header; normal reads/writes expose the payload according to SANA-II flags.
## 9. Packet provider and IPv6

The packet provider is user-mode libslirp, not a privileged host TAP/raw-NIC attachment. `ACPacketInfo.flags` reports IPv4, IPv6 and user-mode capability. The provider is created with IPv6 enabled.

This means SANA-II/BPF software can carry and inspect IPv6 Ethernet/IP traffic today. The classic `bsdsocket.library` application ABI remains IPv4 because the target NDK does not define an IPv6 socket ABI. A future ACNet-native IPv6 socket API may be specified independently without pretending it is a Roadshow 3.x interface.

BPF capture and SANA-II do not consume one another's frames. The shared provider remains alive while either subsystem owns it and closes only when the last owner releases it.
## 10. acwifi.device

`acwifi.device` version 1 wraps the native Wi-Fi command range. `ACW_Call` is the private call at LVO -42; `ACWIFI_IO_CALL` provides a standard-device facade.

Commands:

- `ACWIFI_SCAN`;
- `ACWIFI_STATUS`;
- `ACWIFI_JOIN`;
- `ACWIFI_LEAVE`;
- `ACWIFI_FORGET`.

The host adapter uses NetworkManager. Join/forget operate on known profiles; passwords are not transported through the Amiga ABI. Every host Wi-Fi-changing operation is permission-gated by Cradle's per-instance Host Wi-Fi control setting. Requests are asynchronous and return through the same event architecture as the rest of ACNet.
## 11. HostSocket wire protocol

The ACNet card exposes a big-endian register/buffer contract. Important command ranges are:

| Range | Use |
| --- | --- |
| $01-$02 | hello and status |
| $10-$20 | socket lifecycle, data, options, poll and pending bytes |
| $30-$3D | resolver, hostname, state/control, inventory, network DB and log |
| $40-$46 | Wi-Fi |
| $50-$55 | raw Ethernet packet provider |
| $60-$67 | BPF channels/filter/capture |

The card has a transmit area at $0100, receive area at $4000, event/IRQ registers and INT2 signalling. All externally supplied lengths are bounded before crossing the guest/host boundary.

Errno values on the wire are Amiga BSD errno numbers. HostSocket maps host errors back to those numbers; resolver failures use the classic `h_errno` domain where required.
## 12. ACNetControl and command tools

ACNetControl is a stock AmigaOS 3.2.3 ReAction Commodity. It has Status, Wi-Fi, Connections, Diagnostics and Log pages. Status/Connections read the native `acnetwork.library` views; Refresh and soft online/offline are real controls. DNS/TCP diagnostics use the public BSD socket facade. The event log is backed by the bounded HostSocket log service.

The ACTCPTools suite consists of:

`hostname`, `resolve`, `ping`, `traceroute`, `arp`, `ifconfig`, `route`, `netstat`, `acnetctl`.

The tools are installed under `SYS:Tools/ACNetwork Tools/` to avoid overwriting similarly named third-party commands. They remain usable from Shell by path/assign/alias policy chosen by the user.
## 13. Installation and Cradle contract

AmiCradle vendors a self-contained OS3 guest payload under `vendor/acnet-os32`. Its format-2 manifest records SHA-256, size, guest install destination and Amiga protection bits for 14 production files. Qualification probes are carried separately and are not installed as production commands.

The transactional installer `scripts/acnet_guest.py`:

1. verifies every payload hash before writing;
2. resolves the instance's active folder-backed DH0 and rejects path escape;
3. refuses to replace guest symlinks;
4. updates an older ACNet installation;
5. refuses a foreign `LIBS:bsdsocket.library` without writing anything;
6. atomically replaces files and updates `.amicradle-meta.json`;
7. rolls files and metadata back if any step fails.

Cradle's hardware save path checks this payload when an AmigaOS 3.x instance has networking enabled. A required install/update is allowed only while that guest is stopped. AROS is not given the OS3 ACNet payload because it retains its own stack.
## 14. Security and authority boundaries

- Cradle's Network switch is authoritative over guest soft-online state.
- Wi-Fi-changing actions require the separate Host Wi-Fi control permission.
- The socket fast path refuses direct access to the host PC's own network addresses.
- Guest loopback is instance-private.
- General unrestricted raw sockets remain refused.
- ICMP and traceroute receive narrowly virtualized compatibility personalities.
- SANA-II/BPF use an unprivileged user-mode Ethernet provider.
- BPF taps only the ACNet virtual segment; it is not host-interface promiscuous capture.
- Route/interface/DNS mutation through Roadshow compatibility remains guarded.
- Roadshow mbuf, global-stack-data, server-private and IP-filter internals are not ACNet core APIs.

Those guarded APIs are deliberate authority boundaries, not unfinished TCP/IP engine code.
## 15. Build and qualification state

Previously live-qualified on Instance-6:

- clean core: 47/47, RC 0;
- ICMP-expanded BSD qualification: 49 PASS / 0 FAIL;
- live ping, traceroute, ARP, ifconfig, route, netstat, hostname, resolve and acnetctl paths;
- Roadshow-shaped read-only probe: zero failures.

Complete-stack branch evidence on 3 October:

- guest stack sealed at `f126774` (`feature/complete-stack-20261003`);
- matching host/runtime and deployment payload sealed at `0ffaa7c` (`net/complete-stack-20261003`);
- full OS3 cross-build: 46/46 core vectors, zero core stubs;
- Roadshow compatibility tail: 27 implemented vectors after BPF/modern additions;
- ACNet guest build and all nine ACTCPTools binaries complete;
- HostSocket complete-stack service tests: 35/35;
- transactional guest installer tests: 6/6;
- additional instance-hardware/host/deployment/network integration gate: 47/47;
- production payload manifest: 21 entries verified by SHA-256, comprising 14 installed production files plus seven qualification probes;
- real Roadshow AmigaOS libpcap 0.8.1 cross-build produced a 140 KiB m68k `libpcap.a`.

The sealed complete-stack guest/host pair has not yet had its consolidated live Instance-6 campaign. Build/test success must not be reported as that live qualification.
## 16. Deliberate non-APIs and remaining release gates

The following are not required code gaps in the specified OS3 stack:

- mutable host route/interface/DNS administration from legacy Roadshow calls;
- Roadshow mbuf/kernel-memory internals;
- Roadshow global-stack/server-private/IP-filter APIs;
- a fabricated classic IPv6 socket ABI that the target NDK does not possess.

Remaining release work is integration and evidence:

1. merge the sealed guest (`f126774`) and host/runtime (`0ffaa7c`) complete-stack branches through the normal review path;
2. deploy matching HostSocket/runtime and guest payload together;
3. run `modern-compat-probe`, `bpfprobe`, `sana2probe`, `acwifitest`, core BSD and ACTCPTools on the dedicated OS3 qualification guest;
4. run the real-application matrix and broader bsdsock conformance;
5. qualify the Cradle one-switch installer on a disposable copy before normal release deployment.

The tcpdump 3.8.1 archive supplied with the NDK is separately missing `rpc/pmap_prot.h`; its libpcap/BPF dependency has already built successfully. Fixing that source-package gap is a tcpdump packaging task, not ACNet stack completion.
