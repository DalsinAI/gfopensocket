#!/bin/sh
# Build acwifi.device, the product-6 ACNet Wi-Fi control facade.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
# soft float: link libnix's libm020, not libm881 (specs/m68k-soft-m68040.specs)
SPECS="$(cd "$HERE/../../.." && pwd)/specs/m68k-soft-m68040.specs"
GUEST=$(cd "$HERE/../.." && pwd)
STOVE=${STOVE:-$HOME/AmiCradle/stoves/os32}
CC="$STOVE/prefix/bin/m68k-amigaos-gcc"
OUT=${1:-$GUEST/build/guest/os32/acwifi}
mkdir -p "$OUT"

BARE="-m68040 -specs=$SPECS -O2 -fno-delete-null-pointer-checks -fomit-frame-pointer -fno-toplevel-reorder -fno-builtin -Wall -Wno-pointer-sign -nostartfiles -nostdlib"
INC="-I$GUEST/network/acnetwork/include -I$HERE/include"

"$CC" $BARE $INC -o "$OUT/gfopensocketwifi.device" \
    "$HERE/device/acwifi_device.c" -lgcc
echo "$OUT/gfopensocketwifi.device ($(wc -c < "$OUT/gfopensocketwifi.device") bytes)"

if [ -f "$HERE/tests/acwifitest.c" ]; then
    "$CC" -m68040 -specs=$SPECS -O2 -fno-delete-null-pointer-checks -Wall -Wno-pointer-sign -noixemul $INC \
        -o "$OUT/acwifitest" "$HERE/tests/acwifitest.c"
    echo "$OUT/acwifitest ($(wc -c < "$OUT/acwifitest") bytes)"
fi
