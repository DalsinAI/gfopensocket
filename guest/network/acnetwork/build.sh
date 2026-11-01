#!/bin/sh
# Build acnetwork.library, the native ACNet session API for AmigaOS 3.x.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
# soft float: link libnix's libm020, not libm881 (specs/m68k-soft-m68040.specs)
SPECS="$(cd "$HERE/../../.." && pwd)/specs/m68k-soft-m68040.specs"
GUEST=$(cd "$HERE/../.." && pwd)
ACNET="$GUEST/network/acnet"
if [ -z "${STOVE:-}" ]; then
  echo "Set STOVE to the AmigaOS 3.x cross-build stove." >&2
  exit 2
fi
CC="$STOVE/prefix/bin/m68k-amigaos-gcc"
OUT=${1:-$GUEST/build/guest/os32/acnet}
mkdir -p "$OUT"
BARE="-m68040 -specs=$SPECS -O2 -fno-delete-null-pointer-checks -include sys/types.h -fomit-frame-pointer -fno-toplevel-reorder -fno-builtin -Wall -Wno-pointer-sign -nostartfiles -nostdlib"
INC="-I$GUEST/common/protocol -I$HERE/include -I$ACNET/include"

"$CC" $BARE $INC -I"$HERE/library" -o "$OUT/gfopensocket.library" \
  "$HERE/library/acnetwork.c" -lgcc
echo "$OUT/gfopensocket.library ($(wc -c < "$OUT/gfopensocket.library") bytes)"
