// Followed makers (1.3): the store shared by the MPC screen and the phone page.
// Build: zig c++ -target x86_64-windows-gnu -O2 -std=c++17 -DTDK_TEST -I ../src test_follow.cpp -lws2_32
// Run:   test_follow <empty folder>
#include "core/manager.h"
#include "core/web.h"
#include <cstdio>

int main(int argc, char** argv)
{
    if (argc < 2) { std::printf("usage: test_follow <empty folder>\n"); return 2; }
    int fails = 0;
    auto check = [&](bool ok, const char* what) { std::printf("  %s  %s\n", ok ? "PASS" : "FAIL", what); if (!ok) ++fails; };
    namespace fs = tdk::fs;
    fs::path drive = argv[1];
    fs::path plug = drive / "VST2 Plugins" / "The Disk King - VST - MPC Plugins Manager";
    fs::create_directories(plug);
    {
        tdk::Manager m;
        m.start(plug.string());
        check(m.follows().makers.empty(), "nobody followed at first");
        m.setFollow("The Disk King", true);
        m.setFollow("sunskiefer", true);
        m.setFollow("sunskiefer", false);
        auto f = m.follows();
        check(f.makers.count("The Disk King") == 1 && f.makers.count("sunskiefer") == 0, "follow, and unfollow");
        m.markSeen("king-ex65", "1.0.0");
        m.markSeen("king-ex65", "0.9.0");                  // older: ignored
        check(m.follows().seen["king-ex65"] == "1.0.0", "seen keeps the newest version");
        std::string js = tdk::stateJson(m, false);
        check(js.find("\"followed\":[\"The Disk King\"]") != std::string::npos, "the phone page gets the followed makers");
        check(js.find("\"seen\":{\"king-ex65\":\"1.0.0\"}") != std::string::npos, "the phone page gets what was seen");
        m.setFollow("bad\nname", true);
        check(m.follows().makers.count("bad\nname") == 0, "a name with a line break is refused");
        m.stop();
    }
    {
        tdk::Manager m2;                                     // a restart: read back from the drive
        m2.start(plug.string());
        auto f = m2.follows();
        check(f.makers.count("The Disk King") == 1 && f.seen["king-ex65"] == "1.0.0", "kept on the drive across a restart");
        check(fs::exists(drive / "MPC Plugins Manager" / "followed.txt"), "stored in <drive>/MPC Plugins Manager/followed.txt");
        m2.stop();
    }
    std::printf(fails ? "\n%d FAILED\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
