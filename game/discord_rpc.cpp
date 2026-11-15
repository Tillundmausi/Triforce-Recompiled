// SPDX-License-Identifier: GPL-2.0-or-later
#include "discord_rpc.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace TFH {

namespace {

using Clock = std::chrono::steady_clock;

enum Op : std::uint32_t { kHandshake = 0, kFrame = 1, kClose = 2, kPing = 3, kPong = 4 };

std::string JsonString(std::string_view s) {
    std::string o = "\"";
    for (char ch : s) {
        const unsigned char c = static_cast<unsigned char>(ch);
        switch (c) {
        case '"': o += "\\\""; break;
        case '\\': o += "\\\\"; break;
        case '\n': o += "\\n"; break;
        case '\r': o += "\\r"; break;
        case '\t': o += "\\t"; break;
        default:
            if (c < 0x20) {
                static const char* hx = "0123456789abcdef";
                o += "\\u00";
                o += hx[c >> 4];
                o += hx[c & 15];
            } else {
                o += ch;
            }
        }
    }
    return o + "\"";
}

// Extracts the string value of the first "key":"..." after `from` (minimal, for READY/ERROR).
std::string JsonField(const std::string& json, const char* key, std::size_t from = 0) {
    const std::string pat = std::string("\"") + key + "\"";
    std::size_t p = json.find(pat, from);
    if (p == std::string::npos) return {};
    p = json.find(':', p + pat.size());
    if (p == std::string::npos) return {};
    p = json.find_first_not_of(" \t\r\n", p + 1);
    if (p == std::string::npos || json[p] != '"') return {};
    std::string out;
    for (++p; p < json.size() && json[p] != '"'; ++p) {
        if (json[p] == '\\' && p + 1 < json.size()) ++p;
        out += json[p];
    }
    return out;
}

std::uint32_t ProcessId() {
#ifdef _WIN32
    return static_cast<std::uint32_t>(GetCurrentProcessId());
#else
    return static_cast<std::uint32_t>(getpid());
#endif
}

// ---- transport ------------------------------------------------------------------------------
class Pipe {
public:
    ~Pipe() { Close(); }

    bool Open() {
#ifdef _WIN32
        for (int i = 0; i < 10; ++i) {
            const std::wstring name = L"\\\\.\\pipe\\discord-ipc-" + std::to_wstring(i);
            HANDLE h = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
            if (h != INVALID_HANDLE_VALUE) {
                handle = h;
                return true;
            }
        }
        return false;
#else
        std::vector<std::string> dirs;
        for (const char* env : {"XDG_RUNTIME_DIR", "TMPDIR", "TMP", "TEMP"})
            if (const char* v = std::getenv(env); v && *v) dirs.emplace_back(v);
        dirs.emplace_back("/tmp");
        static const char* kSub[] = {"", "/app/com.discordapp.Discord", "/snap.discord"};
        for (const auto& d : dirs) {
            for (const char* sub : kSub) {
                for (int i = 0; i < 10; ++i) {
                    const std::string path = d + sub + "/discord-ipc-" + std::to_string(i);
                    sockaddr_un addr{};
                    if (path.size() >= sizeof(addr.sun_path)) continue;
                    addr.sun_family = AF_UNIX;
                    std::memcpy(addr.sun_path, path.c_str(), path.size() + 1);
                    const int s = socket(AF_UNIX, SOCK_STREAM, 0);
                    if (s < 0) return false;
                    if (connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
                        fd = s;
                        return true;
                    }
                    close(s);
                }
            }
        }
        return false;
#endif
    }

    void Close() {
#ifdef _WIN32
        if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
        handle = INVALID_HANDLE_VALUE;
#else
        if (fd >= 0) close(fd);
        fd = -1;
#endif
    }

    bool IsOpen() const {
#ifdef _WIN32
        return handle != INVALID_HANDLE_VALUE;
#else
        return fd >= 0;
#endif
    }

    bool Send(std::uint32_t op, const std::string& payload) {
        std::string buf(8 + payload.size(), '\0');
        const std::uint32_t len = static_cast<std::uint32_t>(payload.size());
        for (int i = 0; i < 4; ++i) {
            buf[i] = static_cast<char>((op >> (8 * i)) & 0xFF);
            buf[4 + i] = static_cast<char>((len >> (8 * i)) & 0xFF);
        }
        std::memcpy(buf.data() + 8, payload.data(), payload.size());
        return WriteAll(buf.data(), buf.size());
    }

    // 1 = frame read, 0 = nothing within timeout, -1 = connection lost.
    int Receive(std::uint32_t& op, std::string& data, int timeout_ms) {
        const auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
        for (;;) {
            const int r = Readable();
            if (r < 0) return -1;
            if (r > 0) break;
            if (Clock::now() >= deadline) return 0;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        unsigned char hdr[8];
        if (!ReadAll(hdr, 8)) return -1;
        op = hdr[0] | (hdr[1] << 8) | (hdr[2] << 16) | (std::uint32_t(hdr[3]) << 24);
        const std::uint32_t len = hdr[4] | (hdr[5] << 8) | (hdr[6] << 16) | (std::uint32_t(hdr[7]) << 24);
        if (len > (1u << 20)) return -1;
        data.assign(len, '\0');
        if (len && !ReadAll(data.data(), len)) return -1;
        return 1;
    }

private:
    // >0 data waiting, 0 none, <0 broken
    int Readable() {
#ifdef _WIN32
        DWORD avail = 0;
        if (!PeekNamedPipe(handle, nullptr, 0, nullptr, &avail, nullptr)) return -1;
        return avail >= 8 ? 1 : 0;
#else
        pollfd p{fd, POLLIN, 0};
        const int r = poll(&p, 1, 0);
        if (r < 0) return -1;
        if (r == 0) return 0;
        if (p.revents & (POLLERR | POLLNVAL)) return -1;
        return 1; // POLLIN or POLLHUP (the read then reports the closed socket)
#endif
    }

    bool WriteAll(const void* p, std::size_t n) {
        const char* c = static_cast<const char*>(p);
        while (n) {
#ifdef _WIN32
            DWORD w = 0;
            if (!WriteFile(handle, c, static_cast<DWORD>(n), &w, nullptr) || w == 0) return false;
#else
            const ssize_t w = send(fd, c, n, MSG_NOSIGNAL);
            if (w <= 0) return false;
#endif
            c += w;
            n -= static_cast<std::size_t>(w);
        }
        return true;
    }

    bool ReadAll(void* p, std::size_t n) {
        char* c = static_cast<char*>(p);
        while (n) {
#ifdef _WIN32
            DWORD r = 0;
            if (!ReadFile(handle, c, static_cast<DWORD>(n), &r, nullptr) || r == 0) return false;
#else
            const ssize_t r = recv(fd, c, n, 0);
            if (r <= 0) return false;
#endif
            c += r;
            n -= static_cast<std::size_t>(r);
        }
        return true;
    }

#ifdef _WIN32
    HANDLE handle = INVALID_HANDLE_VALUE;
#else
    int fd = -1;
#endif
};

} // namespace

std::string BuildSetActivity(const DiscordActivity* a, std::uint32_t pid, const std::string& nonce) {
    std::string j = "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":" + std::to_string(pid) + ",\"activity\":";
    if (!a) {
        j += "null";
    } else {
        j += "{\"type\":0";
        // Discord rejects details/state shorter than 2 characters.
        if (a->details.size() >= 2) j += ",\"details\":" + JsonString(a->details.substr(0, 128));
        if (a->state.size() >= 2) j += ",\"state\":" + JsonString(a->state.substr(0, 128));
        if (a->start > 0) j += ",\"timestamps\":{\"start\":" + std::to_string(a->start) + "}";
        if (!a->large_image.empty()) {
            j += ",\"assets\":{\"large_image\":" + JsonString(a->large_image);
            if (a->large_text.size() >= 2) j += ",\"large_text\":" + JsonString(a->large_text.substr(0, 128));
            j += "}";
        }
        if (!a->party_id.empty() && a->party_max > 0) {
            j += ",\"party\":{\"id\":" + JsonString(a->party_id) + ",\"size\":[" + std::to_string(a->party_size) +
                 "," + std::to_string(a->party_max) + "]}";
        }
        if (!a->button_label.empty() && !a->button_url.empty()) {
            j += ",\"buttons\":[{\"label\":" + JsonString(a->button_label.substr(0, 32)) +
                 ",\"url\":" + JsonString(a->button_url) + "}]";
        }
        j += "}";
    }
    j += "},\"nonce\":" + JsonString(nonce) + "}";
    return j;
}

DiscordRpc::~DiscordRpc() { Stop(); }

void DiscordRpc::Start(const std::string& id) {
    if (running && id == app_id_running) return;
    Stop();
    if (id.empty()) {
        std::lock_guard lk(mutex);
        status = "Off";
        return;
    }
    app_id_running = id;
    running = true;
    thread = std::thread([this, id] { Run(id); });
}

void DiscordRpc::Stop() {
    if (!thread.joinable()) return;
    running = false;
    wake.notify_all();
    thread.join();
    app_id_running.clear();
    connected = false;
    std::lock_guard lk(mutex);
    status = "Off";
}

void DiscordRpc::SetActivity(const DiscordActivity& a) {
    std::lock_guard lk(mutex);
    if (has_desired && desired == a) return;
    desired = a;
    has_desired = true;
    ++desired_rev;
    wake.notify_all();
}

void DiscordRpc::ClearActivity() {
    std::lock_guard lk(mutex);
    if (!has_desired) return;
    has_desired = false;
    ++desired_rev;
    wake.notify_all();
}

std::string DiscordRpc::Status() const {
    std::lock_guard lk(mutex);
    return status;
}

void DiscordRpc::Run(std::string app_id) {
    Pipe pipe;
    auto setStatus = [&](std::string s) {
        std::lock_guard lk(mutex);
        status = std::move(s);
    };
    auto disconnect = [&](const char* why) {
        pipe.Close();
        connected = false;
        setStatus(why);
    };

    std::uint64_t sent_rev = ~0ull;
    std::uint64_t nonce = 0;
    auto next_attempt = Clock::now();
    auto last_send = Clock::now() - std::chrono::seconds(60);
    setStatus("Looking for Discord...");

    while (running) {
        const auto now = Clock::now();
        if (!pipe.IsOpen() && now >= next_attempt) {
            next_attempt = now + std::chrono::seconds(10);
            if (!pipe.Open()) {
                setStatus("Discord is not running (checking again every 10 s)");
            } else if (!pipe.Send(kHandshake, "{\"v\":1,\"client_id\":" + JsonString(app_id) + "}")) {
                disconnect("Could not talk to Discord");
            } else {
                std::uint32_t op = 0;
                std::string data;
                const int r = pipe.Receive(op, data, 5000);
                if (r == 1 && op == kFrame && data.find("\"READY\"") != std::string::npos) {
                    const auto user = data.find("\"user\"");
                    std::string name = JsonField(data, "global_name", user == std::string::npos ? 0 : user);
                    if (name.empty()) name = JsonField(data, "username", user == std::string::npos ? 0 : user);
                    connected = true;
                    sent_rev = ~0ull; // (re)send the current presence
                    setStatus(name.empty() ? "Connected to Discord" : "Connected to Discord as " + name);
                } else if (r == 1 && op == kClose) {
                    const std::string msg = JsonField(data, "message");
                    disconnect(("Discord refused the connection" + (msg.empty() ? "" : ": " + msg)).c_str());
                } else {
                    disconnect("Discord did not answer");
                }
            }
        }

        if (connected) {
            // Incoming frames (pings, close, command replies).
            for (;;) {
                std::uint32_t op = 0;
                std::string data;
                const int r = pipe.Receive(op, data, 0);
                if (r == 0) break;
                if (r < 0 || op == kClose) {
                    disconnect("Discord was closed (reconnecting...)");
                    next_attempt = Clock::now() + std::chrono::seconds(5);
                    break;
                }
                if (op == kPing) pipe.Send(kPong, data);
            }
        }

        if (connected) {
            DiscordActivity act;
            bool has = false;
            std::uint64_t rev = 0;
            {
                std::lock_guard lk(mutex);
                act = desired;
                has = has_desired;
                rev = desired_rev;
            }
            // Discord allows about 5 updates per 20 s; stay well below that.
            if (rev != sent_rev && Clock::now() - last_send >= std::chrono::seconds(4)) {
                const std::string payload = BuildSetActivity(has ? &act : nullptr, ProcessId(), std::to_string(++nonce));
                if (pipe.Send(kFrame, payload)) {
                    sent_rev = rev;
                    last_send = Clock::now();
                } else {
                    disconnect("Discord was closed (reconnecting...)");
                    next_attempt = Clock::now() + std::chrono::seconds(5);
                }
            }
        }

        std::unique_lock lk(mutex);
        wake.wait_for(lk, std::chrono::milliseconds(500), [&] { return !running.load(); });
    }

    if (pipe.IsOpen() && connected) pipe.Send(kFrame, BuildSetActivity(nullptr, ProcessId(), "bye"));
    pipe.Close();
    connected = false;
}

} // namespace TFH
