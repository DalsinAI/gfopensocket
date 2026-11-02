# GFOpenSocketDirect

GFOpenSocket's own TCP/IP stack over a SANA-II device: bsdsocket.library's
**Direct** backend, chosen by the card layer (`../library/card.c`) when the
disk has a network driver or configuration.

| File | What |
| --- | --- |
| `lwip/` | lwIP 2.2.1 (`STABLE-2_2_1_RELEASE`), unchanged: the core, IPv4 (ARP, ICMP, IP, fragments, DHCP), UDP, TCP, DNS and Ethernet. Its own BSD licence: `lwip/COPYING`. |
| `port/` | lwIP's options (`lwipopts.h`: no OS layer, memory from exec) and the 68k compiler layer (`arch/cc.h`). |
| `direct_stack.c` | The stack's process, the SANA-II interface (raw frames, reads for IPv4 and ARP, writes from any task), and what lwIP asks of the platform. |
| `direct_sock.c` | HostSocket's commands (`common/protocol/achostsocket.h`) over lwIP's raw API, so bsdsocket.library's front end works as it does over the bridge. |
| `direct.h` | What bsdsocket.library calls (`struct DirectOps`). |
| `romseg.c`, `romseg.h` | A hunk image loaded from memory into RAM: copied unchanged from amicradle-guest `libraries/openrtg` (MIT, AmiCradle Team). |
| `build.sh` | Builds the image and `direct_image.c`, the image as C data. |

## In the ROM

bsdsocket.library must keep no writable data, so it runs from the ROM. lwIP
keeps its state in globals: built for this stack, its objects have 53 data
and BSS symbols in 14 of its files (12 bytes of data, 2,832 of BSS, measured
with `m68k-amigaos-nm` and `size` at `-Os`). Moving them into allocated
memory would mean changing lwIP in those 14 files and following every
upstream release. So the stack is a hunk image of its own (58,660 bytes at
`-Os`, stripped), which the library carries as data and loads into RAM with
romseg at the init, when Direct is chosen. Only the image's RAM copy is
written; the library stays ROM-clean (its build checks this).

lwIP's heap and pools come from exec (`AllocVec`) as they are needed, so the
image itself has no large tables.

## Threads

lwIP runs with `NO_SYS`: one lock (a SignalSemaphore) is held for every call
into it, from the stack's process (frames in, timers every 50 ms) or from a
bsdsocket.library caller. Loopback frames (127.0.0.1, and the interface's
own address) are delivered before the lock is let go, so a connect over
loopback is complete when the call returns. Every armed caller is signalled
when anything happens, and bsdsocket.library's front end polls again.

## SANA-II

Reads and writes go to the device's `BeginIO` vector, not through
`SendIO`/`DoIO`: those set `io_Flags`, where SANA-II carries `SANA2IOF_RAW`.
Measured on t5-net: a raw `CMD_WRITE` sent with `SendIO` left with a second
Ethernet header in front (the host's packet tap showed it), and no reply
came back.

Addressing: the configured address, or DHCP when none is set (`cfg->dhcp`),
with the name servers DHCP offers, or those configured. The interface's MAC
is the device's current station address; a device not configured yet is
configured with its default address.
