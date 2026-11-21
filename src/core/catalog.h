// MPC Plugins Manager by The Disk King. Copyright (c) 2026 The Disk King. All rights reserved.
// Reads catalog.tsv, the plain plugin list published every night at mpcplugins.github.io.
#pragma once
#include <map>
#include <string>
#include <vector>

namespace tdk {

struct Build
{
    std::string url, sha256, folder, inside;   // folder: "Maker - VST - Name"; inside: path to it in the zip
    long long size = 0;
    bool copyOk = false;                       // the folder has its own plugin.json, so copying it works on 3.10.1
};

struct Entry
{
    std::string id, name, maker, kind, tier, version, testedOn, userFiles, summary, style, screenshot, updated;
    std::string notes, history, needs;          // newest release notes, 'ver|date|downloads;...', what it needs
    long long downloads = 0;
    int featured = 0;                          // 1, 2, 3...: hand-picked by The Disk King, shown first
    double rating = 0; int ratingCount = 0;    // users' star rating (from thediskking.com)
    Build sdk, gen1, gen2;

    bool tested() const { return tier == "verified"; }
    // One download for every MPC (made with the MPC Plugin SDK: 32-bit Gen1 and 64-bit Gen2 code in one folder).
    bool universal() const { return sdk.copyOk && !sdk.url.empty(); }
    bool beta() const { return tier == "experimental"; }

    // The download to use on this MPC: one that already has plugin.json first (works-on-both, then our
    // generation's); else our generation's build with a plugin folder, for which we write plugin.json ourselves.
    const Build* pick(bool gen2Device) const
    {
        const Build& g = gen2Device ? gen2 : gen1;
        if (sdk.copyOk && !sdk.url.empty()) return &sdk;
        if (g.copyOk && !g.url.empty()) return &g;
        if (!g.folder.empty() && !g.url.empty()) return &g;
        return nullptr;
    }

    // Every folder name this plugin can have, for matching what is already installed.
    std::vector<std::string> folders() const
    {
        std::vector<std::string> out;
        for (const Build* b : {&sdk, &gen1, &gen2})
            if (!b->folder.empty() && (out.empty() || out.back() != b->folder)) out.push_back(b->folder);
        return out;
    }
};

// Compares versions number by number: "1.10.0" is newer than "1.9.2", "2.0" equals "2.0.0". -1, 0 or 1.
// Text after a dash ("1.2.0-beta1") makes it older than the same plain number.
inline int compareVersions(const std::string& a, const std::string& b)
{
    size_t i = 0, j = 0;
    auto skipV = [](const std::string& s, size_t& k) { if (k < s.size() && (s[k] == 'v' || s[k] == 'V')) ++k; };
    skipV(a, i); skipV(b, j);
    while (i < a.size() || j < b.size())
    {
        if ((i < a.size() && a[i] == '-') || (j < b.size() && b[j] == '-'))
        {
            bool da = i < a.size() && a[i] == '-', db = j < b.size() && b[j] == '-';
            return da == db ? 0 : (da ? -1 : 1);
        }
        long long x = 0, y = 0;
        while (i < a.size() && a[i] >= '0' && a[i] <= '9') x = x * 10 + (a[i++] - '0');
        while (j < b.size() && b[j] >= '0' && b[j] <= '9') y = y * 10 + (b[j++] - '0');
        if (x != y) return x < y ? -1 : 1;
        if (i < a.size() && a[i] == '.') ++i;
        if (j < b.size() && b[j] == '.') ++j;
        if (i < a.size() && a[i] != '-' && !(a[i] >= '0' && a[i] <= '9')) ++i;   // ignore anything else
        if (j < b.size() && b[j] != '-' && !(b[j] >= '0' && b[j] <= '9')) ++j;
    }
    return 0;
}

inline std::vector<std::string> splitTabs(const std::string& line)
{
    std::vector<std::string> out(1);
    for (char c : line)
    {
        if (c == '\t') out.emplace_back();
        else if (c != '\r') out.back() += c;
    }
    return out;
}

// Parses the whole file. Returns false (and leaves `out` empty) if it is not a catalog we understand.
inline bool parseCatalog(const std::string& text, std::vector<Entry>& out, std::string& generated, std::string& error)
{
    out.clear();
    std::vector<std::string> lines;
    size_t start = 0;
    while (start < text.size())
    {
        size_t nl = text.find('\n', start);
        if (nl == std::string::npos) nl = text.size();
        lines.push_back(text.substr(start, nl - start));
        start = nl + 1;
    }
    if (lines.size() < 2) { error = "the plugin list is empty"; return false; }
    std::vector<std::string> head = splitTabs(lines[0]);
    if (head.size() < 2 || head[0] != "#mpcplugins-tsv") { error = "the plugin list is not in the expected format"; return false; }
    if (head[1] != "1") { error = "the plugin list is a newer format: please update MPC Plugins Manager"; return false; }
    generated = head.size() > 2 ? head[2] : "";
    std::vector<std::string> cols = splitTabs(lines[1]);
    std::map<std::string, size_t> at;
    for (size_t i = 0; i < cols.size(); ++i) at[cols[i]] = i;
    for (const char* need : {"id", "name", "tier", "version", "sdk_url", "gen1_url", "gen2_url"})
        if (!at.count(need)) { error = std::string("the plugin list is missing the column ") + need; return false; }

    for (size_t i = 2; i < lines.size(); ++i)
    {
        if (lines[i].empty()) continue;
        std::vector<std::string> c = splitTabs(lines[i]);
        auto get = [&](const char* k) -> std::string {
            auto it = at.find(k);
            return (it != at.end() && it->second < c.size()) ? c[it->second] : std::string();
        };
        auto num = [&](const char* k) -> long long {
            std::string s = get(k);
            long long v = 0;
            for (char ch : s) { if (ch < '0' || ch > '9') break; v = v * 10 + (ch - '0'); }
            return v;
        };
        Entry e;
        e.id = get("id"); e.name = get("name"); e.maker = get("maker"); e.kind = get("kind"); e.tier = get("tier");
        e.version = get("version"); e.testedOn = get("tested_on"); e.userFiles = get("user_files"); e.summary = get("summary");
        e.style = get("style"); e.updated = get("updated"); e.screenshot = get("screenshot");
        e.notes = get("notes"); e.history = get("history"); e.needs = get("needs");
        if (e.screenshot.rfind("https://", 0) != 0) e.screenshot.clear();
        e.downloads = num("downloads");
        e.featured = (int)num("featured");
        e.ratingCount = (int)num("rating_count");
        { std::string r = get("rating"); e.rating = r.empty() ? 0 : std::atof(r.c_str()); }
        auto build = [&](const std::string& k, Build& b) {
            b.url = get((k + "_url").c_str()); b.sha256 = get((k + "_sha256").c_str());
            b.folder = get((k + "_folder").c_str()); b.inside = get((k + "_inside").c_str());
            b.size = num((k + "_size").c_str()); b.copyOk = get((k + "_copy_ok").c_str()) == "1";
#if defined(TDK_TEST)
            if (b.url.rfind("https://", 0) != 0 && b.url.rfind("file://", 0) != 0) b.url.clear();   // tests use local files
#else
            if (b.url.rfind("https://", 0) != 0) b.url.clear();          // only ever download over HTTPS
#endif
            for (char& ch : b.sha256) if (ch >= 'A' && ch <= 'F') ch = (char)(ch - 'A' + 'a');
        };
        build("sdk", e.sdk); build("gen1", e.gen1); build("gen2", e.gen2);
        bool safeId = !e.id.empty();
        for (char ch : e.id) if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-')) safeId = false;
        if (safeId && !e.name.empty()) out.push_back(std::move(e));
    }
    if (out.empty()) { error = "the plugin list has no plugins"; return false; }
    return true;
}

} // namespace tdk
