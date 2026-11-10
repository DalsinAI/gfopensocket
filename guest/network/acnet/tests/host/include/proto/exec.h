/* exec's CopyMem, for building GFOpenSocketDirect's socket layer on the build
 * machine (tests/host). Not the NDK's header. MIT. */
#ifndef PROTO_EXEC_H
#define PROTO_EXEC_H
#include <string.h>
#include <exec/types.h>
#define CopyMem(s, d, n) memmove((d), (s), (n))
#endif
