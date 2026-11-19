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

**Install:** download `MPC-Plugins-Manager-1.0.0-mpc-sdk.zip`, unzip it, copy the folder `The Disk King - VST - MPC Plugins Manager` into `VST2 Plugins` on your MPC, restart, add a Plugin track and pick External Plugins > The Disk King > MPC Plugins Manager, then tap the arrow next to its name. Needs MPC OS 3.10.1 with Enable Enhancements and External Plugin Support on. Full guide: https://thediskking.com/plugins/install/

Provided as is: The Disk King is not responsible in any way for any damage or loss. Back up first.
