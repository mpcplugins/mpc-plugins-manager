"""MPC Plugins Manager screen, drawn by code. Copyright (c) 2026 The Disk King. All rights reserved.

Writes the MPC touch screen (TUI.json + pictures) for every color theme, plus the plugin screenshot strip:
    skins/                       the screen: all themes in one TUI.json (each shown only while "Theme Pick" is on it,
                                 so switching is live) + pictures, copied into "Plugin Skins" by build.py
    skins/thumbs.png + Content/thumbs.txt   small screenshots of the plugins, one frame each (frame 0 = no picture)

Usage (after build.py has made build/params.json):
    python "plugins/MPC Plugins Manager/make_skin.py" [--no-thumbs]
    python tools/build.py "plugins/MPC Plugins Manager"
The screen is 1280 x 628. Parameter numbers come from build/params.json by name, so the C++ order is the truth.
"""
import hashlib
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
ROWS = 3                                   # 3 big cards; roll a finger over them to scroll the list
CARD_Y0, CARD_H, CARD_GAP = 124, 136, 4
THUMB_W, THUMB_H = 200, 100                # one screenshot frame (shown at 236 x 118)
SHOT_X, SHOT_W, SHOT_H = 26, 236, 118
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
def bounds(x, y, w, h, focus=False, when="Always", only_when=None):
    b = {"version": 1, "acceptsHWFocus": "Yes" if focus else "No", "showWhenDataModelInvalid": "Show",
         "whenVisible": when, "boundsType": "Absolute", "bounds": "%d %d %d %d" % (x, y, w, h)}
    if only_when:                                     # shown only while these handles are valid (see main())
        b["version"] = 2
        b["additionalInvalidatingHandles"] = list(only_when)
    return b


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


def film(name, file, frames, param, x, y, w, h, only_when=None):
    return entry(name, "Image", {"version": 2, "imageType": "FilmStrip", "colour": "0", "image": file, "numFrames": frames, "handleName": "Data"},
                 bounds(x, y, w, h, only_when=only_when), "Parameter %d" % param)


def drag_area(name, file, param, x, y, w, h):
    """An invisible vertical-drag knob: rolling a finger over the cards moves the list position."""
    return entry(name, "Knob", {"version": 3, "knobType": "FilmStrip", "filmStrip": file, "numFrames": 2, "invert": False,
                                "dragOrientation": "Vertical", "handleName": "Data"}, bounds(x, y, w, h), "Parameter %d" % param)


def button(name, on, off, param, x, y, w, h, button_id=1, group=1):
    return entry(name, "Button", {"version": 2, "onImage": on, "offImage": off, "buttonId": button_id, "numButtonsInGroup": group,
                                  "handleName": "Data", "gestureBehaviour": "Instant"}, bounds(x, y, w, h, focus=False), "Parameter %d" % param)


# ------------------------------------------------------------------ one theme
def build_theme(name, idx, out, prefix):
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
        d.rounded_rectangle((SHOT_X, y + 9, SHOT_X + SHOT_W, y + 9 + SHOT_H), radius=8, fill=rgb(c["panel"]))
    # a thin scroll guide on the right of the list
    d.rounded_rectangle((1268, CARD_Y0 + 8, 1274, CARD_Y0 + ROWS * (CARD_H + CARD_GAP) - 12), radius=3, fill=rgb(c["line"]))
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
    bg.save(os.path.join(out, prefix + "bg.png"))

    # tabs (a 5-way radio group on "Show")
    tabs = ["TESTED & WORKING", "UNTESTED", "INSTALLED", "UPDATES", "REMOVED"]
    tab_w = [230, 150, 150, 150, 140]
    for i, t in enumerate(tabs):
        red = t == "UNTESTED"
        on = pill(tab_w[i], 40, rgb(c["bad"] if red else c["tab_on"]), None, ("\u26a0 " if red else "\u2713 " if i == 0 else "") + t,
                  rgb("ffffff" if red else c["tab_on_text"]), 15, radius=10)
        off = pill(tab_w[i], 40, clear, rgb(c["bad"]) if red else None, ("\u26a0 " if red else "\u2713 " if i == 0 else "") + t,
                   rgb(c["bad"] if red else c["muted"]), 15, radius=10)
        on.save(os.path.join(out, prefix + "tab%d_on.png" % i))
        off.save(os.path.join(out, prefix + "tab%d_off.png" % i))
    # blank pills that carry live text (sort, type, theme, page) and arrows
    pill(150, 40, rgb(c["card"]), rgb(c["line"]), None, None, radius=10).save(os.path.join(out, prefix + "pill150.png"))
    pill(170, 40, rgb(c["card"]), rgb(c["line"]), None, None, radius=10).save(os.path.join(out, prefix + "pill170.png"))
    pill(48, 48, rgb(c["card"]), rgb(c["line"]), "<", rgb(c["text"]), 22, radius=10).save(os.path.join(out, prefix + "prev.png"))
    pill(48, 48, rgb(c["card"]), rgb(c["line"]), ">", rgb(c["text"]), 22, radius=10).save(os.path.join(out, prefix + "next.png"))
    pill(190, 48, clear, rgb(c["warn"]), "UPDATE ALL", rgb(c["warn"]), 17, radius=10, black=True).save(os.path.join(out, prefix + "updall.png"))
    pill(150, 48, clear, rgb(c["line"]), "GET LIST", rgb(c["text"]), 15, radius=10).save(os.path.join(out, prefix + "refresh.png"))
    pill(244, 48, clear, rgb(c["line"]), None, None, radius=10).save(os.path.join(out, prefix + "themebtn.png"))
    Image.new("RGBA", (244, 48), clear).save(os.path.join(out, prefix + "hittheme.png"))
    Image.new("RGBA", (180, 52), clear).save(os.path.join(out, prefix + "hit180.png"))   # see-through tap areas over live pictures
    Image.new("RGBA", (THUMB_W, THUMB_H), clear).save(os.path.join(out, prefix + "hitthumb.png"))
    strip([Image.new("RGBA", (8, 8), clear)] * 2).save(os.path.join(out, prefix + "clear2.png"))

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
    strip(frames).save(os.path.join(out, prefix + "action.png"))
    badges = [("", None), ("STABLE", c["muted"]), ("BETA", c["bad"]), ("UNTESTED", c["warn"]), ("INSTALLED", c["good"])]
    strip([Image.new("RGBA", (110, 28), clear) if not t else pill(110, 28, clear, rgb(col), t, rgb(col), 13, radius=6)
           for t, col in badges]).save(os.path.join(out, prefix + "badge.png"))
    chips = [Image.new("RGBA", (300, 28), clear),
             pill(300, 28, clear, rgb(c["good"]), None, None, radius=6),
             pill(300, 28, clear, rgb(c["warn"]), None, None, radius=6)]
    strip(chips).save(os.path.join(out, prefix + "chip.png"))
    dot = [pill(14, 14, rgb(c["bad"]), None), pill(14, 14, rgb(c["good"]), None)]
    strip(dot).save(os.path.join(out, prefix + "online.png"))

    # ---------------- TUI.json
    kids = [image("background", prefix + "bg.png", 0, 0, W, H)]
    kids.append(label("status", idx["Status"], 330, 4, 560, 28, c["text"], 19, True, "horizontallyCentred verticallyCentred"))
    kids.append(label("status2", idx["Status 2"], 330, 30, 560, 24, c["muted"], 15, True, "horizontallyCentred verticallyCentred"))
    kids.append(film("online", prefix + "online.png", 2, idx["Online"], 900, 22, 14, 14))
    kids.append(label("storage", idx["Storage"], 922, 6, 340, 46, c["muted"], 17, True, "right verticallyCentred"))
    x = 24
    for i, wdt in enumerate(tab_w):
        kids.append(button("tab%d" % i, prefix + "tab%d_on.png" % i, prefix + "tab%d_off.png" % i, idx["Show"], x, 71, wdt, 40, i, 5))
        x += wdt + 6
    kids.append(label("upd count", idx["Update Count"], 24 + sum(tab_w[:4]) + 18 - 40, 71, 36, 40, c["warn"], 18, True, "left verticallyCentred"))
    kids.append(image("sort pill", prefix + "pill170.png", 920, 71, 170, 40))
    kids.append(label("sort", idx["Sort"], 920, 71, 170, 40, c["text"], 16, True, "horizontallyCentred verticallyCentred"))
    kids.append(button("sort btn", prefix + "hit180.png", prefix + "hit180.png", idx["Sort Tap"], 920, 71, 170, 40))
    kids.append(image("kind pill", prefix + "pill150.png", 1100, 71, 150, 40))
    kids.append(label("kind", idx["Type"], 1100, 71, 150, 40, c["text"], 16, True, "horizontallyCentred verticallyCentred"))
    kids.append(button("kind btn", prefix + "hit180.png", prefix + "hit180.png", idx["Type Tap"], 1100, 71, 150, 40))
    for r in range(ROWS):                              # card pictures and texts
        n = r + 1
        y = CARD_Y0 + r * (CARD_H + CARD_GAP)
        bank = "Thumb Bank %d" % n
        kids.append(film("thumbA%d" % n, "thumbsA.png", -1, idx["Thumb %d" % n], SHOT_X, y + 9, SHOT_W, SHOT_H,
                         only_when=["IndexedEnabling/0/2/Parameter %d" % idx[bank]]))
        kids.append(film("thumbB%d" % n, "thumbsB.png", -1, idx["Thumb %d" % n], SHOT_X, y + 9, SHOT_W, SHOT_H,
                         only_when=["IndexedEnabling/1/2/Parameter %d" % idx[bank]]))
        kids.append(label("name%d" % n, idx["Name %d" % n], 278, y + 6, 520, 40, c["text"], 28, True))
        kids.append(label("maker%d" % n, idx["Maker %d" % n], 278, y + 44, 250, 28, c["accent"] if name != "High Contrast" else c["text"], 17, True))
        kids.append(label("meta%d" % n, idx["Meta %d" % n], 532, y + 44, 270, 28, c["muted"], 17, False))
        kids.append(label("about1-%d" % n, idx["About A %d" % n], 278, y + 74, 520, 26, c["muted"], 17, False))
        kids.append(label("about2-%d" % n, idx["About B %d" % n], 278, y + 100, 520, 26, c["muted"], 17, False))
        kids.append(film("badge%d" % n, prefix + "badge.png", 5, idx["Badge %d" % n], 812, y + 10, 110, 28))
        kids.append(label("version%d" % n, idx["Version %d" % n], 924, y + 6, 150, 36, c["text"], 22, True, "right verticallyCentred"))
        kids.append(film("chip%d" % n, prefix + "chip.png", 3, idx["Chip %d" % n], 812, y + 50, 262, 28))
        kids.append(label("tested%d" % n, idx["Tested %d" % n], 818, y + 50, 250, 28, c["text"], 15, True, "horizontallyCentred verticallyCentred"))
        kids.append(label("stats%d" % n, idx["Stats %d" % n], 812, y + 88, 262, 30, c["muted"], 15, True, "left verticallyCentred"))
        kids.append(film("actimg%d" % n, prefix + "action.png", 7, idx["Action Look %d" % n], 1080, y + 42, 180, 52))
    # finger scrolling: an invisible drag strip over the cards, under the buttons (so buttons still take taps)
    kids.append(drag_area("scroll", prefix + "clear2.png", idx["Scroll"], 16, CARD_Y0, 1060, ROWS * (CARD_H + CARD_GAP) - CARD_GAP))
    for r in range(ROWS):
        n = r + 1
        y = CARD_Y0 + r * (CARD_H + CARD_GAP)
        kids.append(button("act%d" % n, prefix + "hit180.png", prefix + "hit180.png", idx["Action %d" % n], 1080, y + 42, 180, 52))
    kids.append(button("prev", prefix + "prev.png", prefix + "prev.png", idx["Prev"], 24, 564, 48, 48))
    kids.append(label("page", idx["Page"], 76, 564, 150, 48, c["text"], 18, True, "horizontallyCentred verticallyCentred"))
    kids.append(button("next", prefix + "next.png", prefix + "next.png", idx["Next"], 230, 564, 48, 48))
    kids.append(label("summary", idx["Summary"], 292, 556, 364, 34, c["text"], 16, True))
    kids.append(label("phone", idx["Phone"], 292, 586, 250, 30, c["muted"], 13, False))
    kids.append(label("pin", idx["PIN"], 548, 586, 108, 30, c["text"], 14, True))
    kids.append(button("refresh", prefix + "refresh.png", prefix + "refresh.png", idx["Get List"], 664, 564, 150, 48))
    kids.append(button("update all", prefix + "updall.png", prefix + "updall.png", idx["Update All"], 822, 564, 190, 48))
    kids.append(image("theme pill", prefix + "themebtn.png", 1020, 564, 244, 48))
    kids.append(label("theme", idx["Theme"], 1020, 564, 244, 48, c["text"], 15, True, "horizontallyCentred verticallyCentred"))
    kids.append(button("theme btn", prefix + "hittheme.png", prefix + "hittheme.png", idx["Theme Tap"], 1020, 564, 244, 48))
    kids = [k for k in kids if k]

    # ---------------- the details page (tap a card's picture). The cards hide while it is open.
    det = idx["Detail Open"]
    list_parts = ("thumb", "name", "maker", "meta", "about", "badge", "version", "chip", "tested", "stats", "actimg", "act", "scroll")
    for k in kids:
        nm = k["componentData"]["name"]
        if nm.startswith(list_parts):
            b = k["bounds"]
            b["version"] = 2
            b["additionalInvalidatingHandles"] = b.get("additionalInvalidatingHandles", []) + ["IndexedEnabling/0/2/Parameter %d" % det]
    # see-through tap areas over each card's picture open the page (on top of the scroll strip)
    Image.new("RGBA", (SHOT_W, SHOT_H), clear).save(os.path.join(out, prefix + "hitshot.png"))
    for r in range(ROWS):
        y = CARD_Y0 + r * (CARD_H + CARD_GAP)
        kids.append(button("open%d" % (r + 1), prefix + "hitshot.png", prefix + "hitshot.png", idx["Open %d" % (r + 1)], SHOT_X, y + 9, SHOT_W, SHOT_H))
        kids[-1]["bounds"]["version"] = 2
        kids[-1]["bounds"]["additionalInvalidatingHandles"] = ["IndexedEnabling/0/2/Parameter %d" % det]
    # the page's own background, with its headings drawn in
    top, bottom = CARD_Y0, CARD_Y0 + ROWS * (CARD_H + CARD_GAP) - CARD_GAP
    page = Image.new("RGBA", (1248, bottom - top), clear)
    pd = ImageDraw.Draw(page)
    pd.rounded_rectangle((0, 0, 1247, bottom - top - 1), radius=14, fill=rgb(c["card"]), outline=rgb(c["line"]), width=2)
    pd.rounded_rectangle((8, 60, 8 + 400, 60 + 200), radius=10, fill=rgb(c["panel"]))
    hf = font(14, black=True)
    pd.text((10, 276), "WHAT'S NEW", font=hf, fill=rgb(c["accent"] if name != "High Contrast" else c["text"]))
    pd.text((640, 276), "VERSIONS", font=hf, fill=rgb(c["accent"] if name != "High Contrast" else c["text"]))
    pd.line((10, 268, 1236, 268), fill=rgb(c["line"]), width=2)
    page.save(os.path.join(out, prefix + "detail_bg.png"))
    pill(130, 44, rgb(c["panel"]), rgb(c["line"]), "\u2190 BACK", rgb(c["text"]), 16, radius=10, black=True).save(os.path.join(out, prefix + "back.png"))
    only = ["IndexedEnabling/1/2/Parameter %d" % det]
    def D(n):
        return idx[n]
    X0, Y0 = 16, top
    dk = [entry("detail bg", "Image", {"version": 2, "imageType": "Regular", "colour": "0", "image": prefix + "detail_bg.png"},
                bounds(X0, Y0, 1248, bottom - top, only_when=only))]
    dk.append(button("back", prefix + "back.png", prefix + "back.png", D("Back"), X0 + 8, Y0 + 8, 130, 44))
    dk.append(film("dthumbA", "thumbsA.png", -1, D("D Thumb"), X0 + 8, Y0 + 60, 400, 200, only_when=only + ["IndexedEnabling/0/2/Parameter %d" % D("D Thumb Bank")]))
    dk.append(film("dthumbB", "thumbsB.png", -1, D("D Thumb"), X0 + 8, Y0 + 60, 400, 200, only_when=only + ["IndexedEnabling/1/2/Parameter %d" % D("D Thumb Bank")]))
    dk.append(label("dname", D("D Name"), X0 + 150, Y0 + 6, 560, 48, c["text"], 32, True))
    dk.append(film("dbadge", prefix + "badge.png", 5, D("D Badge"), X0 + 720, Y0 + 16, 110, 28))
    dk.append(label("dversion", D("D Version"), X0 + 840, Y0 + 10, 210, 40, c["text"], 24, True, "right verticallyCentred"))
    dk.append(label("dmaker", D("D Maker"), X0 + 424, Y0 + 62, 330, 28, c["accent"] if name != "High Contrast" else c["text"], 18, True))
    dk.append(label("dmeta", D("D Meta"), X0 + 760, Y0 + 62, 300, 28, c["muted"], 17, False))
    dk.append(film("dchip", prefix + "chip.png", 3, D("D Chip"), X0 + 424, Y0 + 94, 262, 28))
    dk.append(label("dtested", D("D Tested"), X0 + 430, Y0 + 94, 250, 28, c["text"], 15, True, "horizontallyCentred verticallyCentred"))
    dk.append(label("dstats", D("D Stats"), X0 + 700, Y0 + 94, 360, 28, c["muted"], 15, True))
    for i in range(6):
        dk.append(label("dabout%d" % i, D("D About %d" % (i + 1)), X0 + 424, Y0 + 128 + i * 22, 640, 22, c["text"], 16, False))
    for i in range(4):
        dk.append(label("dnew%d" % i, D("D New %d" % (i + 1)), X0 + 10, Y0 + 298 + i * 24, 600, 24, c["text"], 16, False))
    for i in range(4):
        dk.append(label("dhist%d" % i, D("D Hist %d" % (i + 1)), X0 + 640, Y0 + 298 + i * 24, 400, 24, c["muted"], 16, True))
    dk.append(film("dlook", prefix + "action.png", 7, D("D Look"), X0 + 1060, Y0 + 300, 180, 52))
    dk.append(button("daction", prefix + "hit180.png", prefix + "hit180.png", D("Detail Action"), X0 + 1060, Y0 + 300, 180, 52))
    dk = [k for k in dk if k]
    for k in dk:                          # everything on the page shows only while it is open
        b = k["bounds"]
        hs = b.get("additionalInvalidatingHandles", [])
        if only[0] not in hs:
            b["version"] = 2
            b["additionalInvalidatingHandles"] = hs + only
    kids += dk
    # needs line(s) go under the description, in the warning colour
    for i in range(2):
        kids.append(label("dneeds%d" % i, D("D Needs %d" % (i + 1)), X0 + 1060 - 0, Y0 + 360 + i * 22, 180, 22, c["warn"], 13, True))
        kids[-1]["bounds"]["version"] = 2
        kids[-1]["bounds"]["additionalInvalidatingHandles"] = list(only)
    return kids


def qlinks(idx):
    """Q-Link 1 scrolls the list, Q-Link 2 changes the tab (both screen and program mode)."""
    screen = {"Q-Link %d" % i: -1 for i in range(1, 17)}
    screen["Q-Link 13"] = idx["Scroll"]
    screen["Q-Link 14"] = idx["Show"]
    program = {"Q-Link %d" % i: -1 for i in range(1, 17)}
    program["Q-Link 1"] = idx["Scroll"]
    program["Q-Link 2"] = idx["Show"]
    return {"version": 4, "info": {"version": 1, "type": "CompleteDescription"},
            "Screen Mode Q-Links": {"version": 4, "map": [{"Tab": 1, "SubTab": 1, "Bank Direction": "Column", "Q-Links": screen}]},
            "Program Mode Q-Links": program}


def name_tile(name, maker):
    """A clean tile with the plugin's name, for plugins without a picture of their own."""
    S = 3
    W_, H_ = THUMB_W * S, THUMB_H * S
    im = Image.new("RGB", (W_, H_), (24, 21, 19))
    d = ImageDraw.Draw(im)
    for y in range(H_):
        c = int(24 + 22 * y / H_)
        d.line((0, y, W_, y), fill=(c + 10, c + 2, c - 4))
    d.rounded_rectangle((6, 6, W_ - 7, H_ - 7), radius=20, outline=(234, 134, 26), width=4)
    words, lines = name.split(), []
    size = 64
    while size > 22:
        f = ImageFont.truetype("C:/Windows/Fonts/seguibl.ttf", size)
        lines, cur = [], ""
        for w in words:
            test = (cur + " " + w).strip()
            if d.textlength(test, font=f) <= W_ - 60:
                cur = test
            else:
                if cur: lines.append(cur)
                cur = w
        if cur: lines.append(cur)
        if len(lines) <= 2 and all(d.textlength(l, font=f) <= W_ - 60 for l in lines):
            break
        size -= 4
    total = len(lines) * size * 1.15
    y = (H_ - total) / 2 - 18
    for l in lines:
        d.text(((W_ - d.textlength(l, font=f)) / 2, y), l, font=f, fill=(245, 245, 220))
        y += size * 1.15
    fm = ImageFont.truetype("C:/Windows/Fonts/seguisb.ttf", 24)
    by = "by " + maker
    d.text(((W_ - d.textlength(by, font=fm)) / 2, H_ - 58), by, font=fm, fill=(234, 134, 26))
    return im.resize((THUMB_W, THUMB_H), Image.LANCZOS)


def make_thumbs(want):
    """Downloads every plugin's screenshot once and builds the strip. Frame 0 is the blank 'no picture' frame."""
    cache = os.path.join(HERE, "build", "thumb_cache")
    os.makedirs(cache, exist_ok=True)
    frames, ids = [Image.new("RGBA", (THUMB_W, THUMB_H), (0, 0, 0, 0))], []
    if want:
        cat = json.load(urllib.request.urlopen(CATALOG, timeout=60))
        own = os.path.join(HERE, "docs", "manager_icon_large.png")
        for p in sorted(cat["plugins"], key=lambda p: p["id"]):
            url = p.get("screenshot") or ""
            # keyed by the picture's address too, so a plugin's new picture replaces the old one
            path = os.path.join(cache, "%s-%s-%dx%d.png" % (p["id"], hashlib.sha1(url.encode()).hexdigest()[:10], THUMB_W, THUMB_H))
            if p["id"] == "mpc-plugins-manager" and os.path.isfile(own):       # our own tile
                Image.open(own).convert("RGB").resize((THUMB_W, THUMB_H), Image.LANCZOS).save(path)
                url = "local"
            tile = (not url.startswith("https://") and url != "local") or (p.get("screenshot_shared") and url != "local")
            if tile:                                  # no picture of its own: a name tile, so no card looks like another
                tpath = os.path.join(cache, "tile-%s-%dx%d.png" % (p["id"], THUMB_W, THUMB_H))
                if not os.path.exists(tpath):
                    name_tile(p["name"], p.get("maker", "")).save(tpath)
                frames.append(Image.open(tpath).convert("RGBA"))
                ids.append(p["id"] + "\t0")
                continue
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
                ids.append(p["id"] + "\t1")                 # 1 = a real picture
            except Exception as err:
                print("  no thumbnail for", p["id"], err)
    return frames, ids


def main():
    params_json = os.path.join(HERE, "build", "params.json")
    if not os.path.isfile(params_json):
        sys.exit("build/params.json missing: run tools/build.py on this plugin first")
    plist = json.load(open(params_json, encoding="utf-8"))["params"]
    idx = {p["name"]: i for i, p in enumerate(plist)}
    if "Theme Pick" not in idx:
        sys.exit("the plugin has no 'Theme Pick' parameter")
    frames, ids = make_thumbs("--no-thumbs" not in sys.argv)
    print("thumbnails:", len(ids))
    # two strips (A = first half of the plugins, B = the rest), so frames can be big without a huge picture.
    # The plugin computes the same split from thumbs.txt: half = (count + 1) // 2.
    half = (len(ids) + 1) // 2
    blank = frames[0]
    strip_a = [blank] + frames[1:1 + half]
    strip_b = [blank] + frames[1 + half:]
    strip_b += [blank] * (len(strip_a) - len(strip_b))
    for old in (SKINS, THEMES_OUT):
        if os.path.isdir(old):
            shutil.rmtree(old)
    os.makedirs(SKINS)
    n = len(THEME_ORDER)
    kids = []
    for i, name in enumerate(THEME_ORDER):
        for k in build_theme(name, idx, SKINS, "t%d_" % i):
            if "thumb" in k["componentData"]["name"]:              # card and details-page screenshots
                k["componentData"]["data"]["numFrames"] = len(strip_a)
            b = k["bounds"]
            # shown only while "Theme Pick" is on this theme: switching is live, no restart
            k["bounds"] = {"version": 2, "acceptsHWFocus": b["acceptsHWFocus"], "showWhenDataModelInvalid": b["showWhenDataModelInvalid"],
                           "whenVisible": b["whenVisible"], "boundsType": b["boundsType"], "bounds": b["bounds"],
                           "additionalInvalidatingHandles": b.get("additionalInvalidatingHandles", [])
                                                            + ["IndexedEnabling/%d/%d/Parameter %d" % (i, n, idx["Theme Pick"])]}
            kids.append(k)
    first = THEMES[DEFAULT_THEME]
    tui = {"pageData": {"version": 1, "componentDefinitions": {"version": 2, "importFiles": [], "localComponentDefinitions": []},
                        "replacementStrings": [{"key": "Parameter %d" % idx["Theme Pick"], "value": THEME_ORDER}],
                        "info": {"version": 1, "type": "CompleteDescription"},
                        "tabs": [{"version": 1, "tabName": "PLUGINS", "fnKeyIndex": 0, "fnKeySubIndex": 0,
                                  "componentDefinition": {"version": 2, "actions": [],
                                                          "backgroundData": {"version": 1, "focussed": {"version": 1, "colour": "ff" + first["bg"], "image": ""},
                                                                             "unfocussed": {"version": 1, "colour": "ff" + first["bg"], "image": ""}},
                                                          "ignoreMousePresses": False, "disableCoarseDataWheel": False, "componentsData": kids},
                                  "qlinkBoundsData": []}]}}
    json.dump(tui, open(os.path.join(SKINS, "TUI.json"), "w", encoding="utf-8"), separators=(",", ":"))
    json.dump(qlinks(idx), open(os.path.join(SKINS, "Q-Links.json"), "w", encoding="utf-8"), indent=1)
    strip(strip_a).save(os.path.join(SKINS, "thumbsA.png"), optimize=True)
    strip(strip_b).save(os.path.join(SKINS, "thumbsB.png"), optimize=True)
    os.makedirs(os.path.join(HERE, "Content"), exist_ok=True)
    open(os.path.join(HERE, "Content", "thumbs.txt"), "w", encoding="utf-8", newline="\n").write("\n".join(ids) + "\n")
    open(os.path.join(HERE, "Content", "themes.txt"), "w", encoding="utf-8", newline="\n").write("\n".join(THEME_ORDER) + "\n")
    print("themes:", n, "| components:", len(kids), "| TUI.json", os.path.getsize(os.path.join(SKINS, "TUI.json")) // 1024, "KB",
          "| files:", len(os.listdir(SKINS)))


if __name__ == "__main__":
    main()
