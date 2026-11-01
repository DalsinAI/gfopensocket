#!/bin/sh
# OpenSocket (formerly ACNet, renamed 4 Oct 2026) for AmigaOS 3.2.3:
# opensocket.device (DEVS:Networks/), opensocket.library (built by
# ../acnetwork/build.sh) and the bsdsocket.library face (bare: no startup code
# or C library), plus the test programs (libnix), with the os32 stove (bebbo's
# m68k-amigaos-gcc, NDK 3.2). The library vector table is generated from its own classic
# Amiga BSD socket ABI manifest; no third-party socket SFD is required.
# bsdqual links common/os3/acnet_stack.c: this libnix ignores __stack, so it
# swaps to a stack of its own.
#   network/acnet/build.sh [OUT_DIR]     (default OUT=, else build/guest/os32/acnet)
#   network/acnet/build.sh module        only bsdsocket.library for a ROM:
#     $OUT/modules/bsdsocket.module (OUT= or build-amiga/), RTF_AUTOINIT and
#     no start class, so ramlib makes it at the first OpenLibrary; built
#     with OPT= (default -Os, for size). The same code as LIBS:'s: its init
#     chooses the backend (library/card.c) and keeps nothing when there is
#     no card and no device. It fails if the library keeps data or BSS, or
#     leaves anything undefined.
set -eu
TARGET=all
if [ "${1:-}" = module ]; then TARGET=module; shift; fi
HERE=$(cd "$(dirname "$0")" && pwd)
# soft float: link libnix's libm020, not libm881 (specs/m68k-soft-m68040.specs)
SPECS="$(cd "$HERE/../../.." && pwd)/specs/m68k-soft-m68040.specs"
GUEST=$(cd "$HERE/../.." && pwd)
if [ -z "${STOVE:-}" ]; then
  echo "Set STOVE to an AmigaOS 3.x cross-build environment containing m68k-amigaos-gcc and standard NDK headers." >&2
  exit 2
fi
CC="$STOVE/prefix/bin/m68k-amigaos-gcc"
ABI="$HERE/abi/bsdsocket-v4.json"
GUARD="$HERE/compat/roadshow/guard-v4.json"
if [ "$TARGET" = module ]; then OUT=${OUT:-$HERE/build-amiga}; else OUT=${1:-${OUT:-$GUEST/build/guest/os32/acnet}}; fi
mkdir -p "$OUT"
BARE="-m68040 -specs=$SPECS -O2 -fno-delete-null-pointer-checks -include sys/types.h -fomit-frame-pointer -fno-toplevel-reorder -fno-builtin -Wall -Wno-pointer-sign -nostartfiles -nostdlib"
INC="-I$GUEST/common/protocol -I$HERE/include -I$GUEST/network/acnetwork/include"
# The library itself and its tools use the kernel-side network headers
# (net/route.h, net/bpf.h, sys/mbuf.h, netinet/*_var.h), which OpenNDK
# doesn't have yet (sdk/README.md); until it does, they build against the
# vendor NDK the stoves keep beside it, as before the stoves took OpenNDK.
if [ -d "$STOVE/prefix/m68k-amigaos/ndk-include.vendor" ]; then
  VENDOR_NDK="-I$STOVE/prefix/m68k-amigaos/ndk-include.vendor"
else
  VENDOR_NDK=""
fi
COMPAT_INC="-I$STOVE/ndk/SANA+RoadshowTCP-IP/include"
COMPAT_INC="$VENDOR_NDK $COMPAT_INC"
L="$HERE/library"
# lib_base.c first: its start() must be the library's first code.
LIB="$L/lib_base.c $L/lib_fd.c $L/lib_errno.c $L/lib_tags.c $L/lib_strings.c $L/lib_select.c $L/lib_conn.c $L/lib_io.c $L/lib_opt.c $L/lib_names.c $L/lib_inet.c $HERE/compat/roadshow/routes.c $HERE/compat/roadshow/state.c $HERE/compat/roadshow/modern.c $HERE/compat/roadshow/bpf.c"
# the card layer and the backends behind it (no bsd_ vectors in these)
PROV="$L/card.c $L/card_parse.c $L/provider.c $L/provider_acnetwork.c $L/provider_direct.c"
# Direct's stack (direct/, lwIP): built as a hunk image of its own, which
# the library carries as data (direct_image.c) and loads into RAM (romseg.c)

# nothing written but the bases: the same code runs from LIBS: or a ROM.
# Each check reads the unstripped link: the data and BSS symbols, then the
# size of each (__sdata..__edata, __bss_start..__end).
no_data() {
    NM="$STOVE/prefix/bin/m68k-amigaos-nm"
    und=$("$NM" -u "$1" || true)
    [ -z "$und" ] || { echo "undefined in $1: $und" >&2; exit 1; }
    data=$("$NM" "$1" | awk 'NF == 3 && $2 ~ /^[dDbBcC]$/ && $3 !~ /^__/ {print $3}' || true)
    [ -z "$data" ] || { echo "$1 keeps data or BSS: $data" >&2; exit 1; }
    ends=$("$NM" "$1" | awk 'NF == 3 { a[$3] = $1 }
        END { print (a["__sdata"] == a["__edata"] && a["__bss_start"] == a["__end"]) ? "none" :
              "data " a["__sdata"] ".." a["__edata"] ", BSS " a["__bss_start"] ".." a["__end"] }')
    [ "$ends" = none ] || { echo "$1 keeps data or BSS: $ends" >&2; exit 1; }
}

if [ "$TARGET" = module ]; then
    OPT=${OPT:--Os}
    mkdir -p "$OUT/modules"
    python3 "$HERE/library/gen_vectors.py" "$ABI" "$OUT/bsdsocket_vectors.c" $LIB --guard "$GUARD"
    MBARE=$(echo "$BARE" | sed "s/ -O2 / $OPT /")
    STOVE="$STOVE" OPT="$OPT" sh "$HERE/direct/build.sh" "$OUT/direct"
    "$CC" $MBARE $INC $COMPAT_INC -I"$HERE/library" -I"$HERE/direct" -I"$OUT" -o "$OUT/modules/bsdsocket.module" \
        $LIB $PROV "$HERE/direct/romseg.c" "$OUT/direct/direct_image.c" "$OUT/bsdsocket_vectors.c" "$GUEST/common/os3/string.c" -lgcc
    no_data "$OUT/modules/bsdsocket.module"
    echo "$OUT/modules/bsdsocket.module ($(wc -c < "$OUT/modules/bsdsocket.module") bytes, $OPT)"
    exit 0
fi

"$CC" $BARE $INC $COMPAT_INC -o "$OUT/opensocket.device" "$HERE/device/acnet_device.c" "$GUEST/common/os3/string.c" -lgcc
echo "$OUT/opensocket.device ($(wc -c < "$OUT/opensocket.device") bytes)"

STOVE="$STOVE" "$GUEST/network/acnetwork/build.sh" "$OUT"

python3 "$HERE/library/gen_vectors.py" "$ABI" "$OUT/bsdsocket_vectors.c" $LIB --guard "$GUARD"
STOVE="$STOVE" sh "$HERE/direct/build.sh" "$OUT/direct"
"$CC" $BARE $INC $COMPAT_INC -I"$HERE/library" -I"$HERE/direct" -I"$OUT" -o "$OUT/bsdsocket.library" \
    $LIB $PROV "$HERE/direct/romseg.c" "$OUT/direct/direct_image.c" "$OUT/bsdsocket_vectors.c" "$GUEST/common/os3/string.c" -lgcc
no_data "$OUT/bsdsocket.library"
echo "$OUT/bsdsocket.library ($(wc -c < "$OUT/bsdsocket.library") bytes)"

if [ -f "$HERE/tests/acnettest.c" ]; then
    "$CC" -m68040 -specs=$SPECS -O2 -fno-delete-null-pointer-checks -Wall -noixemul -o "$OUT/acnettest" "$HERE/tests/acnettest.c"
    echo "$OUT/acnettest ($(wc -c < "$OUT/acnettest") bytes)"
fi

if [ -f "$HERE/tests/bsdqual.c" ]; then
    "$CC" -m68040 -specs=$SPECS -O2 -fno-delete-null-pointer-checks -Wall -Wno-pointer-sign -noixemul -I"$GUEST/common/os3" -o "$OUT/bsdqual" \
        "$HERE/tests/bsdqual.c" "$GUEST/common/os3/acnet_stack.c"
    echo "$OUT/bsdqual ($(wc -c < "$OUT/bsdqual") bytes)"
fi

if [ -f "$HERE/tests/roadshow_readonly_probe.c" ]; then
    "$CC" -m68040 -specs=$SPECS -O2 -fno-delete-null-pointer-checks -Wall -Wno-pointer-sign -noixemul $VENDOR_NDK \
        -o "$OUT/roadshow-readonly-probe" "$HERE/tests/roadshow_readonly_probe.c"
    echo "$OUT/roadshow-readonly-probe ($(wc -c < "$OUT/roadshow-readonly-probe") bytes)"
fi

if [ -f "$HERE/tests/modern_compat_probe.c" ]; then
    "$CC" -m68040 -specs=$SPECS -O2 -fno-delete-null-pointer-checks -Wall -Wno-pointer-sign -noixemul $VENDOR_NDK \
        -o "$OUT/modern-compat-probe" "$HERE/tests/modern_compat_probe.c"
    echo "$OUT/modern-compat-probe ($(wc -c < "$OUT/modern-compat-probe") bytes)"
fi

if [ -f "$HERE/tests/bpfprobe.c" ]; then
    "$CC" -m68040 -specs=$SPECS -O2 -fno-delete-null-pointer-checks -Wall -Wno-pointer-sign -noixemul $COMPAT_INC \
        -o "$OUT/bpfprobe" "$HERE/tests/bpfprobe.c"
    echo "$OUT/bpfprobe ($(wc -c < "$OUT/bpfprobe") bytes)"
fi

if [ -f "$HERE/tests/sana2probe.c" ]; then
    "$CC" -m68040 -specs=$SPECS -O2 -fno-delete-null-pointer-checks -Wall -Wno-pointer-sign -noixemul $INC $COMPAT_INC \
        -o "$OUT/sana2probe" "$HERE/tests/sana2probe.c"
    echo "$OUT/sana2probe ($(wc -c < "$OUT/sana2probe") bytes)"
fi
if [ -f "$HERE/tests/directprobe.c" ]; then
    "$CC" -m68040 -specs=$SPECS -O2 -fno-delete-null-pointer-checks -Wall -Wno-format -noixemul $COMPAT_INC \
        -o "$OUT/directprobe" "$HERE/tests/directprobe.c"
    echo "$OUT/directprobe ($(wc -c < "$OUT/directprobe") bytes)"
fi

# OpenSocketControl: the Commodity, in GadTools on the shared core (AmigaOS
# 3.x programs are GadTools or MUI). control/acnetcontrol.c, the retired
# ReAction version, is no longer built; it stays as the reference for the
# pages OpenSocketControl still lacks (live control, diagnostics, log, Wi-Fi).
# -fno-common: the program's own library bases must win over libnix's
# auto-open stubs.
# No -lamiga here: the compiler already links amiga.lib after libnix (for
# NewList and HotKey). Named on the command line, amiga.lib came first, and
# its sprintf (RawDoFmt's: %u and %d read 16 bits, no %llu) replaced libnix's.
# Each address byte then printed as two numbers, so 192.168.x.x showed as
# 0.192.0.168 (12 Oct 2026). The link map is checked for that below.
if [ -f "$HERE/control/acnetcontrol_gt.c" ]; then
    "$CC" -m68040 -specs=$SPECS -O2 -fno-delete-null-pointer-checks -fno-common -Wall -Wno-pointer-sign -noixemul -I"$HERE/include" \
        -I"$GUEST/network/acnetwork/include" -I"$GUEST/network/acwifi/include" \
        -Wl,-Map="$OUT/OpenSocketControl.map" \
        -o "$OUT/OpenSocketControl" "$HERE/control/acnetcontrol_gt.c" "$HERE/control/acnetcontrol_core.c"
    if grep -Eq 'libamiga\.a\((v?s?printf)\.o\)' "$OUT/OpenSocketControl.map"; then
        echo "OpenSocketControl: printf or sprintf came from amiga.lib, whose %u is 16 bits" >&2
        exit 1
    fi
    echo "$OUT/OpenSocketControl ($(wc -c < "$OUT/OpenSocketControl") bytes)"
fi

if [ -f "$GUEST/network/acwifi/build.sh" ]; then
    sh "$GUEST/network/acwifi/build.sh" "$OUT"
fi
