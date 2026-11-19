// MPC Plugins Manager by The Disk King. Copyright (c) 2026 The Disk King. All rights reserved.
// A tiny web server for the phone page, on the MPC's local network only. Every request that reads or
// changes anything needs the 4-digit PIN shown on the MPC's screen; 5 wrong PINs lock it for a minute.
#pragma once
#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include "manager.h"
#include "page.h"

#if !defined(_WIN32)
  #include <cerrno>
  #include <netinet/in.h>
  #include <poll.h>
  #include <sys/socket.h>
  #include <sys/time.h>
  #include <unistd.h>
#endif

namespace tdk {

inline std::string jsonEscape(const std::string& s)
{
    std::string o;
    o.reserve(s.size() + 8);
    for (unsigned char c : s)
    {
        switch (c)
        {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            case '<': o += "\\u003c"; break;     // never closes a script tag
            default:
                if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); o += b; }
                else o += (char)c;
        }
    }
    return o;
}
inline std::string q(const std::string& s) { return "\"" + jsonEscape(s) + "\""; }

inline std::string urlDecode(const std::string& s)
{
    std::string o;
    for (size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] == '+') o += ' ';
        else if (s[i] == '%' && i + 2 < s.size())
        {
            auto hex = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; };
            int a = hex(s[i + 1]), b = hex(s[i + 2]);
            if (a < 0 || b < 0) { o += s[i]; continue; }
            o += (char)(a * 16 + b);
            i += 2;
        }
        else o += s[i];
    }
    return o;
}

inline std::string queryValue(const std::string& query, const std::string& key)
{
    size_t s = 0;
    while (s <= query.size())
    {
        size_t e = query.find('&', s);
        if (e == std::string::npos) e = query.size();
        std::string part = query.substr(s, e - s);
        size_t eq = part.find('=');
        if (eq != std::string::npos && part.substr(0, eq) == key) return urlDecode(part.substr(eq + 1));
        s = e + 1;
    }
    return "";
}

// The JSON the page reads. list=true adds the whole plugin list (sent once, and again when it changes).
inline std::string stateJson(Manager& m, bool list)
{
    Snapshot s = m.snapshot();
    std::string o = "{";
    o += "\"status\":" + q(s.status) + ",\"detail\":" + q(s.detail) + ",\"progress\":" + std::to_string(s.progress);
    o += ",\"busy\":" + std::string(s.busy ? "true" : "false") + ",\"queued\":" + std::to_string(s.queued);
    o += ",\"restart\":" + std::string(s.restartNeeded ? "true" : "false") + ",\"online\":" + std::string(s.online ? "true" : "false");
    o += ",\"error\":" + q(s.lastError) + ",\"generated\":" + q(s.generated) + ",\"drive\":" + q(s.drive);
    o += ",\"gen\":" + q(m.gen2 ? "gen2" : "gen1");
    o += ",\"installed\":{";
    bool first = true;
    for (auto& kv : s.installed) { o += (first ? "" : ",") + q(kv.first) + ":" + q(kv.second); first = false; }
    o += "},\"removed\":[";
    first = true;
    for (auto& r : s.removed) { o += (first ? "" : ",") + q(r); first = false; }
    o += "],\"drives\":[";
    first = true;
    for (auto& d : s.drives) { o += std::string(first ? "" : ",") + "{\"root\":" + q(d.root) + ",\"label\":" + q(d.label) + ",\"current\":" + (d.current ? "true" : "false") + "}"; first = false; }
    o += "]";
    if (list)
    {
        o += ",\"plugins\":[";
        first = true;
        for (auto& e : s.plugins)
        {
            const Build* b = e.pick(m.gen2);
            std::string folders;
            for (auto& f : e.folders()) folders += (folders.empty() ? "" : "|") + f;
            o += std::string(first ? "" : ",") + "{\"id\":" + q(e.id) + ",\"name\":" + q(e.name) + ",\"maker\":" + q(e.maker)
               + ",\"kind\":" + q(e.kind) + ",\"tier\":" + q(e.tier) + ",\"version\":" + q(e.version)
               + ",\"tested\":" + q(e.testedOn) + ",\"dl\":" + std::to_string(e.downloads) + ",\"own\":" + q(e.userFiles)
               + ",\"about\":" + q(e.summary) + ",\"folders\":" + q(folders) + ",\"can\":" + (b ? "true" : "false")
               + ",\"folder\":" + q(b ? b->folder : "") + ",\"style\":" + q(e.style) + ",\"shot\":" + q(e.screenshot)
               + ",\"date\":" + q(e.updated) + "}";
            first = false;
        }
        o += "]";
    }
    return o + "}";
}

#if !defined(_WIN32)

class WebServer
{
public:
    WebServer(Manager& m, std::string pin) : m_(m), pin_(std::move(pin)) {}
    ~WebServer() { stop(); }

    bool start(int port)
    {
        fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (fd_ < 0) return false;
        int one = 1;
        setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_port = htons((uint16_t)port);
        a.sin_addr.s_addr = htonl(INADDR_ANY);
        if (bind(fd_, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0 || listen(fd_, 8) != 0) { close(fd_); fd_ = -1; return false; }
        port_ = port;
        running_ = true;
        thread_ = std::thread([this] { loop(); });
        return true;
    }

    void stop()
    {
        if (!running_.exchange(false)) return;
        if (thread_.joinable()) thread_.join();
        if (fd_ >= 0) close(fd_);
        fd_ = -1;
    }
    int port() const { return port_; }

private:
    void loop()
    {
        while (running_)
        {
            pollfd p{fd_, POLLIN, 0};
            if (poll(&p, 1, 500) <= 0) continue;           // wakes twice a second to notice stop()
            int c = accept(fd_, nullptr, nullptr);
            if (c < 0) continue;
            timeval tv{3, 0};
            setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
            setsockopt(c, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
            handle(c);
            close(c);
        }
    }

    void send(int c, int code, const char* type, const std::string& body)
    {
        const char* text = code == 200 ? "OK" : code == 403 ? "Forbidden" : code == 404 ? "Not Found" : code == 429 ? "Too Many Requests" : "Bad Request";
        std::string h = "HTTP/1.1 " + std::to_string(code) + " " + text + "\r\nContent-Type: " + type
                      + "\r\nContent-Length: " + std::to_string(body.size())
                      + "\r\nCache-Control: no-store\r\nX-Frame-Options: DENY\r\nX-Content-Type-Options: nosniff"
                        "\r\nReferrer-Policy: no-referrer\r\nConnection: close\r\n\r\n";
        std::string all = h + body;
        size_t off = 0;
        while (off < all.size())
        {
            ssize_t n = ::send(c, all.data() + off, all.size() - off, MSG_NOSIGNAL);
            if (n <= 0) return;
            off += (size_t)n;
        }
    }

    void handle(int c)
    {
        std::string req;
        char buf[4096];
        while (req.find("\r\n\r\n") == std::string::npos && req.size() < 16384)
        {
            ssize_t n = recv(c, buf, sizeof buf, 0);
            if (n <= 0) return;
            req.append(buf, (size_t)n);
        }
        size_t sp1 = req.find(' '), sp2 = sp1 == std::string::npos ? sp1 : req.find(' ', sp1 + 1);
        if (sp2 == std::string::npos) return send(c, 400, "text/plain", "bad request");
        std::string method = req.substr(0, sp1), target = req.substr(sp1 + 1, sp2 - sp1 - 1);
        size_t qm = target.find('?');
        std::string path = target.substr(0, qm), query = qm == std::string::npos ? "" : target.substr(qm + 1);

        if (path == "/" || path == "/index.html") return send(c, 200, "text/html; charset=utf-8", kPageHtml);
        if (path.rfind("/api/", 0) != 0) return send(c, 404, "text/plain", "not found");

        auto now = std::chrono::steady_clock::now();
        if (now < lockedUntil_) return send(c, 429, "application/json", "{\"error\":\"Too many wrong PINs. Wait a minute.\"}");
        std::string pin = queryValue(query, "pin");
        size_t hp = req.find("\r\nX-PIN: ");
        if (pin.empty() && hp != std::string::npos) pin = req.substr(hp + 9, req.find("\r\n", hp + 2) - hp - 9);
        if (pin != pin_)
        {
            if (++wrong_ >= 5) { lockedUntil_ = now + std::chrono::seconds(60); wrong_ = 0; }
            return send(c, 403, "application/json", "{\"error\":\"Wrong PIN. Use the 4 digits on your MPC's screen.\"}");
        }
        wrong_ = 0;
        if (path == "/api/state") return send(c, 200, "application/json", stateJson(m_, queryValue(query, "list") == "1"));
        if (path == "/api/do" && method == "POST")
        {
            std::string answer = m_.request(queryValue(query, "op"), queryValue(query, "arg"));
            return send(c, 200, "application/json", "{\"answer\":" + q(answer) + "}");
        }
        send(c, 404, "application/json", "{\"error\":\"unknown\"}");
    }

    Manager& m_;
    std::string pin_;
    std::atomic<bool> running_{false};
    std::thread thread_;
    int fd_ = -1, port_ = 0, wrong_ = 0;
    std::chrono::steady_clock::time_point lockedUntil_{};
};

#else

class WebServer   // computers: no server (the page is only served by the MPC)
{
public:
    WebServer(Manager&, std::string) {}
    bool start(int) { return false; }
    void stop() {}
    int port() const { return 0; }
};

#endif

} // namespace tdk
