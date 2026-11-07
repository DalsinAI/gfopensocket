/* ACNet bsdsocket.library: the library itself. Its ROMTag, its init, and a
 * base per OpenLibrary: the master's jump table and Library header are
 * copied in front of each opener's own state, as classic Amiga socket libraries do,
 * so errno, signals and descriptors belong to the opener. MIT. */
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/resident.h>
#include <exec/execbase.h>
#include <dos/dos.h>
#include <proto/exec.h>

#include "lib_internal.h"

/* Run as a program, the library does nothing. This must stay the first code
 * in the library: build.sh links this file first and keeps source order. */
int start(void) { return -1; }

static const char lib_name[] = "bsdsocket.library";
static const char lib_id[] = "bsdsocket.library 4.5 (7.11.2026) GFOpenSocket (AmiCradle)\r\n";
static const char lib_ver[] __attribute__((used)) = "$VER: bsdsocket.library 4.5 (7.11.2026) GFOpenSocket (AmiCradle)";

extern const APTR acnet_vectors[];

static struct SocketBase *lib_init(REG(d0, struct SocketBase *b), REG(a0, BPTR seglist), REG(a6, struct ExecBase *sys))
{
    (void)sys;
    b->seglist = seglist;
    b->master = NULL;
    if (!(b->shared = AllocMem(sizeof(struct SocketShared), MEMF_PUBLIC | MEMF_CLEAR))) {
        FreeMem((UBYTE *)b - b->lib.lib_NegSize, b->lib.lib_NegSize + b->lib.lib_PosSize);
        return NULL;
    }
    /* The card layer: no card and no device, and the init keeps nothing.
     * From a ROM, ramlib then goes on to LIBS:bsdsocket.library (another
     * stack); from LIBS:, OpenLibrary fails, as with no stack at all. */
    if (card_choose(&b->shared->card) == BACKEND_NONE) {
        FreeMem(b->shared, sizeof(struct SocketShared));
        FreeMem((UBYTE *)b - b->lib.lib_NegSize, b->lib.lib_NegSize + b->lib.lib_PosSize);
        return NULL;
    }
    if (b->shared->card.backend == BACKEND_DIRECT) direct_load(b->shared);
    b->shared->next_id = 1;
    b->lib.lib_Node.ln_Type = NT_LIBRARY;
    b->lib.lib_Node.ln_Name = (char *)lib_name;
    b->lib.lib_Flags = LIBF_SUMUSED | LIBF_CHANGED;
    b->lib.lib_Version = ACNET_LIB_VERSION;
    b->lib.lib_Revision = ACNET_LIB_REVISION;
    b->lib.lib_IdString = (APTR)lib_id;
    return b;
}

/* A base of the opener's own. */
struct SocketBase *lib_open(REG(d0, ULONG version), REG(a6, struct SocketBase *m))
{
    (void)version;
    m->lib.lib_OpenCnt++;
    m->lib.lib_Flags &= ~LIBF_DELEXP;
    ULONG neg = m->lib.lib_NegSize, total = neg + m->lib.lib_PosSize;
    UBYTE *mem = AllocMem(total, MEMF_PUBLIC | MEMF_CLEAR);
    if (!mem) { m->lib.lib_OpenCnt--; return NULL; }
    CopyMem((UBYTE *)m - neg, mem, neg + sizeof(struct Library));
    CacheClearU();                                  /* the copied jump table is code */
    struct SocketBase *sb = (struct SocketBase *)(mem + neg);
    sb->lib.lib_Node.ln_Succ = sb->lib.lib_Node.ln_Pred = NULL;   /* not in the library list */
    sb->lib.lib_OpenCnt = 1;
    sb->master = m;
    sb->shared = m->shared;
    sb->owner = FindTask(NULL);
    sb->sigbit = -1;
    sb->sigintr = SIGBREAKF_CTRL_C;
    sb->dtablesize = DTABLE_DEFAULT;
    sb->fds = AllocMem(DTABLE_DEFAULT * sizeof(struct FD), MEMF_PUBLIC | MEMF_CLEAR);
    if (!sb->fds || prov_open(sb)) {
        if (sb->fds) FreeMem(sb->fds, DTABLE_DEFAULT * sizeof(struct FD));
        FreeMem(mem, total);
        m->lib.lib_OpenCnt--;
        return NULL;
    }
    return sb;
}

BPTR lib_expunge(REG(a6, struct SocketBase *m))
{
    if (m->master) return 0;                       /* only the master leaves the list */
    if (m->lib.lib_OpenCnt) { m->lib.lib_Flags |= LIBF_DELEXP; return 0; }
    BPTR seg = m->seglist;
    Remove(&m->lib.lib_Node);
    FreeMem(m->shared, sizeof(struct SocketShared));
    FreeMem((UBYTE *)m - m->lib.lib_NegSize, m->lib.lib_NegSize + m->lib.lib_PosSize);
    return seg;
}

/* The opener's sockets close, then its base goes. */
BPTR lib_close(REG(a6, struct SocketBase *sb))
{
    struct SocketBase *m = sb->master;
    if (!m) return 0;
    for (LONG s = 0; s < sb->dtablesize; s++)
        if (sb->fds[s].handle) fd_free(sb, s);
    names_close(sb);
    timer_close(sb);
    bpf_close_all(sb);
    prov_close(sb);
    if (sb->scratch) FreeMem(sb->scratch, SCRATCH_SIZE);
    FreeMem(sb->fds, sb->dtablesize * sizeof(struct FD));
    FreeMem((UBYTE *)sb - sb->lib.lib_NegSize, sb->lib.lib_NegSize + sb->lib.lib_PosSize);
    if (--m->lib.lib_OpenCnt == 0 && (m->lib.lib_Flags & LIBF_DELEXP)) return lib_expunge(m);
    return 0;
}

ULONG lib_reserved(void) { return 0; }

/* A vector ACNet does not provide (the generated stubs call this). */
void lib_unimplemented(struct SocketBase *sb) { set_errno(sb, AE_NOSYS); }

static const struct { ULONG size; const APTR *vectors; APTR data; APTR init; } lib_inittable = {
    sizeof(struct SocketBase), acnet_vectors, NULL, (APTR)lib_init,
};
const struct Resident lib_romtag = {
    RTC_MATCHWORD, (struct Resident *)&lib_romtag, (APTR)(&lib_romtag + 1),
    RTF_AUTOINIT, ACNET_LIB_VERSION, NT_LIBRARY, 0, (char *)lib_name, (char *)lib_id, (APTR)&lib_inittable,
};
