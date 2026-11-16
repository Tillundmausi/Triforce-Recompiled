// SPDX-License-Identifier: GPL-2.0-or-later
// Frontend settings, stored as a simple key=value file in user/config.ini.
#pragma once

#include <array>
#include <filesystem>
#include <string>

#include <SDL_scancode.h>

namespace TFH {

// Keyboard-bindable 3DS inputs (order is also the order shown in the UI).
enum class Bind : int {
    A, B, X, Y, L, R, ZL, ZR, Start, Select, Up, Down, Left, Right,
    StickUp, StickDown, StickLeft, StickRight, StickSlow,
    Count
};
const char* BindName(Bind b);    // e.g. "A", "Circle Pad Up"
int DefaultScancode(Bind b);     // SDL_Scancode

struct AppConfig {
    // Profile
    std::string name;            // console user name shown in game (max 10 chars)
    bool use_steam_name = true;

    // Graphics
    int resolution_factor = 3;   // 1 = native 3DS (400x240), up to 10
    bool fullscreen = false;
    int layout = 0;              // Settings::LayoutOption
    bool swap_screens = false;
    bool vsync = true;
    int texture_filter = 0;      // Settings::TextureFilter
    int texture_sampling = 0;    // Settings::TextureSampling
    int aspect_ratio = 0;        // Settings::AspectRatio
    bool linear_filter = true;   // smooth output scaling (filter_mode)
    int frame_limit = 100;       // percent, 0 = unlimited
    bool async_shaders = false;

    // Audio
    int volume = 100;            // percent

    // Input
    std::array<int, static_cast<int>(Bind::Count)> keys{};
    bool controller_enabled = true;
    float stick_deadzone = 0.15f;

    // Single-player Doppel switch: one key / controller button taps the three
    // Doppel panels on the touch screen in turn, so switching characters does
    // not need the mouse. Positions are touch-screen pixels (320x240), which
    // does not depend on window size, screen layout or swapped screens.
    bool doppel_switch_enabled = true;
    int doppel_key = SDL_SCANCODE_TAB;      // SDL_Scancode, 0 = none
    int doppel_button = 8;                  // SDL_CONTROLLER_BUTTON_RIGHTSTICK, -1 = none
    std::array<int, 3> doppel_x{236, 236, 236};
    std::array<int, 3> doppel_y{36, 115, 196};

    // Research only (never written to config.ini): force the game's doppel flag while
    // playing, to see whether an online session tolerates doppels. See
    // docs/2PLAYER_DOPPEL.md - experimental, off at every start on purpose.
    bool research_force_doppels = false;

    // Online
    std::string last_join_code;

    // Discord Rich Presence
    bool discord_enabled = true;
    bool discord_show_level = true;
    bool discord_show_lobby = true;
    std::string discord_app_id;  // empty = built-in application ID

    // Setup
    std::string game_path;       // dump chosen in the setup assistant (empty = auto-detect)
    bool setup_done = false;
};

AppConfig LoadConfig(const std::filesystem::path& file);
void SaveConfig(const AppConfig& cfg, const std::filesystem::path& file);

// Pushes the config into Azahar's Settings::values (graphics, audio, input profile).
void ApplyToAzahar(const AppConfig& cfg);

} // namespace TFH
