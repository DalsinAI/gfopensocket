/* GFOpenSocket's card layer: the backend is chosen once, at the library's
 * init. That is its first OpenLibrary, which ramlib makes after DOS, in
 * ramlib's own process, so the configuration can be read from disk. The
 * disk comes first, then the default:
 *
 *  1. A network driver or configuration on the disk:
 *     a. ENV:GFOpenSocket/Interface (or ENVARC:GFOpenSocket/Interface) names a
 *        backend, or a device: that backend;
 *     b. a Roadshow interface file in DEVS:NetInterfaces/ names a SANA-II
 *        device: Direct, GFOpenSocket's own stack over that device;
 *     c. a SANA-II driver in DEVS:Networks/ other than GFOpenSocket's own
 *        (gfopensocket*.device, acnet.device): Direct over its unit 0.
 *  2. The default: the GFOpenSocket card is fitted (Zorro II, AmiCradle Team,
 *     product 6): Host, through gfopensocket.library and gfopensocket.device.
 *     Each is the ROM's copy, unless the disk's (LIBS:gfopensocket.library,
 *     DEVS:Networks/gfopensocket.device) has a higher version.revision; on
 *     equal versions the ROM's stays. The one chosen is started here, once,
 *     so every later open, by name or by path, finds it in memory.
 *  3. None of these: no backend. The init keeps nothing, so ramlib goes on
 *     to LIBS:bsdsocket.library (another stack), as for any ROM library
 *     whose init kept nothing.
 *
 * With no address configured, the interface is addressed by DHCP (cfg->dhcp).
 *
 * The files are keyword=value lines ("keyword value" is taken too), with
 * '#' or ';' starting a comment. GFOpenSocket's own keywords: backend
 * (host, direct or none), device, unit, configure (dhcp), address, netmask,
 * gateway and nameserver (up to three). From a Roadshow interface file only
 * its documented keywords are read: device, unit, configure=dhcp, address
 * and netmask; the rest is skipped. Name servers for Direct come from
 * DEVS:Internet/name_resolution's "nameserver" lines when the GFOpenSocket
 * file names none. No requester is shown for a missing ENV: or DEVS:.
 *
 * Nothing here is kept in a global: the choice goes into the master
 * base's shared block (acnet_lib.h), so the library runs from a ROM. MIT. */
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/execbase.h>
#include <exec/resident.h>
#include <exec/io.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/exall.h>
#include <libraries/configvars.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/expansion.h>

#include "acnet_lib.h"
#include "achostsocket.h"
#include "acnetwork.h"
#include "card.h"

#define WORK_FILE 2048   /* the most of one file that is read */
#define WORK_PATH 160

struct Work {
    char path[WORK_PATH];
    char buf[WORK_FILE + 1];
};

/* A file into w->buf: its length, or -1 when it can't be read. */
static LONG read_file(struct DosLibrary *DOSBase, struct Work *w, const char *name)
{
    BPTR fh = Open((STRPTR)name, MODE_OLDFILE);
    LONG n;
    if (!fh) return -1;
    n = Read(fh, w->buf, WORK_FILE);
    Close(fh);
    if (n < 0) return -1;
    w->buf[n] = 0;
    return n;
}

static LONG parse_file(struct DosLibrary *DOSBase, struct Work *w, struct CardConfig *cfg,
                       const char *name, int ours)
{
    LONG n = read_file(DOSBase, w, name);
    return n < 0 ? -1 : card_parse(cfg, w->buf, n, ours);
}

/* dir/name into w->path; 0 when it doesn't fit. */
static int join(struct Work *w, const char *dir, const char *nm, LONG len)
{
    LONG i = 0;
    while (dir[i]) i++;
    if (i + len + 2 > WORK_PATH) return 0;
    for (i = 0; dir[i]; i++) w->path[i] = dir[i];
    w->path[i++] = '/';
    card_copy(w->path + i, WORK_PATH - i, nm, len);
    return 1;
}

static int starts_with(const char *nm, LONG len, const char *want)
{
    LONG i;
    for (i = 0; want[i]; i++) {
        char c = i < len ? nm[i] : 0;
        if (c >= 'A' && c <= 'Z') c += 32;
        if (c != want[i]) return 0;
    }
    return 1;
}

typedef int (*one_fn)(struct DosLibrary *, struct Work *, struct CardConfig *, const char *, LONG);

/* Each plain file in dir (not icons, not hidden names) is offered to one();
 * the first it takes (1) ends the walk. */
static int each_file(struct DosLibrary *DOSBase, struct Work *w, struct CardConfig *cfg,
                     const char *dir, one_fn one)
{
    struct FileInfoBlock *fib;
    BPTR lock = Lock((STRPTR)dir, SHARED_LOCK);
    int found = 0;
    if (!lock) return 0;
    fib = AllocDosObject(DOS_FIB, NULL);
    if (fib && Examine(lock, fib)) {
        while (!found && ExNext(lock, fib)) {
            const char *nm = (const char *)fib->fib_FileName;
            LONG len = 0;
            while (nm[len]) len++;
            if (fib->fib_DirEntryType >= 0 || !len || nm[0] == '.' || nm[0] == '~') continue;
            if (len >= 5 && card_key_is(nm + len - 5, 5, ".info")) continue;
            if (!join(w, dir, nm, len)) continue;
            found = one(DOSBase, w, cfg, nm, len);
        }
    }
    if (fib) FreeDosObject(DOS_FIB, fib);
    UnLock(lock);
    return found;
}

/* A Roadshow interface file that names a device: 1, with cfg filled from it. */
static int one_interface(struct DosLibrary *DOSBase, struct Work *w, struct CardConfig *cfg,
                         const char *nm, LONG len)
{
    struct CardConfig one = *cfg;
    if (parse_file(DOSBase, w, &one, w->path, 0) > 0 && one.device[0]) {
        *cfg = one;
        card_copy(cfg->ifname, sizeof(cfg->ifname), nm, len);
        return 1;
    }
    return 0;
}

/* A SANA-II driver that is not GFOpenSocket's own: 1, with cfg naming it. */
static int one_driver(struct DosLibrary *DOSBase, struct Work *w, struct CardConfig *cfg,
                      const char *nm, LONG len)
{
    LONG plen = 0;
    (void)DOSBase;
    if (len < 8 || !card_key_is(nm + len - 7, 7, ".device")) return 0;
    if (starts_with(nm, len, "gfopensocket") || starts_with(nm, len, "acnet.")) return 0;
    while (w->path[plen]) plen++;
    card_copy(cfg->device, sizeof(cfg->device), w->path, plen);
    cfg->unit = 0;
    return 1;
}

/* The GFOpenSocket card (Zorro II, AmiCradle Team or the old 2011, product 6). */
static int card_fitted(void)
{
    struct Library *ExpansionBase = OpenLibrary((CONST_STRPTR)"expansion.library", 0);
    struct ConfigDev *cd;
    if (!ExpansionBase) return 0;
    cd = FindConfigDev(NULL, ACNET_MANUFACTURER, ACNET_PRODUCT);
    if (!cd) cd = FindConfigDev(NULL, ACNET_MANUFACTURER_OLD, ACNET_PRODUCT);
    CloseLibrary(ExpansionBase);
    return cd != NULL;
}

/* The version.revision in a file's "$VER: name V.R" (V << 16 | R), or 0. */
static ULONG file_version(struct DosLibrary *DOSBase, struct Work *w, const char *name)
{
    BPTR fh = Open((STRPTR)name, MODE_OLDFILE);
    LONG have = 0, n, i;
    ULONG v = 0;
    if (!fh) return 0;
    for (;;) {
        n = Read(fh, w->buf + have, WORK_FILE - have);
        if (n <= 0) break;
        have += n;
        for (i = 0; i + 5 <= have; i++)
            if (w->buf[i] == '$' && w->buf[i + 1] == 'V' && w->buf[i + 2] == 'E' &&
                w->buf[i + 3] == 'R' && w->buf[i + 4] == ':') break;
        if (i + 5 <= have) {
            if (have - i < 96 && have == WORK_FILE && i) {   /* the string runs on: read on */
                CopyMem(w->buf + i, w->buf, have - i);
                have -= i;
                continue;
            }
            v = card_version(w->buf + i + 5, have - i - 5);
            break;
        }
        if (have > 4) {                                      /* keep a tail a "$VER:" may start in */
            CopyMem(w->buf + have - 4, w->buf, 4);
            have = 4;
        }
    }
    Close(fh);
    return v;
}

/* Host's library or device: the ROM's copy (its RomTag), or the disk's at
 * path when its version.revision is higher; started once here, so that
 * later opens by name or by path find it in memory. 1: the disk's. */
static int host_module(struct DosLibrary *DOSBase, struct Work *w, UBYTE type,
                       const char *name, const char *path)
{
    struct ExecBase *sys = SysBase;
    struct Resident *rt;
    struct Node *in;
    ULONG rom = 0, disk;
    int use_disk;

    Forbid();
    in = FindName(type == NT_DEVICE ? &sys->DeviceList : &sys->LibList, (STRPTR)name);
    Permit();
    if (in) return 0;                                        /* already running: kept */
    rt = FindResident((STRPTR)name);
    if (rt && rt->rt_IdString) {
        const char *id = (const char *)rt->rt_IdString;
        LONG len = 0;
        while (id[len] && len < 80) len++;
        rom = card_version(id, len);
    }
    disk = DOSBase ? file_version(DOSBase, w, path) : 0;
    use_disk = disk > rom;
    if (type == NT_DEVICE) {
        struct IOStdReq io;
        UBYTE *p = (UBYTE *)&io;
        LONG i;
        for (i = 0; i < (LONG)sizeof(io); i++) p[i] = 0;
        if (OpenDevice((STRPTR)(use_disk ? path : name), 0, (struct IORequest *)&io, 0) == 0)
            CloseDevice((struct IORequest *)&io);
    } else {
        struct Library *lib = OpenLibrary((STRPTR)(use_disk ? path : name), 0);
        if (lib) CloseLibrary(lib);
    }
    return use_disk;
}

LONG card_choose(struct CardConfig *cfg)
{
    static const char ifname[] = "gfopensocket0";
    struct DosLibrary *DOSBase = NULL;
    struct Process *me = (struct Process *)FindTask(NULL);
    struct Work *w = NULL;
    APTR oldwin = NULL;
    LONG i;
    int files = 0;

    for (i = 0; i < (LONG)sizeof(*cfg); i++) ((UBYTE *)cfg)[i] = 0;
    card_copy(cfg->ifname, sizeof(cfg->ifname), ifname, sizeof(ifname) - 1);

    /* Files only from a process (ramlib is one), and with no requester for
     * a volume or assign that isn't there. */
    if (me->pr_Task.tc_Node.ln_Type == NT_PROCESS) {
        DOSBase = (struct DosLibrary *)OpenLibrary((CONST_STRPTR)"dos.library", 37);
        w = AllocMem(sizeof(struct Work), MEMF_ANY);
        if (DOSBase && w) {
            files = 1;
            oldwin = me->pr_WindowPtr;
            me->pr_WindowPtr = (APTR)-1;
        }
    }

    /* 1. the disk */
    if (files) {
        LONG got = parse_file(DOSBase, w, cfg, "ENV:GFOpenSocket/Interface", 1);
        if (got < 0) got = parse_file(DOSBase, w, cfg, "ENVARC:GFOpenSocket/Interface", 1);
        if (got > 0) {
            if (!(got & GOT_BACKEND)) cfg->backend = BACKEND_DIRECT;   /* a device alone */
            cfg->chosen_by = CHOSEN_BY_CONFIG;
            goto done;
        }
        if (each_file(DOSBase, w, cfg, "DEVS:NetInterfaces", one_interface)) {
            cfg->backend = BACKEND_DIRECT;
            cfg->chosen_by = CHOSEN_BY_ROADSHOW;
            goto done;
        }
        if (each_file(DOSBase, w, cfg, "DEVS:Networks", one_driver)) {
            cfg->backend = BACKEND_DIRECT;
            cfg->chosen_by = CHOSEN_BY_DRIVER;
            goto done;
        }
    }
    /* 2. the default: the bridge card */
    if (card_fitted()) {
        cfg->backend = BACKEND_HOST;
        cfg->chosen_by = CHOSEN_BY_CARD;
    }
done:
    if (cfg->backend == BACKEND_HOST) {
        cfg->dev_disk = host_module(files ? DOSBase : NULL, w, NT_DEVICE,
                                    OPENSOCKET_DEVICE_NAME, OPENSOCKET_DEVICE_PATH);
        cfg->lib_disk = host_module(files ? DOSBase : NULL, w, NT_LIBRARY,
                                    ACNETWORK_LIBRARY_NAME, "LIBS:" ACNETWORK_LIBRARY_NAME);
    }
    if (files && cfg->backend == BACKEND_DIRECT && !cfg->nameserver[0])
        parse_file(DOSBase, w, cfg, "DEVS:Internet/name_resolution", 0);
    if (!cfg->address && !cfg->dhcp) cfg->dhcp = 1;   /* no address set: DHCP */
    if (files) me->pr_WindowPtr = oldwin;
    if (w) FreeMem(w, sizeof(struct Work));
    if (DOSBase) CloseLibrary((struct Library *)DOSBase);
    return cfg->backend;
}
