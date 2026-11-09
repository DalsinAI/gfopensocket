/*
 * GFOpenSocketControl's core: what the Commodity shows and does, apart from
 * drawing it. The GadTools front end (acnetcontrol_gt.c) lays each page out
 * from the tables here: per group, its lines, its input fields, a list and
 * its buttons. The lines and lists are the core's own buffers, which
 * acnc_refresh() and acnc_press() rewrite.
 *
 * (The retired ReAction ACNetControl, acnetcontrol.c, does not use it.)
 *
 * MIT.
 */
#ifndef ACNETCONTROL_CORE_H
#define ACNETCONTROL_CORE_H

#include <exec/types.h>
#include <exec/lists.h>
#include <exec/ports.h>

enum {
    GID_TABS = 1,
    GID_REFRESH,
    GID_GO_OFFLINE,             /* its label says Go offline or Go online */
    GID_WIFI_RESCAN,
    GID_WIFI_JOIN,
    GID_WIFI_LEAVE,
    GID_WIFI_FORGET,
    GID_DIAG_DNS,
    GID_DIAG_CONNECT,
    GID_DIAG_INTERNET,
    GID_DIAG_COPY,
    GID_LOG_CLEAR,
    GID_LOG_SAVE,
    GID_NET_READ,               /* the Settings page (9 Nov 2026) */
    GID_NET_APPLY,
    GID_OWN_READ,
    GID_OWN_SAVE,
    GID_F_WIFI_SSID,            /* input fields: GID_F_WIFI_SSID up to GID_L_WIFI */
    GID_F_DIAG_NAME,
    GID_F_DIAG_HOST,
    GID_F_DIAG_PORT,
    GID_F_WIFI_KEY,
    GID_F_NET_HOST,
    GID_F_NET_LAN,
    GID_F_NET_ADDR,
    GID_F_NET_GW,
    GID_F_NET_DNS,
    GID_F_OWN_BACKEND,
    GID_F_OWN_ADDR,
    GID_F_OWN_MASK,
    GID_F_OWN_GW,
    GID_F_OWN_DNS,
    GID_L_WIFI,                 /* lists */
    GID_L_CONN,
    GID_L_LOG,
    GID_FIRST_FREE              /* a front end's own gadgets start here */
};

#define ACNC_PAGES       6
#define ACNC_MAX_LINES   12
#define ACNC_MAX_FIELDS  5
#define ACNC_MAX_BUTTONS 4

typedef struct ACNCButton {
    ULONG id;                   /* 0 ends the list */
    const char *label;          /* '_' marks the shortcut key */
    BOOL disabled;              /* no backend for it */
} ACNCButton;

typedef struct ACNCField {
    ULONG id;                   /* 0 ends the list */
    const char *label;          /* shown to its left */
    char *buf;                  /* the core's copy: the front end writes what */
    UWORD size;                 /* was typed here before a button runs */
    UWORD chars;                /* how wide it shows */
} ACNCField;

typedef struct ACNCGroup {
    const char *title;
    const char *line[ACNC_MAX_LINES];   /* NULL ends the list; "" is a blank line */
    ACNCField field[ACNC_MAX_FIELDS];
    ULONG list_id;              /* a read-only scrolling list under the fields, or 0 */
    UWORD list_rows, list_chars;
    ACNCButton button[ACNC_MAX_BUTTONS];
    BOOL buttons_in_a_row;      /* else one under another */
} ACNCGroup;

typedef struct ACNCPage {
    const char *name;           /* the tab */
    int groups;                 /* 1, or 2 side by side */
    ACNCGroup group[2];
} ACNCPage;

extern const ACNCPage acnc_page[ACNC_PAGES];

#define ACNC_TITLE   "GFOpenSocketControl - Network Configuration"
#define ACNC_HOTKEY  "ctrl alt n"

/* Opens gfopensocket.library (when it is there) and reads everything once. */
void acnc_open(void);
void acnc_close(void);

/* Reads the status, interface, routes, DNS, sockets and new log entries
 * again. TRUE when what a page shows changed; *state_changed (may be NULL)
 * when online/offline changed, which changes a button's label. */
BOOL acnc_refresh(BOOL *state_changed);

/* The nodes a list shows (ln_Name is the text), by its id. */
struct List *acnc_list(ULONG list_id);

/* Runs a button, after the front end has copied each field's text into its
 * buf. confirm() asks the user and returns TRUE to go on. TRUE when what a
 * page shows changed. */
BOOL acnc_press(ULONG id, BOOL (*confirm)(const char *text));

/* A row picked in a list: for the Wi-Fi list, its network's name goes into
 * the SSID field. TRUE when a field changed. After acnc_press the front end
 * writes every field's buf back into its gadget: the core may have changed
 * one (the Wi-Fi password is cleared once it is used). */
BOOL acnc_pick(ULONG list_id, UWORD row);

const char *acnc_status_line(void);     /* the status bar */

/* The commodity. show() and hide() are the front end's window calls.
 * acnc_broker_signal() is 0 until acnc_broker_open() succeeds.
 * acnc_broker_open(): 1 ready, 0 failed, ACNC_ALREADY_RUNNING when a copy
 * already runs (Exchange has told that copy to show itself). */
#define ACNC_ALREADY_RUNNING (-1)
int acnc_broker_open(void);
void acnc_broker_close(void);
ULONG acnc_broker_signal(void);
/* Handles Exchange's commands and the hotkey; FALSE when told to quit. */
BOOL acnc_broker_handle(void (*show)(void), void (*hide)(void));

/* Runs body on a stack of at least `bytes`. Our libnix ignores __stack, so
 * a program started with Run from a boot-time shell gets 4 KB. */
int acnc_main_with_stack(int (*body)(void), unsigned long bytes);

#endif
