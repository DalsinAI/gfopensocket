/* Copyright (c) 2026 AmiCradle Team. MIT licence (see LICENSE at the repository root).
 *
 * romseg: a hunk executable kept in the ROM as data, loaded into RAM the
 * way dos's LoadSeg loads one from a file: each hunk in memory of its own
 * (a longword of size, the next segment's BPTR, then the hunk), DATA and
 * CODE copied, BSS cleared, RELOC32 and RELOC32SHORT applied, caches
 * cleared. It needs no dos, so a RomTag can run it before DOS.
 *
 * It is for a module that keeps writable globals (its own patch vectors,
 * tables in BSS), which a ROM cannot hold: the ROM keeps the file, RAM
 * holds the running copy. Written from the AmigaDOS hunk format
 * (dos/doshunks.h) and the dos autodocs' LoadSeg; no code from elsewhere.
 */
#ifndef OPENRTG_ROMSEG_H
#define OPENRTG_ROMSEG_H

#include <exec/types.h>
#include <exec/resident.h>

/* The image at img, longs longwords long, loaded: its seglist, or 0 when
 * the image is damaged or memory ran out (nothing is kept then). */
BPTR rs_load(const ULONG *img, ULONG longs);

/* A seglist made by rs_load freed, as UnLoadSeg would. 0 does nothing. */
void rs_unload(BPTR seglist);

/* The first RomTag in the seglist (rt_MatchWord $4AFC, rt_MatchTag
 * pointing at itself), or NULL. */
struct Resident *rs_romtag(BPTR seglist);

#endif
