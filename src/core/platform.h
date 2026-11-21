// MPC Plugins Manager by The Disk King. Copyright (c) 2026 The Disk King. All rights reserved.
// The few things that differ between the MPC (Linux) and a test computer: downloading, the network address,
// and a random PIN. On the MPC, downloads use the system's own libcurl (loaded at run time, nothing bundled),
// so HTTPS certificates come from the MPC itself.
#pragma once
#include <cstdio>
#include <functional>
#include <random>
#include <string>

#if !defined(_WIN32)
  #include <dlfcn.h>
  #include <ifaddrs.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <net/if.h>
  #include <sys/stat.h>
#endif

namespace tdk {

inline std::string randomPin()
{
    std::random_device rd;
    return std::to_string(1000 + (int)(rd() % 9000));
}

#if defined(__aarch64__)
inline bool isGen2Device() { return true; }
#else
inline bool isGen2Device() { return false; }   // 32-bit ARM: MPC Live, Live II, One, X, Key, Force
#endif

#if !defined(_WIN32)

// The MPC's address on the local network (Wi-Fi or Ethernet), for the phone page.
inline std::string lanAddress()
{
    std::string best;
    struct ifaddrs* list = nullptr;
    if (getifaddrs(&list) != 0) return "";
    for (struct ifaddrs* a = list; a; a = a->ifa_next)
    {
        if (!a->ifa_addr || a->ifa_addr->sa_family != AF_INET || !(a->ifa_flags & IFF_UP) || (a->ifa_flags & IFF_LOOPBACK)) continue;
        char buf[INET_ADDRSTRLEN];
        auto* in = reinterpret_cast<struct sockaddr_in*>(a->ifa_addr);
        if (!inet_ntop(AF_INET, &in->sin_addr, buf, sizeof buf)) continue;
        std::string ip = buf;
        if (ip.rfind("169.254.", 0) == 0) continue;            // no real network
        if (best.empty() || ip.rfind("192.168.", 0) == 0 || ip.rfind("10.", 0) == 0) best = ip;
    }
    freeifaddrs(list);
    return best;
}

// ---- HTTPS through the MPC's own libcurl (only the handful of calls we need, by their published numbers)
class Curl
{
public:
    static Curl& get() { static Curl c; return c; }
    bool ready() const { return init_ && setopt_ && perform_ && cleanup_; }

    // Downloads url into path. progress(bytesSoFar) is called now and then. False with a reason on failure.
    bool download(const std::string& url, const std::string& path, std::string& error,
                  const std::function<bool(long long)>& progress = nullptr, long long maxBytes = 400LL * 1024 * 1024)
    {
        if (!ready()) { error = "this MPC has no download library (libcurl)"; return false; }
        FILE* f = std::fopen(path.c_str(), "wb");
        if (!f) { error = "could not write to the drive"; return false; }
        Sink sink{f, 0, maxBytes, progress, false};
        void* h = init_();
        if (!h) { std::fclose(f); error = "could not start the download"; return false; }
        char errbuf[256] = {0};
        setopt_(h, 10002, url.c_str());                 // CURLOPT_URL
        setopt_(h, 20011, &Curl::write);                // CURLOPT_WRITEFUNCTION
        setopt_(h, 10001, &sink);                       // CURLOPT_WRITEDATA
        setopt_(h, 52, 1L);                             // CURLOPT_FOLLOWLOCATION (GitHub sends downloads elsewhere)
        setopt_(h, 45, 1L);                             // CURLOPT_FAILONERROR
        setopt_(h, 99, 1L);                             // CURLOPT_NOSIGNAL (safe from a worker thread)
        setopt_(h, 78, 20L);                            // CURLOPT_CONNECTTIMEOUT seconds
        setopt_(h, 19, 512L);                           // CURLOPT_LOW_SPEED_LIMIT bytes/s ...
        setopt_(h, 20, 60L);                            // CURLOPT_LOW_SPEED_TIME ... for 60 s = give up
        setopt_(h, 10018, "MPCPluginsManager/1.0 (The Disk King)");   // CURLOPT_USERAGENT
        setopt_(h, 10010, errbuf);                      // CURLOPT_ERRORBUFFER
        setopt_(h, 181, 2L);                            // CURLOPT_PROTOCOLS = HTTPS only
        setopt_(h, 182, 2L);                            // CURLOPT_REDIR_PROTOCOLS = HTTPS only, redirects included
        for (const char* ca : {"/etc/ssl/certs/ca-certificates.crt", "/etc/pki/tls/certs/ca-bundle.crt", "/etc/ssl/cert.pem"})
        {
            struct stat st;
            if (stat(ca, &st) == 0) { setopt_(h, 10065, ca); break; }   // CURLOPT_CAINFO
        }
        int rc = perform_(h);
        cleanup_(h);
        bool closed = std::fclose(f) == 0;
        if (sink.cancelled) { error = "cancelled"; std::remove(path.c_str()); return false; }
        if (rc != 0 || !closed)
        {
            error = errbuf[0] ? errbuf : (strerror_ ? strerror_(rc) : "download failed");
            std::remove(path.c_str());
            return false;
        }
        return true;
    }

    // Sends a small form (a rating) over HTTPS. Only ever called after the person taps something.
    bool post(const std::string& url, const std::string& body, std::string& error)
    {
        if (!ready()) { error = "no download library"; return false; }
        void* h = init_();
        if (!h) { error = "could not start"; return false; }
        char errbuf[256] = {0};
        Sink sink{nullptr, 0, 65536, nullptr, false};
        setopt_(h, 10002, url.c_str());                 // CURLOPT_URL
        setopt_(h, 47, 1L);                             // CURLOPT_POST
        setopt_(h, 10015, body.c_str());                // CURLOPT_POSTFIELDS
        setopt_(h, 20011, &Curl::discard);              // CURLOPT_WRITEFUNCTION: the answer is not needed
        setopt_(h, 10001, &sink);
        setopt_(h, 45, 1L); setopt_(h, 99, 1L); setopt_(h, 78, 15L); setopt_(h, 13, 30L);   // FAILONERROR, NOSIGNAL, timeouts
        setopt_(h, 181, 2L); setopt_(h, 182, 2L);       // HTTPS only
        setopt_(h, 10018, "MPCPluginsManager/1.2 (The Disk King)");
        setopt_(h, 10010, errbuf);
        for (const char* ca : {"/etc/ssl/certs/ca-certificates.crt", "/etc/pki/tls/certs/ca-bundle.crt", "/etc/ssl/cert.pem"})
        {
            struct stat st;
            if (stat(ca, &st) == 0) { setopt_(h, 10065, ca); break; }
        }
        int rc = perform_(h);
        cleanup_(h);
        if (rc != 0) { error = errbuf[0] ? errbuf : "could not send"; return false; }
        return true;
    }

private:
    struct Sink { FILE* f; long long got, max; std::function<bool(long long)> progress; bool cancelled; };
    static size_t discard(char*, size_t size, size_t n, void*) { return size * n; }

    static size_t write(char* p, size_t size, size_t n, void* user)
    {
        Sink* s = static_cast<Sink*>(user);
        size_t bytes = size * n;
        s->got += (long long)bytes;
        if (s->got > s->max) return 0;                   // too big: stop
        if (s->progress && !s->progress(s->got)) { s->cancelled = true; return 0; }
        return std::fwrite(p, 1, bytes, s->f);
    }

    Curl()
    {
        void* lib = nullptr;
        for (const char* n : {"libcurl.so.4", "libcurl-gnutls.so.4", "libcurl.so"})
            if ((lib = dlopen(n, RTLD_NOW | RTLD_LOCAL))) break;
        if (!lib) return;
        init_ = reinterpret_cast<void* (*)()>(dlsym(lib, "curl_easy_init"));
        setopt_ = reinterpret_cast<int (*)(void*, int, ...)>(dlsym(lib, "curl_easy_setopt"));
        perform_ = reinterpret_cast<int (*)(void*)>(dlsym(lib, "curl_easy_perform"));
        cleanup_ = reinterpret_cast<void (*)(void*)>(dlsym(lib, "curl_easy_cleanup"));
        strerror_ = reinterpret_cast<const char* (*)(int)>(dlsym(lib, "curl_easy_strerror"));
        auto global = reinterpret_cast<int (*)(long)>(dlsym(lib, "curl_global_init"));
        if (global) global(3);                           // CURL_GLOBAL_ALL, once
    }

    void* (*init_)() = nullptr;
    int (*setopt_)(void*, int, ...) = nullptr;
    int (*perform_)(void*) = nullptr;
    void (*cleanup_)(void*) = nullptr;
    const char* (*strerror_)(int) = nullptr;
};

inline bool downloadFile(const std::string& url, const std::string& path, std::string& error,
                         const std::function<bool(long long)>& progress = nullptr)
{
    return Curl::get().download(url, path, error, progress);
}

inline bool postForm(const std::string& url, const std::string& body, std::string& error)
{
    return Curl::get().post(url, body, error);
}

#else   // ---------- test computer: no network; file:// copies a local file so the install steps can be tested

inline std::string lanAddress() { return "127.0.0.1"; }

inline bool postForm(const std::string&, const std::string&, std::string& error) { error = "only on the MPC"; return false; }

inline bool downloadFile(const std::string& url, const std::string& path, std::string& error,
                         const std::function<bool(long long)>& progress = nullptr)
{
    (void)progress;
    if (url.rfind("file://", 0) != 0) { error = "downloads only work on the MPC"; return false; }
    FILE* in = std::fopen(url.substr(7).c_str(), "rb");
    if (!in) { error = "test file missing"; return false; }
    FILE* out = std::fopen(path.c_str(), "wb");
    if (!out) { std::fclose(in); error = "could not write"; return false; }
    char buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, in)) > 0) std::fwrite(buf, 1, n, out);
    std::fclose(in);
    std::fclose(out);
    return true;
}

#endif

} // namespace tdk
