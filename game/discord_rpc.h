// SPDX-License-Identifier: GPL-2.0-or-later
// Discord Rich Presence over Discord's local IPC (named pipe "discord-ipc-N" on
// Windows, Unix socket elsewhere). No Discord SDK needed. Runs on its own
// thread, reconnects when Discord is started later, and rate-limits updates.
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace TFH {

struct DiscordActivity {
    std::string details;      // first line, e.g. level
    std::string state;        // second line, e.g. "Online lobby"
    std::int64_t start = 0;   // unix seconds, 0 = no timer
    std::string large_image;  // asset key or https URL (empty = none)
    std::string large_text;
    std::string party_id;     // empty = no party
    int party_size = 0, party_max = 0;
    std::string button_label, button_url;

    bool operator==(const DiscordActivity&) const = default;
};

// Builds the SET_ACTIVITY payload (exposed for tests). `activity` == nullptr clears it.
std::string BuildSetActivity(const DiscordActivity* activity, std::uint32_t pid, const std::string& nonce);

class DiscordRpc {
public:
    DiscordRpc() = default;
    ~DiscordRpc();
    DiscordRpc(const DiscordRpc&) = delete;
    DiscordRpc& operator=(const DiscordRpc&) = delete;

    // Starts (or restarts with another application ID). Empty ID = stop.
    void Start(const std::string& application_id);
    void Stop();

    void SetActivity(const DiscordActivity& a);
    void ClearActivity();

    bool Connected() const { return connected; }
    std::string Status() const; // human readable, for the launcher

private:
    void Run(std::string app_id);

    std::thread thread;
    std::atomic_bool running{false};
    std::atomic_bool connected{false};
    mutable std::mutex mutex;
    std::condition_variable wake;
    std::string app_id_running;
    bool has_desired = false;
    DiscordActivity desired;
    std::uint64_t desired_rev = 0;
    std::string status = "Off";
};

} // namespace TFH
