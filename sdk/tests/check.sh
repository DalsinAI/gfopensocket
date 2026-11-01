#!/bin/sh
# Copyright (c) 2026 DalsinAI. MIT licence (see LICENSE).
# sdk/tests/check.sh [OPENNDK]: the SFD matches the ABI manifests; with
# OPENNDK (a GFOpenNDK Include_H holding these headers and the generated
# bsdsocket ones) and STOVE (default ~/AmiCradle/stoves/os32-gcc16), each
# header compiles on its own in C and C++, and in the orders programs use.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
python3 "$HERE/tools/mksfd.py" --check
[ -n "${1:-}" ] || exit 0
NDK=$1
STOVE=${STOVE:-$HOME/AmiCradle/stoves/os32-gcc16}
P=$STOVE/prefix/bin/m68k-amigaos-
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
bad=0
for h in $(cd "$HERE/Include_H" && find . -name '*.h' | sed 's|^\./||' | sort) proto/bsdsocket.h; do
    printf '#include <%s>\n' "$h" > "$T/h.c"; cp "$T/h.c" "$T/h.cpp"
    "${P}gcc" -I"$NDK" -Wall -Wextra -Werror -fsyntax-only "$T/h.c" || { echo "FAIL $h (C)"; bad=1; }
    "${P}g++" -I"$NDK" -Wall -Wextra -Werror -fsyntax-only "$T/h.cpp" || { echo "FAIL $h (C++)"; bad=1; }
done
for order in "sys/socket.h netinet/in.h proto/bsdsocket.h arpa/inet.h netdb.h" \
             "proto/bsdsocket.h sys/socket.h arpa/inet.h netdb.h syslog.h pwd.h sys/ioctl.h net/if.h" \
             "unistd.h sys/types.h proto/bsdsocket.h" "sys/types.h sys/select.h sys/socket.h netinet/tcp.h"; do
    { for h in $order; do echo "#include <$h>"; done
      echo 'struct Library *SocketBase;'
      echo 'int f(void) { fd_set s; FD_ZERO(&s); FD_SET(3, &s); return socket(AF_INET, SOCK_STREAM, 0) + FD_ISSET(3, &s); }'; } > "$T/o.c"
    "${P}gcc" -I"$NDK" -Wall -Werror -c "$T/o.c" -o "$T/o.o" || { echo "FAIL order: $order"; bad=1; }
done
[ $bad = 0 ] && echo "all GFOpenSocket SDK checks passed"
exit $bad
