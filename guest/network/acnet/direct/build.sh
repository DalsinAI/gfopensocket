#!/bin/sh
# GFOpenSocketDirect's image: lwIP (lwip/, BSD licence) and GFOpenSocket's glue,
# linked as one hunk executable that keeps its own globals, then turned into
# C data (direct_image.c) for bsdsocket.library to carry and load into RAM
# when the card layer chooses Direct (provider_direct.c, romseg.c).
#   STOVE=... sh build.sh OUT_DIR       writes OUT_DIR/gfopensocketdirect.image
#                                       and OUT_DIR/direct_image.c
# OPT= (default -Os). MIT.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
# soft float: link libnix's libm020, not libm881 (specs/m68k-soft-m68040.specs)
SPECS="$(cd "$HERE/../../../.." && pwd)/specs/m68k-soft-m68040.specs"
GUEST=$(cd "$HERE/../../.." && pwd)
OUT=${1:?usage: build.sh OUT_DIR}
[ -n "${STOVE:-}" ] || { echo "Set STOVE" >&2; exit 2; }
CC="$STOVE/prefix/bin/m68k-amigaos-gcc"
OPT=${OPT:--Os}
mkdir -p "$OUT"
L="$HERE/lwip/src"
LWIP="$L/core/def.c $L/core/dns.c $L/core/inet_chksum.c $L/core/init.c $L/core/ip.c $L/core/mem.c
  $L/core/memp.c $L/core/netif.c $L/core/pbuf.c $L/core/raw.c $L/core/tcp.c $L/core/tcp_in.c
  $L/core/tcp_out.c $L/core/timeouts.c $L/core/udp.c $L/core/ipv4/dhcp.c $L/core/ipv4/etharp.c
  $L/core/ipv4/icmp.c $L/core/ipv4/ip4.c $L/core/ipv4/ip4_addr.c $L/core/ipv4/ip4_frag.c
  $L/netif/ethernet.c"
# direct_stack.c first: its RomTag is the first in the image
# shellcheck disable=SC2086
"$CC" -m68040 -specs=$SPECS $OPT ${DIRECT_CFLAGS:-} -s -fomit-frame-pointer -fno-builtin -Wall -Wno-pointer-sign -Werror -nostartfiles -nostdlib \
  -I"$HERE/port" -I"$L/include" -I"$HERE" -I"$HERE/../library" -I"$GUEST/common/protocol" \
  -I"$GUEST/network/acnetwork/include" -I"$HERE/../include" -I"$STOVE/ndk/SANA+RoadshowTCP-IP/include" \
  -o "$OUT/gfopensocketdirect.image" "$HERE/direct_stack.c" "$HERE/direct_sock.c" $LWIP \
  "$GUEST/common/os3/string.c" -lgcc
python3 - "$OUT/gfopensocketdirect.image" "$OUT/direct_image.c" <<'PY'
import sys
b = open(sys.argv[1], 'rb').read()
assert len(b) % 4 == 0 and b[:4] == b'\0\0\x03\xf3', 'not a hunk executable'
w = [int.from_bytes(b[i:i+4], 'big') for i in range(0, len(b), 4)]
with open(sys.argv[2], 'w') as f:
    f.write('/* Made by direct/build.sh from gfopensocketdirect.image: do not edit. */\n')
    f.write('const unsigned long direct_image_longs = %d;\n' % len(w))
    f.write('const unsigned long direct_image[%d] = {\n' % len(w))
    for i in range(0, len(w), 8):
        f.write('  ' + ', '.join('0x%08x' % x for x in w[i:i+8]) + ',\n')
    f.write('};\n')
PY
echo "$OUT/gfopensocketdirect.image ($(wc -c < "$OUT/gfopensocketdirect.image") bytes, $OPT)"
