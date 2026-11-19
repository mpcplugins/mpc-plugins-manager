// Tests for the manager's core (runs on a computer): SHA-256, the plugin list reader, and unzip.
// Usage: test_core <catalog.tsv> <zip> <expected_dir_listing.txt>
//   prints PASS/FAIL lines; the zip test compares every unpacked file's SHA-256 with a listing made by Python.
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include "../src/core/sha256.h"
#include "../src/core/catalog.h"
#include "../src/core/unzip.h"

static int fails = 0;
static void check(bool ok, const std::string& what) { std::printf("%s %s\n", ok ? "PASS" : "FAIL", what.c_str()); if (!ok) ++fails; }

int main(int argc, char** argv)
{
    {   // FIPS 180-4 test vectors
        tdk::Sha256 a; check(a.hex() == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "sha256 empty");
        tdk::Sha256 b; b.update("abc", 3); check(b.hex() == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "sha256 abc");
        tdk::Sha256 c; std::string m(1000000, 'a'); c.update(m.data(), m.size());
        check(c.hex() == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0", "sha256 million a");
        check(tdk::crc32(0, "123456789", 9) == 0xCBF43926u, "crc32 check value");
    }
    if (argc > 1)
    {
        std::ifstream f(argv[1], std::ios::binary);
        std::stringstream ss; ss << f.rdbuf();
        std::vector<tdk::Entry> list; std::string gen, err;
        bool ok = tdk::parseCatalog(ss.str(), list, gen, err);
        check(ok && list.size() > 200, "catalog parsed: " + std::to_string(list.size()) + " plugins " + err);
        int pickable = 0;
        for (auto& e : list) if (e.pick(true)) ++pickable;
        check(pickable > 50, "plugins installable by copy on Gen2: " + std::to_string(pickable));
        check(!list.empty() && list[0].tested(), "first plugin is Tested & Working: " + (list.empty() ? std::string() : list[0].name));
    }
    if (argc > 3)
    {
        std::map<std::string, std::string> want;
        std::ifstream l(argv[3]);
        std::string name, digest;
        while (std::getline(l, name) && std::getline(l, digest)) want[name] = digest;
        tdk::ZipFile z; std::string err;
        bool ok = z.open(argv[2], err);
        check(ok, "zip opened " + err);
        int good = 0, bad = 0;
        for (auto& it : z.items)
        {
            if (it.isDir()) continue;
            std::vector<uint8_t> data;
            if (!z.read(it, data, err)) { ++bad; std::printf("  read failed: %s\n", err.c_str()); continue; }
            tdk::Sha256 s; s.update(data.data(), data.size());
            if (want[it.name] == s.hex()) ++good; else { ++bad; std::printf("  mismatch: %s\n", it.name.c_str()); }
        }
        check(bad == 0 && good == (int)want.size(), "unzip: " + std::to_string(good) + " files identical to Python's, " + std::to_string(bad) + " bad");
        check(!tdk::safeZipPath("../evil") && !tdk::safeZipPath("a/../../b") && !tdk::safeZipPath("/etc/x") && tdk::safeZipPath("A - VST - B/x.so"), "unsafe zip paths refused");
    }
    std::printf(fails ? "FAILED %d\n" : "ALL PASS\n", fails);
    return fails ? 1 : 0;
}
