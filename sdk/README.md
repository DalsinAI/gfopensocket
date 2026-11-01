# GFOpenSocket SDK

The headers programs build against bsdsocket.library with, as The GFOpen SDK
gathers them into GFOpenNDK (its `manifest.json` names this repository and
commit). They are GFOpenSocket's own: interface facts only (names, values,
layouts, offsets and registers), with no text from any other header set.

| Path | What |
| --- | --- |
| `SFD/bsdsocket_lib.sfd` | the library's calls, written by `tools/mksfd.py`; GFOpenNDK makes proto/, clib/, inline/ and pragmas/ from it |
| `tools/mksfd.py` | the SFD from the ABI manifests: `guest/network/acnet/abi/bsdsocket-v4.json`, `guest/network/acnet/compat/roadshow/guard-v4.json` and `abi/bsdsocket-extensions.json` |
| `abi/bsdsocket-extensions.json` | the signatures of the extension calls the guard manifest lists by slot only |
| `Include_H/` | the user-level headers: `libraries/bsdsocket.h` (and `bsdsocket/`, `amitcp/socketbasetags.h`), `sys/socket.h`, `sys/uio.h`, `sys/ioctl.h`, `sys/ioccom.h`, `sys/filio.h`, `sys/sockio.h`, `netinet/in.h`, `netinet/tcp.h`, `netdb.h`, `arpa/inet.h`, `net/if.h`, `syslog.h`, `sys/syslog.h`, `pwd.h` |
| `tests/check.sh` | the SFD is up to date, and each header compiles on its own in C and C++ |

Notes:

- The calls the BSD headers (and the C library's `unistd.h`) also declare take the
  same types in the SFD as there, so a program may include both in any order.
- The BSD headers declare their calls with the name in parentheses, so the inline
  calls of `proto/bsdsocket.h` are not expanded there; `__NO_NET_API` leaves them out.
- Where the C library has `sys/types.h`, its types are used; otherwise these headers
  define them, behind the same guards.
- Not here yet: the kernel-side headers (`sys/mbuf.h`, `netinet/*_var.h`, `net/route.h`,
  `net/bpf.h`, `net/if_dl.h` and the like), which GFOpenSocket's own tools take from
  elsewhere for now.
