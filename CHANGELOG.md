# Changelog

All notable changes to MPC Plugins Manager by The Disk King.

## Next version (not released yet)

- **Each MPC gets only its own file.** A works-on-both download carries a 32-bit and a 64-bit program file. A Gen1
  MPC (Live, Live II, One, X, Key, Force) now installs only the 32-bit file, and a Gen2 MPC (Live III, XL) only the
  64-bit one. Updates also clear out an old copy of the other file.

## 1.5.1 (2026-11-25)

- **More from the same maker, right on the plugin's page.** Open a plugin's page and roll your finger up: the page
  scrolls down into **MORE FROM <maker>**, every other plugin by that maker as pictures with names, 8 at a time.
  Keep rolling for the rest; roll back down for the top of the page. Tap a picture to open its page. Under WHAT'S NEW
  the page says how many more there are.
- Fixes 1.5.0: its maker list opened as a separate list and the plugin's page did not scroll. Scrolling is smooth (no
  whole-screen redraws while you roll).

## 1.5.0 (2026-11-25)

- **Maker pages.** Tap a maker's name (on a plugin's card or its details page) and the list shows everything by that
  maker. The phone page does the same: tap a maker's name, "Show all" goes back.

## 1.4.0 (2026-11-25)

- **Follow makers on the phone page too.** Every plugin there has a **Follow maker** button; NEW plugins from the
  makers you follow come first with an orange **NEW from <maker>** strip, and **Followed makers** in the type list
  shows everything from them.
- The MPC screen and the phone page share one list of followed makers: follow on one, it shows on the other.

## 1.3.0 (2026-11-25)

- **Follow your favorite makers.** Open a plugin's details page and tap **FOLLOW MAKER**. From then on, every new
  plugin and every new version from that maker comes **first in the list, marked NEW** ("NEW from <maker>"), until you
  open it or install it. Tap **FOLLOWING** to stop.
- **TYPE > Followed** shows everything from the makers you follow.
- The list footer counts what is new ("2 NEW | ...").
- Your followed makers are kept on your MPC (MPC Plugins Manager/followed.txt); nothing is sent anywhere.

## 1.2.0 (2026-11-21)

- **Gen1 owners see more plugins in the main list.** A plugin tested on Gen2 that comes as one download for every MPC
  (made with the MPC Plugin SDK) is now shown with the tested plugins on a Gen1 too, marked "Tested on Gen2, built for
  Gen1", instead of on the red Untested tab. The same the other way round. Installing it still asks for a second tap.

## 1.1.0 (2026-11-19)

**Bigger, clearer, and a details page for every plugin.**

- **Big cards:** 3 large cards at a time, each with a big screenshot, a 2-line description, downloads and size, badge, version, tested chip and the action button.
- **Roll your finger to scroll.** Pages of 20 plugins; drag over the cards to move through the page, and use the arrows for the next or previous 20. Q-Link 1 scrolls too.
- **Details page:** tap a plugin's picture for its full page: big screenshot, description, what's new in the latest version, version history with dates and downloads, what it needs, and the install button always on the page. BACK returns to the list.
- **Every plugin has its own picture** (or a clean name tile), so plugins from the same maker no longer look alike. Plugins with real pictures come first.
- **Themes change live.** Tap THEME and the whole screen switches at once, no restart. Your theme is remembered.
- **Featured:** The Disk King's hand-picked plugins come first, marked FEATURED.
- **UPDATE only shows for newer versions** (it no longer offers to go back to an older one).
- Texts fit the MPC's screen limits, and the Q-Link box no longer covers the list.
- Fixed a blank screen caused by a picture setting.

## 1.0.0 (2026-11-19)

First release. Tested on an MPC Live III with MPC OS 3.10.1.

- Browse every free plugin on the MPC's own screen: Tested & Working, Untested, Installed, Updates and Removed tabs, 5 cards per page with screenshots, badges and versions.
- "Tested" means tested on your kind of MPC (Gen1 or Gen2), not just tested somewhere.
- One button per plugin: INSTALL, UPDATE, REMOVE, RESTORE. UPDATE ALL in one tap.
- Untested installs, REMOVE and UPDATE ALL ask for a second tap.
- Downloads over HTTPS only, checked against their SHA-256 checksum before anything is copied.
- Updates keep your own files (ROMs, presets, patterns); the old version is saved in Removed Plugins.
- Writes the `plugin.json` MPC OS 3.10.1 needs for releases that only carry the older `mpc-plugin.json`.
- Sort (most downloaded, newest, A to Z) and type filter (instruments, effects, addins).
- 10 color themes, Disk King by default. A new theme shows after the next MPC restart.
- Optional phone page on the same Wi-Fi, protected by a 4-digit PIN; 5 wrong PINs lock it for a minute.
- Starts its work only when it is on a track, never while the MPC scans plugins.
