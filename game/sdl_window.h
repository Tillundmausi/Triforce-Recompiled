// SPDX-License-Identifier: GPL-2.0-or-later
// Game window: SDL2 window whose native handle is handed to Azahar's Vulkan
// renderer. All SDL event handling happens on the main thread (the main loop
// routes events here); PollEvents() is called by the renderer from the
// emulation thread and must therefore not touch SDL.
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <utility>

#include "core/frontend/emu_window.h"

struct SDL_Window;
union SDL_Event;

namespace TFH {

// A point on the 3DS touch screen (bottom screen). Always in 3DS pixels
// (0..319 / 0..239), so it survives window resizes, screen layouts and
// swapped screens.
struct TouchPoint {
    int x = 0;
    int y = 0;
};

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

    // Presses the touch screen at p and releases it again after a few frames.
    // Used by the frontend's own shortcuts (e.g. the Doppel switch).
    void TapTouch(const TouchPoint& p);
    // Releases a synthetic tap once it was held long enough; call every loop.
    void UpdateTouch();
    // Last position the mouse touched on the bottom screen.
    TouchPoint LastTouch() const { return last_touch; }
    bool HasLastTouch() const { return has_last_touch; }
    bool HasFocus() const;

    bool IsOpen() const { return open.load(); }
    void RequestClose() { open = false; }
    void SetTitle(const std::string& title);
    void ToggleFullscreen();
    bool IsFullscreen() const;
    void Relayout(); // re-apply framebuffer layout after layout settings changed

    // Frontend::EmuWindow
    void PollEvents() override {}

private:
    // 3DS touch screen <-> framebuffer (window pixel) coordinates.
    std::pair<int, int> ToFramebuffer(const TouchPoint& p) const;
    TouchPoint ToTouchScreen(int framebuffer_x, int framebuffer_y) const;

    SDL_Window* window = nullptr;
    std::atomic_bool open{true};
    bool touch_down = false;
    bool synthetic_touch = false;
    std::chrono::steady_clock::time_point touch_release_at{};
    TouchPoint last_touch{};
    bool has_last_touch = false;
    int held_keys[512] = {};
};

} // namespace TFH
