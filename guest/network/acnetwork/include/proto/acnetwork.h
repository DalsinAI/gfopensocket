#ifndef PROTO_ACNETWORK_H
#define PROTO_ACNETWORK_H

#include <exec/libraries.h>
#include <acnetwork.h>

extern struct Library *ACNetworkBase;

static inline LONG ACNetwork_Call(struct ACNetworkRequest *request)
{
    register struct ACNetworkRequest *a0 __asm("a0") = request;
    register struct Library *a6 __asm("a6") = ACNetworkBase;
    register LONG d0 __asm("d0");
    __asm volatile ("jsr -30(%%a6)" : "=r"(d0), "+r"(a0) : "r"(a6) : "d1", "a1", "cc", "memory");
    return d0;
}

static inline void ACNetwork_Arm(void)
{
    register struct Library *a6 __asm("a6") = ACNetworkBase;
    __asm volatile ("jsr -36(%%a6)" : : "r"(a6) : "d0", "d1", "a0", "a1", "cc", "memory");
}

static inline void ACNetwork_Disarm(void)
{
    register struct Library *a6 __asm("a6") = ACNetworkBase;
    __asm volatile ("jsr -42(%%a6)" : : "r"(a6) : "d0", "d1", "a0", "a1", "cc", "memory");
}

static inline ULONG ACNetwork_SignalMask(void)
{
    register struct Library *a6 __asm("a6") = ACNetworkBase;
    register ULONG d0 __asm("d0");
    __asm volatile ("jsr -48(%%a6)" : "=r"(d0) : "r"(a6) : "d1", "a0", "a1", "cc", "memory");
    return d0;
}

static inline ULONG ACNetwork_LastError(void)
{
    register struct Library *a6 __asm("a6") = ACNetworkBase;
    register ULONG d0 __asm("d0");
    __asm volatile ("jsr -54(%%a6)" : "=r"(d0) : "r"(a6) : "d1", "a0", "a1", "cc", "memory");
    return d0;
}

static inline ULONG ACNetwork_State(void)
{
    register struct Library *a6 __asm("a6") = ACNetworkBase;
    register ULONG d0 __asm("d0");
    __asm volatile ("jsr -60(%%a6)" : "=r"(d0) : "r"(a6) : "d1", "a0", "a1", "cc", "memory");
    return d0;
}

/* 1.2 (ACNETWORK_WAITERS_REVISION): w is a struct ACNWaiter (acnet_device.h) */
static inline void ACNetwork_AddWaiter(APTR w)
{
    register APTR a0 __asm("a0") = w;
    register struct Library *a6 __asm("a6") = ACNetworkBase;
    __asm volatile ("jsr -66(%%a6)" : "+r"(a0) : "r"(a6) : "d0", "d1", "a1", "cc", "memory");
}

static inline void ACNetwork_RemWaiter(APTR w)
{
    register APTR a0 __asm("a0") = w;
    register struct Library *a6 __asm("a6") = ACNetworkBase;
    __asm volatile ("jsr -72(%%a6)" : "+r"(a0) : "r"(a6) : "d0", "d1", "a1", "cc", "memory");
}

#endif
