// SPDX-License-Identifier: GPL-2.0-or-later
// Game window: SDL2 window whose native handle is handed to Azahar's Vulkan
// renderer. All SDL event handling happens on the main thread (the main loop
// routes events here); PollEvents() is called by the renderer from the
// emulation thread and must therefore not touch SDL.
#pragma once

#include <atomic>
#include <cstdint>
#include <string>

#include "core/frontend/emu_window.h"

struct SDL_Window;
union SDL_Event;

namespace TFH {

class SdlEmuWindow final : public Frontend::EmuWindow {
public:
    explicit SdlEmuWindow(bool fullscreen);
    ~SdlEmuWindow() override;

    SdlEmuWindow(const SdlEmuWindow&) = delete;
    SdlEmuWindow& operator=(const SdlEmuWindow&) = delete;

    std::uint32_t WindowId() const;
    // Handles game input (keyboard -> 3DS buttons, mouse -> touch) and window events.
    void HandleEvent(const SDL_Event& e);
    // Releases all held keys/touch (e.g. when the settings window takes focus).
    void ReleaseAllInput();

    bool IsOpen() const { return open.load(); }
    void RequestClose() { open = false; }
    void SetTitle(const std::string& title);
    void ToggleFullscreen();
    bool IsFullscreen() const;
    void Relayout(); // re-apply framebuffer layout after layout settings changed

    // Frontend::EmuWindow
    void PollEvents() override {}

private:
    SDL_Window* window = nullptr;
    std::atomic_bool open{true};
    bool touch_down = false;
    int held_keys[512] = {};
};

} // namespace TFH
