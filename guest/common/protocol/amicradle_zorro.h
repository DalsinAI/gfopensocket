/* AmiCradle's own Zorro manufacturer number, 0xDA15 (55829), the
 * AmiCradle Team's from 1 October 2026. Before that the boards used 2011,
 * the number UAE and home-made test boards share (ShowConfig calls it "UAE
 * Amiga Emulator", Linux's zorro.ids "Hacker Test Board"). 0xDA15 is in
 * neither list.
 *
 * Products stay as they were: 1 ACBridge control, 2 ACStorage, 9 ACRTG,
 * 11 AC090's RAM.
 *
 * Drivers look for 0xDA15 first, then 2011, so one build works with either.
 * A boot ROM whose drivers do carries AMICRADLE_ROM_MARK; the runtime
 * presents 0xDA15 to a ROM that has it (or AMICRADLE_ROM_MARK_TEAM, the
 * mark of a ROM built from 1 October to 1 November 2026, while the Team was
 * the AmigaChrome Team, or AMICRADLE_ROM_MARK_OLD, the Dalsin mark of a ROM
 * built before that) and 2011 to one built before either. The host matches
 * the same three strings: change them only together with it. */
#ifndef AMICRADLE_ZORRO_H
#define AMICRADLE_ZORRO_H

#define AMICRADLE_MANUFACTURER      0xDA15     /* 55829 */
#define AMICRADLE_MANUFACTURER_OLD  2011
#define AMICRADLE_ROM_MARK            "Zorro manufacturer AmiCradle Team $DA15"
#define AMICRADLE_ROM_MARK_TEAM       "Zorro manufacturer AmigaChrome Team $DA15"
#define AMICRADLE_ROM_MARK_OLD        "Zorro manufacturer Dalsin $DA15"

#endif
