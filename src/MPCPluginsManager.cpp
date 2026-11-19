// MPC Plugins Manager - by The Disk King. Copyright (c) 2026 The Disk King. All rights reserved.
//
// Install, update, remove and restore free plugins right on the MPC's own screen.
// Put it on a Plugin track. Tabs at the top pick a list (Tested & Working, Untested, Installed, Updates, Removed);
// each page shows 5 plugin cards with a screenshot and one button that does the right thing (INSTALL, UPDATE,
// REMOVE, RESTORE). Turn pages with the arrows or Q-Link 1. SORT, TYPE and THEME are tap-to-change pills.
// A phone or computer on the same Wi-Fi can also use the address and PIN shown at the bottom.
// The list comes from mpcplugins.github.io (the same list as thediskking.com/plugins).
//
// How it stays safe:
//   * Nothing starts while the MPC is only scanning plugins: the worker starts once the plugin runs on a track.
//   * Downloads come from each maker's GitHub over HTTPS and must match their SHA-256 before anything is copied.
//   * It only writes inside "<drive>/VST2 Plugins", its own "<drive>/MPC Plugins Manager" folder, and its own
//     "Plugin Skins" folder (for the theme).
//   * REMOVE moves a plugin to "MPC Plugins Manager/Removed Plugins" (RESTORE puts it back) and asks twice.
//   * Untested plugins ask twice before installing.
//
// The plugin makes no sound. process() only notes that audio is running.

#include "mpc_plugin.h"
#include "core/manager.h"
#include "core/web.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr int kCards = 5;
constexpr int kFirstPort = 8791;

// Parameter order is permanent once released (projects store it). The skin finds parameters by NAME.
enum ParamIndex
{
    kVolume = 0,          // harmless first parameter (the MPC nudges parameter 0 on every load)
    kShow,                // which list: 0 Tested, 1 Untested, 2 Installed, 3 Updates, 4 Removed
    kPageKnob,            // Q-Link 1: page number
    kPrev, kNext,         // buttons
    kSortTap, kTypeTap, kThemeTap, kRefresh, kUpdateAll,   // buttons
    kStatus, kOnline, kStorage, kUpdateCount, kSort, kType, kTheme, kPage, kSummary, kPhone, kStatus2, kPin,   // live text / pictures
    kCardBase             // then 10 per card: Thumb, Name, Maker, Meta, Badge, Version, Chip, Tested, Action Look, Action
};
enum CardField { cThumb, cName, cMaker, cMeta, cBadge, cVersion, cChip, cTested, cLook, cAction, kPerCard };
// The framework passes at most 31 characters of text to the MPC (kStr = 32), so every text here fits in 31.
constexpr size_t kMaxText = 31;
constexpr int kNumParams = kCardBase + kCards * kPerCard;
constexpr int card(int c, CardField f) { return kCardBase + c * kPerCard + f; }

// picture frames (must match make_skin.py)
enum ActionFrame { aInstall, aUpdate, aRemove, aRestore, aWorking, aNotHere, aBlank, kActionFrames };
enum BadgeFrame { bBlank, bStable, bBeta, bUntested, bInstalled, kBadgeFrames };
enum ChipFrame { chNone, chTested, chOther, kChipFrames };

const char* const kSorts[] = {"Most downloaded", "Newest", "A to Z"};
const char* const kTypes[] = {"All types", "Instruments", "Effects", "Addins"};

struct Item
{
    std::string id, folder, name, maker, meta, version, tested;
    int action = aBlank, badge = bBlank, chip = chNone, thumb = 0;
    bool testedHere = false, installed = false, update = false, canInstall = false;
    long long downloads = 0;
    std::string date;
};

} // namespace

class MPCPluginsManager : public mpc::Plugin
{
public:
    MPCPluginsManager() : mpc::Plugin(makeInfo())
    {
        addParam(mpc::floatParam("volume", "Volume", -60.0f, 0.0f, -60.0f, "dB", 0));
        addParam(mpc::choiceParam("show", "Show", {"Tested & Working", "Untested", "Installed", "Updates", "Removed"}, 0));
        addParam(mpc::intParam("pageknob", "Page Knob", 1, 200, 1));
        for (const char* b : {"Prev", "Next", "Sort Tap", "Type Tap", "Theme Tap", "Get List", "Update All"})
        {
            std::string id = b;
            id.erase(std::remove(id.begin(), id.end(), ' '), id.end());
            for (char& ch : id) ch = (char)std::tolower((unsigned char)ch);
            addParam(mpc::boolParam(id.c_str(), b, false));
        }
        for (const char* t : {"Status", "Online", "Storage", "Update Count", "Sort", "Type", "Theme", "Page", "Summary", "Phone", "Status 2", "PIN"})
            addText(t);
        for (int c = 1; c <= kCards; ++c)
            for (const char* f : {"Thumb", "Name", "Maker", "Meta", "Badge", "Version", "Chip", "Tested", "Action Look", "Action"})
            {
                std::string name = std::string(f) + " " + std::to_string(c);
                if (std::string(f) == "Action") { std::string id = "action" + std::to_string(c); addParam(mpc::boolParam(id.c_str(), name.c_str(), false)); }
                else addText(name.c_str());
            }
        text_.assign(kNumParams, std::string());
        text_[kStatus] = "Starting...";
        text_[card(0, cName)] = "MPC Plugins Manager";
        text_[card(0, cMaker)] = "by The Disk King";
        text_[card(0, cMeta)] = "The list appears in a moment";
    }

    ~MPCPluginsManager() override { shutdown(); }

    void prepare(double, int) override
    {
        if (!watcher_.joinable()) watcher_ = std::thread([this] { watch(); });   // waits for real use, see watch()
    }

    void process(mpc::ProcessContext& ctx) override
    {
        (void)ctx;
        playing_.store(true, std::memory_order_relaxed);
    }

    // May run on the audio thread: only note what happened; our own thread does the work.
    void parameterChanged(int index, float plainValue) override
    {
        bool on = plainValue >= 0.5f;
        if ((index >= kPrev && index <= kUpdateAll) || isAction(index)) { if (on) pressed_.fetch_or(bit(index)); return; }
        if (index == kShow || index == kPageKnob) dirty_ = true;
    }

    bool formatValue(int index, float plainValue, std::string& out) override
    {
        if (index == kShow) { static const char* v[] = {"Tested", "Untested", "Installed", "Updates", "Removed"}; int i = (int)plainValue; out = v[i < 0 ? 0 : i > 4 ? 4 : i]; return true; }
        if (index < kStatus || isAction(index)) return false;
        std::lock_guard<std::mutex> g(textMu_);
        out = text_[(size_t)index];
        return true;
    }

private:
    static mpc::PluginInfo makeInfo()
    {
        mpc::PluginInfo info;
        info.name         = "MPC Plugins Manager";
        info.vendor       = "The Disk King";
        info.uniqueId     = MPC_FOURCC('T', 'D', 'K', 'M');   // must match plugin.json "uid": "TDKM"
        info.version      = 1000;
        info.isInstrument = true;
        info.numInputs    = 0;
        info.numOutputs   = 2;
        return info;
    }

    void addText(const char* name)
    {
        std::string id = name;
        id.erase(std::remove(id.begin(), id.end(), ' '), id.end());
        for (char& ch : id) ch = (char)std::tolower((unsigned char)ch);
        addParam(mpc::floatParam(id.c_str(), name, 0.0f, 1.0f, 0.0f, "", 0));
    }
    static bool isAction(int i) { return i >= kCardBase && (i - kCardBase) % kPerCard == cAction; }
    // one press flag per button: the footer/header buttons by their number, card buttons as 40 + card
    static unsigned long long bit(int i) { return isAction(i) ? 1ull << (40 + (i - kCardBase) / kPerCard) : 1ull << i; }
    static double clock() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

    // ---------------------------------------------------------------- our own thread
    void watch()
    {
        int ticks = 0;                                   // a plugin scan never runs audio this long
        while (!stopping_)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            if (playing_.load() && ++ticks >= 8) break;
        }
        if (stopping_) return;
#if defined(_WIN32) || defined(__APPLE__)
        setText(kStatus, "Runs on the MPC only");
        return;
#else
        folder_ = mpc::pluginFolder();
        loadThumbs();
        loadThemes();
        manager_ = std::make_unique<tdk::Manager>();
        manager_->start(folder_);
        pin_ = tdk::randomPin();
        web_ = std::make_unique<tdk::WebServer>(*manager_, pin_);
        for (int p = kFirstPort; p < kFirstPort + 5 && !port_; ++p)
            if (web_->start(p)) port_ = p;
        pressed_ = 0;                                    // ignore anything from before we started
        unsigned seen = ~0u;
        double lastSlow = 0;
        while (!stopping_)
        {
            unsigned long long presses = pressed_.exchange(0);
            if (presses) handlePresses(presses);
            unsigned now = manager_->changeCount();
            if (presses || dirty_.exchange(false) || now != seen || clock() - lastSlow > 5.0)
            {
                seen = now;
                lastSlow = clock();
                rebuild();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(120));
        }
#endif
    }

    void loadThumbs()
    {
        std::string text = tdk::readText(tdk::fs::path(mpc::contentPath("thumbs.txt")));
        size_t s = 0;
        int frame = 1;
        while (s < text.size())
        {
            size_t e = text.find('\n', s);
            if (e == std::string::npos) e = text.size();
            std::string id = text.substr(s, e - s);
            if (!id.empty()) thumbIndex_[id] = frame++;
            s = e + 1;
        }
        thumbFrames_ = frame;                            // frame 0 = no picture
    }

    void loadThemes()
    {
        std::string text = tdk::readText(tdk::fs::path(mpc::contentPath("themes.txt")));
        size_t s = 0;
        while (s < text.size())
        {
            size_t e = text.find('\n', s);
            if (e == std::string::npos) e = text.size();
            if (e > s) themes_.push_back(text.substr(s, e - s));
            s = e + 1;
        }
        std::string current = tdk::readText(tdk::fs::path(folder_) / "Plugin Skins" / ".theme");
        themeNow_ = current.empty() ? (themes_.empty() ? "Disk King" : themes_[0]) : current;
        themeWanted_ = themeNow_;
    }

    // Copies the chosen theme's screen files over our own "Plugin Skins" (the screenshot strip stays).
    bool applyTheme(const std::string& name)
    {
        namespace fs = tdk::fs;
        std::error_code ec;
        fs::path from = fs::path(mpc::contentPath("Themes")) / name, to = fs::path(folder_) / "Plugin Skins";
        if (!fs::is_directory(from, ec) || !fs::is_directory(to, ec)) return false;
        for (auto& e : fs::directory_iterator(from, ec))
        {
            if (!e.is_regular_file(ec)) continue;
            fs::path tmp = to / (e.path().filename().string() + ".new");
            fs::copy_file(e.path(), tmp, fs::copy_options::overwrite_existing, ec);
            if (ec) return false;
            fs::rename(tmp, to / e.path().filename(), ec);
            if (ec) return false;
        }
        return true;
    }

    // Puts a button back to "off" after a press, so every tap is one press (and projects never store a pressed button).
    void release(int index) { setNormalised(index, 0.0f); notifyHostParameterChanged(index); }

    void setPage(int page)
    {
        set(kPageKnob, (float)page);
        notifyHostParameterChanged(kPageKnob);
    }

    void handlePresses(unsigned long long presses)
    {
        auto has = [&](int i) { return (presses & bit(i)) != 0; };
        for (int i = kPrev; i <= kUpdateAll; ++i) if (has(i)) release(i);
        for (int c = 0; c < kCards; ++c) if (has(card(c, cAction))) release(card(c, cAction));

        int pages;
        { std::lock_guard<std::mutex> g(listMu_); pages = std::max(1, ((int)list_.size() + kCards - 1) / kCards); }
        int page = std::max(1, std::min(getInt(kPageKnob), pages));
        if (has(kPrev)) setPage(page > 1 ? page - 1 : pages);
        if (has(kNext)) setPage(page < pages ? page + 1 : 1);
        if (has(kSortTap)) { sort_ = (sort_ + 1) % 3; setPage(1); }
        if (has(kTypeTap)) { type_ = (type_ + 1) % 4; setPage(1); }
        if (has(kRefresh)) { manager_->request("refresh", ""); note("Getting the newest plugin list"); }
        if (has(kThemeTap) && !themes_.empty())
        {
            auto it = std::find(themes_.begin(), themes_.end(), themeWanted_);
            themeWanted_ = (it == themes_.end() || it + 1 == themes_.end()) ? themes_[0] : *(it + 1);
            themeAt_ = clock();                          // applied 2 s after the last tap, so tapping through is quick
        }
        if (has(kUpdateAll))
        {
            std::vector<std::string> ids;
            { std::lock_guard<std::mutex> g(listMu_); for (auto& it : all_) if (it.update) ids.push_back(it.id); }
            if (ids.empty()) note("Everything is up to date");
            else if (!confirmed("updateall")) note("Tap UPDATE ALL again to update " + std::to_string(ids.size()));
            else { for (auto& id : ids) manager_->request("install", id); note("Updating " + std::to_string(ids.size()) + " plugins"); }
        }
        for (int c = 0; c < kCards; ++c)
        {
            if (!has(card(c, cAction))) continue;
            Item it;
            {
                std::lock_guard<std::mutex> g(listMu_);
                size_t i = (size_t)((page - 1) * kCards + c);
                if (i >= list_.size()) continue;
                it = list_[i];
            }
            switch (it.action)
            {
                case aInstall:
                    if (!it.testedHere && !confirmed("install:" + it.id)) { note("Not tested on your MPC. Tap INSTALL again to go ahead"); break; }
                    note(manager_->request("install", it.id) == "OK" ? "Installing " + it.name : "Already waiting");
                    break;
                case aUpdate:
                    note(manager_->request("install", it.id) == "OK" ? "Updating " + it.name : "Already waiting");
                    break;
                case aRemove:
                    if (!confirmed("remove:" + it.folder)) { note("Tap REMOVE again to remove " + it.name); break; }
                    manager_->request("remove", it.folder);
                    break;
                case aRestore: manager_->request("restore", it.folder); break;
                case aNotHere: note("Use the maker's installer for " + it.name); break;
                default: break;
            }
        }
        dirty_ = true;
    }

    bool confirmed(const std::string& key)               // a second press within 6 seconds
    {
        double t = clock();
        bool ok = confirmKey_ == key && t - confirmAt_ < 6.0;
        confirmKey_ = ok ? "" : key;
        confirmAt_ = t;
        return ok;
    }

    void note(const std::string& s) { notice_ = s; noticeAt_ = clock(); dirty_ = true; }

    // Builds every card and text from the manager's state.
    void rebuild()
    {
        if (themeWanted_ != themeNow_ && themeAt_ > 0 && clock() - themeAt_ > 2.0)
        {
            themeAt_ = 0;
            if (applyTheme(themeWanted_)) { themeNow_ = themeWanted_; themeChanged_ = true; note("Theme " + themeNow_ + ": restart the MPC to see it"); }
            else { themeWanted_ = themeNow_; note("Could not change the theme"); }
        }
        tdk::Snapshot s = manager_->snapshot();
        const bool gen2 = manager_->gen2;
        const int view = std::max(0, std::min(getInt(kShow), 4));
        std::vector<Item> all, shown;
        std::set<std::string> matched;
        int updates = 0, testedHere = 0;
        for (const tdk::Entry& e : s.plugins)
        {
            Item it;
            it.id = e.id; it.name = e.name; it.downloads = e.downloads; it.date = e.updated;
            it.testedHere = e.tested() && (e.testedOn.empty() || e.testedOn.find(gen2 ? "gen2" : "gen1") != std::string::npos);
            std::string have;
            for (const std::string& f : e.folders())
            {
                auto x = s.installed.find(f);
                if (x != s.installed.end()) { it.folder = f; it.installed = true; have = x->second; matched.insert(f); break; }
            }
            it.canInstall = e.pick(gen2) != nullptr;
            it.update = it.installed && it.canInstall && have != e.version;
            it.action = it.update ? aUpdate : it.installed ? aRemove : it.canInstall ? aInstall : aNotHere;
            if (!s.current.empty() && (s.current == e.id || s.current == it.folder)) it.action = aWorking;
            it.badge = it.installed ? bInstalled : e.beta() ? bBeta : e.tested() ? bStable : bUntested;
            it.chip = it.testedHere ? chTested : e.tested() ? chOther : chNone;
            it.tested = it.testedHere ? "Tested on your MPC (" + std::string(gen2 ? "Gen2" : "Gen1") + ")"
                      : e.tested() ? "Tested on " + std::string(gen2 ? "Gen1" : "Gen2") + " only" : "";
            it.version = it.update ? "v" + have + " > v" + e.version : "v" + e.version;
            std::string kind = e.kind.empty() ? "plugin" : e.kind;
            it.maker = "by " + e.maker;
            it.meta = kind + (e.style.empty() || e.style == "other" ? "" : " | " + e.style) + (e.userFiles.empty() ? "" : " | own files");
            auto th = thumbIndex_.find(e.id);
            it.thumb = th == thumbIndex_.end() ? 0 : th->second;
            if (it.update) ++updates;
            if (it.testedHere) ++testedHere;
            bool typeOk = type_ == 0 || (type_ == 1 && e.kind == "instrument") || (type_ == 2 && e.kind == "effect") || (type_ == 3 && e.kind == "addin");
            all.push_back(it);
            if (!typeOk) continue;
            if ((view == 0 && it.testedHere) || (view == 1 && !it.testedHere) || (view == 2 && it.installed) || (view == 3 && it.update))
                shown.push_back(it);
        }
        if (view == 2)                                       // installed folders that are not on the list
            for (auto& kv : s.installed)
                if (!matched.count(kv.first))
                {
                    Item it; it.folder = kv.first; it.installed = true; it.name = kv.first.substr(kv.first.find(" - VST - ") + 9);
                    it.maker = "Not on our list"; it.meta = "installed by hand"; it.version = kv.second.empty() ? "" : "v" + kv.second;
                    it.action = s.current == kv.first ? aWorking : aRemove; it.badge = bInstalled;
                    shown.push_back(it);
                }
        if (view == 4)
            for (const std::string& r : s.removed)
            {
                Item it; it.folder = r; it.name = r.substr(r.find(" - VST - ") == std::string::npos ? 0 : r.find(" - VST - ") + 9);
                it.maker = "In Removed Plugins"; it.meta = "RESTORE puts it back"; it.action = s.current == r ? aWorking : aRestore;
                shown.push_back(it);
            }
        if (view != 4)
            std::stable_sort(shown.begin(), shown.end(), [this](const Item& a, const Item& b) {
                if (sort_ == 1) return a.date > b.date;
                if (sort_ == 2) return a.name < b.name;
                return a.downloads > b.downloads;
            });
        if (view != lastView_) { lastView_ = view; setPage(1); }
        int pages = std::max(1, ((int)shown.size() + kCards - 1) / kCards);
        int page = std::max(1, std::min(getInt(kPageKnob), pages));
        if (page != getInt(kPageKnob)) setPage(page);

        // ---- the texts and pictures
        std::vector<std::pair<int, std::string>> texts;
        std::vector<std::pair<int, float>> frames;
        auto frame = [&](int param, int f, int count) { frames.emplace_back(param, (f + 0.5f) / (float)count); };
        for (int c = 0; c < kCards; ++c)
        {
            size_t i = (size_t)((page - 1) * kCards + c);
            if (i < shown.size())
            {
                const Item& it = shown[i];
                texts.emplace_back(card(c, cName), it.name);
                texts.emplace_back(card(c, cMaker), it.maker);
                texts.emplace_back(card(c, cMeta), it.meta);
                texts.emplace_back(card(c, cVersion), it.version);
                texts.emplace_back(card(c, cTested), it.tested);
                frame(card(c, cThumb), it.thumb, thumbFrames_);
                frame(card(c, cBadge), it.badge, kBadgeFrames);
                frame(card(c, cChip), it.chip, kChipFrames);
                frame(card(c, cLook), it.action, kActionFrames);
            }
            else
            {
                std::string empty = (c == 0 && shown.empty()) ? (view == 3 ? "Everything is up to date" : s.plugins.empty() ? "Getting the plugin list..." : "Nothing here") : "";
                for (CardField f : {cName, cMaker, cMeta, cVersion, cTested}) texts.emplace_back(card(c, f), f == cName ? empty : "");
                frame(card(c, cThumb), 0, thumbFrames_);
                frame(card(c, cBadge), bBlank, kBadgeFrames);
                frame(card(c, cChip), chNone, kChipFrames);
                frame(card(c, cLook), aBlank, kActionFrames);
            }
        }
        std::string status = manager_->screenStatus();
        if (!notice_.empty() && clock() - noticeAt_ < 6.0) status = notice_;
        else if ((s.restartNeeded || themeChanged_) && !s.busy) status = status.substr(0, kMaxText) + " Restart the MPC to load them";
        std::string drive = s.drive;
        for (auto& d : s.drives) if (d.root == s.drive) drive = d.label;
        std::error_code ec;
        auto sp = tdk::fs::space(s.drive, ec);
        char free[64] = "";
        if (!ec) std::snprintf(free, sizeof free, " | %.1f GB free", (double)sp.available / 1e9);
        if (drive == "Internal drive") drive = "Internal";
        std::string ip = tdk::lanAddress();
        std::string line1, line2;
        splitLine(status, line1, line2);
        texts.emplace_back(kStatus, line1);
        texts.emplace_back(kStatus2, line2);
        texts.emplace_back(kStorage, (s.online ? "" : "OFFLINE | ") + drive + free);
        texts.emplace_back(kUpdateCount, updates ? std::to_string(updates) : "");
        texts.emplace_back(kSort, std::string("Sort: ") + kSorts[sort_]);
        texts.emplace_back(kType, kTypes[type_]);
        texts.emplace_back(kTheme, "Theme: " + themeWanted_);
        texts.emplace_back(kPage, std::to_string(page) + " / " + std::to_string(pages));
        texts.emplace_back(kSummary, std::to_string(shown.size()) + " plugins | " + std::to_string(updates) + (updates == 1 ? " update" : " updates"));
        texts.emplace_back(kPhone, !port_ ? "" : ip.empty() ? "Phone: connect to Wi-Fi" : "Phone: http://" + ip + ":" + std::to_string(port_));
        texts.emplace_back(kPin, port_ && !ip.empty() ? "PIN " + pin_ : "");
        frame(kOnline, s.online ? 1 : 0, 2);
        {
            std::lock_guard<std::mutex> g(listMu_);
            list_ = std::move(shown);
            all_ = std::move(all);
        }
        publish(texts, frames);
    }

    // Updates only what changed. A changed value makes the MPC re-read that text or picture.
    void publish(const std::vector<std::pair<int, std::string>>& texts, const std::vector<std::pair<int, float>>& frames)
    {
        std::vector<int> changed;
        {
            std::lock_guard<std::mutex> g(textMu_);
            for (auto& t : texts)
                if (text_[(size_t)t.first] != t.second) { text_[(size_t)t.first] = t.second; changed.push_back(t.first); }
        }
        for (int i : changed) { setNormalised(i, getNormalised(i) > 0.5f ? 0.1f : 0.9f); notifyHostParameterChanged(i); }
        for (auto& f : frames)
            if (std::abs(getNormalised(f.first) - f.second) > 1e-4f) { setNormalised(f.first, f.second); notifyHostParameterChanged(f.first); changed.push_back(f.first); }
        if (!changed.empty() && clock() - lastFullUpdate_ > 3.0) { requestDisplayUpdate(); lastFullUpdate_ = clock(); }
    }

    // Splits a message into two lines of at most 31 characters, at a space where possible.
    static void splitLine(const std::string& s, std::string& a, std::string& b)
    {
        if (s.size() <= kMaxText) { a = s; b.clear(); return; }
        size_t cut = s.rfind(' ', kMaxText);
        if (cut == std::string::npos || cut < 12) cut = kMaxText;
        a = s.substr(0, cut);
        b = s.substr(cut + (s[cut] == ' ' ? 1 : 0));
        if (b.size() > kMaxText) b = b.substr(0, kMaxText - 3) + "...";
    }

    void setText(int i, const std::string& s) { std::lock_guard<std::mutex> g(textMu_); text_[(size_t)i] = s; }

    void shutdown()
    {
        stopping_ = true;
        if (watcher_.joinable()) watcher_.join();
        if (web_) web_->stop();
        if (manager_) manager_->stop();
        web_.reset();
        manager_.reset();
    }

    std::atomic<bool> playing_{false}, stopping_{false}, dirty_{false};
    std::atomic<unsigned long long> pressed_{0};
    std::thread watcher_;
    std::unique_ptr<tdk::Manager> manager_;
    std::unique_ptr<tdk::WebServer> web_;
    std::string folder_, pin_, confirmKey_, notice_, themeNow_, themeWanted_;
    std::vector<std::string> themes_;
    std::map<std::string, int> thumbIndex_;
    int port_ = 0, lastView_ = -1, sort_ = 0, type_ = 0, thumbFrames_ = 1;
    bool themeChanged_ = false;
    double confirmAt_ = 0, noticeAt_ = 0, lastFullUpdate_ = 0, themeAt_ = 0;
    std::mutex textMu_, listMu_;
    std::vector<std::string> text_;
    std::vector<Item> list_, all_;
};

MPC_PLUGIN_ENTRY(MPCPluginsManager)
