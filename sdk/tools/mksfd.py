#!/usr/bin/env python3
# Copyright (c) 2026 DalsinAI. MIT licence (see LICENSE).
"""Write sdk/SFD/bsdsocket_lib.sfd from GFOpenSocket's own ABI manifests:
the classic table (guest/network/acnet/abi/bsdsocket-v4.json), the
extension table after it (guest/network/acnet/compat/roadshow/guard-v4.json),
and the signatures of the extension calls the guard lists by slot only
(sdk/abi/bsdsocket-extensions.json).

    mksfd.py [--check]

The registers and the order are the manifests'. The calls the BSD headers
(and the C library's unistd.h) also declare take the same types as there,
so a program may include both in any order. With --check, compare with the
file in the repository instead of writing it."""
import json, os, sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
CORE = os.path.join(ROOT, "guest/network/acnet/abi/bsdsocket-v4.json")
GUARD = os.path.join(ROOT, "guest/network/acnet/compat/roadshow/guard-v4.json")
EXT = os.path.join(ROOT, "sdk/abi/bsdsocket-extensions.json")
OUT = os.path.join(ROOT, "sdk/SFD/bsdsocket_lib.sfd")

# the POSIX types, in argument order, and the return type
POSIX = {
    "socket": ("int", ["int", "int", "int"]),
    "bind": ("int", ["int", "const struct sockaddr *", "socklen_t"]),
    "listen": ("int", ["int", "int"]),
    "accept": ("int", ["int", "struct sockaddr *", "socklen_t *"]),
    "connect": ("int", ["int", "const struct sockaddr *", "socklen_t"]),
    "sendto": ("ssize_t", ["int", "const void *", "size_t", "int", "const struct sockaddr *", "socklen_t"]),
    "send": ("ssize_t", ["int", "const void *", "size_t", "int"]),
    "recvfrom": ("ssize_t", ["int", "void *", "size_t", "int", "struct sockaddr *", "socklen_t *"]),
    "recv": ("ssize_t", ["int", "void *", "size_t", "int"]),
    "shutdown": ("int", ["int", "int"]),
    "setsockopt": ("int", ["int", "int", "int", "const void *", "socklen_t"]),
    "getsockopt": ("int", ["int", "int", "int", "void *", "socklen_t *"]),
    "getsockname": ("int", ["int", "struct sockaddr *", "socklen_t *"]),
    "getpeername": ("int", ["int", "struct sockaddr *", "socklen_t *"]),
    "sendmsg": ("ssize_t", ["int", "const struct msghdr *", "int"]),
    "recvmsg": ("ssize_t", ["int", "struct msghdr *", "int"]),
    "inet_addr": ("in_addr_t", ["const char *"]),
    "inet_network": ("in_addr_t", ["const char *"]),
    "inet_aton": ("int", ["const char *", "struct in_addr *"]),
    "gethostbyname": ("struct hostent *", ["const char *"]),
    "gethostbyaddr": ("struct hostent *", ["const void *", "socklen_t", "int"]),
    "getnetbyname": ("struct netent *", ["const char *"]),
    "getnetbyaddr": ("struct netent *", ["in_addr_t", "int"]),
    "getservbyname": ("struct servent *", ["const char *", "const char *"]),
    "getservbyport": ("struct servent *", ["int", "const char *"]),
    "getprotobyname": ("struct protoent *", ["const char *"]),
    "getprotobynumber": ("struct protoent *", ["int"]),
    "vsyslog": ("void", ["int", "const char *", "va_list"]),
    "gethostname": ("int", ["char *", "size_t"]),
    "gethostid": ("long", []),
    "getdtablesize": ("int", []),
}

# the varargs forms: (name, the tag/array call, the arguments before "...")
VARARGS = {
    "SocketBaseTagList": ("SocketBaseTags", [("Tag", "tag1")]),
    "vsyslog": ("syslog", [("int", "pri"), ("const char *", "msg")]),
    "AddRouteTagList": ("AddRouteTags", [("Tag", "tag1")]),
    "DeleteRouteTagList": ("DeleteRouteTags", [("Tag", "tag1")]),
    "ChangeRouteTagList": ("ChangeRouteTags", [("Tag", "tag1")]),
    "AddInterfaceTagList": ("AddInterfaceTags", None),
    "ConfigureInterfaceTagList": ("ConfigureInterfaceTags", None),
    "QueryInterfaceTagList": ("QueryInterfaceTags", None),
    "CreateAddrAllocMessageA": ("CreateAddrAllocMessage", None),
    "AddNetMonitorHookTagList": ("AddNetMonitorHookTags", None),
}

INCLUDES = ["exec/types.h", "exec/lists.h", "utility/tagitem.h", "utility/hooks.h", "devices/timer.h", "dos/dosextens.h",
            "sys/socket.h", "netinet/in.h", "netdb.h", "sys/syslog.h", "libraries/bsdsocket.h"]


def line(name, ret, args):
    decl = ", ".join("%s%s%s" % (t, "" if t.endswith("*") else " ", n) for t, n, _ in args)
    return "%s %s(%s) (%s)" % (ret, name, decl, ",".join(r for _, _, r in args))


def call(s):
    name = s["name"]
    args = [(t, n, r) for t, n, r in s["args"]]
    ret = s["ret"]
    if name in POSIX:
        pret, ptypes = POSIX[name]
        if len(ptypes) != len(args):
            raise SystemExit("%s: %d POSIX types for %d arguments" % (name, len(ptypes), len(args)))
        ret, args = pret, [(pt, n, r) for pt, (_, n, r) in zip(ptypes, args)]
    out = [line(name, ret, args)]
    if name in VARARGS:
        vname, fixed = VARARGS[name]
        if fixed is None:                   # all but the last, then a tag
            fixed = [(t, n) for t, n, _ in args[:-1]] + [("Tag", "tag1")]
        regs = ",".join(r for _, _, r in args)
        decl = ", ".join("%s%s%s" % (t, "" if t.endswith("*") else " ", n) for t, n in fixed)
        out += ["==varargs", "%s %s(%s, ...) (%s)" % (ret, vname, decl, regs)]
    return out


def sfd():
    core = json.load(open(CORE))
    guard = json.load(open(GUARD))
    ext = json.load(open(EXT))["calls"]
    if core.get("bias") != 30:
        raise SystemExit("unexpected bias")
    out = ["==id GFOpenSocket bsdsocket_lib.sfd",
           "* bsdsocket.library's calls: GFOpenSocket's classic table and its extensions,",
           "* written from its ABI manifests by sdk/tools/mksfd.py. Do not edit.",
           "* Copyright (c) 2026 DalsinAI. MIT licence (see LICENSE).",
           "==base _SocketBase", "==basetype struct Library *", "==libname bsdsocket.library",
           "==bias 30", "==public"] + ["==include <%s>" % h for h in INCLUDES]
    index = 0                               # the next vector, 0 at the bias
    for s in core["slots"]:
        if "reserved" in s:
            out.append("==reserve %d" % s["reserved"])
            index += s["reserved"]
            continue
        out += call(s)
        index += 1
    for s in guard["slots"]:
        want = s["slot"] - 1                # the guard counts from 1
        if want < index:
            raise SystemExit("guard slot %d overlaps" % s["slot"])
        if want > index:
            out.append("==reserve %d" % (want - index))
            index = want
        if s.get("kind") == "reserved" or "name" not in s:
            out.append("==reserve 1")
        else:
            if "args" not in s:
                if s["name"] not in ext:
                    raise SystemExit("no signature for %s" % s["name"])
                s = dict(s, ret=ext[s["name"]]["ret"], args=ext[s["name"]]["args"])
            out += call(s)
        index += 1
    out.append("==end")
    return "\n".join(out) + "\n"


def main():
    text = sfd()
    if "--check" in sys.argv[1:]:
        if open(OUT).read() != text:
            raise SystemExit("sdk/SFD/bsdsocket_lib.sfd is stale: run sdk/tools/mksfd.py")
        print("bsdsocket_lib.sfd matches the manifests")
        return
    open(OUT, "w").write(text)


if __name__ == "__main__":
    main()
