# Restart: GFOpenSocket

_Written 6 October 2026 at about 23:55 UTC, while all work is paused on @SacredTrees's word (23:28 UTC). Read this first when work resumes; the newest capsule and the live PR list win if they disagree._

## What this repo is

GFOpenSocket: bsdsocket.library and a network stack for AmigaOS 3.2, with AmiCradle's host back end and GFOpenSocketDirect for real hardware (PiStorm first).

## Where it stands

bsdsocket.library 4.2 with the WaitSelect fix is merged and in GFOpenUp. GFOpenSocketControl's Status page shows byte-rotated addresses; that fix is queued with Main Discourse.

## Merged lately

- #9 (0f7170c, 2026-10-06): Credit who made GFOpenSocket: CONTRIBUTORS.md
- #8 (3603f02, 2026-10-04): Say "we" in the repository's text, not a person's name
- #4 (a1f48d7, 2026-10-04): GFOpenSocket consolidation: guest candidate (complete stack, GadTools, phase 0 rename)
- #5 (9df4fb8, 2026-10-04): gfopensocket.readme: GFOpenSocket's Aminet readme
- #6 (7e30f32, 2026-10-04): bsdsocket.library 4.2: fix WaitSelect timeouts after the first wait
- #1 (1e80058, 2026-10-02): bsdsocket: name lookups that have to wait now return

## Open pull requests

- None.

## Next step

1. Fix the GFOpenSocketControl address display.
2. GFOpenSocketDirect on the PiStorm once the card runs.

## Waiting on @SacredTrees

- Nothing.

## Who owns it

Main Discourse.

## Capsules

Restart capsules for this repo's workstreams, in amigachrome's `capjumps/` shelf:

- [`20261006_AmigaChrome_OpenUp_MainDiscourse_Restart_Capsule.zip`](https://github.com/DalsinAI/amigachrome/tree/main/capjumps)

Team rules that still hold: commits as SacredTrees with no co-author lines; third-party code only on "yes with review" (licence checked, commit and sha256 pinned, fetched at build, never committed); deploys with deploy_dev.py only, on a typed line.
