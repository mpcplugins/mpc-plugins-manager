"""Makes test inputs for test_core: the live plugin list as catalog.tsv, and real plugin zips with a
listing of every file's SHA-256 made by Python's own zip reader (the answer key for our unzip)."""
import hashlib
import json
import os
import sys
import urllib.request
import zipfile

out = sys.argv[1]
os.makedirs(out, exist_ok=True)
sys.path.insert(0, r"C:\Users\jason\Desktop\MPC Plugins Tracker\tracker")
from mpcplugins import site  # noqa: E402

cat = json.load(urllib.request.urlopen("https://mpcplugins.github.io/catalog.json"))
open(os.path.join(out, "catalog.tsv"), "w", encoding="utf-8", newline="\n").write(site.catalog_tsv(cat))
by = {p["id"]: p for p in cat["plugins"]}
for pid, kind in (("dexed-dx7", "sdk"), ("acid", "gen2"), ("jv-880", "gen2")):
    b = by[pid]["latest"]["builds"][kind]
    zp = os.path.join(out, "%s-%s.zip" % (pid, kind))
    if not os.path.exists(zp):
        urllib.request.urlretrieve(b["url"], zp)
    with zipfile.ZipFile(zp) as z, open(zp + ".txt", "w", encoding="utf-8", newline="\n") as f:
        for n in z.namelist():
            if not n.endswith("/"):
                f.write(n + "\n" + hashlib.sha256(z.read(n)).hexdigest() + "\n")
    print(zp, os.path.getsize(zp))
