/* Copyright (c) 2026 AmiCradle Team. MIT licence (see LICENSE at the repository root).
 *
 * romseg: a hunk executable loaded from the ROM into RAM (romseg.h).
 * No writable globals: it runs from the ROM.
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/resident.h>
#include <exec/execbase.h>
#include <dos/dos.h>
#include <proto/exec.h>

#include "romseg.h"

/* exec's base, read from address 4 (AbsExecBase) through a register, as
 * gcc 16's -Warray-bounds wants for a pointer made from a constant */
static inline struct ExecBase *rs_sysbase(void)
{ struct ExecBase **p = (struct ExecBase **)4; __asm__ ("" : "+r"(p)); return *p; }
#define SysBase (rs_sysbase())

/* dos/doshunks.h's numbers */
#define RS_HUNK_NAME         0x3E8
#define RS_HUNK_CODE         0x3E9
#define RS_HUNK_DATA         0x3EA
#define RS_HUNK_BSS          0x3EB
#define RS_HUNK_RELOC32      0x3EC
#define RS_HUNK_SYMBOL       0x3F0
#define RS_HUNK_DEBUG        0x3F1
#define RS_HUNK_END          0x3F2
#define RS_HUNK_HEADER       0x3F3
#define RS_HUNK_DREL32       0x3F7     /* in an executable: RELOC32SHORT, as LoadSeg reads it */
#define RS_HUNK_RELOC32SHORT 0x3FC
#define RS_MAX_HUNKS         16

/* A segment: [0] its allocation's size in bytes, [1] the next segment's
 * BPTR, then the hunk. The seglist's BPTR points at [1], as LoadSeg's. */
static ULONG *seg_of(BPTR b) { return (ULONG *)BADDR(b) - 1; }

void rs_unload(BPTR seglist)
{
    while (seglist) {
        ULONG *m = seg_of(seglist);
        seglist = (BPTR)m[1];
        FreeMem(m, m[0]);
    }
}

BPTR rs_load(const ULONG *p, ULONG longs)
{
    const ULONG *end = p + longs;
    ULONG *seg[RS_MAX_HUNKS];
    ULONG bytes[RS_MAX_HUNKS];
    ULONG n, first, last, count, i;
    BPTR list = 0;

    if (longs < 5 || *p++ != RS_HUNK_HEADER) return 0;
    while (p < end && (n = *p++) != 0) p += n;   /* resident library names: none in a LoadSeg file */
    if (p + 3 > end) return 0;
    p++;                                          /* the table's size */
    first = *p++;
    last = *p++;
    if (last < first || last - first >= RS_MAX_HUNKS) return 0;
    count = last - first + 1;
    for (i = 0; i < count; i++) {
        ULONG s, req = MEMF_PUBLIC | MEMF_CLEAR;
        if (p >= end) goto bad;
        s = *p++;
        if ((s & 0xC0000000UL) == 0xC0000000UL) {
            if (p >= end) goto bad;
            req = *p++ | MEMF_CLEAR;
        } else if (s & 0x40000000UL) req |= MEMF_CHIP;
        else if (s & 0x80000000UL) req |= MEMF_FAST;
        s &= 0x3FFFFFFFUL;
        bytes[i] = s * 4;
        if (!(seg[i] = AllocMem(s * 4 + 8, req))) goto bad;
        seg[i][0] = s * 4 + 8;
        seg[i][1] = 0;
        if (i) seg[i - 1][1] = (ULONG)MKBADDR(&seg[i][1]);
        else list = MKBADDR(&seg[0][1]);
    }
    i = 0;
    while (i < count) {
        ULONG t;
        if (p >= end) goto bad;
        t = *p++ & 0x3FFFFFFFUL;
        switch (t) {
        case RS_HUNK_CODE:
        case RS_HUNK_DATA:
            if (p >= end) goto bad;
            n = *p++ & 0x3FFFFFFFUL;
            if (n * 4 > bytes[i] || p + n > end) goto bad;
            CopyMemQuick((APTR)p, &seg[i][2], n * 4);
            p += n;
            break;
        case RS_HUNK_BSS:
            p++;                                  /* AllocMem cleared it */
            break;
        case RS_HUNK_RELOC32:
            for (;;) {
                ULONG h, base;
                if (p >= end) goto bad;
                if (!(n = *p++)) break;
                if (p + 1 + n > end) goto bad;
                h = *p++ - first;
                if (h >= count) goto bad;
                base = (ULONG)&seg[h][2];
                while (n--) {
                    ULONG off = *p++;
                    if (off + 4 > bytes[i]) goto bad;
                    *(ULONG *)((UBYTE *)&seg[i][2] + off) += base;
                }
            }
            break;
        case RS_HUNK_RELOC32SHORT:
        case RS_HUNK_DREL32: {
            const UWORD *w = (const UWORD *)p, *wend = (const UWORD *)end;
            for (;;) {
                ULONG h, base;
                if (w >= wend) goto bad;
                if (!(n = *w++)) break;
                if (w + 1 + n > wend) goto bad;
                h = *w++ - first;
                if (h >= count) goto bad;
                base = (ULONG)&seg[h][2];
                while (n--) {
                    ULONG off = *w++;
                    if (off + 4 > bytes[i]) goto bad;
                    *(ULONG *)((UBYTE *)&seg[i][2] + off) += base;
                }
            }
            p = (const ULONG *)(((ULONG)w + 3) & ~3UL);   /* the block ends on a longword */
            break;
        }
        case RS_HUNK_SYMBOL:
            for (;;) {
                if (p >= end) goto bad;
                if (!(n = *p++ & 0x00FFFFFFUL)) break;
                p += n + 1;
            }
            break;
        case RS_HUNK_NAME:
        case RS_HUNK_DEBUG:
            if (p >= end) goto bad;
            n = *p++;
            p += n;
            break;
        case RS_HUNK_END:
            i++;
            break;
        default:
            goto bad;
        }
    }
    CacheClearU();
    return list;
bad:
    rs_unload(list);
    return 0;
}

struct Resident *rs_romtag(BPTR seglist)
{
    while (seglist) {
        ULONG *m = seg_of(seglist);
        UBYTE *data = (UBYTE *)&m[2];
        ULONG len = m[0] > 8 ? m[0] - 8 : 0;
        for (ULONG off = 0; off + sizeof(struct Resident) <= len; off += 2) {
            struct Resident *r = (struct Resident *)(data + off);
            if (r->rt_MatchWord == RTC_MATCHWORD && r->rt_MatchTag == r) return r;
        }
        seglist = (BPTR)m[1];
    }
    return NULL;
}
