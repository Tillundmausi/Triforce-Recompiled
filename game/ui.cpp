// SPDX-License-Identifier: GPL-2.0-or-later
#include "ui.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_sdlrenderer2.h>

#include "config.h"
#include "steam_net.h"

namespace TFH {

namespace {

int g_capture_bind = -1; // Bind index waiting for a key, -1 = none
bool g_capture_doppel = false; // waiting for the Doppel-switch key
bool g_capture_changed = false;

// Controller list for Settings -> Controls (also used to debug pads that the
// game does not seem to see).
struct PadInfo {
    SDL_Joystick* joy = nullptr;
    std::string name;
    std::string guid;
    bool game_controller = false;
    int axes = 0;
    int buttons = 0;
    int hats = 0;
};
std::vector<PadInfo> g_pad_list;
std::uint64_t g_pad_list_scan = 0;

void ClosePadList() {
    for (auto& p : g_pad_list)
        if (p.joy) SDL_JoystickClose(p.joy);
    g_pad_list.clear();
}

void ScanPadList() {
    ClosePadList();
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        SDL_Joystick* joy = SDL_JoystickOpen(i);
        if (!joy) continue;
        PadInfo info;
        info.joy = joy;
        info.name = SDL_JoystickName(joy) ? SDL_JoystickName(joy) : "?";
        char guid[40] = {};
        SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(joy), guid, sizeof(guid));
        info.guid = guid;
        info.game_controller = SDL_IsGameController(i) == SDL_TRUE;
        info.axes = SDL_JoystickNumAxes(joy);
        info.buttons = SDL_JoystickNumButtons(joy);
        info.hats = SDL_JoystickNumHats(joy);
        g_pad_list.push_back(std::move(info));
    }
}

// What the pad is sending right now, so a user can tell which button is which.
std::string PadLiveState(const PadInfo& p) {
    std::string out;
    for (int b = 0; b < p.buttons; ++b)
        if (SDL_JoystickGetButton(p.joy, b)) out += "B" + std::to_string(b) + " ";
    for (int h = 0; h < p.hats; ++h) {
        const int v = SDL_JoystickGetHat(p.joy, h);
        if (v) out += "hat" + std::to_string(h) + "=" + std::to_string(v) + " ";
    }
    for (int a = 0; a < p.axes; ++a) {
        const int v = SDL_JoystickGetAxis(p.joy, a);
        if (v > 8000 || v < -8000) {
            char buf[48];
            std::snprintf(buf, sizeof(buf), "A%d=%+.2f ", a, static_cast<float>(v) / 32767.0f);
            out += buf;
        }
    }
    return out.empty() ? "(nothing pressed)" : out;
}


constexpr ImVec4 kAccent{0.24f, 0.62f, 0.33f, 1.0f};      // Link green
constexpr ImVec4 kAccentHover{0.30f, 0.72f, 0.40f, 1.0f};
constexpr ImVec4 kGold{0.95f, 0.80f, 0.30f, 1.0f};        // Triforce gold
constexpr ImVec4 kMuted{0.65f, 0.67f, 0.70f, 1.0f};
constexpr ImVec4 kError{0.95f, 0.45f, 0.40f, 1.0f};
constexpr ImVec4 kOk{0.45f, 0.85f, 0.50f, 1.0f};

void ApplyStyle() {
    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 0.0f;
    s.FrameRounding = 5.0f;
    s.TabRounding = 5.0f;
    s.GrabRounding = 5.0f;
    s.FramePadding = {10, 6};
    s.ItemSpacing = {10, 8};
    s.WindowPadding = {18, 16};
    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg] = {0.09f, 0.10f, 0.11f, 1.0f};
    c[ImGuiCol_ChildBg] = {0.12f, 0.13f, 0.14f, 1.0f};
    c[ImGuiCol_Button] = {0.20f, 0.23f, 0.25f, 1.0f};
    c[ImGuiCol_ButtonHovered] = kAccentHover;
    c[ImGuiCol_ButtonActive] = kAccent;
    c[ImGuiCol_Tab] = {0.16f, 0.18f, 0.19f, 1.0f};
    c[ImGuiCol_TabHovered] = kAccentHover;
    c[ImGuiCol_TabSelected] = kAccent;
    c[ImGuiCol_FrameBg] = {0.16f, 0.18f, 0.19f, 1.0f};
    c[ImGuiCol_FrameBgHovered] = {0.22f, 0.25f, 0.27f, 1.0f};
    c[ImGuiCol_CheckMark] = kGold;
    c[ImGuiCol_SliderGrab] = kAccent;
    c[ImGuiCol_SliderGrabActive] = kAccentHover;
    c[ImGuiCol_Header] = {0.20f, 0.23f, 0.25f, 1.0f};
    c[ImGuiCol_HeaderHovered] = kAccentHover;
    c[ImGuiCol_HeaderActive] = kAccent;
    c[ImGuiCol_Separator] = {0.25f, 0.27f, 0.29f, 1.0f};
    c[ImGuiCol_PlotHistogram] = kAccent;
}

void LoadFont() {
    ImGuiIO& io = ImGui::GetIO();
    const char* candidates[] = {
        "C:\\Windows\\Fonts\\segoeui.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
    };
    for (const char* f : candidates) {
        std::error_code ec;
        if (std::filesystem::exists(f, ec) && io.Fonts->AddFontFromFileTTF(f, 18.0f)) return;
    }
    io.Fonts->AddFontDefault();
}

void Hint(const char* text) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

void SectionTitle(const char* t) {
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, kGold);
    ImGui::TextUnformatted(t);
    ImGui::PopStyleColor();
    ImGui::Separator();
}

void Bullet(const char* t) {
    ImGui::Bullet();
    ImGui::TextWrapped("%s", t);
}

bool BigButton(const char* label, float w = 0, float h = 44) {
    ImGui::PushStyleColor(ImGuiCol_Button, kAccent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kAccentHover);
    const bool r = ImGui::Button(label, {w, h});
    ImGui::PopStyleColor(2);
    return r;
}

// Step header with a status tag: [DONE] / [TODO] / [OPTIONAL]
void StepHeader(int n, const char* title, int state /*0 todo, 1 done, 2 optional*/) {
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, kGold);
    ImGui::Text("Step %d  -  %s", n, title);
    ImGui::PopStyleColor();
    ImGui::SameLine();
    if (state == 1) ImGui::TextColored(kOk, "[DONE]");
    else if (state == 2) ImGui::TextColored(kMuted, "[OPTIONAL]");
    else ImGui::TextColored(kError, "[TO DO]");
    ImGui::Separator();
}

ImVec4 CheckColor(const DumpCheck& c) {
    switch (c.state) {
    case DumpCheck::State::Ok: return c.is_tfh ? kOk : kGold;
    case DumpCheck::State::None: return kMuted;
    default: return kError;
    }
}

// ---------------------------------------------------------------- settings pages --
bool DrawGraphics(AppConfig& c, bool running, bool& toggle_fullscreen) {
    bool changed = false;
    SectionTitle("Picture");
    static const char* kRes[] = {"1x  (400x240, original)", "2x  (800x480)",  "3x  (1200x720, ~720p)",
                                 "4x  (1600x960)",          "5x  (2000x1200, ~1080p)", "6x  (2400x1440, ~1440p)",
                                 "7x  (2800x1680)",         "8x  (3200x1920)", "9x  (3600x2160, ~4K)",
                                 "10x (4000x2400)"};
    int res = c.resolution_factor - 1;
    if (ImGui::Combo("Internal resolution", &res, kRes, IM_ARRAYSIZE(kRes))) {
        c.resolution_factor = res + 1;
        changed = true;
    }
    Hint("How sharp the game is rendered. Higher = sharper, needs a faster GPU.");

    changed |= ImGui::Checkbox("Start in fullscreen", &c.fullscreen);
    if (running) {
        ImGui::SameLine();
        if (ImGui::Button("Toggle fullscreen now (F11)")) toggle_fullscreen = true;
    }
    changed |= ImGui::Checkbox("VSync (prevents tearing)", &c.vsync);

    static const char* kAspect[] = {"Default (3DS)", "16:9", "4:3", "21:9", "16:10", "Stretch"};
    changed |= ImGui::Combo("Top screen aspect ratio", &c.aspect_ratio, kAspect, IM_ARRAYSIZE(kAspect));
    changed |= ImGui::Checkbox("Smooth scaling (linear)", &c.linear_filter);
    Hint("Off = sharp pixels when the picture is scaled to the window size.");

    SectionTitle("Screens");
    // Values = Settings::LayoutOption (SeparateWindows is not supported here).
    static const int kLayoutVals[] = {0, 3, 2, 1, 5};
    static const char* kLayoutNames[] = {"Stacked (like a 3DS)", "Side by side", "Large top screen",
                                         "Single screen", "Hybrid"};
    int layout_idx = 0;
    for (int i = 0; i < IM_ARRAYSIZE(kLayoutVals); ++i)
        if (kLayoutVals[i] == c.layout) layout_idx = i;
    if (ImGui::Combo("Layout (F10)", &layout_idx, kLayoutNames, IM_ARRAYSIZE(kLayoutNames))) {
        c.layout = kLayoutVals[layout_idx];
        changed = true;
    }
    changed |= ImGui::Checkbox("Swap screens (F9)", &c.swap_screens);

    SectionTitle("Textures");
    static const char* kFilter[] = {"Off", "Anime4K", "Bicubic", "ScaleForce", "xBRZ", "MMPX"};
    changed |= ImGui::Combo("Texture upscaling", &c.texture_filter, kFilter, IM_ARRAYSIZE(kFilter));
    Hint("Upscales the original textures. A matter of taste - 'Off' is closest to the original.");
    static const char* kSampling[] = {"Game controlled", "Pixelated (nearest)", "Smooth (linear)"};
    changed |= ImGui::Combo("Texture sampling", &c.texture_sampling, kSampling, IM_ARRAYSIZE(kSampling));

    SectionTitle("Performance");
    changed |= ImGui::SliderInt("Game speed (%)", &c.frame_limit, 0, 300, c.frame_limit == 0 ? "Unlimited" : "%d %%");
    Hint("100 % = original speed. Always keep 100 % in online games!");
    changed |= ImGui::Checkbox("Compile shaders asynchronously", &c.async_shaders);
    Hint("Fewer stutters when new effects appear for the first time; effects may be missing for a moment.");
    return changed;
}

bool DrawAudio(AppConfig& c) {
    SectionTitle("Sound");
    return ImGui::SliderInt("Volume", &c.volume, 0, 100, "%d %%");
}

bool DrawDiscord(AppConfig& c, const LauncherModel& m) {
    bool changed = false;
    SectionTitle("Discord Rich Presence");
    changed |= ImGui::Checkbox("Show my game in Discord", &c.discord_enabled);
    Hint("Needs the Discord desktop app running on this PC. Your friends then see what you play.");
    ImGui::BeginDisabled(!c.discord_enabled);
    changed |= ImGui::Checkbox("Show the current area / level", &c.discord_show_level);
    changed |= ImGui::Checkbox("Show online lobby and party size", &c.discord_show_lobby);
    Hint("The lobby code itself is never shown.");
    ImGui::EndDisabled();
    ImGui::Spacing();
    ImGui::TextColored(m.discord_status.rfind("Connected", 0) == 0 ? kOk : kMuted, "Status: %s",
                       m.discord_status.c_str());

    SectionTitle("Level detection");
    ImGui::TextWrapped("The level is detected from the game files that are loaded. "
                       "Levels that are not in the list yet are shown as \"Playing\".");
    if (m.game_running) {
        ImGui::Text("Current level: %s", m.current_level.empty() ? "(not recognised yet)" : m.current_level.c_str());
        ImGui::TextColored(kMuted, "Last loaded file: %s", m.last_game_file.empty() ? "-" : m.last_game_file.c_str());
        ImGui::TextColored(kMuted, "Different files loaded: %d", static_cast<int>(m.game_files_seen));
    } else {
        ImGui::TextColored(kMuted, "Start the game to see the detection live.");
    }
    if (ImGui::TreeNode("Help add level names")) {
        ImGui::TextWrapped("Every game file is written to this log the first time it is loaded:");
        ImGui::TextColored(kGold, "%s", m.game_files_log.c_str());
        ImGui::TextWrapped("Play a few areas and write down roughly when you entered each one "
                           "(the log shows seconds since the game started). Share the log and your notes "
                           "on GitHub so the level names can be added.");
        ImGui::TreePop();
    }

    SectionTitle("Advanced");
    static char id_buf[32] = {};
    static bool id_init = false;
    if (!id_init) {
        std::snprintf(id_buf, sizeof(id_buf), "%s", c.discord_app_id.c_str());
        id_init = true;
    }
    ImGui::SetNextItemWidth(260);
    if (ImGui::InputTextWithHint("Discord application ID", "built-in", id_buf, sizeof(id_buf),
                                 ImGuiInputTextFlags_CharsDecimal)) {
        c.discord_app_id = id_buf;
        changed = true;
    }
    Hint("Leave empty to use the official Triforce Recompiled application.");
    return changed;
}

bool DrawControls(AppConfig& c, const LauncherModel& m, LauncherActions& a) {
    bool changed = false;
    SectionTitle("Keyboard");
    ImGui::TextColored(kMuted, "Click a key, then press the new key (Esc = cancel).");
    if (ImGui::BeginTable("binds", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("3DS", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, 220.0f);
        for (int i = 0; i < static_cast<int>(Bind::Count); ++i) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(BindName(static_cast<Bind>(i)));
            ImGui::TableSetColumnIndex(1);
            ImGui::PushID(i);
            const char* key_name = SDL_GetScancodeName(static_cast<SDL_Scancode>(c.keys[i]));
            std::string label = g_capture_bind == i ? std::string("... press a key ...")
                                                    : (key_name && *key_name ? key_name : "(none)");
            if (ImGui::Button(label.c_str(), {-1, 0})) g_capture_bind = i;
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (g_capture_changed) {
        g_capture_changed = false;
        changed = true;
    }
    if (ImGui::Button("Reset keyboard to defaults")) {
        for (int i = 0; i < static_cast<int>(Bind::Count); ++i) c.keys[i] = DefaultScancode(static_cast<Bind>(i));
        changed = true;
    }

    SectionTitle("Controller");
    changed |= ImGui::Checkbox("Use controllers (Xbox, PlayStation, ...)", &c.controller_enabled);
    ImGui::TextColored(kMuted, "Buttons are mapped by position like on a 3DS: right = A, bottom = B, top = X, left = Y.");
    float dz = c.stick_deadzone * 100.0f;
    if (ImGui::SliderFloat("Stick deadzone", &dz, 0.0f, 50.0f, "%.0f %%")) {
        c.stick_deadzone = dz / 100.0f;
        changed = true;
    }
    // A pad that works everywhere else but not here is usually a pad SDL does
    // not know. Listing them takes the guesswork out of such reports.
    if (SDL_GetTicks64() - g_pad_list_scan > 1000) {
        g_pad_list_scan = SDL_GetTicks64();
        ScanPadList();
    }
    ImGui::Spacing();
    SectionTitle("Detected controllers");
    if (g_pad_list.empty()) {
        ImGui::TextColored(kMuted, "None. Connect the controller (and start the emulator app), then wait a second.");
    } else {
        bool any_generic = false;
        for (const auto& p : g_pad_list) {
            ImGui::PushID(p.joy);
            ImGui::Text("%s", p.name.c_str());
            ImGui::SameLine();
            if (p.game_controller) {
                ImGui::TextColored(kOk, "[recognised]");
            } else {
                ImGui::TextColored(kGold, "[generic pad]");
                any_generic = true;
            }
            ImGui::TextColored(kMuted, "  %d axes, %d buttons, %d hats - GUID %s", p.axes, p.buttons, p.hats,
                               p.guid.c_str());
            ImGui::TextColored(kMuted, "  Now: %s", PadLiveState(p).c_str());
            ImGui::PopID();
        }
        if (any_generic) {
            Hint("A 'generic pad' is not known to SDL. It still works with the usual Xbox layout "
                 "(bottom button = B, right button = A, ...). If the buttons are wrong, add a mapping "
                 "line for its GUID to user/gamecontrollerdb.txt and restart.");
        }
        if (ImGui::Button("Copy controller info")) {
            std::string text = "Triforce Recompiled - controllers\n";
            for (const auto& p : g_pad_list)
                text += p.name + " | " + (p.game_controller ? "SDL game controller" : "generic pad") + " | "
                        + std::to_string(p.axes) + " axes, " + std::to_string(p.buttons) + " buttons, "
                        + std::to_string(p.hats) + " hats | GUID " + p.guid + "\n";
            SDL_SetClipboardText(text.c_str());
        }
    }

    SectionTitle("Doppel switch (single player)");
    changed |= ImGui::Checkbox("Switch characters with one button", &c.doppel_switch_enabled);
    ImGui::TextColored(kMuted, "In single player the game hands control to whichever Doppel panel you tap. "
                               "With this on, one key / button taps the three panels in turn.");
    ImGui::Spacing();

    {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Key");
        ImGui::SameLine(170);
        const char* key_name = SDL_GetScancodeName(static_cast<SDL_Scancode>(c.doppel_key));
        const std::string label = g_capture_doppel ? std::string("... press a key ...")
                                                   : (key_name && *key_name ? key_name : "(none)");
        if (ImGui::Button(label.c_str(), {220, 0})) g_capture_doppel = true;

        bool clash = false;
        for (int i = 0; i < static_cast<int>(Bind::Count); ++i) clash = clash || c.keys[i] == c.doppel_key;
        if (clash && c.doppel_switch_enabled)
            ImGui::TextColored(kError, "That key also triggers a 3DS button - pick another one.");
    }
    {
        struct Option {
            const char* name;
            int value;
        };
        static const Option kOptions[] = {
            {"None", -1},
            {"Right stick click", SDL_CONTROLLER_BUTTON_RIGHTSTICK},
            {"Left stick click", SDL_CONTROLLER_BUTTON_LEFTSTICK},
            {"Left shoulder", SDL_CONTROLLER_BUTTON_LEFTSHOULDER},
            {"Right shoulder", SDL_CONTROLLER_BUTTON_RIGHTSHOULDER},
        };
        constexpr int kCount = static_cast<int>(sizeof(kOptions) / sizeof(kOptions[0]));
        int current = 0;
        for (int i = 0; i < kCount; ++i)
            if (kOptions[i].value == c.doppel_button) current = i;
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Controller button");
        ImGui::SameLine(170);
        ImGui::SetNextItemWidth(220);
        if (ImGui::BeginCombo("##doppel_button", kOptions[current].name)) {
            for (int i = 0; i < kCount; ++i) {
                const bool selected = i == current;
                if (ImGui::Selectable(kOptions[i].name, selected)) {
                    c.doppel_button = kOptions[i].value;
                    changed = true;
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    }

    ImGui::Spacing();
    ImGui::TextColored(kMuted, "Positions of the three panels on the touch screen (0-319 / 0-239). "
                               "Tap a panel in the game with the mouse, then press \"Use last tap\".");
    for (int i = 0; i < 3; ++i) {
        ImGui::PushID(400 + i);
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Panel %d", i + 1);
        ImGui::SameLine(90);
        ImGui::SetNextItemWidth(70);
        if (ImGui::DragInt("x", &c.doppel_x[i], 1, 0, 319)) changed = true;
        ImGui::SameLine();
        ImGui::SetNextItemWidth(70);
        if (ImGui::DragInt("y", &c.doppel_y[i], 1, 0, 239)) changed = true;
        ImGui::SameLine();
        char tap_label[64];
        std::snprintf(tap_label, sizeof(tap_label), "Use last tap (%d, %d)", m.last_touch[0],
                      m.last_touch[1]);
        ImGui::BeginDisabled(!m.last_touch_valid);
        if (ImGui::Button(tap_label)) {
            c.doppel_x[i] = m.last_touch[0];
            c.doppel_y[i] = m.last_touch[1];
            changed = true;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!m.game_running);
        if (ImGui::Button("Tap now")) a.doppel_test = i;
        ImGui::EndDisabled();
        ImGui::PopID();
    }
    Hint("The defaults are the panels of the normal in-game screen (right column). If a tap lands "
         "somewhere else, record the three spots with the mouse once - that also covers other screen "
         "layouts and swapped screens. Multiplayer uses the same panels, so turn this off when you "
         "play online.");

    SectionTitle("Research: doppel in online sessions");
    const Research::Snapshot& r = m.research;
    if (!m.game_running) {
        ImGui::TextColored(kMuted, "Start the game to see its session state.");
    } else {
        ImGui::Text("DoppelsEnabled %d   ActiveLink %d   Level %u   Stage %u   HostData 0x%08X",
                    r.doppels_enabled, r.active_link, r.level_id, r.stage_id, r.host_data);
        for (int i = 0; i < 3; ++i)
            ImGui::Text("Hero %d: %8.2f %8.2f %8.2f", i + 1, r.pos[i][0], r.pos[i][1], r.pos[i][2]);
        if (ImGui::Button("Copy session state")) {
            char buf[512];
            std::snprintf(buf, sizeof(buf),
                          "DoppelsEnabled=%d ActiveLink=%d Level=%u Stage=%u HostData=0x%08X\n"
                          "Hero1=%.2f,%.2f,%.2f Hero2=%.2f,%.2f,%.2f Hero3=%.2f,%.2f,%.2f",
                          r.doppels_enabled, r.active_link, r.level_id, r.stage_id, r.host_data,
                          r.pos[0][0], r.pos[0][1], r.pos[0][2], r.pos[1][0], r.pos[1][1],
                          r.pos[1][2], r.pos[2][0], r.pos[2][1], r.pos[2][2]);
            SDL_SetClipboardText(buf);
        }
        ImGui::SameLine();
        ImGui::TextColored(kMuted, "Hero 1/2/3 = green/blue/red, positions in world units");
    }
    changed |= ImGui::Checkbox("Force DoppelsEnabled = 1 (experimental)", &c.research_force_doppels);
    Hint("Reads the game's session state (only the supported EUR build is mapped) and, while checked, "
         "writes the doppel flag every frame. Meant for the experiment in docs/2PLAYER_DOPPEL.md: set it "
         "in an online session to see whether the game keeps the three bodies apart. It does not create "
         "the missing third body, and the session may desync - turn it off when you are done.");

    SectionTitle("Other keys");
    ImGui::TextColored(kMuted, "Esc = open this launcher   F11 = fullscreen   F10 = layout   F9 = swap screens");
    ImGui::TextColored(kMuted, "Mouse = touch screen (bottom screen)");
    return changed;
}

void DrawOnline(SteamNet& steam, AppConfig& cfg, bool running) {
    SectionTitle("Online co-op via Steam");
    if (!steam.Available()) {
        ImGui::TextColored(kError, "Steam is not connected.");
        ImGui::TextWrapped("Start Steam, then restart Triforce Recompiled. Online play uses Steam lobbies "
                           "(invite friends or join with a code).");
        return;
    }
    ImGui::Text("Signed in as: %s", steam.PersonaName().c_str());

    const bool busy = steam.Busy();
    if (!steam.InLobby()) {
        ImGui::BeginDisabled(busy);
        if (BigButton("Create lobby (friends)", 260)) steam.CreateLobby(false);
        ImGui::SameLine();
        if (ImGui::Button("Create public lobby", {260, 44})) steam.CreateLobby(true);

        ImGui::Spacing();
        static char code[16] = {};
        if (code[0] == 0 && !cfg.last_join_code.empty())
            std::strncpy(code, cfg.last_join_code.c_str(), sizeof(code) - 1);
        ImGui::SetNextItemWidth(160);
        ImGui::InputTextWithHint("##code", "CODE", code, sizeof(code), ImGuiInputTextFlags_CharsUppercase);
        ImGui::SameLine();
        if (ImGui::Button("Join with code")) {
            cfg.last_join_code = code;
            steam.JoinByCode(code);
        }

        ImGui::Spacing();
        if (ImGui::Button("Find open lobbies")) steam.RefreshLobbyList();
        ImGui::EndDisabled();
        const auto list = steam.LobbyList();
        if (!list.empty() && ImGui::BeginTable("lobbies", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
            ImGui::TableSetupColumn("Host");
            ImGui::TableSetupColumn("Players", ImGuiTableColumnFlags_WidthFixed, 80);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 110);
            ImGui::TableHeadersRow();
            for (const auto& l : list) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(l.host.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%d / %d", l.members, l.max_members);
                ImGui::TableSetColumnIndex(2);
                ImGui::PushID(static_cast<int>(l.id & 0x7fffffff));
                if (ImGui::Button("Join")) steam.JoinLobby(l.id);
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    } else {
        const std::string code = steam.LobbyCode();
        ImGui::Text("Lobby code:");
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, kGold);
        ImGui::TextUnformatted(code.c_str());
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::SmallButton("Copy")) ImGui::SetClipboardText(code.c_str());

        if (ImGui::Button("Invite friends (Steam overlay)")) steam.InviteFriends();
        ImGui::SameLine();
        if (ImGui::Button("Leave lobby")) steam.LeaveLobby();

        const auto members = steam.Members();
        ImGui::Text("Players (%d / 3):", static_cast<int>(members.size()));
        if (ImGui::BeginTable("members", 3, ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Name");
            ImGui::TableSetupColumn("Role", ImGuiTableColumnFlags_WidthFixed, 90);
            ImGui::TableSetupColumn("Ping", ImGuiTableColumnFlags_WidthFixed, 90);
            for (const auto& m : members) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%s%s", m.name.c_str(), m.is_self ? "  (you)" : "");
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(m.is_owner ? "Host" : "");
                ImGui::TableSetColumnIndex(2);
                if (m.is_self) ImGui::TextUnformatted("-");
                else if (m.ping_ms >= 0) ImGui::Text("%d ms", m.ping_ms);
                else ImGui::TextColored(kMuted, "connecting ...");
            }
            ImGui::EndTable();
        }
        ImGui::Spacing();
        ImGui::TextWrapped(running ? "In the game: Main menu -> Multiplayer -> Local Play. One player creates the "
                                     "group, the others join it."
                                   : "When everyone is in the lobby: press PLAY, then choose Multiplayer -> Local Play "
                                     "in the game. One player creates the group, the others join it.");
        ImGui::TextColored(kMuted, "Keep this launcher open while playing online - it hosts the connection.");
    }
    const std::string st = steam.Status();
    if (!st.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(kMuted, "%s", st.c_str());
    }
}

void DrawProfile(AppConfig& c, SteamNet& steam, bool& changed) {
    SectionTitle("Profile");
    const bool steam_ok = steam.Available();
    if (steam_ok) changed |= ImGui::Checkbox("Use my Steam name", &c.use_steam_name);
    if (steam_ok && c.use_steam_name) {
        ImGui::Text("Name in game: %s", steam.PersonaName().substr(0, 10).c_str());
    } else {
        char buf[32] = {};
        std::strncpy(buf, c.name.c_str(), sizeof(buf) - 1);
        ImGui::SetNextItemWidth(220);
        if (ImGui::InputText("Name in game", buf, 11)) {
            c.name = buf;
            changed = true;
        }
    }
    ImGui::TextColored(kMuted, "Max. 10 characters (3DS limit). Takes effect the next time the game starts.");
}

void DrawGameStatus(const LauncherModel& m) {
    if (m.dump_path.empty()) {
        ImGui::TextColored(kError, "No game file selected - open the Setup tab.");
        return;
    }
    ImGui::TextWrapped("%s", m.dump_path.c_str());
    if (m.checking) {
        ImGui::TextColored(kMuted, "Checking file ...");
        return;
    }
    ImGui::PushStyleColor(ImGuiCol_Text, CheckColor(m.check));
    ImGui::TextWrapped("%s", m.check.message.c_str());
    ImGui::PopStyleColor();
}

void DrawSetup(LauncherModel& m, LauncherActions& a, bool& go_play) {
    AppConfig& cfg = *m.cfg;
    ImGui::TextWrapped("Welcome! This assistant explains everything you need to play The Legend of Zelda: "
                       "Tri Force Heroes on your PC. It only takes a few minutes.");

    StepHeader(1, "What you need", 1);
    Bullet("Your own copy of Tri Force Heroes (cartridge or eShop) and a Nintendo 3DS / 2DS with custom "
           "firmware (Luma3DS + GodMode9). Guide: https://3ds.hacks.guide");
    Bullet("Tested version: the European release (CTR-P-EA3P) without update. Other versions may work.");
    Bullet("A Windows 10/11 PC (64-bit) with a graphics card that supports Vulkan (most cards from 2016 on).");
    Bullet("For online co-op: Steam running and signed in. Everyone needs their own copy of the game.");
    ImGui::TextColored(kMuted, "This project contains no Nintendo files. Piracy is not supported.");

    const bool file_ok = m.check.state == DumpCheck::State::Ok;
    StepHeader(2, "Make a decrypted copy of your game", file_ok ? 1 : 0);
    ImGui::TextWrapped("Game files from a 3DS are encrypted. Decrypt them on your own console:");
    ImGui::TextWrapped("1. Copy the game's .cia to the SD card (or install it on the 3DS).");
    ImGui::TextWrapped("2. Hold START while turning the 3DS on - GodMode9 starts.");
    ImGui::TextWrapped("3. Go to [0:] SDCARD, press (A) on the .cia, choose 'CIA image options...' -> "
                       "'Decrypt file'. (Cartridge: [C:] GAMECART -> '.3ds' -> 'Decrypt file'.)");
    ImGui::TextWrapped("4. The decrypted file is in gm9/out/ on the SD card. Copy it to your PC.");

    StepHeader(3, "Select your game file", file_ok ? 1 : 0);
    if (BigButton("Browse ...", 160, 36)) a.browse = true;
    ImGui::SameLine();
    static char path_buf[1024] = {};
    if (path_buf[0] == 0 && !m.dump_path.empty())
        std::strncpy(path_buf, m.dump_path.c_str(), sizeof(path_buf) - 1);
    ImGui::SetNextItemWidth(-130);
    ImGui::InputTextWithHint("##path", "or paste the full path to the file here", path_buf, sizeof(path_buf));
    ImGui::SameLine();
    if (ImGui::Button("Use path", {120, 36})) {
        a.use_path = true;
        a.path_input = path_buf;
    }
    ImGui::TextColored(kMuted, "Tip: you can also put the file into the 'game' folder next to TriforceRecompiled.exe.");
    DrawGameStatus(m);

    const bool need_install = file_ok && m.check.is_cia;
    StepHeader(4, "Install the game", (file_ok && (!m.check.is_cia || m.installed)) ? 1 : 0);
    if (!file_ok) {
        ImGui::TextColored(kMuted, "Select a valid, decrypted game file first.");
    } else if (!m.check.is_cia) {
        ImGui::TextWrapped("Cartridge dumps (.3ds/.cci/.cxi) don't need to be installed.");
    } else if (m.installed) {
        ImGui::TextColored(kOk, "Installed. Save data and settings are stored in: %s", m.user_dir.c_str());
    } else if (m.installing) {
        ImGui::TextUnformatted("Installing ...");
        ImGui::ProgressBar(static_cast<float>(m.install_percent) / 100.0f, {-1, 0});
    } else {
        ImGui::TextWrapped(".cia files are installed once into the emulated 3DS (like on a real console). "
                           "This copies about 550 MB.");
        ImGui::BeginDisabled(!need_install);
        if (BigButton("Install", 160, 36)) a.install = true;
        ImGui::EndDisabled();
    }
    if (!m.install_error.empty()) ImGui::TextColored(kError, "%s", m.install_error.c_str());

    StepHeader(5, "Online co-op (optional)", 2);
    if (m.steam->Available())
        ImGui::TextColored(kOk, "Steam is connected (%s). Use the Online tab to create or join a lobby.",
                           m.steam->PersonaName().c_str());
    else
        ImGui::TextWrapped("Steam is not running. Single player works without it. For online co-op start Steam "
                           "before Triforce Recompiled.");
    ImGui::TextWrapped("Steam will show you as playing 'Spacewar' - that is normal (Valve's free test app ID).");

    const bool ready = file_ok && (!m.check.is_cia || m.installed);
    StepHeader(6, "Ready", ready ? 1 : 0);
    ImGui::BeginDisabled(!ready);
    if (BigButton("Finish setup", 200, 40)) {
        cfg.setup_done = true;
        a.settings_changed = true;
        go_play = true;
    }
    ImGui::EndDisabled();
}

void DrawInfo(const LauncherModel& m) {
    SectionTitle("Triforce Recompiled");
    ImGui::Text("Version: %s", m.version.c_str());
    ImGui::TextWrapped("An unofficial PC version of The Legend of Zelda: Tri Force Heroes (3DS) with modern "
                       "controls, graphics options and online co-op over Steam. Runs on the core of the "
                       "open-source 3DS emulator Azahar (GPLv2+).");
    ImGui::TextWrapped("Source code: https://github.com/Tillundmausi/Triforce-Recompiled");
    SectionTitle("Controls (default)");
    ImGui::TextWrapped("WASD = move (Shift = walk slowly)  |  J = B (sword)  |  K = A  |  U = Y  |  I = X  |  "
                       "Q / E = L / R  |  Enter = Start  |  Backspace = Select  |  Mouse = touch screen");
    ImGui::TextWrapped("Esc = this launcher  |  F11 = fullscreen  |  F10 = screen layout  |  F9 = swap screens");
    SectionTitle("Files");
    ImGui::TextWrapped("User folder (saves, settings, logs): %s", m.user_dir.c_str());
    SectionTitle("Legal");
    ImGui::TextWrapped("The Legend of Zelda and Tri Force Heroes are trademarks of Nintendo. This project is not "
                       "affiliated with or endorsed by Nintendo and contains no game data - you need your own, "
                       "legally obtained copy.");
}

} // namespace

// ---------------------------------------------------------------- Launcher --------
bool Launcher::Create(const std::string& title) {
    window = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1000, 760,
                              SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window) return false;
    // No vsync: the main loop also drives the game window and must not block on present.
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer) {
        SDL_DestroyWindow(window);
        window = nullptr;
        return false;
    }
    ctx = ImGui::CreateContext();
    ImGui::SetCurrentContext(ctx);
    ImGui::GetIO().IniFilename = nullptr;
    ApplyStyle();
    LoadFont();
    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);
    if (const char* forced = std::getenv("TFH_UI_TAB")) SelectTab(forced); // debug aid for screenshots
    return true;
}

void Launcher::Destroy() {
    if (!window) return;
    ImGui::SetCurrentContext(ctx);
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext(ctx);
    ctx = nullptr;
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    renderer = nullptr;
    window = nullptr;
}

std::uint32_t Launcher::WindowId() const { return window ? SDL_GetWindowID(window) : 0; }

void Launcher::BringToFront() {
    if (!window) return;
    SDL_RestoreWindow(window);
    SDL_RaiseWindow(window);
}

void Launcher::Minimize() { if (window) SDL_MinimizeWindow(window); }

void Launcher::SelectTab(const char* name) {
    pending_tab = name;
    pending_frames = 2;
}

static std::uint32_t EventWindowId(const SDL_Event& e) {
    switch (e.type) {
    case SDL_WINDOWEVENT: return e.window.windowID;
    case SDL_KEYDOWN: case SDL_KEYUP: return e.key.windowID;
    case SDL_TEXTINPUT: return e.text.windowID;
    case SDL_TEXTEDITING: return e.edit.windowID;
    case SDL_MOUSEMOTION: return e.motion.windowID;
    case SDL_MOUSEBUTTONDOWN: case SDL_MOUSEBUTTONUP: return e.button.windowID;
    case SDL_MOUSEWHEEL: return e.wheel.windowID;
    default: return 0;
    }
}

bool Launcher::HandleEvent(const SDL_Event& e, AppConfig& cfg) {
    if (!window || EventWindowId(e) != WindowId()) return false;
    if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_CLOSE) {
        close_requested = true;
        return true;
    }
    if (g_capture_doppel && e.type == SDL_KEYDOWN && !e.key.repeat) {
        if (e.key.keysym.scancode != SDL_SCANCODE_ESCAPE) {
            cfg.doppel_key = e.key.keysym.scancode;
            g_capture_changed = true;
        }
        g_capture_doppel = false;
        return true;
    }
    if (g_capture_bind >= 0 && e.type == SDL_KEYDOWN && !e.key.repeat) {
        if (e.key.keysym.scancode != SDL_SCANCODE_ESCAPE) {
            cfg.keys[g_capture_bind] = e.key.keysym.scancode;
            g_capture_changed = true;
        }
        g_capture_bind = -1;
        return true;
    }
    ImGui::SetCurrentContext(ctx);
    ImGui_ImplSDL2_ProcessEvent(&e);
    return true;
}

bool IsCapturingKey() { return g_capture_bind >= 0 || g_capture_doppel; }

LauncherActions Launcher::Frame(LauncherModel& m) {
    LauncherActions a;
    if (!window) return a;
    if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) return a; // nothing to draw

    AppConfig& cfg = *m.cfg;
    SteamNet& steam = *m.steam;
    ImGui::SetCurrentContext(ctx);
    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::Begin("##root", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);

    ImGui::PushStyleColor(ImGuiCol_Text, kGold);
    ImGui::SetWindowFontScale(1.6f);
    ImGui::TextUnformatted("Triforce Recompiled");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleColor();
    ImGui::TextColored(kMuted, "The Legend of Zelda: Tri Force Heroes for PC  -  %s%s", m.version.c_str(),
                       m.game_running ? "  -  game running" : "");
    ImGui::Spacing();

    auto flags = [&](const char* name) {
        return (pending_frames > 0 && pending_tab == name) ? ImGuiTabItemFlags_SetSelected : 0;
    };
    bool go_play = false;
    if (ImGui::BeginTabBar("tabs")) {
        if (ImGui::BeginTabItem("Setup", nullptr, flags("Setup"))) {
            DrawSetup(m, a, go_play);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Play", nullptr, flags("Play"))) {
            DrawProfile(cfg, steam, a.settings_changed);
            SectionTitle("Game");
            DrawGameStatus(m);
            SectionTitle("Online");
            if (steam.InLobby())
                ImGui::Text("In lobby %s with %d player(s).", steam.LobbyCode().c_str(),
                            static_cast<int>(steam.Members().size()));
            else
                ImGui::TextColored(kMuted, steam.Available() ? "Not in a lobby (see the Online tab)."
                                                             : "Steam not connected - single player only.");
            ImGui::Spacing();
            ImGui::Spacing();
            if (m.game_running) {
                ImGui::TextColored(kOk, "The game is running. Press Esc in the game to come back here.");
                if (ImGui::Button("Stop game", {200, 44})) a.stop_game = true;
            } else {
                const bool ready = m.check.state == DumpCheck::State::Ok && (!m.check.is_cia || m.installed);
                ImGui::BeginDisabled(!ready || m.installing);
                if (BigButton("PLAY", 260, 56)) a.play = true;
                ImGui::EndDisabled();
                if (!ready) ImGui::TextColored(kMuted, "Finish the Setup tab first.");
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Online", nullptr, flags("Online"))) {
            DrawOnline(steam, cfg, m.game_running);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Settings", nullptr, flags("Settings"))) {
            bool toggle_fs = false;
            if (ImGui::BeginTabBar("settings_tabs")) {
                if (ImGui::BeginTabItem("Graphics")) {
                    a.settings_changed |= DrawGraphics(cfg, m.game_running, toggle_fs);
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Audio")) {
                    a.settings_changed |= DrawAudio(cfg);
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Controls")) {
                    a.settings_changed |= DrawControls(cfg, m, a);
                    ImGui::EndTabItem();
                }
                const char* sub = std::getenv("TFH_UI_SUBTAB"); // debug aid for screenshots
                if (ImGui::BeginTabItem("Discord", nullptr,
                                        (sub && std::strcmp(sub, "Discord") == 0 && pending_frames > 0)
                                            ? ImGuiTabItemFlags_SetSelected : 0)) {
                    a.settings_changed |= DrawDiscord(cfg, m);
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
            if (toggle_fs) {
                cfg.fullscreen = !cfg.fullscreen;
                a.settings_changed = true;
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Info", nullptr, flags("Info"))) {
            DrawInfo(m);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    if (pending_frames > 0) --pending_frames;
    if (go_play) SelectTab("Play");

    ImGui::End();
    ImGui::Render();
    const ImGuiIO& io = ImGui::GetIO();
    SDL_RenderSetScale(renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
    SDL_SetRenderDrawColor(renderer, 23, 25, 28, 255);
    SDL_RenderClear(renderer);
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
    SDL_RenderPresent(renderer);
    return a;
}

} // namespace TFH
