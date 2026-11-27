// Runs the manager's real install / update / remove / restore steps on a pretend drive (a folder on a computer).
// Usage: test_manager <drive_folder> <acid-gen2.zip> <acid sha256> <dexed-sdk.zip> <dexed sha256>
#define TDK_TEST 1
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include "../src/core/manager.h"

namespace fs = std::filesystem;
static int fails = 0;
static void check(bool ok, const std::string& what) { std::printf("%s %s\n", ok ? "PASS" : "FAIL", what.c_str()); if (!ok) ++fails; }

static tdk::Snapshot waitIdle(tdk::Manager& m)
{
    for (int i = 0; i < 600; ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        tdk::Snapshot s = m.snapshot();
        if (!s.busy && s.queued == 0) return s;
    }
    return m.snapshot();
}

int main(int argc, char** argv)
{
    if (argc < 6) { std::printf("usage\n"); return 2; }
    fs::path drive = argv[1];
    std::error_code ec;
    fs::remove_all(drive, ec);
    fs::create_directories(drive / "VST2 Plugins" / "The Disk King - VST - MPC Plugins Manager");
    fs::create_directories(drive / "MPC Plugins Manager");
    std::string cols = "id\tname\tmaker\tkind\ttier\tversion\ttested_on\tdownloads\tuser_files\tsummary";
    for (const char* k : {"sdk", "gen1", "gen2"})
        for (const char* c : {"_url", "_sha256", "_size", "_folder", "_inside", "_copy_ok"}) cols += std::string("\t") + k + c;
    std::ofstream cat(drive / "MPC Plugins Manager" / "catalog.tsv", std::ios::binary);
    cat << "#mpcplugins-tsv\t1\t2026-11-19T00:00:00Z\n" << cols << "\n";
    // Acid: only a Gen2 build, inside portable/. Dexed: a works-on-both build. Bad: wrong checksum.
    cat << "acid\tAcid\tsd88me\tinstrument\tverified\t3.0.1\tgen2\t100\t\tTB-303 style\t"
        << "\t\t\t\t\t0\t" << "\t\t\t\t\t0\t"
        << "file://" << argv[2] << "\t" << argv[3] << "\t1\tsd88me - VST - Acid\tAcid-3.0.1/portable\t1\n";
    cat << "dexed-dx7\tDexed DX7\tsd88me\tinstrument\tverified\t2.0.5\tgen2\t90\t\tFM\t"
        << "file://" << argv[4] << "\t" << argv[5] << "\t1\tsd88me - VST - Dexed (DX7)\t\t1\t" << "\t\t\t\t\t0\t" << "\t\t\t\t\t0\n";
    if (argc > 7)   // a Gen2 release with no plugin.json: the manager writes one from mpc-plugin.json
        cat << "boris-granular\tBoris Granular\tsd88me\teffect\tlisted\t1.0.1\t\t1\t\tgranular\t"
            << "\t\t\t\t\t0\t" << "\t\t\t\t\t0\t"
            << "file://" << argv[6] << "\t" << argv[7] << "\t1\tsd88me - VST - Boris Granular\tBoris-Granular-1.0.1/portable\t0\n";
    cat << "bad\tBad\tx\tinstrument\tlisted\t1.0.0\t\t1\t\tbad\t"
        << "file://" << argv[4] << "\t" << std::string(64, 'a') << "\t1\tx - VST - Bad\t\t1\t" << "\t\t\t\t\t0\t" << "\t\t\t\t\t0\n";
    cat.close();

#if defined(_WIN32)
    _putenv_s("MPC_MANAGER_DRIVE", drive.string().c_str());
#else
    setenv("MPC_MANAGER_DRIVE", drive.string().c_str(), 1);
#endif
    tdk::Manager m;
    const bool gen1Run = std::getenv("MPC_TEST_GEN1") != nullptr;   // set it to test as a Gen1 (32-bit) MPC
    m.gen2 = !gen1Run;
    std::printf("Testing as a %s MPC\n", gen1Run ? "Gen1" : "Gen2");
    m.start((drive / "VST2 Plugins" / "The Disk King - VST - MPC Plugins Manager").string());
    tdk::Snapshot s = waitIdle(m);
    check(s.plugins.size() == (argc > 7 ? 4u : 3u), "saved list loaded: " + std::to_string(s.plugins.size()) + " plugins");
    if (argc > 7 && !gen1Run)   // Boris Granular has only a Gen2 build
    {
        m.request("install", "boris-granular");
        s = waitIdle(m);
        fs::path pj = drive / "VST2 Plugins" / "sd88me - VST - Boris Granular" / "plugin.json";
        std::ifstream f(pj);
        std::stringstream ss; ss << f.rdbuf();
        std::string j = ss.str();
        check(!j.empty(), "Boris Granular installed with a plugin.json written by the manager (" + s.status + " " + s.lastError + ")");
        check(j.find("\"name\": \"Boris Granular\"") != std::string::npos && j.find("\"manufacturer\": \"sd88me\"") != std::string::npos
              && j.find("\"uid\": \"0x") != std::string::npos && j.find("\"file64\": \"") != std::string::npos
              && j.find("\"kind\": \"effect\"") != std::string::npos, "written plugin.json has name, maker, ID, program file, kind");
        std::printf("%s\n", j.c_str());
    }

    m.request("install", "acid");
    m.request("install", "dexed-dx7");
    s = waitIdle(m);
    fs::path acid = drive / "VST2 Plugins" / "sd88me - VST - Acid", dexed = drive / "VST2 Plugins" / "sd88me - VST - Dexed (DX7)";
    if (gen1Run) check(!fs::exists(acid), "Gen1 MPC: Acid (Gen2-only build) is not installed");
    else check(fs::exists(acid / "plugin.json"), "Acid installed from inside portable/ (" + s.status + ")");
    check(fs::exists(dexed / "plugin.json"), "Dexed installed from the works-on-both zip");
    {
        std::string j = tdk::readText(dexed / "plugin.json");
        std::string f32 = tdk::jsonString(j, "file32"), f64 = tdk::jsonString(j, "file64");
        bool has32 = !f32.empty() && fs::exists(dexed / fs::u8path(f32)), has64 = !f64.empty() && fs::exists(dexed / fs::u8path(f64));
        if (gen1Run) check(has32 && !has64, "Gen1 MPC: only the 32-bit file installed (" + f32 + ")");
        else check(has64 && !has32, "Gen2 MPC: only the 64-bit file installed (" + f64 + ")");
    }
    if (!gen1Run) check(s.installed.count("sd88me - VST - Acid") && s.installed["sd88me - VST - Acid"] == "3.0.1", "Acid shows as installed v3.0.1");
    check(!fs::exists(drive / "VST2 Plugins" / "sd88me - VST - Acid.installing"), "no half-installed folder left");
    check(fs::is_empty(drive / "MPC Plugins Manager" / "Temp"), "temporary download cleaned up");

    // Update over the top (Dexed on Gen1, Acid on Gen2). On Gen1 an old 64-bit file left in the folder must go too.
    fs::path up = gen1Run ? dexed : acid;
    std::string stale64 = tdk::jsonString(tdk::readText(dexed / "plugin.json"), "file64");
    if (gen1Run && !stale64.empty()) { std::ofstream old(dexed / fs::u8path(stale64)); old << "old 64-bit file"; }
    { std::ofstream rom(up / "My Patterns.bank"); rom << "mine"; }
    m.request("install", gen1Run ? "dexed-dx7" : "acid");
    s = waitIdle(m);
    check(fs::exists(up / "My Patterns.bank"), "update kept the person's own file");
    if (gen1Run) check(!fs::exists(dexed / fs::u8path(stale64)), "Gen1 update: an old 64-bit file is not carried over");
    check(s.removed.size() == 1, "old version kept in Removed Plugins: " + (s.removed.empty() ? std::string("none") : s.removed[0]));

    m.request("install", "bad");
    s = waitIdle(m);
    check(!fs::exists(drive / "VST2 Plugins" / "x - VST - Bad"), "wrong checksum: nothing installed (" + s.lastError + ")");

    m.request("remove", "sd88me - VST - Dexed (DX7)");
    s = waitIdle(m);
    check(!fs::exists(dexed), "Dexed removed from VST2 Plugins");
    std::string back;
    for (auto& r : s.removed) if (r.rfind("sd88me - VST - Dexed (DX7) (removed", 0) == 0) back = r;
    check(!back.empty(), "Dexed is in Removed Plugins");
    m.request("restore", back);
    s = waitIdle(m);
    check(fs::exists(dexed / "plugin.json"), "Restore put Dexed back");

    m.request("remove", "The Disk King - VST - MPC Plugins Manager");
    s = waitIdle(m);
    check(fs::exists(drive / "VST2 Plugins" / "The Disk King - VST - MPC Plugins Manager"), "the manager refuses to remove itself");
    m.request("remove", "../../Windows");
    s = waitIdle(m);
    check(!s.lastError.empty(), "a folder name outside VST2 Plugins is refused");
    check(fs::exists(drive / "MPC Plugins Manager" / "history.txt"), "history log written");
    m.stop();
    std::printf(fails ? "FAILED %d\n" : "ALL PASS\n", fails);
    return fails ? 1 : 0;
}
