/*
 * GFOpenSocketControl - the GFOpenSocket Commodity, in GadTools (AmigaOS 2.04
 * and later; AmigaOS 3.x programs are GadTools or MUI).
 *
 * Five pages from acnetcontrol_core.c: Status, Wi-Fi, Connections,
 * Diagnostics and Log, under a row of tabs drawn as MUI's Register class
 * draws them (1.0.1; the same tabs as GFOpenLook's editors); each page is drawn
 * as ridged groups with their lines, input fields, a list and buttons. While
 * the window is active the page refreshes itself every 3 seconds. The
 * window follows the screen's font (Topaz 8 when that would not fit the
 * screen) and can be resized. Closing it hides the Commodity; networking
 * never depends on it.
 *
 * Keys: Tab and Shift-Tab change page, 1 to 5 pick one, Esc hides, and the
 * underlined letters press buttons.
 *
 * MIT.
 */
#include <exec/types.h>
#include <exec/libraries.h>
#include <dos/dos.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/text.h>
#include <graphics/rastport.h>
#include <libraries/gadtools.h>
#include <devices/inputevent.h>
#include <string.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/gadtools.h>

#include "acnetcontrol_core.h"

struct Library *GadToolsBase;

#define VERSION_TEXT "GFOpenSocketControl 1.0.2 (12.10.2026)"   /* 1.0.2: addresses in the right order */
static const char version[] __attribute__((used)) = "$VER: " VERSION_TEXT;

enum { GID_PAGES = GID_FIRST_FREE, GID_STATUS, GID_COUNT };
#define TAB_ID 900                       /* the tabs: TAB_ID + page (1.0.1) */
#define TAB_PAD 12
#define TAB_LIFT 2
enum { M_ABOUT = 1, M_HIDE, M_QUIT };

#define PAD     6       /* between groups, and between buttons */
#define INSET   10      /* from a group's ridge to its text */
#define MARGIN  8       /* inside the window's borders */
#define TICKS   30      /* IntuiTicks between refreshes: about 3 s */

static struct NewMenu menus[] = {
    { NM_TITLE, "Project", NULL, 0, 0, NULL },
    { NM_ITEM, "About...", NULL, 0, 0, (APTR)M_ABOUT },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "Hide", "H", 0, 0, (APTR)M_HIDE },
    { NM_ITEM, "Quit", "Q", 0, 0, (APTR)M_QUIT },
    { NM_END, NULL, NULL, 0, 0, NULL }
};

static STRPTR page_names[ACNC_PAGES + 1];

static struct Screen *scr;
static APTR vi;
static struct Window *win;
static struct Menu *menu;
static struct Gadget *glist;
static struct TextFont *font;
static struct TextAttr font_attr;
static struct TextFont *mono;           /* a fixed-pitch font for the lists and their headings (1.0.1) */
static struct TextAttr mono_attr;
static struct RastPort measure_mono;
static struct RastPort measure;
static int page;
static int fh, line_h, btn_h, mx_w, title_band;
static int nat_w, nat_h;            /* the page area every page fits in */
static BOOL quit_now;
static struct Gadget *gad[GID_COUNT];   /* the page's gadgets by id */
static int ticks;

static struct TextAttr topaz8 = { "topaz.font", 8, FS_NORMAL, FPF_ROMFONT };
static int tab_x, tab_y, tab_w, tab_h, tab_tx[ACNC_PAGES], tab_tw[ACNC_PAGES];
static int text_w(const char *s);

/* ---- tabs, as MUI's Register draws them (1.0.1) -------------------------- */

/* Each tab a GENERIC_KIND gadget with no imagery (drawn by tabs_draw), as wide as its name. */
static struct Gadget *tabs_make(struct Gadget *g, struct NewGadget *ng, int x, int y, int w, int lh)
{
    int i, at = x;
    tab_x = x; tab_y = y; tab_w = w; tab_h = lh;
    for (i = 0; i < ACNC_PAGES; i++) {
        tab_tw[i] = text_w(acnc_page[i].name) + 2 * TAB_PAD + 1;
        tab_tx[i] = at;
        at += tab_tw[i] - 1;
    }
    for (i = 0; i < ACNC_PAGES && g; i++) {
        ng->ng_LeftEdge = tab_tx[i];
        ng->ng_TopEdge = y - TAB_LIFT;
        ng->ng_Width = tab_tw[i];
        ng->ng_Height = lh + TAB_LIFT;
        ng->ng_GadgetText = NULL;
        ng->ng_Flags = 0;
        ng->ng_GadgetID = TAB_ID + i;
        if ((g = CreateGadget(GENERIC_KIND, g, ng, TAG_DONE))) {
            g->Flags = GFLG_GADGHNONE;
            g->Activation = GACT_RELVERIFY;
            g->GadgetType |= GTYP_BOOLGADGET;
            g->GadgetRender = g->SelectRender = NULL;
        }
    }
    return g;
}

/* The tabs and the page's frame down to bottom. The open page's tab stands taller, in bold. */
static void tabs_draw(struct Window *win, int bottom)
{
    struct RastPort *rp = win->RPort;
    struct DrawInfo *dri = GetScreenDrawInfo(win->WScreen);
    UWORD shine = dri ? dri->dri_Pens[SHINEPEN] : 2, shadow = dri ? dri->dri_Pens[SHADOWPEN] : 1;
    UWORD text = dri ? dri->dri_Pens[TEXTPEN] : 1;
    int i, base = tab_y + tab_h, left = tab_x - 4, right = tab_x + tab_w + 3;
    if (right > win->Width - win->BorderRight - 4) right = win->Width - win->BorderRight - 4;
    SetFont(rp, font);
    SetDrMd(rp, JAM1);
    for (i = 0; i <= ACNC_PAGES; i++) {
        int t = i < ACNC_PAGES ? i : page, open = t == page, x0, x1, y0, ty;
        const char *name = acnc_page[t].name;
        if (i < ACNC_PAGES && i == page) continue;          /* the open one last, over the shared edges */
        x0 = tab_tx[t]; x1 = x0 + tab_tw[t] - 1; y0 = open ? tab_y - TAB_LIFT : tab_y;
        if (open) EraseRect(rp, x0 + 1, y0 + 1, x1 - 1, base);   /* in the window's own colour: no grey box */
        SetAPen(rp, shine);
        Move(rp, x0, base - (open ? 0 : 1)); Draw(rp, x0, y0 + 2); Draw(rp, x0 + 2, y0); Draw(rp, x1 - 2, y0);
        SetAPen(rp, shadow);
        Move(rp, x1 - 1, y0 + 1); Draw(rp, x1, y0 + 2); Draw(rp, x1, base - (open ? 0 : 1));
        SetAPen(rp, text);
        ty = y0 + (tab_h + (open ? TAB_LIFT : 0) - fh) / 2 + font->tf_Baseline + 1;
        Move(rp, x0 + TAB_PAD, ty);
        Text(rp, (STRPTR)name, strlen(name));
        if (open) { Move(rp, x0 + TAB_PAD + 1, ty); Text(rp, (STRPTR)name, strlen(name)); }
    }
    SetAPen(rp, shine);
    Move(rp, left, base); Draw(rp, tab_tx[page], base);
    Move(rp, tab_tx[page] + tab_tw[page] - 1, base); Draw(rp, right, base);
    Move(rp, left, base); Draw(rp, left, bottom);
    SetAPen(rp, shadow);
    Move(rp, left + 1, bottom); Draw(rp, right, bottom); Draw(rp, right, base + 1);
    if (dri) FreeScreenDrawInfo(win->WScreen, dri);
}

/* ---- measuring ---------------------------------------------------------- */

static int text_w(const char *s)
{
    char plain[96];
    int n = 0;
    for (; *s && n < (int)sizeof(plain) - 1; ++s)
        if (*s != '_') plain[n++] = *s;          /* the shortcut mark takes no room */
    return TextLength(&measure, plain, n);
}

/* The lists' rows and headings are columns, so they are set in a fixed-pitch
 * font: the screen's when it is one, else Topaz 8 (1.0.1). */
static BOOL in_mono(const ACNCGroup *g)
{
    return g->list_id != 0;
}

static int line_w(const ACNCGroup *g, const char *s)
{
    return in_mono(g) ? TextLength(&measure_mono, (STRPTR)s, strlen(s)) : text_w(s);
}

static int button_w(const ACNCButton *b)
{
    return text_w(b->label) + 2 * 8;
}

static int field_label_w(const ACNCGroup *g)
{
    int i, w = 0;
    for (i = 0; i < ACNC_MAX_FIELDS && g->field[i].id; ++i)
        if (text_w(g->field[i].label) > w) w = text_w(g->field[i].label);
    return w;
}

static int list_h(const ACNCGroup *g)
{
    return g->list_rows * (mono->tf_YSize + 1) + 4;
}

static void group_natural(const ACNCGroup *g, int *w, int *h)
{
    int i, gw = text_w(g->title) + 4 * PAD, gh = title_band + PAD, row = 0, cw = font->tf_XSize;
    /* widths below are the group's: its text plus INSET each side */
    for (i = 0; i < ACNC_MAX_LINES && g->line[i]; ++i) {
        int tw = line_w(g, g->line[i]);
        if (tw > gw - 2 * INSET) gw = tw + 2 * INSET;
        gh += line_h;
    }
    for (i = 0; i < ACNC_MAX_FIELDS && g->field[i].id; ++i) {
        int fw = field_label_w(g) + 8 + g->field[i].chars * cw + 12;
        if (fw > gw - 2 * INSET) gw = fw + 2 * INSET;
        gh += PAD + btn_h;
    }
    if (g->list_id) {
        int lw = g->list_chars * mono->tf_XSize + 24;
        if (lw > gw - 2 * INSET) gw = lw + 2 * INSET;
        gh += PAD + list_h(g);
    }
    for (i = 0; i < ACNC_MAX_BUTTONS && g->button[i].id; ++i) {
        if (g->buttons_in_a_row) {
            row += button_w(&g->button[i]) + (i ? PAD : 0);
            if (!i) gh += PAD + btn_h;
        } else {
            if (button_w(&g->button[i]) > gw - 2 * INSET) gw = button_w(&g->button[i]) + 2 * INSET;
            gh += PAD + btn_h;
        }
    }
    if (row > gw - 2 * INSET) gw = row + 2 * INSET;
    *w = gw;
    *h = gh + PAD;
}

static void measure_pages(void)
{
    int p;
    fh = font->tf_YSize;
    line_h = (mono && mono->tf_YSize > fh ? mono->tf_YSize : fh) + 2;
    btn_h = fh + 6;
    title_band = fh;
    mx_w = 0;
    for (p = 0; p < ACNC_PAGES; ++p) {
        int w0, h0, w1 = 0, h1 = 0, w, h;
        int lw = text_w(acnc_page[p].name);
        if (lw > mx_w) mx_w = lw;
        group_natural(&acnc_page[p].group[0], &w0, &h0);
        if (acnc_page[p].groups == 2) group_natural(&acnc_page[p].group[1], &w1, &h1);
        w = w0 + (w1 ? PAD + w1 : 0);
        h = h0 > h1 ? h0 : h1;
        if (w > nat_w) nat_w = w;
        if (h > nat_h) nat_h = h;
    }
    mx_w = 0;
    for (p = 0; p < ACNC_PAGES; ++p) mx_w += text_w(acnc_page[p].name) + 2 * TAB_PAD;   /* the tabs' row */
    if (mx_w + 8 > nat_w) nat_w = mx_w + 8;
}

/* The screen's font, or Topaz 8 when the window would not fit the screen. */
static BOOL choose_font(void)
{
    int pass;
    if (!mono) {
        mono_attr = *scr->Font;
        mono = OpenFont(&mono_attr);
        if (mono && (mono->tf_Flags & FPF_PROPORTIONAL)) { CloseFont(mono); mono = NULL; }
        if (!mono) { mono_attr = topaz8; mono = OpenFont(&mono_attr); }
        if (!mono) return FALSE;
        InitRastPort(&measure_mono);
        SetFont(&measure_mono, mono);
    }
    for (pass = 0; pass < 2; ++pass) {
        font_attr = pass ? topaz8 : *scr->Font;
        font = OpenFont(&font_attr);
        if (!font) continue;
        InitRastPort(&measure);
        SetFont(&measure, font);
        nat_w = nat_h = 0;
        measure_pages();
        if (pass || (MARGIN * 3 + nat_w + scr->WBorLeft + scr->WBorRight + 18 <= scr->Width &&
                     MARGIN * 4 + line_h * 3 + TAB_LIFT + nat_h + btn_h + scr->WBorTop + scr->Font->ta_YSize + 12 <= scr->Height))
            return TRUE;
        CloseFont(font);
        font = NULL;
    }
    return font != NULL;
}

/* ---- the window's insides ---------------------------------------------- */

struct Zone { int x, y, w, h; };

static void inner(struct Zone *a)
{
    a->x = win->BorderLeft + MARGIN;
    a->y = win->BorderTop + MARGIN;
    a->w = win->Width - win->BorderLeft - win->BorderRight - 2 * MARGIN;
    a->h = win->Height - win->BorderTop - win->BorderBottom - 2 * MARGIN;
}

/* Where the page's groups sit: one fills the page area, two share it in
 * proportion to what they need. */
static void group_areas(const struct Zone *pa, struct Zone g[2])
{
    const ACNCPage *p = &acnc_page[page];
    g[0] = *pa;
    if (p->groups == 2) {
        int w0, h0, w1, h1, spare;
        group_natural(&p->group[0], &w0, &h0);
        group_natural(&p->group[1], &w1, &h1);
        spare = pa->w - PAD - w0 - w1;
        if (spare < 0) spare = 0;
        g[0].w = w0 + spare / 2;
        g[1] = *pa;
        g[1].x = pa->x + g[0].w + PAD;
        g[1].w = pa->w - g[0].w - PAD;
    }
}

/* The tabs' row: under the heading, lifted a little above the page's frame. */
static int tabs_top(void)
{
    struct Zone a;
    inner(&a);
    return a.y + line_h + PAD + TAB_LIFT;
}

static void page_area(struct Zone *pa)
{
    struct Zone a;
    inner(&a);
    pa->x = a.x + 6;
    pa->y = tabs_top() + line_h + 2 + PAD;
    pa->w = a.w - 12;
    pa->h = a.h - (pa->y - a.y) - PAD - 4 - (line_h + 4) - PAD;
}

static struct Gadget *make_gadgets(void)
{
    struct NewGadget ng;
    struct Gadget *g;
    struct Zone a, pa, ga[2];
    int i, k;

    glist = NULL;
    memset(gad, 0, sizeof(gad));
    g = CreateContext(&glist);
    if (!g) return NULL;
    inner(&a);
    page_area(&pa);
    memset(&ng, 0, sizeof(ng));
    ng.ng_TextAttr = &font_attr;
    ng.ng_VisualInfo = vi;

    g = tabs_make(g, &ng, pa.x, tabs_top(), pa.w, line_h + 2);

    group_areas(&pa, ga);
    for (k = 0; k < acnc_page[page].groups; ++k) {
        const ACNCGroup *grp = &acnc_page[page].group[k];
        int lines = 0, x = ga[k].x + INSET, y;
        while (lines < ACNC_MAX_LINES && grp->line[lines]) ++lines;
        y = ga[k].y + title_band + PAD + lines * line_h;
        for (i = 0; i < ACNC_MAX_FIELDS && grp->field[i].id; ++i) {
            const ACNCField *f = &grp->field[i];
            int lw = field_label_w(grp) + 8;
            ng.ng_LeftEdge = x + lw;
            ng.ng_TopEdge = y + PAD;
            ng.ng_Width = f->chars * font->tf_XSize + 12;
            ng.ng_Height = btn_h;
            ng.ng_GadgetText = (UBYTE *)f->label;
            ng.ng_GadgetID = f->id;
            ng.ng_Flags = PLACETEXT_LEFT;
            g = CreateGadget(STRING_KIND, g, &ng, GTST_String, (ULONG)f->buf, GTST_MaxChars, f->size - 1, TAG_DONE);
            if (f->id < GID_COUNT) gad[f->id] = g;
            y += PAD + btn_h;
        }
        if (grp->list_id) {
            ng.ng_LeftEdge = x;
            ng.ng_TopEdge = y + PAD;
            ng.ng_Width = ga[k].w - 2 * INSET;
            ng.ng_Height = list_h(grp);
            ng.ng_GadgetText = NULL;
            ng.ng_GadgetID = grp->list_id;
            ng.ng_Flags = 0;
            ng.ng_TextAttr = &mono_attr;
            g = CreateGadget(LISTVIEW_KIND, g, &ng, GTLV_Labels, (ULONG)acnc_list(grp->list_id),
                             GTLV_ReadOnly, grp->list_id != GID_L_WIFI, TAG_DONE);
            ng.ng_TextAttr = &font_attr;
            if (grp->list_id < GID_COUNT) gad[grp->list_id] = g;
            y += PAD + list_h(grp);
        }
        for (i = 0; i < ACNC_MAX_BUTTONS && grp->button[i].id; ++i) {
            ng.ng_LeftEdge = x;
            ng.ng_TopEdge = y + PAD;
            ng.ng_Width = grp->buttons_in_a_row ? button_w(&grp->button[i]) : ga[k].w - 2 * INSET;
            ng.ng_Height = btn_h;
            ng.ng_GadgetText = (UBYTE *)grp->button[i].label;
            ng.ng_GadgetID = grp->button[i].id;
            ng.ng_Flags = PLACETEXT_IN;
            g = CreateGadget(BUTTON_KIND, g, &ng, GT_Underscore, '_', GA_Disabled, grp->button[i].disabled, TAG_DONE);
            if (grp->buttons_in_a_row) x += ng.ng_Width + PAD;
            else y += PAD + btn_h;
        }
    }

    ng.ng_LeftEdge = a.x;
    ng.ng_TopEdge = a.y + a.h - (line_h + 4);
    ng.ng_Width = a.w;
    ng.ng_Height = line_h + 4;
    ng.ng_GadgetText = NULL;
    ng.ng_GadgetID = GID_STATUS;
    ng.ng_Flags = 0;
    g = CreateGadget(TEXT_KIND, g, &ng, GTTX_Text, (ULONG)acnc_status_line(), GTTX_Border, TRUE,
                     GTTX_CopyText, TRUE, TAG_DONE);
    gad[GID_STATUS] = g;
    return g;
}

static void ridge(struct RastPort *rp, const struct Zone *r)
{
    if (GadToolsBase->lib_Version >= 39) {
        DrawBevelBox(rp, r->x, r->y, r->w, r->h, GT_VisualInfo, (ULONG)vi, GTBB_FrameType, BBFT_RIDGE, TAG_DONE);
    } else {                        /* 2.04: a ridge from two boxes */
        DrawBevelBox(rp, r->x, r->y, r->w, r->h, GT_VisualInfo, (ULONG)vi, GTBB_Recessed, TRUE, TAG_DONE);
        DrawBevelBox(rp, r->x + 1, r->y + 1, r->w - 2, r->h - 2, GT_VisualInfo, (ULONG)vi, TAG_DONE);
    }
}

static void say(struct RastPort *rp, int x, int y, const char *s, UWORD pen)
{
    SetAPen(rp, pen);
    Move(rp, x, y + font->tf_Baseline);
    Text(rp, (STRPTR)s, strlen(s));
}

/* A line that may have grown since the window was measured: cut to fit. */
static void say_in(struct RastPort *rp, int x, int y, int w, const char *s, UWORD pen, struct TextFont *f)
{
    struct TextExtent te;
    ULONG n;
    SetFont(rp, f);
    n = TextFit(rp, (STRPTR)s, strlen(s), &te, NULL, 1, w, f->tf_YSize + 1);
    SetAPen(rp, pen);
    Move(rp, x, y + f->tf_Baseline);
    Text(rp, (STRPTR)s, n);
    SetFont(rp, font);
}

/* What is not a gadget: the heading and the page's groups. */
static void draw_static(void)
{
    struct RastPort *rp = win->RPort;
    struct DrawInfo *dri = GetScreenDrawInfo(scr);
    UWORD text = dri ? dri->dri_Pens[TEXTPEN] : 1;
    UWORD hi = dri ? dri->dri_Pens[HIGHLIGHTTEXTPEN] : 2;
    struct Zone a, pa, ga[2];
    int k, i;

    inner(&a);
    page_area(&pa);
    group_areas(&pa, ga);
    SetFont(rp, font);
    SetDrMd(rp, JAM1);
    say(rp, a.x, a.y, ACNC_TITLE, hi);
    tabs_draw(win, pa.y + pa.h + 4);

    for (k = 0; k < acnc_page[page].groups; ++k) {
        const ACNCGroup *grp = &acnc_page[page].group[k];
        struct Zone frame = ga[k];
        int tw = text_w(grp->title);
        frame.y += title_band / 2;
        frame.h -= title_band / 2;
        ridge(rp, &frame);
        /* the title sits in a gap in the ridge, cleared in the window's own colour (GFOpenLook's) */
        EraseRect(rp, ga[k].x + (ga[k].w - tw) / 2 - 4, ga[k].y, ga[k].x + (ga[k].w + tw) / 2 + 3, ga[k].y + fh - 1);
        say(rp, ga[k].x + (ga[k].w - tw) / 2, ga[k].y, grp->title, text);
        for (i = 0; i < ACNC_MAX_LINES && grp->line[i]; ++i)
            say_in(rp, ga[k].x + INSET, ga[k].y + title_band + PAD + i * line_h, ga[k].w - 2 * INSET,
                   grp->line[i], text, in_mono(grp) ? mono : font);
    }
    if (dri) FreeScreenDrawInfo(scr, dri);
}

/* What a refresh changes, without rebuilding the page: the lines, the lists
 * and the status bar. */
static void live_update(void)
{
    struct RastPort *rp = win->RPort;
    struct DrawInfo *dri = GetScreenDrawInfo(scr);
    UWORD text = dri ? dri->dri_Pens[TEXTPEN] : 1;
    struct Zone pa, ga[2];
    int k, i;

    page_area(&pa);
    group_areas(&pa, ga);
    SetFont(rp, font);
    SetDrMd(rp, JAM1);
    for (k = 0; k < acnc_page[page].groups; ++k) {
        const ACNCGroup *grp = &acnc_page[page].group[k];
        for (i = 0; i < ACNC_MAX_LINES && grp->line[i]; ++i) {
            int y = ga[k].y + title_band + PAD + i * line_h;
            EraseRect(rp, ga[k].x + INSET, y, ga[k].x + ga[k].w - INSET - 1, y + line_h - 1);
            say_in(rp, ga[k].x + INSET, y, ga[k].w - 2 * INSET, grp->line[i], text, in_mono(grp) ? mono : font);
        }
        if (grp->list_id && grp->list_id < GID_COUNT && gad[grp->list_id]) {
            GT_SetGadgetAttrs(gad[grp->list_id], win, NULL, GTLV_Labels, ~0UL, TAG_DONE);
            GT_SetGadgetAttrs(gad[grp->list_id], win, NULL, GTLV_Labels, (ULONG)acnc_list(grp->list_id), TAG_DONE);
        }
    }
    if (gad[GID_STATUS])
        GT_SetGadgetAttrs(gad[GID_STATUS], win, NULL, GTTX_Text, (ULONG)acnc_status_line(), TAG_DONE);
    if (dri) FreeScreenDrawInfo(scr, dri);
}

static const ACNCField *find_field(ULONG id)
{
    int k, i;
    for (k = 0; k < acnc_page[page].groups; ++k)
        for (i = 0; i < ACNC_MAX_FIELDS && acnc_page[page].group[k].field[i].id; ++i)
            if (acnc_page[page].group[k].field[i].id == id) return &acnc_page[page].group[k].field[i];
    return NULL;
}

/* What was typed into the page's fields, into the core's buffers. */
static void copy_fields(void)
{
    int k, i;
    for (k = 0; k < acnc_page[page].groups; ++k)
        for (i = 0; i < ACNC_MAX_FIELDS && acnc_page[page].group[k].field[i].id; ++i) {
            const ACNCField *f = &acnc_page[page].group[k].field[i];
            struct Gadget *g = f->id < GID_COUNT ? gad[f->id] : NULL;
            if (g && g->SpecialInfo) {
                strncpy(f->buf, (char *)((struct StringInfo *)g->SpecialInfo)->Buffer, f->size - 1);
                f->buf[f->size - 1] = 0;
            }
        }
}

/* Builds the gadgets for the current page and size, and draws it all. */
static void redo(void)
{
    if (glist) copy_fields();                    /* keep what was being typed */
    if (glist) {
        RemoveGList(win, glist, -1);
        FreeGadgets(glist);
        glist = NULL;
    }
    EraseRect(win->RPort, win->BorderLeft, win->BorderTop,
              win->Width - win->BorderRight - 1, win->Height - win->BorderBottom - 1);
    if (!make_gadgets()) return;
    AddGList(win, glist, ~0, -1, NULL);
    RefreshGList(glist, win, NULL, -1);
    GT_RefreshWindow(win, NULL);
    draw_static();
}

/* ---- the Commodity's window --------------------------------------------- */

static void hide_window(void)
{
    if (win) {
        ClearMenuStrip(win);
        CloseWindow(win);
        win = NULL;
    }
    if (glist) { FreeGadgets(glist); glist = NULL; }
    if (menu) { FreeMenus(menu); menu = NULL; }
    if (vi) { FreeVisualInfo(vi); vi = NULL; }
    if (font) { CloseFont(font); font = NULL; }
    if (mono) { CloseFont(mono); mono = NULL; }
    if (scr) { UnlockPubScreen(NULL, scr); scr = NULL; }
}

static void show_window(void)
{
    int w, h;
    if (win) {
        WindowToFront(win);
        ActivateWindow(win);
        return;
    }
    scr = LockPubScreen(NULL);
    if (!scr) return;
    vi = GetVisualInfo(scr, TAG_DONE);
    if (!vi || !choose_font()) { hide_window(); return; }
    menu = CreateMenus(menus, TAG_DONE);
    if (menu) LayoutMenus(menu, vi, GTMN_NewLookMenus, TRUE, TAG_DONE);

    w = 2 * MARGIN + 12 + nat_w;
    h = 2 * MARGIN + line_h + PAD + TAB_LIFT + line_h + 2 + PAD + nat_h + PAD + 4 + PAD + line_h + 4;
    win = OpenWindowTags(NULL,
        WA_Title, (ULONG)"GFOpenSocketControl",
        WA_ScreenTitle, (ULONG)"GFOpenSocket - networking for the Amiga",
        WA_PubScreen, (ULONG)scr,
        WA_InnerWidth, w, WA_InnerHeight, h,
        WA_Left, (scr->Width - w) / 2, WA_Top, (scr->Height - h) / 2,
        WA_Activate, TRUE, WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE,
        WA_SizeGadget, TRUE, WA_SizeBBottom, TRUE, WA_SmartRefresh, TRUE, WA_NewLookMenus, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_GADGETDOWN | IDCMP_MENUPICK | IDCMP_VANILLAKEY |
                  IDCMP_REFRESHWINDOW | IDCMP_NEWSIZE | IDCMP_INTUITICKS | MXIDCMP | BUTTONIDCMP |
                  TEXTIDCMP | STRINGIDCMP | LISTVIEWIDCMP,
        TAG_DONE);
    if (!win) { hide_window(); return; }
    WindowLimits(win, win->Width, win->Height, ~0, ~0);   /* never smaller than the pages need */
    if (menu) SetMenuStrip(win, menu);
    acnc_refresh(NULL);
    redo();
}

static void set_page(int p)
{
    if (glist) copy_fields();
    page = (p + ACNC_PAGES) % ACNC_PAGES;
    acnc_refresh(NULL);
    if (win) redo();
}

static BOOL confirm(const char *text)
{
    struct EasyStruct es = { sizeof(struct EasyStruct), 0, "GFOpenSocketControl", (UBYTE *)text, "Proceed|Cancel" };
    return EasyRequestArgs(win, &es, NULL, NULL) == 1;
}

static void busy(BOOL on)
{
    if (win && ((struct Library *)IntuitionBase)->lib_Version >= 39) {
        if (on) SetWindowPointer(win, WA_BusyPointer, TRUE, TAG_DONE);
        else SetWindowPointer(win, TAG_DONE);
    }
}

/* A button, by id: the fields first, then the core runs it. */
static void press(ULONG id)
{
    BOOL changed;
    copy_fields();
    busy(TRUE);
    changed = acnc_press(id, confirm);
    busy(FALSE);
    if (changed && win) redo();
}

static void about(void)
{
    struct EasyStruct es = { sizeof(struct EasyStruct), 0, "GFOpenSocketControl",
        VERSION_TEXT "\n\nThe GFOpenSocket Commodity.\nNetworking runs without it.", "OK" };
    EasyRequestArgs(win, &es, NULL, NULL);
}

static void menu_pick(UWORD number)
{
    while (number != MENUNULL && win) {
        struct MenuItem *item = ItemAddress(menu, number);
        if (!item) break;
        switch ((ULONG)GTMENUITEM_USERDATA(item)) {
            case M_ABOUT: about(); break;
            case M_HIDE: hide_window(); return;
            case M_QUIT: quit_now = TRUE; return;
        }
        number = item->NextSelect;
    }
}

/* A button's underlined letter, on the page that shows it. */
static void shortcut(UWORD key)
{
    int k, i;
    for (k = 0; k < acnc_page[page].groups; ++k)
        for (i = 0; i < ACNC_MAX_BUTTONS && acnc_page[page].group[k].button[i].id; ++i) {
            const ACNCButton *b = &acnc_page[page].group[k].button[i];
            const char *u = strchr(b->label, '_');
            if (u && !b->disabled && (u[1] | 0x20) == (key | 0x20)) {
                press(b->id);
                return;
            }
        }
}

static void key(UWORD code, UWORD qualifier)
{
    if (code == 27) hide_window();
    else if (code == 9) set_page(page + ((qualifier & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) ? -1 : 1));
    else if (code >= '1' && code < '1' + ACNC_PAGES) set_page(code - '1');
    else shortcut(code);
}

static void window_events(void)
{
    struct IntuiMessage *im;
    while (win && (im = GT_GetIMsg(win->UserPort)) != NULL) {
        ULONG class = im->Class;
        UWORD code = im->Code, qualifier = im->Qualifier;
        struct Gadget *g = (struct Gadget *)im->IAddress;
        GT_ReplyIMsg(im);
        switch (class) {
            case IDCMP_CLOSEWINDOW: hide_window(); break;
            case IDCMP_REFRESHWINDOW:
                GT_BeginRefresh(win);
                draw_static();
                GT_EndRefresh(win, TRUE);
                break;
            case IDCMP_NEWSIZE: redo(); break;
            case IDCMP_GADGETDOWN:
                break;
            case IDCMP_GADGETUP:
                if (g->GadgetID >= TAB_ID && g->GadgetID < TAB_ID + ACNC_PAGES) {
                    if (g->GadgetID - TAB_ID != page) set_page(g->GadgetID - TAB_ID);
                } else if (g->GadgetID == GID_L_WIFI) {            /* a network picked: its name */
                    const ACNCField *f = find_field(GID_F_WIFI_SSID);
                    if (acnc_pick(GID_L_WIFI, code) && f && gad[GID_F_WIFI_SSID])
                        GT_SetGadgetAttrs(gad[GID_F_WIFI_SSID], win, NULL, GTST_String, (ULONG)f->buf, TAG_DONE);
                } else if (g->GadgetID >= GID_F_WIFI_SSID && g->GadgetID <= GID_F_DIAG_PORT) {
                    copy_fields();                          /* typed; a button runs it */
                } else if (g->GadgetID > GID_TABS && g->GadgetID < GID_F_WIFI_SSID) {
                    press(g->GadgetID);
                }
                break;
            case IDCMP_INTUITICKS:                          /* only while the window is active */
                if (++ticks >= TICKS) {
                    BOOL state;
                    ticks = 0;
                    if (acnc_refresh(&state)) {
                        if (state) redo();                  /* Go online/offline's label */
                        else live_update();
                    }
                }
                break;
            case IDCMP_MENUPICK: menu_pick(code); break;
            case IDCMP_VANILLAKEY: key(code, qualifier); break;
        }
    }
}

static int control_main(void)
{
    int i;
    BOOL running = TRUE;

    GadToolsBase = OpenLibrary("gadtools.library", 37);
    if (!GadToolsBase) {
        PutStr("GFOpenSocketControl: needs gadtools.library 37 (AmigaOS 2.04 or later).\n");
        return 20;
    }
    for (i = 0; i < ACNC_PAGES; ++i) page_names[i] = (STRPTR)acnc_page[i].name;
    acnc_open();
    i = acnc_broker_open();
    if (i != 1) {
        /* ACNC_ALREADY_RUNNING: Exchange has told that copy to show itself. */
        if (i != ACNC_ALREADY_RUNNING) PutStr("GFOpenSocketControl: could not start the Commodity.\n");
        acnc_broker_close();
        acnc_close();
        CloseLibrary(GadToolsBase);
        return i == ACNC_ALREADY_RUNNING ? 5 : 20;
    }
    show_window();

    while (running && !quit_now) {
        ULONG winsig = win ? 1UL << win->UserPort->mp_SigBit : 0;
        ULONG sigs = Wait(winsig | acnc_broker_signal() | SIGBREAKF_CTRL_C);
        if (sigs & SIGBREAKF_CTRL_C) running = FALSE;
        if (sigs & acnc_broker_signal()) running = acnc_broker_handle(show_window, hide_window) && running;
        if (sigs & winsig) window_events();
    }

    hide_window();
    acnc_broker_close();
    acnc_close();
    CloseLibrary(GadToolsBase);
    return 0;
}

int main(void)
{
    return acnc_main_with_stack(control_main, 32768);
}
