#!/bin/sh
set -eu

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
# soft float: link libnix's libm020, not libm881 (specs/m68k-soft-m68040.specs)
SPECS="$(cd "$HERE/../.." && pwd)/specs/m68k-soft-m68040.specs"
STOVE=${STOVE:-}
if [ -z "$STOVE" ]; then
    echo "Set STOVE to the AmiCradle OS 3.2 cross-build stove." >&2
    exit 2
fi

CC="$STOVE/prefix/bin/m68k-amigaos-gcc"
OUT=${1:-"$HERE/build"}
# The library itself and its tools use the kernel-side network headers
# (net/route.h, net/bpf.h, sys/mbuf.h, netinet/*_var.h), which GFOpenNDK
# doesn't have yet (sdk/README.md); until it does, they build against the
# vendor NDK the stoves keep beside it, as before the stoves took GFOpenNDK.
if [ -d "$STOVE/prefix/m68k-amigaos/ndk-include.vendor" ]; then
  VENDOR_NDK="-I$STOVE/prefix/m68k-amigaos/ndk-include.vendor"
else
  VENDOR_NDK=""
fi
CFLAGS="-m68040 -specs=$SPECS -O2 -fno-delete-null-pointer-checks -Wall -Wno-pointer-sign -noixemul -I$HERE/../../guest/network/acnetwork/include"
CFLAGS="$CFLAGS $VENDOR_NDK"
mkdir -p "$OUT"

build_one() {
    name=$1
    "$CC" $CFLAGS -I"$HERE/src" "$HERE/src/$name.c" -o "$OUT/$name"
    echo "$OUT/$name ($(wc -c < "$OUT/$name") bytes)"
}

build_one hostname
build_one resolve
build_one ping
build_one traceroute
build_one arp
build_one ifconfig
build_one route
build_one netstat
# The newer tools share src/ostool.c and the stack helper (libnix ignores __stack).
OSTOOL="$HERE/src/ostool.c $HERE/../../guest/common/os3/acnet_stack.c"
build_net() {
    name=$1
    shift
    "$CC" $CFLAGS -I"$HERE/src" -I"$HERE/../../guest/common/os3" "$@" "$HERE/src/$name.c" $OSTOOL -o "$OUT/$name"
    echo "$OUT/$name ($(wc -c < "$OUT/$name") bytes)"
}

build_net host
build_net whois
build_net finger
build_net sntp
build_net tftp
build_net nc
build_net telnet
build_net ftp
# httpget: https:// through GFOpenTLS (gfopentls.library, opened at run time; its
# API headers in third_party/opentls). No AmiSSL SDK is needed or used: an
# AMISSL setting from older callers is ignored.
build_net httpget -I"$HERE/third_party/opentls/include"

# C:GFOpenSocket (was acnetctl): status, online, offline
"$CC" $CFLAGS -I"$HERE/src" "$HERE/src/opensocket.c" -o "$OUT/GFOpenSocket"
echo "$OUT/GFOpenSocket ($(wc -c < "$OUT/GFOpenSocket") bytes)"

echo "ACTCPTools build complete."