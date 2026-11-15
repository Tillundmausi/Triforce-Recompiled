// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"

#include <algorithm>
#include <fstream>
#include <map>

#include <SDL.h>

#include "any_input.h"
#include "common/settings.h"

namespace TFH {

namespace {
struct BindInfo {
    const char* key;   // ini key
    const char* label; // UI label
    SDL_Scancode def;
};
constexpr BindInfo kBinds[] = {
    {"key_a", "A", SDL_SCANCODE_K},
    {"key_b", "B", SDL_SCANCODE_J},
    {"key_x", "X", SDL_SCANCODE_I},
    {"key_y", "Y", SDL_SCANCODE_U},
    {"key_l", "L", SDL_SCANCODE_Q},
    {"key_r", "R", SDL_SCANCODE_E},
    {"key_zl", "ZL", SDL_SCANCODE_1},
    {"key_zr", "ZR", SDL_SCANCODE_3},
    {"key_start", "Start", SDL_SCANCODE_RETURN},
    {"key_select", "Select", SDL_SCANCODE_BACKSPACE},
    {"key_up", "D-Pad Up", SDL_SCANCODE_UP},
    {"key_down", "D-Pad Down", SDL_SCANCODE_DOWN},
    {"key_left", "D-Pad Left", SDL_SCANCODE_LEFT},
    {"key_right", "D-Pad Right", SDL_SCANCODE_RIGHT},
    {"key_stick_up", "Circle Pad Up", SDL_SCANCODE_W},
    {"key_stick_down", "Circle Pad Down", SDL_SCANCODE_S},
    {"key_stick_left", "Circle Pad Left", SDL_SCANCODE_A},
    {"key_stick_right", "Circle Pad Right", SDL_SCANCODE_D},
    {"key_stick_slow", "Walk slowly (hold)", SDL_SCANCODE_LSHIFT},
};
static_assert(std::size(kBinds) == static_cast<std::size_t>(Bind::Count));

std::string Trim(std::string s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    const auto e = s.find_last_not_of(" \t\r\n");
    return b == std::string::npos ? std::string{} : s.substr(b, e - b + 1);
}
int ToInt(const std::string& s, int def) {
    try { return std::stoi(s); } catch (...) { return def; }
}
float ToFloat(const std::string& s, float def) {
    try { return std::stof(s); } catch (...) { return def; }
}
} // namespace

const char* BindName(Bind b) { return kBinds[static_cast<int>(b)].label; }
int DefaultScancode(Bind b) { return kBinds[static_cast<int>(b)].def; }

AppConfig LoadConfig(const std::filesystem::path& file) {
    AppConfig c;
    for (int i = 0; i < static_cast<int>(Bind::Count); ++i) c.keys[i] = kBinds[i].def;

    std::ifstream in(file);
    std::map<std::string, std::string> kv;
    std::string line;
    while (std::getline(in, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        kv[Trim(line.substr(0, eq))] = Trim(line.substr(eq + 1));
    }
    auto get = [&](const char* k) -> const std::string* {
        auto it = kv.find(k);
        return it == kv.end() ? nullptr : &it->second;
    };
    if (auto v = get("name")) c.name = *v;
    if (auto v = get("use_steam_name")) c.use_steam_name = ToInt(*v, 1) != 0;
    if (auto v = get("resolution_factor")) c.resolution_factor = std::clamp(ToInt(*v, 3), 1, 10);
    if (auto v = get("fullscreen")) c.fullscreen = ToInt(*v, 0) != 0;
    if (auto v = get("layout")) c.layout = std::clamp(ToInt(*v, 0), 0, 6);
    if (auto v = get("swap_screens")) c.swap_screens = ToInt(*v, 0) != 0;
    if (auto v = get("vsync")) c.vsync = ToInt(*v, 1) != 0;
    if (auto v = get("texture_filter")) c.texture_filter = std::clamp(ToInt(*v, 0), 0, 5);
    if (auto v = get("texture_sampling")) c.texture_sampling = std::clamp(ToInt(*v, 0), 0, 2);
    if (auto v = get("aspect_ratio")) c.aspect_ratio = std::clamp(ToInt(*v, 0), 0, 5);
    if (auto v = get("linear_filter")) c.linear_filter = ToInt(*v, 1) != 0;
    if (auto v = get("frame_limit")) c.frame_limit = std::clamp(ToInt(*v, 100), 0, 500);
    if (auto v = get("async_shaders")) c.async_shaders = ToInt(*v, 0) != 0;
    if (auto v = get("volume")) c.volume = std::clamp(ToInt(*v, 100), 0, 100);
    if (auto v = get("controller_enabled")) c.controller_enabled = ToInt(*v, 1) != 0;
    if (auto v = get("stick_deadzone")) c.stick_deadzone = std::clamp(ToFloat(*v, 0.15f), 0.0f, 0.9f);
    if (auto v = get("last_join_code")) c.last_join_code = *v;
    if (auto v = get("discord_enabled")) c.discord_enabled = ToInt(*v, 1) != 0;
    if (auto v = get("discord_show_level")) c.discord_show_level = ToInt(*v, 1) != 0;
    if (auto v = get("discord_show_lobby")) c.discord_show_lobby = ToInt(*v, 1) != 0;
    if (auto v = get("discord_app_id")) c.discord_app_id = *v;
    if (auto v = get("game_path")) c.game_path = *v;
    if (auto v = get("setup_done")) c.setup_done = ToInt(*v, 0) != 0;
    for (int i = 0; i < static_cast<int>(Bind::Count); ++i)
        if (auto v = get(kBinds[i].key)) c.keys[i] = ToInt(*v, kBinds[i].def);
    return c;
}

void SaveConfig(const AppConfig& c, const std::filesystem::path& file) {
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::trunc);
    out << "# Triforce Recompiled - settings (managed by the launcher)\n";
    out << "name=" << c.name << '\n';
    out << "use_steam_name=" << c.use_steam_name << '\n';
    out << "resolution_factor=" << c.resolution_factor << '\n';
    out << "fullscreen=" << c.fullscreen << '\n';
    out << "layout=" << c.layout << '\n';
    out << "swap_screens=" << c.swap_screens << '\n';
    out << "vsync=" << c.vsync << '\n';
    out << "texture_filter=" << c.texture_filter << '\n';
    out << "texture_sampling=" << c.texture_sampling << '\n';
    out << "aspect_ratio=" << c.aspect_ratio << '\n';
    out << "linear_filter=" << c.linear_filter << '\n';
    out << "frame_limit=" << c.frame_limit << '\n';
    out << "async_shaders=" << c.async_shaders << '\n';
    out << "volume=" << c.volume << '\n';
    out << "controller_enabled=" << c.controller_enabled << '\n';
    out << "stick_deadzone=" << c.stick_deadzone << '\n';
    out << "last_join_code=" << c.last_join_code << '\n';
    out << "discord_enabled=" << c.discord_enabled << '\n';
    out << "discord_show_level=" << c.discord_show_level << '\n';
    out << "discord_show_lobby=" << c.discord_show_lobby << '\n';
    out << "discord_app_id=" << c.discord_app_id << '\n';
    out << "game_path=" << c.game_path << '\n';
    out << "setup_done=" << c.setup_done << '\n';
    for (int i = 0; i < static_cast<int>(Bind::Count); ++i) out << kBinds[i].key << '=' << c.keys[i] << '\n';
}

void ApplyToAzahar(const AppConfig& c) {
    auto& v = Settings::values;
    v.graphics_api.SetValue(Settings::GraphicsAPI::Vulkan);
    v.resolution_factor.SetValue(static_cast<u32>(c.resolution_factor));
    v.use_vsync.SetValue(c.vsync);
    v.layout_option.SetValue(static_cast<Settings::LayoutOption>(c.layout));
    v.swap_screen.SetValue(c.swap_screens);
    v.texture_filter.SetValue(static_cast<Settings::TextureFilter>(c.texture_filter));
    v.texture_sampling.SetValue(static_cast<Settings::TextureSampling>(c.texture_sampling));
    v.aspect_ratio.SetValue(static_cast<Settings::AspectRatio>(c.aspect_ratio));
    v.filter_mode.SetValue(c.linear_filter);
    v.frame_limit.SetValue(static_cast<double>(c.frame_limit));
    v.async_shader_compilation.SetValue(c.async_shaders);
    v.volume.SetValue(static_cast<float>(c.volume) / 100.0f);
    Input::ApplyBindings(c);
}

} // namespace TFH
