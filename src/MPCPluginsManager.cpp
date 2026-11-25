// MPC Plugins Manager - by The Disk King. Copyright (c) 2026 The Disk King. All rights reserved.
//
// Install, update, remove and restore free plugins right on the MPC's own screen.
// Put it on a Plugin track. Tabs at the top pick a list (Tested & Working, Untested, Installed, Updates, Removed);
// each page shows 5 plugin cards with a screenshot and one button that does the right thing (INSTALL, UPDATE,
// REMOVE, RESTORE). Turn pages with the arrows or Q-Link 1. SORT, TYPE and THEME are tap-to-change pills.
// A phone or computer on the same Wi-Fi can also use the address and PIN shown at the bottom.
// Follow a maker (FOLLOW on a plugin's details page): their new plugins and new versions come first in the list,
// marked NEW, until you open or install them. TYPE > Followed shows everything from the makers you follow.
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

constexpr int kCards = 3;            // big cards; roll a finger over them to scroll
constexpr int kPageSize = 20;        // a finger scroll moves within one page of 20; the arrows go to the next 20
constexpr int kFirstPort = 8791;

// Parameter order is permanent once released (projects store it). The skin finds parameters by NAME.
enum ParamIndex
{
    kVolume = 0,          // harmless first parameter (the MPC nudges parameter 0 on every load)
    kShow,                // which list: 0 Tested, 1 Untested, 2 Installed, 3 Updates, 4 Removed
    kScroll,              // list position 0..1: the finger-drag strip over the cards, and Q-Link 1
    kPrev, kNext,         // buttons
    kSortTap, kTypeTap, kThemeTap, kRefresh, kUpdateAll,   // buttons
    kStatus, kOnline, kStorage, kUpdateCount, kSort, kType, kTheme, kPage, kSummary, kPhone, kStatus2, kPin,   // live text / pictures
    kCardBase             // then the card fields below, for each card
};
enum CardField { cThumb, cThumbBank, cName, cMaker, cMeta, cAboutA, cAboutB, cStats, cBadge, cVersion, cChip, cTested, cLook, cAction, kPerCard };
// The framework passes at most 31 characters of text to the MPC (kStr = 32), so every text here fits in 31.
constexpr size_t kMaxText = 31;
constexpr int kThemePick = kCardBase + kCards * kPerCard;   // added in 1.1: which theme is showing (live)
// added for the details page (tap a card's picture)
constexpr int kDetailOpen = kThemePick + 1;     // 0 = the list, 1 = a plugin's details page
constexpr int kBack = kDetailOpen + 1, kDetailAction = kBack + 1, kOpen1 = kDetailAction + 1;   // buttons (Open 1..3)
constexpr int kDText = kOpen1 + 3;               // the details page's texts, see kDTextNames
const char* const kDTextNames[] = {"D Name", "D Maker", "D Meta", "D Version", "D Tested", "D Stats",
    "D About 1", "D About 2", "D About 3", "D About 4", "D About 5", "D About 6", "D Needs 1", "D Needs 2",
    "D New 1", "D New 2", "D New 3", "D New 4", "D Hist 1", "D Hist 2", "D Hist 3", "D Hist 4"};
enum DText { dName, dMaker, dMeta, dVersion, dTested, dStats, dAbout1, dNeeds1 = dAbout1 + 6, dNew1 = dNeeds1 + 2, dHist1 = dNew1 + 4, kDTexts = dHist1 + 4 };
constexpr int kDPic = kDText + kDTexts;          // the details page's pictures: Thumb, Thumb Bank, Badge, Chip, Look
enum DPic { pThumb, pThumbBank, pBadge, pChip, pLook, kDPics };
// added in 1.3: follow a maker
constexpr int kFavTap = kDPic + kDPics;          // button on the details page: follow / unfollow its maker
constexpr int kFavLook = kFavTap + 1;            // picture: 0 FOLLOW, 1 FOLLOWING
constexpr int kNumParams = kFavLook + 1;
// must match THEME_ORDER in make_skin.py
const char* const kThemes[] = {"Disk King", "Light", "Dark", "Midnight", "MPC Red", "Studio Green", "Ocean", "Purple Haze", "Sunset", "High Contrast"};
constexpr int kNumThemes = 10;
constexpr int card(int c, CardField f) { return kCardBase + c * kPerCard + f; }

// picture frames (must match make_skin.py)
enum ActionFrame { aInstall, aUpdate, aRemove, aRestore, aWorking, aNotHere, aBlank, kActionFrames };
enum BadgeFrame { bBlank, bStable, bBeta, bUntested, bInstalled, bNew, kBadgeFrames };
enum ChipFrame { chNone, chTested, chOther, kChipFrames };

const char* const kSorts[] = {"Most downloaded", "Newest", "A to Z"};
const char* const kTypes[] = {"All types", "Instruments", "Effects", "Addins", "Followed"};
constexpr int kNumTypes = 5;

struct Item
{
    std::string id, folder, name, maker, makerName, meta, version, latest, tested, about, stats, notes, history, needs;
    int action = aBlank, badge = bBlank, chip = chNone, thumb = 0;
    bool testedHere = false, builtBoth = false, installed = false, update = false, canInstall = false, fresh = false;
    long long downloads = 0;
    int featured = 0;
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
        addParam(mpc::floatParam("scroll", "Scroll", 0.0f, 1.0f, 0.0f, "", 2));
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
            for (const char* f : {"Thumb", "Thumb Bank", "Name", "Maker", "Meta", "About A", "About B", "Stats", "Badge", "Version", "Chip", "Tested", "Action Look", "Action"})
            {
                std::string name = std::string(f) + " " + std::to_string(c);
                if (std::string(f) == "Action") { std::string id = "action" + std::to_string(c); addParam(mpc::boolParam(id.c_str(), name.c_str(), false)); }
                else addText(name.c_str());
            }
        addParam(mpc::choiceParam("themepick", "Theme Pick", std::vector<std::string>(kThemes, kThemes + kNumThemes), 0));
        addParam(mpc::boolParam("detailopen", "Detail Open", false));
        addParam(mpc::boolParam("back", "Back", false));
        addParam(mpc::boolParam("detailaction", "Detail Action", false));
        for (int c = 1; c <= kCards; ++c) { std::string id = "open" + std::to_string(c), n = "Open " + std::to_string(c); addParam(mpc::boolParam(id.c_str(), n.c_str(), false)); }
        for (const char* d : kDTextNames) addText(d);
        for (const char* d : {"D Thumb", "D Thumb Bank", "D Badge", "D Chip", "D Look"}) addText(d);
        addParam(mpc::boolParam("favtap", "Fav Tap", false));
        addText("Fav Look");
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
        if ((index >= kPrev && index <= kUpdateAll) || isAction(index) || index == kBack || index == kDetailAction || index == kFavTap || (index >= kOpen1 && index < kOpen1 + kCards))
        { if (on) pressed_.fetch_or(bit(index)); return; }
        if (index == kShow || index == kScroll || index == kThemePick) dirty_ = true;
    }

    bool formatValue(int index, float plainValue, std::string& out) override
    {
        if (index == kShow) { static const char* v[] = {"Tested", "Untested", "Installed", "Updates", "Removed"}; int i = (int)plainValue; out = v[i < 0 ? 0 : i > 4 ? 4 : i]; return true; }
        if (index == kThemePick) { int i = (int)plainValue; out = kThemes[i < 0 ? 0 : i >= kNumThemes ? kNumThemes - 1 : i]; return true; }
        if (index < kStatus || isAction(index) || (index >= kThemePick && index < kDText) || index >= kFavTap) return false;
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
        info.version      = 1400;
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
    static unsigned long long bit(int i)
    {
        if (isAction(i)) return 1ull << (40 + (i - kCardBase) / kPerCard);
        if (i == kBack) return 1ull << 50;
        if (i == kDetailAction) return 1ull << 51;
        if (i == kFavTap) return 1ull << 56;
        if (i >= kOpen1 && i < kOpen1 + kCards) return 1ull << (52 + i - kOpen1);
        return 1ull << i;                               // header and footer buttons (all below 40)
    }
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
        loadTheme();
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
            std::string id = text.substr(s, e - s);       // "id<TAB>1" = a real picture, "id<TAB>0" = a name tile
            size_t tab = id.find('\t');
            bool real = tab == std::string::npos || id.substr(tab + 1) == "1";
            if (tab != std::string::npos) id = id.substr(0, tab);
            if (!id.empty()) { thumbIndex_[id] = frame++; if (real) realPicture_.insert(id); }
            s = e + 1;
        }
        thumbCount_ = frame - 1;                         // screenshots are in two strips: A = first half, B = the rest
        thumbHalf_ = (thumbCount_ + 1) / 2;              // (same split as make_skin.py)
    }

    // The theme is remembered on the drive, so every project opens with the theme you picked last.
    tdk::fs::path themeFile() const
    {
        tdk::fs::path vst2 = tdk::fs::path(folder_).parent_path();
        return vst2.parent_path() / "MPC Plugins Manager" / "theme.txt";
    }

    void loadTheme()
    {
        std::string saved = tdk::readText(themeFile());
        while (!saved.empty() && (saved.back() == '\n' || saved.back() == '\r')) saved.pop_back();
        for (int i = 0; i < kNumThemes; ++i)
            if (saved == kThemes[i]) { set(kThemePick, (float)i); notifyHostParameterChanged(kThemePick); }
    }

    void saveTheme(int i)
    {
        std::error_code ec;
        tdk::fs::create_directories(themeFile().parent_path(), ec);
        std::ofstream(themeFile()) << kThemes[i] << "\n";
    }

    // (followed makers live in tdk::Manager, shared with the phone page)
    void markSeen(const std::string& id, const std::string& version) { manager_->markSeen(id, version); }

    // Puts a button back to "off" after a press, so every tap is one press (and projects never store a pressed button).
    void release(int index) { setNormalised(index, 0.0f); notifyHostParameterChanged(index); }

    // The list is in pages of 20. The scroll position (0..1) moves within the current page only, so a finger
    // drag moves at most 17 cards and never jumps across the whole list.
    int pageCount(int size) const { return std::max(1, (size + kPageSize - 1) / kPageSize); }
    int offsetFor(int size) const
    {
        int page = std::max(0, std::min(page_, pageCount(size) - 1));
        int start = page * kPageSize;
        int len = std::min(kPageSize, std::max(0, size - start));
        int maxIn = std::max(0, len - kCards);
        return start + (int)std::lround(get(kScroll) * maxIn);
    }
    void setScroll(float v) { set(kScroll, v); notifyHostParameterChanged(kScroll); }
    void goPage(int page, int size)
    {
        int n = pageCount(size);
        page_ = (page % n + n) % n;
        setScroll(0.0f);
    }

    void handlePresses(unsigned long long presses)
    {
        auto has = [&](int i) { return (presses & bit(i)) != 0; };
        for (int i = kPrev; i <= kUpdateAll; ++i) if (has(i)) release(i);
        for (int c = 0; c < kCards; ++c) if (has(card(c, cAction))) release(card(c, cAction));
        for (int i : {kBack, kDetailAction, kFavTap}) if (has(i)) release(i);
        for (int c = 0; c < kCards; ++c) if (has(kOpen1 + c)) release(kOpen1 + c);
        if (has(kBack)) { set(kDetailOpen, 0.0f); notifyHostParameterChanged(kDetailOpen); }

        int size;
        { std::lock_guard<std::mutex> g(listMu_); size = (int)list_.size(); }
        int off = lastOff_;                              // the first card on screen, as last shown
        if (has(kPrev)) goPage(page_ - 1, size);
        if (has(kNext)) goPage(page_ + 1, size);
        if (has(kSortTap)) { sort_ = (sort_ + 1) % 3; goPage(0, size); }
        if (has(kTypeTap)) { type_ = (type_ + 1) % kNumTypes; goPage(0, size); }
        if (has(kRefresh)) { manager_->request("refresh", ""); note("Getting the newest plugin list"); }
        if (has(kThemeTap))                              // live: the screen shows the next theme at once
        {
            int next = (getInt(kThemePick) + 1) % kNumThemes;
            set(kThemePick, (float)next);
            notifyHostParameterChanged(kThemePick);
            saveTheme(next);
        }
        if (has(kUpdateAll))
        {
            std::vector<std::string> ids;
            { std::lock_guard<std::mutex> g(listMu_); for (auto& it : all_) if (it.update) ids.push_back(it.id); }
            if (ids.empty()) note("Everything is up to date");
            else if (!confirmed("updateall")) note("Tap UPDATE ALL again to update " + std::to_string(ids.size()));
            else { for (auto& id : ids) manager_->request("install", id); note("Updating " + std::to_string(ids.size()) + " plugins"); }
        }
        for (int c = 0; c < kCards; ++c)                // tap a card's picture: open its details page
        {
            if (!has(kOpen1 + c)) continue;
            std::lock_guard<std::mutex> g(listMu_);
            size_t i = (size_t)(off + c);
            if (i >= list_.size()) continue;
            detailKey_ = list_[i].id.empty() ? list_[i].folder : list_[i].id;
            if (list_[i].fresh) markSeen(list_[i].id, list_[i].latest);   // opened: no longer NEW
            set(kDetailOpen, 1.0f);
            notifyHostParameterChanged(kDetailOpen);
        }
        if (has(kFavTap) && getBool(kDetailOpen))          // follow / unfollow the maker on the details page
        {
            Item it;
            bool found = false;
            { std::lock_guard<std::mutex> g(listMu_); found = findItem(detailKey_, it); }
            if (found && !it.makerName.empty())
            {
                bool now = manager_->follows().makers.count(it.makerName) == 0;
                manager_->setFollow(it.makerName, now);
                note(now ? "Following " + it.makerName + ": new ones come first" : "Stopped following " + it.makerName);
            }
        }
        if (has(kDetailAction) && getBool(kDetailOpen))
        {
            Item it;
            bool found = false;
            { std::lock_guard<std::mutex> g(listMu_); found = findItem(detailKey_, it); }
            if (found) doAction(it);
        }
        for (int c = 0; c < kCards; ++c)
        {
            if (!has(card(c, cAction))) continue;
            Item it;
            {
                std::lock_guard<std::mutex> g(listMu_);
                size_t i = (size_t)(off + c);
                if (i >= list_.size()) continue;
                it = list_[i];
            }
            doAction(it);
        }
        dirty_ = true;
    }

    // What a card's (or the details page's) button does.
    void doAction(const Item& it)
    {
        {
            switch (it.action)
            {
                case aInstall:
                    if (!it.testedHere && !confirmed("install:" + it.id)) { note("Not tested on your MPC. Tap INSTALL again to go ahead"); break; }
                    if (it.fresh) markSeen(it.id, it.latest);
                    note(manager_->request("install", it.id) == "OK" ? "Installing " + it.name : "Already waiting");
                    break;
                case aUpdate:
                    if (it.folder == ownFolder()) { note("Update the manager by hand for now: thediskking.com"); break; }
                    if (it.fresh) markSeen(it.id, it.latest);
                    note(manager_->request("install", it.id) == "OK" ? "Updating " + it.name : "Already waiting");
                    break;
                case aRemove:
                    if (it.folder == ownFolder()) { note("The manager can't remove itself"); break; }
                    if (!confirmed("remove:" + it.folder)) { note("Tap REMOVE again to remove " + it.name); break; }
                    manager_->request("remove", it.folder);
                    break;
                case aRestore: manager_->request("restore", it.folder); break;
                case aNotHere: note("Use the maker's installer for " + it.name); break;
                default: break;
            }
        }
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
        tdk::Snapshot s = manager_->snapshot();
        const tdk::Manager::Follow fw = manager_->follows();
        const auto& follow_ = fw.makers;
        const auto& seen_ = fw.seen;
        const bool gen2 = manager_->gen2;
        const int view = std::max(0, std::min(getInt(kShow), 4));
        std::vector<Item> all, shown;
        std::set<std::string> matched;
        int updates = 0, testedHere = 0, fresh = 0;
        for (const tdk::Entry& e : s.plugins)
        {
            Item it;
            it.id = e.id; it.name = e.name; it.downloads = e.downloads; it.date = e.updated; it.featured = e.featured;
            it.makerName = e.maker; it.latest = e.version;
            it.testedHere = e.tested() && (e.testedOn.empty() || e.testedOn.find(gen2 ? "gen2" : "gen1") != std::string::npos);
            // Tested on the other kind of MPC and built as one download for both: shown with the tested plugins, marked as such.
            it.builtBoth = !it.testedHere && e.tested() && e.universal();
            std::string have;
            for (const std::string& f : e.folders())
            {
                auto x = s.installed.find(f);
                if (x != s.installed.end()) { it.folder = f; it.installed = true; have = x->second; matched.insert(f); break; }
            }
            it.canInstall = e.pick(gen2) != nullptr;
            it.update = it.installed && it.canInstall && !have.empty() && tdk::compareVersions(have, e.version) < 0;   // only NEWER
            it.action = it.update ? aUpdate : it.installed ? aRemove : it.canInstall ? aInstall : aNotHere;
            if (!s.current.empty() && (s.current == e.id || s.current == it.folder)) it.action = aWorking;
            // NEW: from a maker you follow, and a plugin or version you have not seen (or installed) yet
            if (follow_.count(e.maker) && !(it.installed && !it.update))
            {
                auto sv = seen_.find(e.id);
                it.fresh = sv == seen_.end() || tdk::compareVersions(sv->second, e.version) < 0;
            }
            it.badge = it.installed ? bInstalled : it.fresh ? bNew : e.beta() ? bBeta : e.tested() ? bStable : bUntested;
            it.chip = it.testedHere ? chTested : e.tested() ? chOther : chNone;
            it.tested = it.testedHere ? "Tested on your MPC (" + std::string(gen2 ? "Gen2" : "Gen1") + ")"
                      : it.builtBoth ? "Tested on " + std::string(gen2 ? "Gen1" : "Gen2") + ", built for " + std::string(gen2 ? "Gen2" : "Gen1")
                      : e.tested() ? "Tested on " + std::string(gen2 ? "Gen1" : "Gen2") + " only" : "";
            it.version = it.update ? "v" + have + " > v" + e.version : "v" + e.version;
            std::string kind = e.kind.empty() ? "plugin" : e.kind;
            it.maker = it.fresh ? "NEW from " + e.maker : (e.featured ? "FEATURED | by " : "by ") + e.maker;
            if (it.maker.size() > kMaxText) it.maker = it.maker.substr(0, kMaxText);
            it.meta = kind + (e.style.empty() || e.style == "other" ? "" : " | " + e.style);
            it.about = e.summary; it.notes = e.notes; it.history = e.history; it.needs = e.needs;
            if (it.needs.empty() && !e.userFiles.empty()) it.needs = "Your own files: " + e.userFiles;
            const tdk::Build* b = e.pick(gen2);
            it.stats = withCommas(e.downloads) + " downloads" + (b && b->size > 0 ? " | " + sizeText(b->size) : "")
                     + (e.userFiles.empty() ? "" : " | own files");
            auto th = thumbIndex_.find(e.id);
            it.thumb = th == thumbIndex_.end() ? 0 : th->second;
            if (it.update) ++updates;
            if (it.testedHere) ++testedHere;
            if (it.fresh) ++fresh;
            bool typeOk = type_ == 0 || (type_ == 1 && e.kind == "instrument") || (type_ == 2 && e.kind == "effect") || (type_ == 3 && e.kind == "addin")
                       || (type_ == 4 && follow_.count(e.maker));
            all.push_back(it);
            if (!typeOk) continue;
                // NEW from a followed maker shows in the main list even before anyone has tested it
            if ((view == 0 && (it.testedHere || it.builtBoth || it.fresh)) || (view == 1 && !it.testedHere && !it.builtBoth) || (view == 2 && it.installed) || (view == 3 && it.update))
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
                if (a.fresh != b.fresh) return a.fresh;                                              // NEW from followed makers first
                if (a.fresh && b.fresh && a.date != b.date) return a.date > b.date;
                int fa = a.featured ? a.featured : 1 << 30, fb = b.featured ? b.featured : 1 << 30;   // Featured first
                if (fa != fb) return fa < fb;
                bool pa = realPicture_.count(a.id) > 0, pb = realPicture_.count(b.id) > 0;          // then real pictures
                if (pa != pb) return pa;
                if (sort_ == 1) return a.date > b.date;
                if (sort_ == 2) return a.name < b.name;
                return a.downloads > b.downloads;
            });
        if (view != lastView_) { lastView_ = view; goPage(0, (int)shown.size()); }
        if (page_ >= pageCount((int)shown.size())) goPage(0, (int)shown.size());
        const int off = offsetFor((int)shown.size());

        // ---- the texts and pictures
        std::vector<std::pair<int, std::string>> texts;
        std::vector<std::pair<int, float>> frames;
        auto frame = [&](int param, int f, int count) { frames.emplace_back(param, (f + 0.5f) / (float)count); };
        for (int c = 0; c < kCards; ++c)
        {
            size_t i = (size_t)(off + c);
            if (i < shown.size())
            {
                const Item& it = shown[i];
                std::string a, b;
                splitLine(it.about, a, b);
                texts.emplace_back(card(c, cName), it.name);
                texts.emplace_back(card(c, cMaker), it.maker);
                texts.emplace_back(card(c, cMeta), it.meta);
                texts.emplace_back(card(c, cAboutA), a);
                texts.emplace_back(card(c, cAboutB), b);
                texts.emplace_back(card(c, cStats), it.stats);
                texts.emplace_back(card(c, cVersion), it.version);
                texts.emplace_back(card(c, cTested), it.tested);
                thumbFrame(c, it.thumb, frame);
                frame(card(c, cBadge), it.badge, kBadgeFrames);
                frame(card(c, cChip), it.chip, kChipFrames);
                frame(card(c, cLook), it.action, kActionFrames);
            }
            else
            {
                std::string empty = (c == 0 && shown.empty()) ? (view == 3 ? "Everything is up to date" : s.plugins.empty() ? "Getting the plugin list..." : "Nothing here") : "";
                for (CardField f : {cName, cMaker, cMeta, cAboutA, cAboutB, cStats, cVersion, cTested}) texts.emplace_back(card(c, f), f == cName ? empty : "");
                thumbFrame(c, 0, frame);
                frame(card(c, cBadge), bBlank, kBadgeFrames);
                frame(card(c, cChip), chNone, kChipFrames);
                frame(card(c, cLook), aBlank, kActionFrames);
            }
        }
        // ---- the details page (shown while "Detail Open" is on)
        {
            Item it;
            bool found = false;
            for (auto* lst : {&shown, &all})
                for (auto& x : *lst)
                    if (!found && !detailKey_.empty() && (x.id == detailKey_ || (x.id.empty() && x.folder == detailKey_))) { it = x; found = true; }
            auto dt = [&](int i, const std::string& s) { texts.emplace_back(kDText + i, s); };
            std::vector<std::string> about = wrap(it.about, 6), news = wrap(it.notes, 4), needs = wrap(it.needs, 2);
            dt(dName, found ? it.name : "");
            dt(dMaker, found ? it.maker : "");
            dt(dMeta, found ? it.meta : "");
            dt(dVersion, found ? it.version : "");
            dt(dTested, found ? it.tested : "");
            dt(dStats, found ? it.stats : "");
            for (int k = 0; k < 6; ++k) dt(dAbout1 + k, k < (int)about.size() ? about[(size_t)k] : "");
            for (int k = 0; k < 2; ++k) dt(dNeeds1 + k, k < (int)needs.size() ? needs[(size_t)k] : "");
            for (int k = 0; k < 4; ++k) dt(dNew1 + k, k < (int)news.size() ? news[(size_t)k] : (k == 0 && found ? "No notes for this version" : ""));
            std::vector<std::string> hist;
            size_t s0 = 0;
            while (s0 < it.history.size() && hist.size() < 4)
            {
                size_t e0 = it.history.find(';', s0);
                if (e0 == std::string::npos) e0 = it.history.size();
                std::string one = it.history.substr(s0, e0 - s0);
                size_t a1 = one.find('|'), a2 = a1 == std::string::npos ? a1 : one.find('|', a1 + 1);
                if (a2 != std::string::npos)
                    hist.push_back("v" + one.substr(0, a1) + "  " + one.substr(a1 + 1, a2 - a1 - 1) + "  " + withCommas(std::atoll(one.substr(a2 + 1).c_str())) + " dl");
                s0 = e0 + 1;
            }
            for (int k = 0; k < 4; ++k) dt(dHist1 + k, k < (int)hist.size() ? hist[(size_t)k] : "");
            int g = found ? it.thumb : 0;
            int bank = g > thumbHalf_ ? 1 : 0;
            frame(kDPic + pThumbBank, bank, 2);
            frame(kDPic + pThumb, g == 0 ? 0 : (bank ? g - thumbHalf_ : g), thumbHalf_ + 1);
            frame(kDPic + pBadge, found ? it.badge : bBlank, kBadgeFrames);
            frame(kDPic + pChip, found ? it.chip : chNone, kChipFrames);
            frame(kDPic + pLook, found ? it.action : aBlank, kActionFrames);
            frame(kFavLook, found && fw.makers.count(it.makerName) ? 1 : 0, 2);
            if (!found && getBool(kDetailOpen)) { set(kDetailOpen, 0.0f); notifyHostParameterChanged(kDetailOpen); }
        }
        std::string status = manager_->screenStatus();
        if (!notice_.empty() && clock() - noticeAt_ < 6.0) status = notice_;
        else if (s.restartNeeded && !s.busy) status = status.substr(0, kMaxText) + " Restart the MPC to load them";
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
        texts.emplace_back(kTheme, std::string("Theme: ") + kThemes[std::max(0, std::min(getInt(kThemePick), kNumThemes - 1))]);
        texts.emplace_back(kPage, "Page " + std::to_string(page_ + 1) + " / " + std::to_string(pageCount((int)shown.size())));
        std::string sum = shown.empty() ? "No plugins here" : std::to_string(off + 1) + "-" + std::to_string(std::min((int)shown.size(), off + kCards))
                                     + " of " + std::to_string(shown.size()) + " | " + std::to_string(updates) + (updates == 1 ? " update" : " updates");
        if (fresh && !shown.empty()) sum = std::to_string(fresh) + " NEW | " + sum;
        texts.emplace_back(kSummary, sum.substr(0, kMaxText));
        texts.emplace_back(kPhone, !port_ ? "" : ip.empty() ? "Phone: connect to Wi-Fi" : "Phone: http://" + ip + ":" + std::to_string(port_));
        texts.emplace_back(kPin, port_ && !ip.empty() ? "PIN " + pin_ : "");
        frame(kOnline, s.online ? 1 : 0, 2);
        {
            std::lock_guard<std::mutex> g(listMu_);
            list_ = std::move(shown);
            all_ = std::move(all);
            lastOff_ = off;
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

    // The screenshot for a card: which strip (A or B) and which frame in it. 0 = no picture.
    template <typename F> void thumbFrame(int c, int global, F& frame)
    {
        int bank = global > thumbHalf_ ? 1 : 0;
        int f = global == 0 ? 0 : (bank ? global - thumbHalf_ : global);
        frame(card(c, cThumbBank), bank, 2);
        frame(card(c, cThumb), f, thumbHalf_ + 1);
    }

    std::string ownFolder() const { return tdk::fs::path(folder_).filename().string(); }

    static std::string withCommas(long long n)
    {
        std::string s = std::to_string(n);
        for (int i = (int)s.size() - 3; i > 0; i -= 3) s.insert((size_t)i, ",");
        return s;
    }
    static std::string sizeText(long long bytes)
    {
        char buf[32];
        if (bytes >= 1048576) std::snprintf(buf, sizeof buf, "%.1f MB", bytes / 1048576.0);
        else std::snprintf(buf, sizeof buf, "%lld KB", std::max(1LL, bytes / 1024));
        return buf;
    }

    // Word-wraps text into lines of at most 31 characters (the framework's limit). The last line ends in "..." if cut.
    static std::vector<std::string> wrap(const std::string& text, int maxLines)
    {
        std::vector<std::string> out;
        std::string rest = text;
        while (!rest.empty() && (int)out.size() < maxLines)
        {
            while (!rest.empty() && rest[0] == ' ') rest.erase(0, 1);
            if (rest.size() <= kMaxText) { out.push_back(rest); rest.clear(); break; }
            size_t cut = rest.rfind(' ', kMaxText);
            if (cut == std::string::npos || cut < 10) cut = kMaxText;
            out.push_back(rest.substr(0, cut));
            rest = rest.substr(cut);
        }
        if (!rest.empty() && !out.empty())
        {
            std::string& last = out.back();
            if (last.size() > kMaxText - 3) last = last.substr(0, kMaxText - 3);
            last += "...";
        }
        return out;
    }

    // The item shown on the details page (call with listMu_ held).
    bool findItem(const std::string& key, Item& out) const
    {
        for (auto* lst : {&list_, &all_})
            for (auto& x : *lst)
                if (x.id == key || (x.id.empty() && x.folder == key)) { out = x; return true; }
        return false;
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
    std::string folder_, pin_, confirmKey_, notice_, detailKey_;
    std::map<std::string, int> thumbIndex_;
    std::set<std::string> realPicture_;
    int port_ = 0, lastView_ = -1, sort_ = 0, type_ = 0, thumbCount_ = 0, thumbHalf_ = 0, page_ = 0, lastOff_ = 0;
    double confirmAt_ = 0, noticeAt_ = 0, lastFullUpdate_ = 0;
    std::mutex textMu_, listMu_;
    std::vector<std::string> text_;
    std::vector<Item> list_, all_;
};

MPC_PLUGIN_ENTRY(MPCPluginsManager)
