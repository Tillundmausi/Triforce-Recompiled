// SPDX-License-Identifier: GPL-2.0-or-later
// Dear ImGui launcher (own SDL window + SDL_Renderer, never touches the game's
// Vulkan window). It stays open for the whole session: setup assistant, play,
// online lobby, settings and info. Esc in the game brings it to the front.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "game_file.h"

union SDL_Event;
struct SDL_Window;
struct SDL_Renderer;
struct ImGuiContext;

namespace TFH {

struct AppConfig;
class SteamNet;

// Everything the launcher shows; owned by main.
struct LauncherModel {
    AppConfig* cfg = nullptr;
    SteamNet* steam = nullptr;
    std::string version;
    std::string user_dir;

    std::string dump_path;      // currently selected game file
    DumpCheck check;            // result of validating dump_path
    bool checking = false;      // validation running
    bool installed = false;     // CIA installed into user/sdmc (or not a CIA)

    bool installing = false;
    int install_percent = 0;
    std::string install_error;

    bool game_running = false;

    // Discord Rich Presence
    std::string discord_status;
    std::string current_level;   // empty = not known
    std::string last_game_file;  // most recently loaded game file
    std::size_t game_files_seen = 0;
    std::string game_files_log;  // user/log/game_files.txt

    // Last position the mouse touched on the game's bottom screen (3DS pixels),
    // so the Doppel-panel positions can be picked from the game itself.
    std::array<int, 2> last_touch{};
    bool last_touch_valid = false;
};

// What the user asked for in this frame (handled by main).
struct LauncherActions {
    bool play = false;
    bool stop_game = false;
    bool quit = false;
    bool browse = false;
    bool use_path = false;       // path typed manually -> path_input
    std::string path_input;
    bool install = false;
    bool settings_changed = false;
    int doppel_test = -1;        // tap Doppel panel 0..2 (from the Controls page)
};

class Launcher {
public:
    bool Create(const std::string& title);
    void Destroy();
    bool IsOpen() const { return window != nullptr; }
    std::uint32_t WindowId() const;

    // Routes an SDL event; returns true if it belonged to the launcher.
    bool HandleEvent(const SDL_Event& e, AppConfig& cfg);
    bool CloseRequested() { bool r = close_requested; close_requested = false; return r; }

    LauncherActions Frame(LauncherModel& m);

    void BringToFront();
    void Minimize();
    void SelectTab(const char* name); // e.g. "Settings"

private:
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    ImGuiContext* ctx = nullptr;
    bool close_requested = false;
    std::string pending_tab;
    int pending_frames = 0;
};

// True while the settings page waits for a key press to rebind an input.
bool IsCapturingKey();

} // namespace TFH
