"""MPC Plugins Manager screen, drawn by code. Copyright (c) 2026 The Disk King. All rights reserved.

Writes the MPC touch screen (TUI.json + pictures) for every color theme, plus the plugin screenshot strip:
    skins/                       the default theme (Disk King), copied into "Plugin Skins" by build.py
    Content/Themes/<Theme>/      every theme; the plugin copies one over "Plugin Skins" when you pick it
    skins/thumbs.png + Content/thumbs.txt   small screenshots of the plugins, one frame each (frame 0 = no picture)

Usage (after build.py has made build/params.json):
    python "plugins/MPC Plugins Manager/make_skin.py" [--no-thumbs]
    python tools/build.py "plugins/MPC Plugins Manager"
The screen is 1280 x 628. Parameter numbers come from build/params.json by name, so the C++ order is the truth.
"""
import io
import json
import os
import shutil
import sys
import urllib.request

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
SKINS = os.path.join(HERE, "skins")
THEMES_OUT = os.path.join(HERE, "Content", "Themes")
W, H = 1280, 628
ROWS = 5
CARD_Y0, CARD_H, CARD_GAP = 124, 80, 4
THUMB_W, THUMB_H = 140, 70
CATALOG = "https://mpcplugins.github.io/catalog.json"

# ------------------------------------------------------------------ themes (colors as RRGGBB)
THEMES = {
    "Disk King":     dict(bg="f5f5dc", panel="ece6cf", card="fffdf5", line="d9cfb2", text="4b3621", muted="8a7a62", accent="ea861a", accent_text="ffffff",
                          good="15803d", warn="b7791f", bad="dc2626", tab_on="4b3621", tab_on_text="ffffff", head="f5f5dc"),
    "Light":         dict(bg="e9e7e2", panel="f7f6f3", card="ffffff", line="d6d3cc", text="1b1c1f", muted="6b6b70", accent="d63a30", accent_text="ffffff",
                          good="2f8a5b", warn="b88400", bad="d63a30", tab_on="d8d5ce", tab_on_text="1b1c1f", head="f7f6f3"),
    "Dark":          dict(bg="0c0c0d", panel="161618", card="1d1d20", line="2e2e33", text="f2f2f2", muted="9a9aa2", accent="e5484d", accent_text="ffffff",
                          good="46c37b", warn="e0a100", bad="ff5a5a", tab_on="34343a", tab_on_text="ffffff", head="111113"),
    "Midnight":      dict(bg="0b1020", panel="121a30", card="17213b", line="27345a", text="e8edff", muted="8e9ac0", accent="4f7cff", accent_text="ffffff",
                          good="3ccf91", warn="f2b84b", bad="ff6b6b", tab_on="2a3a6a", tab_on_text="ffffff", head="0e1428"),
    "MPC Red":       dict(bg="101010", panel="1a1a1a", card="202020", line="333333", text="ffffff", muted="a0a0a0", accent="d31145", accent_text="ffffff",
                          good="3fbf6f", warn="f0a020", bad="ff3355", tab_on="d31145", tab_on_text="ffffff", head="141414"),
    "Studio Green":  dict(bg="0f1a14", panel="15241b", card="1b2e22", line="2c4636", text="e9f5ec", muted="93b39d", accent="34c46a", accent_text="0f1a14",
                          good="5fe08c", warn="e6c04a", bad="ff6b5a", tab_on="2c4636", tab_on_text="ffffff", head="122019"),
    "Ocean":         dict(bg="eaf4fb", panel="f6fbff", card="ffffff", line="c9dfee", text="0f2a3d", muted="5b7a90", accent="0b84d6", accent_text="ffffff",
                          good="13865a", warn="b07b00", bad="d6453a", tab_on="0f2a3d", tab_on_text="ffffff", head="f6fbff"),
    "Purple Haze":   dict(bg="150f22", panel="1e1631", card="261c3d", line="3a2d5c", text="f1eaff", muted="a796c9", accent="a259ff", accent_text="ffffff",
                          good="4fd18b", warn="f5b942", bad="ff5f7e", tab_on="3a2d5c", tab_on_text="ffffff", head="1a1229"),
    "Sunset":        dict(bg="fff3e8", panel="fff9f3", card="ffffff", line="f3d8c2", text="3a1f14", muted="8c6a5a", accent="ff6a3d", accent_text="ffffff",
                          good="2f8a5b", warn="c27c00", bad="d6343a", tab_on="3a1f14", tab_on_text="ffffff", head="fff9f3"),
    "High Contrast": dict(bg="000000", panel="000000", card="000000", line="ffffff", text="ffffff", muted="ffff00", accent="ffff00", accent_text="000000",
                          good="00ff66", warn="ffcc00", bad="ff3333", tab_on="ffffff", tab_on_text="000000", head="000000"),
}
DEFAULT_THEME = "Disk King"
THEME_ORDER = list(THEMES)

# ------------------------------------------------------------------ drawing helpers
FONT_BOLD = ["C:/Windows/Fonts/seguisb.ttf", "C:/Windows/Fonts/segoeuib.ttf", "C:/Windows/Fonts/arialbd.ttf"]
FONT_BLACK = ["C:/Windows/Fonts/seguibl.ttf", "C:/Windows/Fonts/ariblk.ttf"] + FONT_BOLD


def font(size, black=False):
    for f in (FONT_BLACK if black else FONT_BOLD):
        if os.path.isfile(f):
            return ImageFont.truetype(f, size)
    return ImageFont.load_default()


def rgb(h, a=255):
    return (int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16), a)


SYMBOLS = "\u2713\u26a0\u2191\u2193\u21ba"


def symbol_font(size):
    for f in ("C:/Windows/Fonts/seguisym.ttf", "C:/Windows/Fonts/segoeui.ttf"):
        if os.path.isfile(f):
            return ImageFont.truetype(f, size)
    return ImageFont.load_default()


def text_center(d, box, text, colour, size, black=False):
    x0, y0, x1, y1 = box
    if text and text[0] in SYMBOLS:           # draw the symbol with a symbol font, then the words
        sym, rest = text[0], text[1:].lstrip()
        s = size
        while s > 9:
            f, fs = font(s, black), symbol_font(s)
            tw = d.textlength(rest, font=f) + d.textlength(sym + " ", font=fs)
            if tw <= (x1 - x0) - 12:
                break
            s -= 1
        l, t, r, b = d.textbbox((0, 0), rest, font=f)
        tx = x0 + ((x1 - x0) - tw) / 2
        ty = y0 + ((y1 - y0) - (b - t)) / 2 - t
        d.text((tx, ty), sym, font=fs, fill=colour)
        d.text((tx + d.textlength(sym + " ", font=fs), ty), rest, font=f, fill=colour)
        return
    s = size
    while s > 9:
        f = font(s, black)
        l, t, r, b = d.textbbox((0, 0), text, font=f)
        if r - l <= (x1 - x0) - 12:
            break
        s -= 1
    l, t, r, b = d.textbbox((0, 0), text, font=f)
    d.text((x0 + ((x1 - x0) - (r - l)) / 2 - l, y0 + ((y1 - y0) - (b - t)) / 2 - t), text, font=f, fill=colour)


def pill(w, h, fill, outline=None, text=None, text_colour=None, size=18, radius=None, black=False, width=2):
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle((0, 0, w - 1, h - 1), radius=radius if radius is not None else h // 2 - 1, fill=fill, outline=outline, width=width)
    if text:
        text_center(d, (0, 0, w, h), text, text_colour, size, black)
    return img


def strip(frames):
    w, h = frames[0].size
    out = Image.new("RGBA", (w, h * len(frames)), (0, 0, 0, 0))
    for i, f in enumerate(frames):
        out.paste(f, (0, i * h))
    return out


# ------------------------------------------------------------------ TUI.json building blocks
def bounds(x, y, w, h, focus=False, when="Always"):
    return {"version": 1, "acceptsHWFocus": "Yes" if focus else "No", "showWhenDataModelInvalid": "Show",
            "whenVisible": when, "boundsType": "Absolute", "bounds": "%d %d %d %d" % (x, y, w, h)}


def entry(name, typ, data, b, handle=None):
    return {"version": 2, "componentData": {"version": 1, "name": name, "type": typ, "data": data},
            "handle remapping": {"version": 1, "map": [{"key": "Data", "value": handle}] if handle else []}, "bounds": b}


def style(colour, size, bold=True, just="left verticallyCentred", case="Original", fontname="Titillium Web"):
    return {"version": 1, "font": {"version": 1, "name": fontname, "style": "SemiBold" if bold else "Regular", "height": float(size)},
            "colour": "ff" + colour, "justification": just, "case": case}


def label(name, param, x, y, w, h, colour, size, bold=True, just="left verticallyCentred"):
    return entry(name, "Label", {"version": 1, "textStyle": style(colour, size, bold, just), "type": "Value", "handleName": "Data"},
                 bounds(x, y, w, h), "Parameter %d" % param)


def fixed_text(name, text, x, y, w, h, colour, size, bold=True, just="left verticallyCentred"):
    return entry(name, "Label", {"version": 1, "textStyle": style(colour, size, bold, just), "type": "Text", "handleName": "Invalid", "text": text},
                 bounds(x, y, w, h))


def image(name, file, x, y, w, h):
    return entry(name, "Image", {"version": 2, "imageType": "Regular", "colour": "0", "image": file}, bounds(x, y, w, h))


def film(name, file, frames, param, x, y, w, h):
    return entry(name, "Image", {"version": 2, "imageType": "FilmStrip", "colour": "0", "image": file, "numFrames": frames, "handleName": "Data"},
                 bounds(x, y, w, h), "Parameter %d" % param)


def button(name, on, off, param, x, y, w, h, button_id=1, group=1):
    return entry(name, "Button", {"version": 2, "onImage": on, "offImage": off, "buttonId": button_id, "numButtonsInGroup": group,
                                  "handleName": "Data", "gestureBehaviour": "Instant"}, bounds(x, y, w, h, focus=False), "Parameter %d" % param)


# ------------------------------------------------------------------ one theme
def build_theme(name, idx, out):
    os.makedirs(out, exist_ok=True)
    c = THEMES[name]
    clear = (0, 0, 0, 0)

    # background: header, tab bar, cards, footer
    bg = Image.new("RGBA", (W, H), rgb(c["bg"]))
    d = ImageDraw.Draw(bg)
    d.rectangle((0, 0, W, 58), fill=rgb(c["head"]))
    d.line((0, 58, W, 58), fill=rgb(c["line"]), width=2)
    d.rounded_rectangle((16, 66, 1264, 116), radius=14, fill=rgb(c["panel"]), outline=rgb(c["line"]), width=2)
    for r in range(ROWS):
        y = CARD_Y0 + r * (CARD_H + CARD_GAP)
        d.rounded_rectangle((16, y, 1264, y + CARD_H), radius=12, fill=rgb(c["card"]), outline=rgb(c["line"]), width=2)
        d.rounded_rectangle((24, y + 5, 24 + THUMB_W, y + 5 + THUMB_H), radius=6, fill=rgb(c["panel"]))
    d.rectangle((0, 548, W, H), fill=rgb(c["head"]))
    d.line((0, 548, W, 548), fill=rgb(c["line"]), width=2)
    # title, drawn in (always visible)
    f1 = font(30, black=True)
    d.text((22, 9), "MPC", font=f1, fill=rgb(c["text"]))
    x = 22 + d.textlength("MPC ", font=f1)
    d.text((x, 9), "PLUGINS", font=f1, fill=rgb(c["accent"] if name != "High Contrast" else c["text"]))
    x += d.textlength("PLUGINS ", font=f1)
    d.text((x, 9), "MANAGER", font=f1, fill=rgb(c["text"]))
    d.text((24, 42), "BY THE DISK KING", font=font(11), fill=rgb(c["muted"]))
    bg.save(os.path.join(out, "bg.png"))

    # tabs (a 5-way radio group on "Show")
    tabs = ["TESTED & WORKING", "UNTESTED", "INSTALLED", "UPDATES", "REMOVED"]
    tab_w = [230, 150, 150, 150, 140]
    for i, t in enumerate(tabs):
        red = t == "UNTESTED"
        on = pill(tab_w[i], 40, rgb(c["bad"] if red else c["tab_on"]), None, ("\u26a0 " if red else "\u2713 " if i == 0 else "") + t,
                  rgb("ffffff" if red else c["tab_on_text"]), 15, radius=10)
        off = pill(tab_w[i], 40, clear, rgb(c["bad"]) if red else None, ("\u26a0 " if red else "\u2713 " if i == 0 else "") + t,
                   rgb(c["bad"] if red else c["muted"]), 15, radius=10)
        on.save(os.path.join(out, "tab%d_on.png" % i))
        off.save(os.path.join(out, "tab%d_off.png" % i))
    # blank pills that carry live text (sort, type, theme, page) and arrows
    pill(150, 40, rgb(c["card"]), rgb(c["line"]), None, None, radius=10).save(os.path.join(out, "pill150.png"))
    pill(170, 40, rgb(c["card"]), rgb(c["line"]), None, None, radius=10).save(os.path.join(out, "pill170.png"))
    pill(48, 48, rgb(c["card"]), rgb(c["line"]), "<", rgb(c["text"]), 22, radius=10).save(os.path.join(out, "prev.png"))
    pill(48, 48, rgb(c["card"]), rgb(c["line"]), ">", rgb(c["text"]), 22, radius=10).save(os.path.join(out, "next.png"))
    pill(190, 48, clear, rgb(c["warn"]), "UPDATE ALL", rgb(c["warn"]), 17, radius=10, black=True).save(os.path.join(out, "updall.png"))
    pill(150, 48, clear, rgb(c["line"]), "GET LIST", rgb(c["text"]), 15, radius=10).save(os.path.join(out, "refresh.png"))
    pill(244, 48, clear, rgb(c["line"]), None, None, radius=10).save(os.path.join(out, "themebtn.png"))
    Image.new("RGBA", (244, 48), clear).save(os.path.join(out, "hittheme.png"))
    Image.new("RGBA", (180, 52), clear).save(os.path.join(out, "hit180.png"))   # see-through tap areas over live pictures
    Image.new("RGBA", (THUMB_W, THUMB_H), clear).save(os.path.join(out, "hitthumb.png"))

    # per-card pictures that change: action button, badge, tested chip
    act = ["INSTALL", "UPDATE", "REMOVE", "RESTORE", "WORKING...", "NOT HERE", ""]
    frames = []
    for i, t in enumerate(act):
        if not t:
            frames.append(Image.new("RGBA", (180, 52), clear)); continue
        if t == "INSTALL": im = pill(180, 52, rgb(c["accent"]), None, "\u2193 " + t, rgb(c["accent_text"]), 18, radius=8, black=True)
        elif t == "UPDATE": im = pill(180, 52, rgb(c["good"]), None, "\u2191 " + t, rgb("ffffff"), 18, radius=8, black=True)
        elif t == "RESTORE": im = pill(180, 52, rgb(c["good"]), None, "\u21ba " + t, rgb("ffffff"), 18, radius=8, black=True)
        elif t == "REMOVE": im = pill(180, 52, clear, rgb(c["line"]), t, rgb(c["text"]), 17, radius=8)
        elif t == "WORKING...": im = pill(180, 52, rgb(c["panel"]), rgb(c["line"]), t, rgb(c["muted"]), 16, radius=8)
        else: im = pill(180, 52, clear, rgb(c["line"]), t, rgb(c["muted"]), 14, radius=8)
        frames.append(im)
    strip(frames).save(os.path.join(out, "action.png"))
    badges = [("", None), ("STABLE", c["muted"]), ("BETA", c["bad"]), ("UNTESTED", c["warn"]), ("INSTALLED", c["good"])]
    strip([Image.new("RGBA", (110, 28), clear) if not t else pill(110, 28, clear, rgb(col), t, rgb(col), 13, radius=6)
           for t, col in badges]).save(os.path.join(out, "badge.png"))
    chips = [Image.new("RGBA", (300, 28), clear),
             pill(300, 28, clear, rgb(c["good"]), None, None, radius=6),
             pill(300, 28, clear, rgb(c["warn"]), None, None, radius=6)]
    strip(chips).save(os.path.join(out, "chip.png"))
    dot = [pill(14, 14, rgb(c["bad"]), None), pill(14, 14, rgb(c["good"]), None)]
    strip(dot).save(os.path.join(out, "online.png"))

    # ---------------- TUI.json
    kids = [image("background", "bg.png", 0, 0, W, H)]
    kids.append(label("status", idx["Status"], 330, 4, 560, 28, c["text"], 19, True, "horizontallyCentred verticallyCentred"))
    kids.append(label("status2", idx["Status 2"], 330, 30, 560, 24, c["muted"], 15, True, "horizontallyCentred verticallyCentred"))
    kids.append(film("online", "online.png", 2, idx["Online"], 900, 22, 14, 14))
    kids.append(label("storage", idx["Storage"], 922, 6, 340, 46, c["muted"], 17, True, "right verticallyCentred"))
    x = 24
    for i, wdt in enumerate(tab_w):
        kids.append(button("tab%d" % i, "tab%d_on.png" % i, "tab%d_off.png" % i, idx["Show"], x, 71, wdt, 40, i, 5))
        x += wdt + 6
    kids.append(label("upd count", idx["Update Count"], 24 + sum(tab_w[:4]) + 18 - 40, 71, 36, 40, c["warn"], 18, True, "left verticallyCentred"))
    kids.append(image("sort pill", "pill170.png", 920, 71, 170, 40))
    kids.append(label("sort", idx["Sort"], 920, 71, 170, 40, c["text"], 16, True, "horizontallyCentred verticallyCentred"))
    kids.append(button("sort btn", "hit180.png", "hit180.png", idx["Sort Tap"], 920, 71, 170, 40))
    kids.append(image("kind pill", "pill150.png", 1100, 71, 150, 40))
    kids.append(label("kind", idx["Type"], 1100, 71, 150, 40, c["text"], 16, True, "horizontallyCentred verticallyCentred"))
    kids.append(button("kind btn", "hit180.png", "hit180.png", idx["Type Tap"], 1100, 71, 150, 40))
    for r in range(ROWS):
        n = r + 1
        y = CARD_Y0 + r * (CARD_H + CARD_GAP)
        kids.append(film("thumb%d" % n, "thumbs.png", -1, idx["Thumb %d" % n], 24, y + 5, THUMB_W, THUMB_H))
        kids.append(label("name%d" % n, idx["Name %d" % n], 180, y + 4, 560, 40, c["text"], 26, True))
        kids.append(label("maker%d" % n, idx["Maker %d" % n], 180, y + 42, 240, 32, c["muted"], 17, False))
        kids.append(label("meta%d" % n, idx["Meta %d" % n], 424, y + 42, 316, 32, c["muted"], 17, False))
        kids.append(film("badge%d" % n, "badge.png", 5, idx["Badge %d" % n], 748, y + 8, 110, 28))
        kids.append(label("version%d" % n, idx["Version %d" % n], 868, y + 4, 150, 36, c["text"], 22, True, "horizontallyCentred verticallyCentred"))
        kids.append(film("chip%d" % n, "chip.png", 3, idx["Chip %d" % n], 748, y + 46, 300, 28))
        kids.append(label("tested%d" % n, idx["Tested %d" % n], 756, y + 46, 290, 28, c["text"], 15, True, "horizontallyCentred verticallyCentred"))
        kids.append(film("actimg%d" % n, "action.png", 7, idx["Action Look %d" % n], 1068, y + 14, 180, 52))
        kids.append(button("act%d" % n, "hit180.png", "hit180.png", idx["Action %d" % n], 1068, y + 14, 180, 52))
    kids.append(button("prev", "prev.png", "prev.png", idx["Prev"], 24, 564, 48, 48))
    kids.append(label("page", idx["Page"], 76, 564, 90, 48, c["text"], 20, True, "horizontallyCentred verticallyCentred"))
    kids.append(button("next", "next.png", "next.png", idx["Next"], 170, 564, 48, 48))
    kids.append(label("summary", idx["Summary"], 236, 556, 420, 34, c["text"], 16, True))
    kids.append(label("phone", idx["Phone"], 236, 586, 300, 30, c["muted"], 13, False))
    kids.append(label("pin", idx["PIN"], 540, 586, 116, 30, c["text"], 14, True))
    kids.append(button("refresh", "refresh.png", "refresh.png", idx["Get List"], 664, 564, 150, 48))
    kids.append(button("update all", "updall.png", "updall.png", idx["Update All"], 822, 564, 190, 48))
    kids.append(image("theme pill", "themebtn.png", 1020, 564, 244, 48))
    kids.append(label("theme", idx["Theme"], 1020, 564, 244, 48, c["text"], 15, True, "horizontallyCentred verticallyCentred"))
    kids.append(button("theme btn", "hittheme.png", "hittheme.png", idx["Theme Tap"], 1020, 564, 244, 48))
    kids = [k for k in kids if k]

    # film strips must know their frame count; thumbs.png's is filled in by main()
    tui = {"pageData": {"version": 1, "componentDefinitions": {"version": 2, "importFiles": [], "localComponentDefinitions": []},
                        "replacementStrings": [], "info": {"version": 1, "type": "CompleteDescription"},
                        "tabs": [{"version": 1, "tabName": "PLUGINS", "fnKeyIndex": 0, "fnKeySubIndex": 0,
                                  "componentDefinition": {"version": 2, "actions": [],
                                                          "backgroundData": {"version": 1, "focussed": {"version": 1, "colour": "ff" + c["bg"], "image": ""},
                                                                             "unfocussed": {"version": 1, "colour": "ff" + c["bg"], "image": ""}},
                                                          "ignoreMousePresses": False, "disableCoarseDataWheel": False, "componentsData": kids},
                                  "qlinkBoundsData": []}]}}   # no Q-Link column boxes over the cards
    return tui


def qlinks(idx):
    """Q-Link 1 turns pages, Q-Link 2 changes the list (both screen and program mode)."""
    screen = {"Q-Link %d" % i: -1 for i in range(1, 17)}
    screen["Q-Link 13"] = idx["Page Knob"]
    screen["Q-Link 14"] = idx["Show"]
    program = {"Q-Link %d" % i: -1 for i in range(1, 17)}
    program["Q-Link 1"] = idx["Page Knob"]
    program["Q-Link 2"] = idx["Show"]
    return {"version": 4, "info": {"version": 1, "type": "CompleteDescription"},
            "Screen Mode Q-Links": {"version": 4, "map": [{"Tab": 1, "SubTab": 1, "Bank Direction": "Column", "Q-Links": screen}]},
            "Program Mode Q-Links": program}


def make_thumbs(want):
    """Downloads every plugin's screenshot once and builds the strip. Frame 0 is the blank 'no picture' frame."""
    cache = os.path.join(HERE, "build", "thumb_cache")
    os.makedirs(cache, exist_ok=True)
    frames, ids = [Image.new("RGBA", (THUMB_W, THUMB_H), (0, 0, 0, 0))], []
    if want:
        cat = json.load(urllib.request.urlopen(CATALOG, timeout=60))
        for p in sorted(cat["plugins"], key=lambda p: p["id"]):
            url = p.get("screenshot") or ""
            if not url.startswith("https://"):
                continue
            path = os.path.join(cache, p["id"] + ".png")
            try:
                if not os.path.exists(path):
                    req = urllib.request.Request(url, headers={"User-Agent": "MPCPluginsManager-build"})
                    data = urllib.request.urlopen(req, timeout=30).read()
                    im = Image.open(io.BytesIO(data)).convert("RGB")
                    im.thumbnail((THUMB_W * 2, THUMB_H * 2))
                    # fill the frame, cropping the middle
                    scale = max(THUMB_W / im.width, THUMB_H / im.height)
                    im = im.resize((max(1, int(im.width * scale + .5)), max(1, int(im.height * scale + .5))), Image.LANCZOS)
                    l, t = (im.width - THUMB_W) // 2, (im.height - THUMB_H) // 2
                    im.crop((l, t, l + THUMB_W, t + THUMB_H)).save(path)
                frames.append(Image.open(path).convert("RGBA"))
                ids.append(p["id"])
            except Exception as err:
                print("  no thumbnail for", p["id"], err)
    return strip(frames), ids


def main():
    params_json = os.path.join(HERE, "build", "params.json")
    if not os.path.isfile(params_json):
        sys.exit("build/params.json missing: run tools/build.py on this plugin first")
    plist = json.load(open(params_json, encoding="utf-8"))["params"]
    idx = {p["name"]: i for i, p in enumerate(plist)}
    thumbs, ids = make_thumbs("--no-thumbs" not in sys.argv)
    print("thumbnails:", len(ids))
    if os.path.isdir(THEMES_OUT):
        shutil.rmtree(THEMES_OUT)
    for name in THEME_ORDER:
        out = os.path.join(THEMES_OUT, name)
        tui = build_theme(name, idx, out)
        for k in tui["pageData"]["tabs"][0]["componentDefinition"]["componentsData"]:
            if k["componentData"]["name"].startswith("thumb"):
                k["componentData"]["data"]["numFrames"] = 1 + len(ids)
        json.dump(tui, open(os.path.join(out, "TUI.json"), "w", encoding="utf-8"), indent=1)
        json.dump(qlinks(idx), open(os.path.join(out, "Q-Links.json"), "w", encoding="utf-8"), indent=1)
        open(os.path.join(out, ".theme"), "w").write(name)
    if os.path.isdir(SKINS):
        shutil.rmtree(SKINS)
    shutil.copytree(os.path.join(THEMES_OUT, DEFAULT_THEME), SKINS)
    thumbs.save(os.path.join(SKINS, "thumbs.png"), optimize=True)
    os.makedirs(os.path.join(HERE, "Content"), exist_ok=True)
    open(os.path.join(HERE, "Content", "thumbs.txt"), "w", encoding="utf-8", newline="\n").write("\n".join(ids) + "\n")
    open(os.path.join(HERE, "Content", "themes.txt"), "w", encoding="utf-8", newline="\n").write("\n".join(THEME_ORDER) + "\n")
    print("themes:", len(THEME_ORDER), "| thumbs.png", thumbs.size, os.path.getsize(os.path.join(SKINS, "thumbs.png")) // 1024, "KB")


if __name__ == "__main__":
    main()
