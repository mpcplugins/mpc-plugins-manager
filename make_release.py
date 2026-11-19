"""Makes release/<version>/ for GitHub: the zip people download and the release notes.
Copyright (c) 2026 The Disk King. All rights reserved.

Usage (after make_skin.py and tools/build.py):  python make_release.py
The version comes from plugin.json; the notes are that version's section of CHANGELOG.md.
The zip is named <Name>-<version>-mpc-sdk.zip and holds just the plugin folder, which is the
"works on every MPC" format MPC OS 3.10.1 reads (and that the plugin lists recognise)."""
import json
import os
import re
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
info = json.load(open(os.path.join(HERE, "plugin.json"), encoding="utf-8"))
version = info["version"]
folder = "%s - VST - %s" % (info["manufacturer"], info["name"])
src = os.path.join(HERE, "build", folder)
if not os.path.isfile(os.path.join(src, "plugin.json")):
    raise SystemExit("build the plugin first: " + src)
out = os.path.join(HERE, "release", version)
os.makedirs(out, exist_ok=True)
zpath = os.path.join(out, "MPC-Plugins-Manager-%s-mpc-sdk.zip" % version)
with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for root, _, files in os.walk(src):
        for f in sorted(files):
            full = os.path.join(root, f)
            z.write(full, os.path.join(folder, os.path.relpath(full, src)).replace("\\", "/"))
log = open(os.path.join(HERE, "CHANGELOG.md"), encoding="utf-8").read()
m = re.search(r"^## %s\b.*?\n(.*?)(?=^## |\Z)" % re.escape(version), log, re.S | re.M)
if not m:
    raise SystemExit("CHANGELOG.md has no section for " + version)
notes = m.group(1).strip()
notes += ("\n\n**Install:** download `%s`, unzip it, copy the folder `%s` into `VST2 Plugins` on your MPC, restart, add a Plugin track "
          "and pick External Plugins > The Disk King > MPC Plugins Manager, then tap the arrow next to its name. "
          "Needs MPC OS 3.10.1 with Enable Enhancements and External Plugin Support on. "
          "Full guide: https://thediskking.com/plugins/install/\n\n"
          "Provided as is: The Disk King is not responsible in any way for any damage or loss. Back up first.") % (os.path.basename(zpath), folder)
open(os.path.join(out, "NOTES.md"), "w", encoding="utf-8", newline="\n").write(notes + "\n")
print(zpath, os.path.getsize(zpath) // 1024, "KB")
