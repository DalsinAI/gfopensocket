/* A stand-in for tests/host. Not the NDK's header. MIT. */
#ifndef EXEC_SEMAPHORES_H
#define EXEC_SEMAPHORES_H
#include <exec/types.h>
struct SignalSemaphore { ULONG ss_stub[12]; };
#endif
