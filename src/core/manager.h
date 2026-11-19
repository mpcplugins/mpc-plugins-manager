// MPC Plugins Manager by The Disk King. Copyright (c) 2026 The Disk King. All rights reserved.
// The manager: keeps the plugin list, knows what is installed, and installs / updates / removes / restores
// plugins on a worker thread. It only ever writes inside "<drive>/VST2 Plugins" and its own
// "<drive>/MPC Plugins Manager" folder (temporary downloads, removed plugins, the cached list, a history log).
#pragma once
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include "catalog.h"
#include "platform.h"
#include "sha256.h"
#include "unzip.h"

namespace tdk {
namespace fs = std::filesystem;

constexpr const char* kCatalogUrl = "https://mpcplugins.github.io/catalog.tsv";
constexpr const char* kWorkFolder = "MPC Plugins Manager";
constexpr const char* kRemovedFolder = "Removed Plugins";

inline bool safeFolderName(const std::string& n)
{
    if (n.empty() || n.size() > 200 || n[0] == '.' || n.find(" - VST - ") == std::string::npos) return false;
    for (char c : n) if (c == '/' || c == '\\' || c == ':' || (unsigned char)c < 32) return false;
    return true;
}

inline std::string nowStamp(const char* fmt = "%Y-%m-%d %H%M%S")
{
    std::time_t t = std::time(nullptr);
    char buf[64];
    std::tm tmv{};
#if defined(_WIN32)
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    std::strftime(buf, sizeof buf, fmt, &tmv);
    return buf;
}

// A simple string value from a small JSON file ("key": "value"), or "" if absent.
inline std::string jsonString(const std::string& s, const std::string& key)
{
    size_t k = s.find("\"" + key + "\"");
    if (k == std::string::npos) return "";
    size_t colon = s.find(':', k + key.size() + 2);
    if (colon == std::string::npos) return "";
    size_t i = colon + 1;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) ++i;
    if (i >= s.size() || s[i] != '"') return "";
    size_t end = s.find('"', i + 1);
    if (end == std::string::npos || end - i > 300) return "";
    return s.substr(i + 1, end - i - 1);
}

inline std::string readText(const fs::path& p)
{
    std::ifstream f(p, std::ios::binary);
    if (!f) return "";
    std::stringstream ss; ss << f.rdbuf();
    return ss.str();
}

// The "version" in a plugin folder's plugin.json, or "" if there is none.
inline std::string folderVersion(const fs::path& folder)
{
    std::string v = jsonString(readText(folder / "plugin.json"), "version");
    return v.size() > 40 ? "" : v;
}

// Writes the plugin.json MPC OS 3.10.1 needs, for a release that only carries the older mpc-plugin.json.
// Name and maker come from the folder name ("Maker - VST - Name"), which is what the plugin itself reports.
inline bool writePluginJson(const fs::path& stage, const std::string& folder, const std::string& info, bool gen2, std::string& why)
{
    size_t cut = folder.find(" - VST - ");
    std::string maker = folder.substr(0, cut), name = folder.substr(cut + 9);
    std::string uid = jsonString(info, "uid"), so = jsonString(info, "so"), kind = jsonString(info, "kind"), ver = jsonString(info, "version");
    bool hex = uid.size() == 8;
    for (char c : uid) if (!std::isxdigit((unsigned char)c)) hex = false;
    if (!hex) { why = "the release does not say the plugin's ID"; return false; }
    if (so.empty() || !safeZipPath(so) || !fs::is_regular_file(stage / fs::u8path(so))) { why = "the plugin's program file is missing"; return false; }
    auto esc = [](const std::string& s) { std::string o; for (char c : s) { if (c == '"' || c == '\\') o += '\\'; o += c; } return o; };
    bool fx = kind == "effect";
    std::ofstream o(stage / "plugin.json", std::ios::binary);
    o << "{\n  \"name\": \"" << esc(name) << "\",\n  \"manufacturer\": \"" << esc(maker) << "\",\n  \"version\": \"" << esc(ver)
      << "\",\n  \"format\": \"vst2\",\n  \"kind\": \"" << (fx ? "effect" : "instrument") << "\",\n  \"uid\": \"0x" << uid
      << "\",\n  \"numInputs\": " << (fx ? 2 : 0) << ",\n  \"numOutputs\": 2,\n  \"" << (gen2 ? "file64" : "file32") << "\": \"" << esc(so)
      << "\",\n  \"note\": \"Written by MPC Plugins Manager (The Disk King) from the release's mpc-plugin.json\"\n}\n";
    if (!o) { why = "could not write plugin.json"; return false; }
    return true;
}

struct Drive { std::string root, label; bool current = false; };

struct Snapshot   // a consistent copy of the state, for the screen and the phone page
{
    std::string status, detail, generated, drive, lastError, current;   // current: the id or folder being worked on
    int progress = -1;                              // 0..100 while downloading, -1 otherwise
    bool busy = false, restartNeeded = false, online = false;
    std::vector<Entry> plugins;
    std::map<std::string, std::string> installed;   // folder -> version
    std::vector<std::string> removed;               // folder names in Removed Plugins
    std::vector<Drive> drives;
    int queued = 0;
};

class Manager
{
public:
    ~Manager() { stop(); }

    // pluginFolder: our own folder ("<drive>/VST2 Plugins/The Disk King - VST - MPC Plugins Manager").
    void start(const std::string& pluginFolder)
    {
        if (running_.exchange(true)) return;
        ownFolder_ = fs::path(pluginFolder).filename().string();
        fs::path vst2 = fs::path(pluginFolder).parent_path();
        std::string root = vst2.filename().string() == "VST2 Plugins" ? vst2.parent_path().string() : vst2.string();
        if (const char* over = std::getenv("MPC_MANAGER_DRIVE")) root = over;   // tests on a computer
        homeDrive_ = root;
        setDrive(root);
        worker_ = std::thread([this] { loop(); });
    }

    void stop()
    {
        if (!running_.exchange(false)) return;
        { std::lock_guard<std::mutex> g(jobsMu_); }
        cv_.notify_all();
        if (worker_.joinable()) worker_.join();
    }

    // Requests from the screen or the phone page. Each returns a short answer for the person.
    std::string request(const std::string& op, const std::string& arg)
    {
        if (op != "install" && op != "remove" && op != "restore" && op != "refresh" && op != "drive" && op != "cancel")
            return "unknown request";
        if (op == "cancel") { cancel_ = true; return "Stopping the current download"; }
        std::lock_guard<std::mutex> g(jobsMu_);
        if (jobs_.size() >= 50) return "Too many requests waiting; try again in a moment";
        for (auto& j : jobs_) if (j.first == op && j.second == arg) return "Already waiting";
        jobs_.emplace_back(op, arg);
        cv_.notify_all();
        return "OK";
    }

    Snapshot snapshot()
    {
        std::lock_guard<std::mutex> g(mu_);
        Snapshot s = state_;
        std::lock_guard<std::mutex> j(jobsMu_);
        s.queued = (int)jobs_.size();
        return s;
    }

    // A short line for the MPC screen, changed only when something new happens.
    std::string screenStatus()
    {
        std::lock_guard<std::mutex> g(mu_);
        if (state_.progress >= 0) return state_.status + " " + std::to_string(state_.progress) + "%";
        return state_.status;
    }
    unsigned changeCount() const { return changes_.load(); }

    bool gen2 = isGen2Device();

private:
    // ---------------------------------------------------------------- state helpers
    void setStatus(const std::string& s, const std::string& detail = "", int progress = -1)
    {
        std::lock_guard<std::mutex> g(mu_);
        state_.status = s;
        state_.detail = detail;
        state_.progress = progress;
        ++changes_;
    }
    void fail(const std::string& what, const std::string& why)
    {
        {
            std::lock_guard<std::mutex> g(mu_);
            state_.lastError = what + ": " + why;
        }
        setStatus(what + " failed", why);
        log("FAILED " + what + ": " + why);
    }
    void log(const std::string& line)
    {
        std::error_code ec;
        fs::create_directories(work_, ec);
        std::ofstream f(work_ / "history.txt", std::ios::app);
        f << nowStamp("%Y-%m-%d %H:%M:%S") << "  " << line << "\n";
    }

    void setDrive(const std::string& root)
    {
        root_ = fs::path(root);
        vst2_ = root_ / "VST2 Plugins";
        work_ = root_ / kWorkFolder;
        std::lock_guard<std::mutex> g(mu_);
        state_.drive = root;
        ++changes_;
    }

    std::vector<Drive> findDrives()
    {
        std::vector<Drive> out;
        std::set<std::string> seen;
        auto add = [&](const fs::path& p) {
            std::error_code ec;
            if (!fs::is_directory(p, ec)) return;
            fs::path real = fs::weakly_canonical(p, ec);
            std::string key = ec ? p.string() : real.string();
            if (!seen.insert(key).second) return;
            Drive d;
            d.root = p.string();
            d.label = p.filename().string();
            if (d.label == "nvme" || d.label == "sdcard") d.label = "Internal drive";
            d.current = p == root_;
            out.push_back(d);
        };
#if !defined(_WIN32)
        add("/nvme");
        add("/sdcard");
        std::error_code ec;
        for (auto& e : fs::directory_iterator("/media", ec))
        {
            std::string n = e.path().filename().string();
            if (n.rfind("az01-", 0) == 0 || n == "root") continue;   // system areas, not user drives
            add(e.path());
        }
#else
        (void)add;
#endif
        if (seen.insert(root_.string()).second) { Drive d; d.root = root_.string(); d.label = root_.filename().string(); d.current = true; out.push_back(d); }
        return out;
    }

    void scanInstalled()
    {
        std::map<std::string, std::string> inst;
        std::vector<std::string> removed;
        std::error_code ec;
        for (auto& e : fs::directory_iterator(vst2_, ec))
            if (e.is_directory(ec) && safeFolderName(e.path().filename().string()))
                inst[e.path().filename().string()] = folderVersion(e.path());
        for (auto& e : fs::directory_iterator(work_ / kRemovedFolder, ec))
            if (e.is_directory(ec)) removed.push_back(e.path().filename().string());
        std::sort(removed.begin(), removed.end());
        std::vector<Drive> drives = findDrives();
        std::lock_guard<std::mutex> g(mu_);
        state_.installed = inst;
        state_.removed = removed;
        state_.drives = drives;
        ++changes_;
    }

    // ---------------------------------------------------------------- the plugin list
    bool loadCatalogText(const std::string& text, bool fromNetwork)
    {
        std::vector<Entry> list;
        std::string gen, err;
        if (!parseCatalog(text, list, gen, err)) { if (fromNetwork) fail("Plugin list", err); return false; }
        std::lock_guard<std::mutex> g(mu_);
        state_.plugins = std::move(list);
        state_.generated = gen;
        state_.online = state_.online || fromNetwork;
        ++changes_;
        return true;
    }

    void refreshCatalog()
    {
        setStatus("Getting the plugin list");
        std::error_code ec;
        fs::create_directories(work_, ec);
        fs::path tmp = work_ / "catalog.tsv.new";
        std::string err;
        if (!downloadFile(kCatalogUrl, tmp.string(), err))
        {
            setStatus(havePlugins() ? "Offline: using the saved list" : "No plugin list yet", err);
            fs::remove(tmp, ec);
            return;
        }
        std::ifstream f(tmp, std::ios::binary);
        std::stringstream ss; ss << f.rdbuf();
        f.close();
        if (loadCatalogText(ss.str(), true))
        {
            fs::rename(tmp, work_ / "catalog.tsv", ec);
            lastRefresh_ = std::chrono::steady_clock::now();
            setStatus("Ready", std::to_string(count()) + " plugins");
        }
        else fs::remove(tmp, ec);
    }
    bool havePlugins() { std::lock_guard<std::mutex> g(mu_); return !state_.plugins.empty(); }
    size_t count() { std::lock_guard<std::mutex> g(mu_); return state_.plugins.size(); }

    bool findEntry(const std::string& id, Entry& out)
    {
        std::lock_guard<std::mutex> g(mu_);
        for (auto& e : state_.plugins) if (e.id == id) { out = e; return true; }
        return false;
    }

    // ---------------------------------------------------------------- jobs
    void install(const std::string& id)
    {
        Entry e;
        if (!findEntry(id, e)) return fail("Install", "that plugin is not on the list");
        const Build* b = e.pick(gen2);
        if (!b) return fail("Install " + e.name, "this release can't be copied to the MPC directly; use the maker's installer");
        if (b->sha256.size() != 64) return fail("Install " + e.name, "no checksum is listed for this download, so it can't be checked");
        const std::string folder = b->folder;
        if (!safeFolderName(folder)) return fail("Install " + e.name, "the plugin folder name is not safe");
        if (folder == ownFolder_) return fail("Install " + e.name, "MPC Plugins Manager can't replace itself while it is running");
        std::error_code ec;
        fs::create_directories(work_ / "Temp", ec);
        fs::create_directories(vst2_, ec);
        if (ec) return fail("Install " + e.name, "could not create the VST2 Plugins folder on this drive");
        fs::path zip = work_ / "Temp" / (e.id + ".zip");

        cancel_ = false;
        setStatus("Downloading " + e.name, "", 0);
        long long total = b->size > 0 ? b->size : 0;
        std::string err;
        int last = -1;
        bool ok = downloadFile(b->url, zip.string(), err, [&](long long got) {
            if (cancel_ || !running_) return false;
            int pct = total > 0 ? (int)(got * 100 / total) : 0;
            if (pct != last) { last = pct; std::lock_guard<std::mutex> g(mu_); state_.progress = pct > 100 ? 100 : pct; ++changes_; }
            return true;
        });
        if (!ok) { fs::remove(zip, ec); return fail("Download " + e.name, err); }

        setStatus("Checking " + e.name);
        if (Sha256::ofFile(zip.string()) != b->sha256)
        {
            fs::remove(zip, ec);
            return fail("Install " + e.name, "the download does not match its checksum, so nothing was installed");
        }

        setStatus("Unpacking " + e.name);
        fs::path stage = vst2_ / (folder + ".installing");
        fs::remove_all(stage, ec);
        std::string prefix = (b->inside.empty() ? "" : b->inside + "/") + folder + "/";
        bool hasJson = false, wrote = false;
        int files = 0;
        std::string info;                                // the release's mpc-plugin.json, if plugin.json must be written
        {
            ZipFile z;
            if (!z.open(zip.string(), err)) { fs::remove(zip, ec); return fail("Install " + e.name, err); }
            for (const ZipItem& it : z.items)
            {
                bool isInfo = it.name == "mpc-plugin.json" || (it.name.size() > 16 && it.name.compare(it.name.size() - 16, 16, "/mpc-plugin.json") == 0);
                if (isInfo && it.size < 65536 && (info.empty() || it.name.compare(0, prefix.size(), prefix) == 0))
                {
                    std::vector<uint8_t> d;
                    if (z.read(it, d, err)) info.assign(d.begin(), d.end());
                }
                if (it.name.compare(0, prefix.size(), prefix) != 0) continue;
                std::string rel = it.name.substr(prefix.size());
                if (rel.empty()) continue;
                if (!safeZipPath(rel)) { fs::remove_all(stage, ec); fs::remove(zip, ec); return fail("Install " + e.name, "the zip has an unsafe file path"); }
                fs::path dest = stage / fs::u8path(rel);
                if (it.isDir()) { fs::create_directories(dest, ec); continue; }
                fs::create_directories(dest.parent_path(), ec);
                std::vector<uint8_t> data;
                if (!z.read(it, data, err)) { fs::remove_all(stage, ec); fs::remove(zip, ec); return fail("Install " + e.name, err); }
                std::ofstream o(dest, std::ios::binary);
                o.write(reinterpret_cast<const char*>(data.data()), (std::streamsize)data.size());
                if (!o) { fs::remove_all(stage, ec); fs::remove(zip, ec); return fail("Install " + e.name, "could not write to the drive (is it full?)"); }
                if (rel == "plugin.json") hasJson = true;
                ++files;
            }
        }
        fs::remove(zip, ec);
        if (files == 0) { fs::remove_all(stage, ec); return fail("Install " + e.name, "the plugin folder was not found in the download"); }
        if (!hasJson)
        {
            std::string why;
            if (info.empty() || !writePluginJson(stage, folder, info, gen2, why))
            {
                fs::remove_all(stage, ec);
                return fail("Install " + e.name, "this release lacks the plugin.json MPC OS 3.10.1 needs, and "
                            + (info.empty() ? std::string("has no details to write one from") : why) + ". Use the maker's installer.");
            }
            wrote = true;
        }

        fs::path target = vst2_ / folder;
        std::string replaced;
        if (fs::exists(target, ec))
        {
            // Keep the person's own files (ROMs, presets): anything in the old folder the new one doesn't have.
            for (auto it = fs::recursive_directory_iterator(target, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
            {
                if (!it->is_regular_file(ec)) continue;
                fs::path rel = fs::relative(it->path(), target, ec);
                fs::path dest = stage / rel;
                if (!fs::exists(dest, ec)) { fs::create_directories(dest.parent_path(), ec); fs::copy_file(it->path(), dest, ec); }
            }
            ec.clear();
            replaced = moveToRemoved(target, "replaced");
            if (replaced.empty()) { fs::remove_all(stage, ec); return fail("Update " + e.name, "could not move the old version aside"); }
        }
        fs::rename(stage, target, ec);
        if (ec) { fs::remove_all(stage, ec); return fail("Install " + e.name, "could not finish copying into VST2 Plugins"); }
        log((replaced.empty() ? "installed " : "updated ") + e.name + " " + e.version + " (" + folder + ")" + (wrote ? " [plugin.json written by the manager]" : ""));
        {
            std::lock_guard<std::mutex> g(mu_);
            state_.restartNeeded = true;
        }
        scanInstalled();
        setStatus((replaced.empty() ? "Installed " : "Updated ") + e.name, "Restart the MPC to see it (on a USB stick or SD card: unplug it and plug it back in)");
    }

    // Moves a plugin folder into "Removed Plugins" with the date, so it can be put back. Returns the new name.
    std::string moveToRemoved(const fs::path& folder, const char* why)
    {
        std::error_code ec;
        fs::create_directories(work_ / kRemovedFolder, ec);
        std::string name = folder.filename().string() + " (" + why + " " + nowStamp() + ")";
        fs::path dest = work_ / kRemovedFolder / name;
        for (int n = 2; fs::exists(dest, ec) && n < 100; ++n) { name = folder.filename().string() + " (" + why + " " + nowStamp() + " #" + std::to_string(n) + ")"; dest = work_ / kRemovedFolder / name; }
        fs::rename(folder, dest, ec);
        if (ec)   // a different drive area: copy, then delete
        {
            ec.clear();
            fs::copy(folder, dest, fs::copy_options::recursive, ec);
            if (ec) { fs::remove_all(dest, ec); return ""; }
            fs::remove_all(folder, ec);
        }
        return name;
    }

    void remove(const std::string& folder)
    {
        if (!safeFolderName(folder)) return fail("Remove", "not a plugin folder");
        if (folder == ownFolder_) return fail("Remove", "MPC Plugins Manager can't remove itself while running; delete its folder by hand");
        std::error_code ec;
        fs::path p = vst2_ / folder;
        if (!fs::is_directory(p, ec)) return fail("Remove", "that plugin is not installed on this drive");
        setStatus("Removing " + folder);
        std::string moved = moveToRemoved(p, "removed");
        if (moved.empty()) return fail("Remove " + folder, "could not move it");
        log("removed " + folder + " -> " + std::string(kRemovedFolder) + "/" + moved);
        {
            std::lock_guard<std::mutex> g(mu_);
            state_.restartNeeded = true;
        }
        scanInstalled();
        setStatus("Removed " + folder, "It is in MPC Plugins Manager/Removed Plugins on this drive. Restore puts it back.");
    }

    void restore(const std::string& removedName)
    {
        if (removedName.empty() || removedName.find('/') != std::string::npos || removedName.find('\\') != std::string::npos
            || removedName.find("..") != std::string::npos)
            return fail("Restore", "not a removed plugin");
        size_t cut = removedName.rfind(" (");
        std::string folder = cut == std::string::npos ? removedName : removedName.substr(0, cut);
        if (!safeFolderName(folder)) return fail("Restore", "not a plugin folder");
        std::error_code ec;
        fs::path from = work_ / kRemovedFolder / removedName, to = vst2_ / folder;
        if (!fs::is_directory(from, ec)) return fail("Restore", "it is not in Removed Plugins");
        if (fs::exists(to, ec)) return fail("Restore " + folder, "a plugin with that folder name is installed; remove it first");
        fs::create_directories(vst2_, ec);
        fs::rename(from, to, ec);
        if (ec) return fail("Restore " + folder, "could not move it back");
        log("restored " + folder);
        { std::lock_guard<std::mutex> g(mu_); state_.restartNeeded = true; }
        scanInstalled();
        setStatus("Restored " + folder, "Restart the MPC to see it");
    }

    void switchDrive(const std::string& root)
    {
        for (const Drive& d : findDrives())
            if (d.root == root)
            {
                setDrive(root);
                scanInstalled();
                setStatus("Using " + d.label);
                return;
            }
        fail("Change drive", "that drive is not available");
    }

    // ---------------------------------------------------------------- the worker
    void loop()
    {
        {   // the saved list first, so the page works at once (and without internet)
            std::ifstream f(work_ / "catalog.tsv", std::ios::binary);
            if (f) { std::stringstream ss; ss << f.rdbuf(); loadCatalogText(ss.str(), false); }
        }
        scanInstalled();
        refreshCatalog();
        while (running_)
        {
            std::pair<std::string, std::string> job;
            {
                std::unique_lock<std::mutex> g(jobsMu_);
                cv_.wait_for(g, std::chrono::seconds(30), [this] { return !jobs_.empty() || !running_; });
                if (!running_) break;
                if (jobs_.empty())
                {
                    g.unlock();
                    scanInstalled();   // drives come and go
                    if (std::chrono::steady_clock::now() - lastRefresh_ > std::chrono::hours(6)) refreshCatalog();
                    continue;
                }
                job = jobs_.front();
                jobs_.pop_front();
            }
            { std::lock_guard<std::mutex> g(mu_); state_.busy = true; state_.current = job.second; state_.lastError.clear(); ++changes_; }
            if (job.first == "install") install(job.second);
            else if (job.first == "remove") remove(job.second);
            else if (job.first == "restore") restore(job.second);
            else if (job.first == "refresh") { refreshCatalog(); scanInstalled(); }
            else if (job.first == "drive") switchDrive(job.second);
            { std::lock_guard<std::mutex> g(mu_); state_.busy = false; state_.current.clear(); ++changes_; }
        }
    }

    std::atomic<bool> running_{false}, cancel_{false};
    std::atomic<unsigned> changes_{0};
    std::thread worker_;
    std::mutex mu_, jobsMu_;
    std::condition_variable cv_;
    std::deque<std::pair<std::string, std::string>> jobs_;
    Snapshot state_;
    fs::path root_, vst2_, work_;
    std::string ownFolder_, homeDrive_;
    std::chrono::steady_clock::time_point lastRefresh_{};
};

} // namespace tdk
