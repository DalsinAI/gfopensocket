# ACNet — acnetwork.library native boundary

**Date:** 2 October 2026

## Purpose

`bsdsocket.library` remains the classic Amiga BSD-socket compatibility facade. Its descriptor table, errno behaviour, tag handling, WaitSelect semantics, netdb compatibility and published v4 vector layout remain compatibility policy.

`acnetwork.library` sits below that facade and owns the native ACNet provider/session boundary:

```text
classic application
       |
bsdsocket.library
       |
acnetwork.library
       |
acnet.device
       |
ACNet card / HostSocket
       |
Linux networking
```

AmiCradle-aware software may later open `acnetwork.library` directly without being constrained by the legacy bsdsocket ABI.

## v1 native vectors

- `ACNetwork_Call()` — one provider request/response
- `ACNetwork_Arm()` — arm the opener's event waiter
- `ACNetwork_Disarm()` — disarm that waiter
- `ACNetwork_SignalMask()` — signal bit used by WaitSelect/native callers
- `ACNetwork_LastError()` — most recent provider error
- `ACNetwork_State()` — card/online state

Each `OpenLibrary()` receives an independent session with its own `acnet.device` open and event signal.

## ICMP compatibility

`bsdsocket.library` now recognises protocol `icmp`/1. A classic application can request `socket(AF_INET, SOCK_RAW, IPPROTO_ICMP)` while ACNet's HostSocket safely virtualises that operation. The Linux host does not receive unrestricted raw-socket capability.

For ICMP Echo the host uses Linux's unprivileged datagram ICMP socket. HostSocket restores the guest ICMP identifier, recomputes the ICMP checksum, and prepends a synthetic IPv4 header so the classic Amiga application sees conventional raw-socket receive semantics.

All other raw protocols remain refused.

## Qualification

On Instance-6 (AmigaOS 3.2.3 ACNet test guest):

- existing BSD compatibility qualification plus ICMP name/number tests: **49 PASS / 0 FAIL**
- ACTCPTools `ping -c 1 -t 2 127.0.0.1`: **RC 0, 0% loss**
- observed loopback RTT after EClock conversion fix: **0.324 ms**

The BSD vector ABI was unchanged by insertion of `acnetwork.library`.

## Native state/control surface — 3 October 2026

The native boundary has grown beyond socket-provider plumbing. `ACNetwork_Call()` now carries bounded fixed-format views for:

- status and counters;
- IPv4 interfaces;
- IPv4 routes;
- neighbours/ARP state;
- DNS servers;
- this-instance socket inventory;
- soft online/offline control;
- hostname and network-database lookups.

The fixed records are deliberately ACNet-native rather than Linux or Roadshow structures. `bsdsocket.library` translates them when classic compatibility requires a different ABI.

## Roadshow-shaped compatibility

The classic compatibility facade now exposes a read-only subset of the Roadshow extension area while preserving the guarded vector envelope. Implemented compatibility includes routes/ARP, interface enumeration/query, DNS server enumeration, network/socket statistics and `gethostid()`.

Mutable interface, route and DNS administration remains guarded. Linux/Cradle owns the real host network configuration; ACNet does not fabricate an Amiga-resident routing stack.

## Current qualification and remaining work

As of 3 October 2026 the guest branch reports all 46 core BSD application vectors implemented. The Roadshow read-only probe passed with zero failures; the host drop-in branch has 29 focused HostSocket tests passing with two expected skips.

ACTCPTools currently includes `hostname`, `resolve`, `ping`, `traceroute`, `arp`, `ifconfig`, `route`, `netstat` and `acnetctl`. ACNetControl consumes the native status/interface/route/socket views and its working diagnostics use public `bsdsocket.library` calls.

Before calling the drop-in release complete:

1. merge the qualified guest and host branches;
2. deploy the matching host/runtime and guest libraries together;
3. rerun the consolidated OS 3.2.3 qualification;
4. run the wider third-party application/conformance matrix;
5. finish installer/conflict handling.

Later lanes remain Wi-Fi control, event-ring logging, packet/SANA-II compatibility, BPF/libpcap/tcpdump, mutable administration if justified, and IPv6.
