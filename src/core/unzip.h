// MPC Plugins Manager by The Disk King. Copyright (c) 2026 The Disk King. All rights reserved.
// Zip reading and DEFLATE decompression, written for this plugin from the published formats
// (PKWARE APPNOTE for zip, RFC 1951 for deflate). No outside code and no system libraries, so it runs the
// same on the MPC and on a computer for testing. Every unpacked file is checked against its CRC-32.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>
#include "sha256.h"

namespace tdk {

// ---------------------------------------------------------------- inflate (RFC 1951)
class Inflater
{
public:
    // Decompresses `in` (raw deflate) into `out`. Returns false on any malformed data.
    static bool run(const uint8_t* in, size_t inLen, std::vector<uint8_t>& out, size_t expected)
    {
        Inflater z(in, inLen, out);
        out.clear();
        out.reserve(expected);
        try { return z.all() && (expected == 0 || out.size() == expected); }
        catch (...) { return false; }
    }

private:
    struct Huff { uint16_t count[16]; uint16_t symbol[320]; };

    Inflater(const uint8_t* in, size_t n, std::vector<uint8_t>& out) : in_(in), n_(n), out_(out) {}

    int bit()
    {
        if (cnt_ == 0)
        {
            if (pos_ >= n_) throw 1;
            buf_ = in_[pos_++];
            cnt_ = 8;
        }
        int b = buf_ & 1;
        buf_ >>= 1;
        --cnt_;
        return b;
    }
    int bits(int need)
    {
        int v = 0;
        for (int i = 0; i < need; ++i) v |= bit() << i;
        return v;
    }

    static bool build(Huff& h, const uint16_t* lengths, int n)
    {
        std::memset(h.count, 0, sizeof h.count);
        for (int i = 0; i < n; ++i) h.count[lengths[i]]++;
        if (h.count[0] == n) return true;               // no codes: allowed (only for distance trees)
        int left = 1;
        for (int len = 1; len < 16; ++len) { left <<= 1; left -= h.count[len]; if (left < 0) return false; }
        uint16_t offs[16];
        offs[1] = 0;
        for (int len = 1; len < 15; ++len) offs[len + 1] = offs[len] + h.count[len];
        for (int i = 0; i < n; ++i) if (lengths[i]) h.symbol[offs[lengths[i]]++] = (uint16_t)i;
        return true;
    }
    int decode(const Huff& h)
    {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len < 16; ++len)
        {
            code |= bit();
            int count = h.count[len];
            if (code - count < first) return h.symbol[index + (code - first)];
            index += count; first += count; first <<= 1; code <<= 1;
        }
        throw 2;
    }

    bool stored()
    {
        buf_ = 0; cnt_ = 0;                              // to a byte boundary
        if (pos_ + 4 > n_) return false;
        unsigned len = in_[pos_] | in_[pos_ + 1] << 8;
        unsigned nlen = in_[pos_ + 2] | in_[pos_ + 3] << 8;
        pos_ += 4;
        if (len != (~nlen & 0xFFFF) || pos_ + len > n_) return false;
        out_.insert(out_.end(), in_ + pos_, in_ + pos_ + len);
        pos_ += len;
        return true;
    }

    bool codes(const Huff& lit, const Huff& dist)
    {
        static const uint16_t lbase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59,
                                           67, 83, 99, 115, 131, 163, 195, 227, 258};
        static const uint16_t lext[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
        static const uint16_t dbase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769,
                                           1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
        static const uint16_t dext[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
        for (;;)
        {
            int sym = decode(lit);
            if (sym < 256) { out_.push_back((uint8_t)sym); continue; }
            if (sym == 256) return true;
            sym -= 257;
            if (sym >= 29) return false;
            size_t len = lbase[sym] + bits(lext[sym]);
            int ds = decode(dist);
            if (ds >= 30) return false;
            size_t d = dbase[ds] + bits(dext[ds]);
            if (d > out_.size()) return false;
            if (out_.size() + len > kMaxOut) return false;
            size_t from = out_.size() - d;
            for (size_t i = 0; i < len; ++i) out_.push_back(out_[from + i]);
        }
    }

    bool fixed()
    {
        static Huff lit, dist;
        static bool ready = false;
        if (!ready)
        {
            uint16_t l[320];
            int i = 0;
            for (; i < 144; ++i) l[i] = 8;
            for (; i < 256; ++i) l[i] = 9;
            for (; i < 280; ++i) l[i] = 7;
            for (; i < 288; ++i) l[i] = 8;
            build(lit, l, 288);
            for (i = 0; i < 30; ++i) l[i] = 5;
            build(dist, l, 30);
            ready = true;
        }
        return codes(lit, dist);
    }

    bool dynamic()
    {
        static const uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
        int nlen = bits(5) + 257, ndist = bits(5) + 1, ncode = bits(4) + 4;
        if (nlen > 286 || ndist > 30) return false;
        uint16_t lengths[320] = {0};
        for (int i = 0; i < ncode; ++i) lengths[order[i]] = (uint16_t)bits(3);
        Huff lencode, lit, dist;
        if (!build(lencode, lengths, 19)) return false;
        int i = 0;
        while (i < nlen + ndist)
        {
            int sym = decode(lencode);
            if (sym < 16) { lengths[i++] = (uint16_t)sym; continue; }
            int len = 0, rep;
            if (sym == 16) { if (i == 0) return false; len = lengths[i - 1]; rep = 3 + bits(2); }
            else if (sym == 17) rep = 3 + bits(3);
            else rep = 11 + bits(7);
            if (i + rep > nlen + ndist) return false;
            while (rep--) lengths[i++] = (uint16_t)len;
        }
        if (lengths[256] == 0) return false;
        if (!build(lit, lengths, nlen)) return false;
        if (!build(dist, lengths + nlen, ndist)) return false;
        return codes(lit, dist);
    }

    bool all()
    {
        int last;
        do
        {
            last = bit();
            int type = bits(2);
            bool ok = type == 0 ? stored() : type == 1 ? fixed() : type == 2 ? dynamic() : false;
            if (!ok) return false;
        } while (!last);
        return true;
    }

    static constexpr size_t kMaxOut = 512u * 1024 * 1024;   // no single file larger than 512 MB
    const uint8_t* in_;
    size_t n_, pos_ = 0;
    int buf_ = 0, cnt_ = 0;
    std::vector<uint8_t>& out_;
};

// ---------------------------------------------------------------- zip
struct ZipItem
{
    std::string name;
    uint32_t crc = 0;
    uint64_t packed = 0, size = 0, headerOffset = 0;
    uint16_t method = 0, flags = 0;
    bool isDir() const { return !name.empty() && name.back() == '/'; }
};

class ZipFile
{
public:
    // Reads the zip's table of contents. False if the file is not a zip we can read.
    bool open(const std::string& path, std::string& error)
    {
        f_ = std::fopen(path.c_str(), "rb");
        if (!f_) { error = "could not open the download"; return false; }
        std::fseek(f_, 0, SEEK_END);
        long long size = std::ftell(f_);
        if (size < 22) { error = "the download is not a zip file"; return false; }
        size_t tail = (size_t)(size < 66000 ? size : 66000);
        std::vector<uint8_t> t(tail);
        std::fseek(f_, (long)(size - (long long)tail), SEEK_SET);
        if (std::fread(t.data(), 1, tail, f_) != tail) { error = "could not read the download"; return false; }
        long eocd = -1;
        for (long i = (long)tail - 22; i >= 0; --i)
            if (u32(&t[(size_t)i]) == 0x06054b50) { eocd = i; break; }
        if (eocd < 0) { error = "the download is not a zip file"; return false; }
        const uint8_t* e = &t[(size_t)eocd];
        uint16_t count = u16(e + 10);
        uint32_t cdSize = u32(e + 12), cdOff = u32(e + 16);
        if (cdOff == 0xFFFFFFFF || count == 0xFFFF) { error = "this zip format (zip64) is not supported"; return false; }
        if ((long long)cdOff + cdSize > size) { error = "the zip file is damaged"; return false; }
        std::vector<uint8_t> cd(cdSize);
        std::fseek(f_, (long)cdOff, SEEK_SET);
        if (std::fread(cd.data(), 1, cdSize, f_) != cdSize) { error = "could not read the zip contents"; return false; }
        size_t p = 0;
        for (uint16_t i = 0; i < count; ++i)
        {
            if (p + 46 > cd.size() || u32(&cd[p]) != 0x02014b50) { error = "the zip contents are damaged"; return false; }
            ZipItem it;
            it.flags = u16(&cd[p + 8]); it.method = u16(&cd[p + 10]); it.crc = u32(&cd[p + 16]);
            it.packed = u32(&cd[p + 20]); it.size = u32(&cd[p + 24]);
            uint16_t nl = u16(&cd[p + 28]), xl = u16(&cd[p + 30]), cl = u16(&cd[p + 32]);
            it.headerOffset = u32(&cd[p + 42]);
            if (p + 46 + nl > cd.size()) { error = "the zip contents are damaged"; return false; }
            it.name.assign((const char*)&cd[p + 46], nl);
            for (char& ch : it.name) if (ch == '\\') ch = '/';
            items.push_back(it);
            p += 46 + (size_t)nl + xl + cl;
        }
        return true;
    }

    // Unpacks one item into memory and checks its CRC.
    bool read(const ZipItem& it, std::vector<uint8_t>& out, std::string& error)
    {
        if (it.flags & 1) { error = it.name + " is encrypted"; return false; }
        uint8_t lh[30];
        std::fseek(f_, (long)it.headerOffset, SEEK_SET);
        if (std::fread(lh, 1, 30, f_) != 30 || u32(lh) != 0x04034b50) { error = it.name + ": damaged entry"; return false; }
        long dataAt = (long)it.headerOffset + 30 + u16(lh + 26) + u16(lh + 28);
        std::vector<uint8_t> packed((size_t)it.packed);
        std::fseek(f_, dataAt, SEEK_SET);
        if (it.packed && std::fread(packed.data(), 1, (size_t)it.packed, f_) != it.packed) { error = it.name + ": truncated"; return false; }
        if (it.method == 0) out.swap(packed);
        else if (it.method == 8)
        {
            if (!Inflater::run(packed.data(), packed.size(), out, (size_t)it.size)) { error = it.name + ": could not decompress"; return false; }
        }
        else { error = it.name + ": unsupported compression"; return false; }
        if (out.size() != it.size || crc32(0, out.data(), out.size()) != it.crc) { error = it.name + ": failed its CRC check"; return false; }
        return true;
    }

    ~ZipFile() { if (f_) std::fclose(f_); }
    std::vector<ZipItem> items;

private:
    static uint16_t u16(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }
    static uint32_t u32(const uint8_t* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
    FILE* f_ = nullptr;
};

// A zip path is safe only if it stays inside the folder it is unpacked to.
inline bool safeZipPath(const std::string& p)
{
    if (p.empty() || p[0] == '/' || p.find(':') != std::string::npos) return false;
    size_t s = 0;
    while (s <= p.size())
    {
        size_t e = p.find('/', s);
        if (e == std::string::npos) e = p.size();
        std::string part = p.substr(s, e - s);
        if (part == "..") return false;
        s = e + 1;
    }
    return true;
}

} // namespace tdk
