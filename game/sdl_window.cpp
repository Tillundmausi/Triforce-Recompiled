// SPDX-License-Identifier: GPL-2.0-or-later
#include "sdl_window.h"

#include <algorithm>
#include <stdexcept>
#include <string>

#include <SDL.h>
#include <SDL_syswm.h>

#include "common/logging/log.h"
#include "core/3ds.h"
#include "input_common/keyboard.h"
#include "input_common/main.h"

namespace TFH {

SdlEmuWindow::SdlEmuWindow(bool fullscreen) {
    // Default size: both 3DS screens stacked, scaled x2 (400x480 -> 800x960),
    // but never taller than the usable desktop area.
    int w = Core::kScreenTopWidth * 2;
    int h = (Core::kScreenTopHeight + Core::kScreenBottomHeight) * 2;
    SDL_Rect usable{};
    if (SDL_GetDisplayUsableBounds(0, &usable) == 0 && h > usable.h - 60) {
        const float s = float(usable.h - 60) / float(h);
        w = int(w * s);
        h = int(h * s);
    }

    Uint32 flags = SDL_WINDOW_RESIZABLE;
    if (fullscreen) flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    window = SDL_CreateWindow("The Legend of Zelda: Tri Force Heroes", SDL_WINDOWPOS_CENTERED,
                              SDL_WINDOWPOS_CENTERED, w, h, flags);
    if (!window) throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
    SDL_SetWindowMinimumSize(window, Core::kScreenTopWidth, Core::kScreenTopHeight + Core::kScreenBottomHeight);

    SDL_SysWMinfo wm;
    SDL_VERSION(&wm.version);
    if (!SDL_GetWindowWMInfo(window, &wm))
        throw std::runtime_error(std::string("SDL_GetWindowWMInfo failed: ") + SDL_GetError());

    switch (wm.subsystem) {
#ifdef SDL_VIDEO_DRIVER_WINDOWS
    case SDL_SYSWM_WINDOWS:
        window_info.type = Frontend::WindowSystemType::Windows;
        window_info.render_surface = reinterpret_cast<void*>(wm.info.win.window);
        break;
#endif
#ifdef SDL_VIDEO_DRIVER_X11
    case SDL_SYSWM_X11:
        window_info.type = Frontend::WindowSystemType::X11;
        window_info.display_connection = wm.info.x11.display;
        window_info.render_surface = reinterpret_cast<void*>(wm.info.x11.window);
        break;
#endif
#ifdef SDL_VIDEO_DRIVER_WAYLAND
    case SDL_SYSWM_WAYLAND:
        window_info.type = Frontend::WindowSystemType::Wayland;
        window_info.display_connection = wm.info.wl.display;
        window_info.render_surface = wm.info.wl.surface;
        break;
#endif
    default:
        throw std::runtime_error("unsupported window system (subsystem " +
                                 std::to_string(int(wm.subsystem)) + ")");
    }
    window_info.render_surface_scale = 1.0f;
    Relayout();
}

SdlEmuWindow::~SdlEmuWindow() {
    if (window) SDL_DestroyWindow(window);
}

std::uint32_t SdlEmuWindow::WindowId() const { return SDL_GetWindowID(window); }

void SdlEmuWindow::Relayout() {
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(window, &w, &h);
    if (w > 0 && h > 0) UpdateCurrentFramebufferLayout(static_cast<unsigned>(w), static_cast<unsigned>(h));
}

std::pair<int, int> SdlEmuWindow::ToFramebuffer(const TouchPoint& p) const {
    const auto& b = GetFramebufferLayout().bottom_screen;
    const float x = std::clamp(p.x, 0, Core::kScreenBottomWidth - 1);
    const float y = std::clamp(p.y, 0, Core::kScreenBottomHeight - 1);
    return {static_cast<int>(b.left + (x + 0.5f) * static_cast<float>(b.GetWidth()) /
                                           static_cast<float>(Core::kScreenBottomWidth)),
            static_cast<int>(b.top + (y + 0.5f) * static_cast<float>(b.GetHeight()) /
                                          static_cast<float>(Core::kScreenBottomHeight))};
}

TouchPoint SdlEmuWindow::ToTouchScreen(int framebuffer_x, int framebuffer_y) const {
    const auto& b = GetFramebufferLayout().bottom_screen;
    const float x = (static_cast<float>(framebuffer_x) - static_cast<float>(b.left)) /
                    static_cast<float>(std::max(b.GetWidth(), 1u)) * static_cast<float>(Core::kScreenBottomWidth);
    const float y = (static_cast<float>(framebuffer_y) - static_cast<float>(b.top)) /
                    static_cast<float>(std::max(b.GetHeight(), 1u)) * static_cast<float>(Core::kScreenBottomHeight);
    TouchPoint p;
    p.x = std::clamp(static_cast<int>(x), 0, Core::kScreenBottomWidth - 1);
    p.y = std::clamp(static_cast<int>(y), 0, Core::kScreenBottomHeight - 1);
    return p;
}

void SdlEmuWindow::TapTouch(const TouchPoint& p) {
    const auto [fx, fy] = ToFramebuffer(p);
    if (!TouchPressed(static_cast<unsigned>(fx), static_cast<unsigned>(fy))) return;
    synthetic_touch = true;
    touch_release_at = std::chrono::steady_clock::now() + std::chrono::milliseconds(60);
}

void SdlEmuWindow::UpdateTouch() {
    if (!synthetic_touch) return;
    if (std::chrono::steady_clock::now() < touch_release_at) return;
    TouchReleased();
    synthetic_touch = false;
}

bool SdlEmuWindow::IsFullscreen() const {
    return (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
}

bool SdlEmuWindow::HasFocus() const {
    return window && (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) != 0;
}

void SdlEmuWindow::ToggleFullscreen() {
    SDL_SetWindowFullscreen(window, IsFullscreen() ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
    Relayout();
}

void SdlEmuWindow::SetTitle(const std::string& title) {
    SDL_SetWindowTitle(window, title.c_str());
}

void SdlEmuWindow::ReleaseAllInput() {
    for (int sc = 0; sc < 512; ++sc) {
        if (held_keys[sc]) {
            InputCommon::GetKeyboard()->ReleaseKey(sc);
            held_keys[sc] = 0;
        }
    }
    if (touch_down) {
        TouchReleased();
        touch_down = false;
    }
    if (synthetic_touch) {
        TouchReleased();
        synthetic_touch = false;
    }
}

void SdlEmuWindow::HandleEvent(const SDL_Event& e) {
    switch (e.type) {
    case SDL_WINDOWEVENT:
        if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED || e.window.event == SDL_WINDOWEVENT_RESIZED)
            Relayout();
        else if (e.window.event == SDL_WINDOWEVENT_CLOSE)
            open = false;
        else if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
            ReleaseAllInput();
        break;
    case SDL_KEYDOWN:
        if (e.key.repeat) break;
        if (e.key.keysym.scancode >= 0 && e.key.keysym.scancode < 512) held_keys[e.key.keysym.scancode] = 1;
        InputCommon::GetKeyboard()->PressKey(e.key.keysym.scancode);
        break;
    case SDL_KEYUP:
        if (e.key.keysym.scancode >= 0 && e.key.keysym.scancode < 512) held_keys[e.key.keysym.scancode] = 0;
        InputCommon::GetKeyboard()->ReleaseKey(e.key.keysym.scancode);
        break;
    case SDL_MOUSEBUTTONDOWN:
        if (e.button.button == SDL_BUTTON_LEFT) {
            touch_down =
                TouchPressed(static_cast<unsigned>(e.button.x), static_cast<unsigned>(e.button.y));
            if (touch_down) {
                last_touch = ToTouchScreen(e.button.x, e.button.y);
                has_last_touch = true;
            }
        }
        break;
    case SDL_MOUSEMOTION:
        if (touch_down) TouchMoved(static_cast<unsigned>(e.motion.x), static_cast<unsigned>(e.motion.y));
        break;
    case SDL_MOUSEBUTTONUP:
        if (e.button.button == SDL_BUTTON_LEFT && touch_down) {
            TouchReleased();
            touch_down = false;
        }
        break;
    default:
        break;
    }
}

} // namespace TFH
