# ACNetControl Test Plan

> **Renamed 4 October 2026.** ACNet is now GFOpenSocket: `acnet.device` is `DEVS:Networks/gfopensocket.device`, `acnetwork.library` is `gfopensocket.library`, `acwifi.device` is `gfopensocketwifi.device`, ACNetControlGT is GFOpenSocketControl (the ReAction version is retired), `acnetctl` is `C:GFOpenSocket`, the tools are in `SYS:Tools/GFOpenSocket/` and the interface is `gfopensocket0`. The current design is `docs/architecture/OPENSOCKET_DESIGN.md` in DalsinAI/amicradle. The entries below keep the names of their time.

ACNetControl is the ReAction Commodity front end for ACNet on AmigaOS 3.2.3.
ACNetControlGT is the same Commodity in GadTools for AmigaOS 2.04 to 3.2;
both draw their pages from `acnetcontrol_core.c`. Gates 0 to 4 apply to both.
Networking must continue to work with ACNetControl absent, hidden or killed.

## Gate 0 - Build

- Build with the OS3 stove as 68040 code.
- `file` must identify `ACNetControl` as an AmigaOS LoadSeg binary.
- Build must complete with no warnings.
- Keep the binary below 40 KB for version 1.

## Gate 1 - Basic launch

Use the disposable ACNet OS3 instance only.

- Start from CLI and Workbench.
- With ACNet fitted and online, Status reports Online.
- With Network off in Cradle, Status reports Off in Cradle.
- With no ACNet card, the application opens and explains the condition.
- Closing the window hides the Commodity and does not stop networking.

## Gate 2 - Commodity behaviour

- A second launch brings the existing copy to the front.
- Exchange: Show, Hide, Enable, Disable and Kill.
- `ctrl alt n` opens/brings the window forward.
- Ctrl-C exits a CLI-started test copy cleanly.
- No duplicate broker remains after Kill.

## Gate 3 - Visual/UI qualification

Capture screenshots from the native replay/frame tools at:

- 640x480 RTG Workbench;
- 640x256 Workbench;
- 640x200 Workbench.

Check all five pages: Status, Wi-Fi, Connections, Diagnostics and Log.
The window must remain font-sensitive/resizable and must not require MUI.
The screencast/reference design is the visual target: restrained native ReAction,
compact information density, and a persistent status line.

## Gate 4 - Existing ACNet path regression

With ACNetControl running and hidden:

- `acnettest` still passes hostname and DNS lookup;
- TCP loopback bind/listen/connect/accept/send/recv still passes;
- HostSocket unit suite remains green;
- no socket is closed merely because ACNetControl is hidden or killed.

## Gate 5 - Telemetry/control backend

After STATS, LOG and ONLINE are implemented in HostSocket/acnet.device:

- live socket count and traffic totals/rates;
- PC address/interface/gateway/DNS;
- soft Go offline / Go online;
- event log ring and sequence handling;
- hidden window waits on ACNet events rather than polling.

Cradle's Network switch remains authoritative and soft-offline never survives a reboot.

## Gate 6 - Connections and diagnostics

- Connections enumerates program/task, protocol, endpoints, state and byte totals.
- Version 1 remains read-only.
- DNS lookup and TCP connection tests only run when the user requests them.
- Check Internet performs no background/external contact.
- Copy report produces a plain-text support summary.

## Gate 7 - Wi-Fi

Use a mock NetworkManager only; automated tests must never change the real host Wi-Fi.

- permission-off page is greyed/explanatory;
- scan returns asynchronously;
- join/leave/forget require confirmation;
- version 1 only joins host-known networks;
- passphrases never transit the Amiga.

## Gate 8 - Robustness

- 100 Show/Hide cycles: no meaningful AvailMem loss.
- Kill frees all resources.
- repeated launch/unique notification does not create a second copy.
- no-card and non-ACNet bsdsocket.library cases remain informative, not fatal.

## Gate GT - the GadTools front end

- Builds with `-fno-common` (without it libnix's auto-open stubs replace the
  programs' own library bases and open label.image and window.class by wrong
  names, so the ReAction build exits before main).
- Opens in the screen's font; falls back to Topaz 8 when the window would not
  fit the screen.
- The radio list changes page; Tab and Shift-Tab, and 1 to 6, do too.
- Resizing grows the groups; the window never shrinks below what the pages need.
- Esc and the close gadget hide it; `ctrl alt n` and Exchange's Show bring it
  back on the same page.
- Starting either front end while the other runs exits quietly (RC 5) and
  brings the running one forward.

## Gate NET - the network from the Amiga (1.1, 9 Nov 2026)

On a PC (an AmiCradle instance):
- Settings › Read says "A PC: its own network, read only here", with the PC's
  Wi-Fi and addresses. Apply says it is read only, and sends nothing.
- Wi-Fi › Join with a password, with Cradle's switch off: "Cradle doesn't allow
  this Amiga to change the PC's network". The Password field is empty again.

On a native card (Pi 5, Pi 4/400, PiStorm):
- Settings › Read says "A native card: this Amiga runs its network", with the
  hostname, LAN and address filled in.
- Wi-Fi › a new network, its password, Join: the card joins it (Rescan shows
  `*` on it) and it is still known after the card restarts. The Password field
  is empty again at once.
- A short password (under 8) is refused on the Amiga, before anything is sent.
- Settings › LAN static, an address, Apply: the card takes it. `bad` in LAN:
  "LAN is dhcp, static or off", nothing sent.
- This Amiga's own address: direct, dhcp, Save: ENVARC:GFOpenSocket/Interface
  has backend=direct and configure=dhcp; after a restart the Amiga has an address
  of its own.
- `GFOpenSocket net get`, `GFOpenSocket wifi join SSID KEY password` and
  `GFOpenSocket net set FILE` do the same from the Shell.

