/* lwIP's platform layer for building GFOpenSocketDirect's socket layer on the
 * build machine (tests/host), in place of direct/port/arch/cc.h (the 68k's):
 * the C library's sys/types.h gives BYTE_ORDER. MIT. */
#ifndef OPENSOCKET_HOST_ARCH_CC_H
#define OPENSOCKET_HOST_ARCH_CC_H
#include <sys/types.h>
#define LWIP_PLATFORM_DIAG(x)   do { } while (0)
#define LWIP_PLATFORM_ASSERT(x) do { } while (0)
#endif
