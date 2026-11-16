// SPDX-License-Identifier: GPL-2.0-or-later
#include "any_input.h"
#include "config.h"

#include <cmath>
#include <cctype>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <SDL.h>

#include "common/logging/log.h"
#include "common/param_package.h"
#include "common/settings.h"
#include "core/frontend/input.h"
#include "input_common/main.h"

namespace TFH::Input {
namespace {

constexpr int kMaxSources = 10;

std::string Combine(const char* engine, const std::vector<std::string>& params) {
    Common::ParamPackage pkg{{"engine", engine}};
    int i = 0;
    for (const auto& p : params) {
        if (i >= kMaxSources) break;
        pkg.Set(std::to_string(i++), p);
    }
    return pkg.Serialize();
}

template <typename Device>
std::vector<std::unique_ptr<Device>> CreateSources(const Common::ParamPackage& params) {
    std::vector<std::unique_ptr<Device>> out;
    for (int i = 0; i < kMaxSources; ++i) {
        const std::string key = std::to_string(i);
        if (!params.Has(key)) continue;
        out.push_back(::Input::CreateDevice<Device>(params.Get(key, "")));
    }
    return out;
}

class AnyButtonDevice final : public ::Input::ButtonDevice {
public:
    explicit AnyButtonDevice(std::vector<std::unique_ptr<::Input::ButtonDevice>> s) : sources(std::move(s)) {}
    bool GetStatus() const override {
        for (const auto& s : sources)
            if (s && s->GetStatus()) return true;
        return false;
    }

private:
    std::vector<std::unique_ptr<::Input::ButtonDevice>> sources;
};

class AnyAnalogDevice final : public ::Input::AnalogDevice {
public:
    explicit AnyAnalogDevice(std::vector<std::unique_ptr<::Input::AnalogDevice>> s) : sources(std::move(s)) {}
    std::tuple<float, float> GetStatus() const override {
        std::tuple<float, float> best{0.0f, 0.0f};
        float bestMag = 0.0f;
        for (const auto& s : sources) {
            if (!s) continue;
            const auto [x, y] = s->GetStatus();
            const float mag = std::sqrt(x * x + y * y);
            if (mag > bestMag) { bestMag = mag; best = {x, y}; }
        }
        return best;
    }

private:
    std::vector<std::unique_ptr<::Input::AnalogDevice>> sources;
};

class AnyButtonFactory final : public ::Input::Factory<::Input::ButtonDevice> {
public:
    std::unique_ptr<::Input::ButtonDevice> Create(const Common::ParamPackage& params) override {
        return std::make_unique<AnyButtonDevice>(CreateSources<::Input::ButtonDevice>(params));
    }
};

class AnyAnalogFactory final : public ::Input::Factory<::Input::AnalogDevice> {
public:
    std::unique_ptr<::Input::AnalogDevice> Create(const Common::ParamPackage& params) override {
        return std::make_unique<AnyAnalogDevice>(CreateSources<::Input::AnalogDevice>(params));
    }
};

// --- generic pads that SDL does not recognise as game controllers ------------
// Kept open on the main thread (RefreshPads) and only read from here.
struct OpenPad {
    SDL_Joystick* joy = nullptr;
    SDL_JoystickID id = -1;
    std::string name;
};

std::mutex g_pad_mutex;
std::vector<OpenPad> g_pads;
std::uint64_t g_last_pad_scan = 0;

// A pad needs at least a stick and a few buttons to be useful.
bool LooksLikeGamepad(SDL_Joystick* joy, const std::string& name) {
    if (!joy || SDL_JoystickNumButtons(joy) < 4 || SDL_JoystickNumAxes(joy) < 2) return false;
    // Wheels, pedals, HOTAS and rhythm controllers are not gamepads and would
    // otherwise drag the sticks around.
    static const char* kSkip[] = {"wheel", "pedal", "hotas", "flight", "throttle", "guitar",
                                  "drum", "dance", "midi"};
    std::string lower = name;
    for (auto& ch : lower) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    for (const char* s : kSkip)
        if (lower.find(s) != std::string::npos) return false;
    return true;
}

// Axis layout of a generic pad: XInput-style pads put the right stick on 2/3
// and the triggers on 4/5, classic HID pads (8 axes: X Y Z Rx Ry Rz ...) on
// 3/4 and 2/5.
struct RawLayout {
    int rx, ry, lt, rt;
};

RawLayout LayoutFor(SDL_Joystick* joy) {
    const int axes = joy ? SDL_JoystickNumAxes(joy) : 0;
    if (axes >= 7) return {3, 4, 2, 5};
    return {2, 3, 4, 5};
}

bool RawButtonDown(int button) {
    std::lock_guard<std::mutex> lock(g_pad_mutex);
    for (const auto& p : g_pads)
        if (p.joy && button < SDL_JoystickNumButtons(p.joy) && SDL_JoystickGetButton(p.joy, button))
            return true;
    return false;
}

bool RawHatDown(Uint8 direction) {
    std::lock_guard<std::mutex> lock(g_pad_mutex);
    for (const auto& p : g_pads)
        if (p.joy && SDL_JoystickNumHats(p.joy) > 0 && (SDL_JoystickGetHat(p.joy, 0) & direction))
            return true;
    return false;
}

class RawPadButtonDevice final : public ::Input::ButtonDevice {
public:
    RawPadButtonDevice(std::string kind_, int value_) : kind(std::move(kind_)), value(value_) {}
    bool GetStatus() const override {
        if (kind == "button") return RawButtonDown(value);
        if (kind == "trigger_left" || kind == "trigger_right") {
            bool down = false;
            {
                std::lock_guard<std::mutex> lock(g_pad_mutex);
                for (const auto& p : g_pads) {
                    if (!p.joy) continue;
                    const RawLayout l = LayoutFor(p.joy);
                    const int axis = kind == "trigger_left" ? l.lt : l.rt;
                    if (axis >= SDL_JoystickNumAxes(p.joy)) continue;
                    const float v =
                        (static_cast<float>(SDL_JoystickGetAxis(p.joy, axis)) + 32768.0f) / 65535.0f;
                    if (v > 0.5f) down = true;
                }
            }
            return down;
        }
        if (kind == "hat_up") return RawHatDown(SDL_HAT_UP);
        if (kind == "hat_down") return RawHatDown(SDL_HAT_DOWN);
        if (kind == "hat_left") return RawHatDown(SDL_HAT_LEFT);
        if (kind == "hat_right") return RawHatDown(SDL_HAT_RIGHT);
        return false;
    }

private:
    std::string kind;
    int value;
};

class RawPadAnalogDevice final : public ::Input::AnalogDevice {
public:
    RawPadAnalogDevice(bool right_stick_, float deadzone_) : right_stick(right_stick_), deadzone(deadzone_) {}
    std::tuple<float, float> GetStatus() const override {
        float xMax = 0.0f, yMax = 0.0f, rMax = 0.0f;
        std::lock_guard<std::mutex> lock(g_pad_mutex);
        for (const auto& p : g_pads) {
            if (!p.joy) continue;
            const RawLayout l = LayoutFor(p.joy);
            const int ax = right_stick ? l.rx : 0;
            const int ay = right_stick ? l.ry : 1;
            if (ax >= SDL_JoystickNumAxes(p.joy) || ay >= SDL_JoystickNumAxes(p.joy)) continue;
            float x = static_cast<float>(SDL_JoystickGetAxis(p.joy, ax)) / 32767.0f;
            float y = -static_cast<float>(SDL_JoystickGetAxis(p.joy, ay)) / 32767.0f; // 3DS: up is +
            float r = std::sqrt(x * x + y * y);
            if (r > 1.0f) { x /= r; y /= r; r = 1.0f; }
            if (r > rMax) { rMax = r; xMax = x; yMax = y; }
        }
        if (rMax > deadzone)
            return {xMax / rMax * (rMax - deadzone) / (1.0f - deadzone),
                    yMax / rMax * (rMax - deadzone) / (1.0f - deadzone)};
        return {0.0f, 0.0f};
    }

private:
    bool right_stick;
    float deadzone;
};

class RawPadButtonFactory final : public ::Input::Factory<::Input::ButtonDevice> {
public:
    std::unique_ptr<::Input::ButtonDevice> Create(const Common::ParamPackage& params) override {
        return std::make_unique<RawPadButtonDevice>(params.Get("kind", "button"), params.Get("value", 0));
    }
};

class RawPadAnalogFactory final : public ::Input::Factory<::Input::AnalogDevice> {
public:
    std::unique_ptr<::Input::AnalogDevice> Create(const Common::ParamPackage& params) override {
        return std::make_unique<RawPadAnalogDevice>(params.Get("stick", "left") == "right",
                                                    params.Get("deadzone", 0.15f));
    }
};

// --- param builders ---------------------------------------------------------
std::string Key(SDL_Scancode sc) {
    return InputCommon::GenerateKeyboardParam(static_cast<int>(sc));
}
// All connected game controllers (Xbox, PlayStation, ...), positional layout.
std::string PadButton(SDL_GameControllerButton b) {
    return Common::ParamPackage{{"engine", "sdl"}, {"api", "controller"}, {"maptype", "all"},
                                {"button", std::to_string(static_cast<int>(b))}}
        .Serialize();
}
std::string PadTrigger(SDL_GameControllerAxis a) {
    return Common::ParamPackage{{"engine", "sdl"}, {"api", "controller"}, {"maptype", "all"},
                                {"axis", std::to_string(static_cast<int>(a))},
                                {"direction", "+"}, {"threshold", "0.5"}}
        .Serialize();
}
std::string PadStick(SDL_GameControllerAxis x, SDL_GameControllerAxis y, float deadzone) {
    return Common::ParamPackage{{"engine", "sdl"}, {"api", "controller"}, {"maptype", "all"},
                                {"axis_x", std::to_string(static_cast<int>(x))},
                                {"axis_y", std::to_string(static_cast<int>(y))},
                                {"deadzone", std::to_string(deadzone)}}
        .Serialize();
}
// Same as above for pads SDL only sees as a joystick (see RefreshPads).
std::string RawButton(int index) {
    return Common::ParamPackage{{"engine", "tfh_rawpad"}, {"kind", "button"},
                                {"value", std::to_string(index)}}
        .Serialize();
}
std::string RawHat(const char* direction) {
    return Common::ParamPackage{{"engine", "tfh_rawpad"}, {"kind", direction}}.Serialize();
}
std::string RawTrigger(bool right) {
    return Common::ParamPackage{{"engine", "tfh_rawpad"}, {"kind", right ? "trigger_right" : "trigger_left"}}
        .Serialize();
}
std::string RawStick(bool right, float deadzone) {
    return Common::ParamPackage{{"engine", "tfh_rawpad"}, {"stick", right ? "right" : "left"},
                                {"deadzone", std::to_string(deadzone)}}
        .Serialize();
}

} // namespace

void RegisterAnyFactories() {
    ::Input::RegisterFactory<::Input::ButtonDevice>("tfh_any", std::make_shared<AnyButtonFactory>());
    ::Input::RegisterFactory<::Input::AnalogDevice>("tfh_any", std::make_shared<AnyAnalogFactory>());
    ::Input::RegisterFactory<::Input::ButtonDevice>("tfh_rawpad", std::make_shared<RawPadButtonFactory>());
    ::Input::RegisterFactory<::Input::AnalogDevice>("tfh_rawpad", std::make_shared<RawPadAnalogFactory>());
}

void UnregisterAnyFactories() {
    ::Input::UnregisterFactory<::Input::ButtonDevice>("tfh_any");
    ::Input::UnregisterFactory<::Input::AnalogDevice>("tfh_any");
    ::Input::UnregisterFactory<::Input::ButtonDevice>("tfh_rawpad");
    ::Input::UnregisterFactory<::Input::AnalogDevice>("tfh_rawpad");
}

void LoadControllerMappings(const std::filesystem::path& file) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(file, ec)) return;
    const int added = SDL_GameControllerAddMappingsFromFile(file.string().c_str());
    if (added > 0) LOG_INFO(Frontend, "Loaded {} controller mapping(s) from {}", added, file.string());
}

void RefreshPads() {
    const std::uint64_t now = SDL_GetTicks64();
    std::lock_guard<std::mutex> lock(g_pad_mutex);
    if (g_last_pad_scan != 0 && now - g_last_pad_scan < 1000) return;
    g_last_pad_scan = now;

    // Forget pads that were unplugged.
    for (auto it = g_pads.begin(); it != g_pads.end();) {
        if (it->joy && SDL_JoystickGetAttached(it->joy)) {
            ++it;
        } else {
            if (it->joy) SDL_JoystickClose(it->joy);
            it = g_pads.erase(it);
        }
    }

    // Pick up pads SDL does not turn into game controllers.
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        if (SDL_IsGameController(i)) continue;
        SDL_Joystick* joy = SDL_JoystickOpen(i);
        if (!joy) continue;
        const char* raw_name = SDL_JoystickName(joy);
        if (!LooksLikeGamepad(joy, raw_name ? raw_name : "")) {
            SDL_JoystickClose(joy);
            continue;
        }
        const SDL_JoystickID id = SDL_JoystickInstanceID(joy);
        bool known = false;
        for (const auto& p : g_pads) known = known || p.id == id;
        if (known) {
            SDL_JoystickClose(joy);
            continue;
        }
        OpenPad pad;
        pad.joy = joy;
        pad.id = id;
        pad.name = raw_name ? raw_name : "?";
        LOG_INFO(Frontend, "Generic pad in use: '{}' ({} buttons, {} axes, {} hats)",
                 pad.name, SDL_JoystickNumButtons(joy), SDL_JoystickNumAxes(joy),
                 SDL_JoystickNumHats(joy));
        g_pads.push_back(std::move(pad));
    }
}

std::string AnyButton(std::initializer_list<std::string> params) {
    return AnyButton(std::vector<std::string>(params));
}
std::string AnyButton(const std::vector<std::string>& params) { return Combine("tfh_any", params); }
std::string AnyAnalog(std::initializer_list<std::string> params) {
    return AnyAnalog(std::vector<std::string>(params));
}
std::string AnyAnalog(const std::vector<std::string>& params) { return Combine("tfh_any", params); }

void ApplyBindings(const AppConfig& cfg) {
    using namespace Settings::NativeButton;
    using namespace Settings::NativeAnalog;
    auto& p = Settings::values.current_input_profile;
    p.name = "Tri Force Heroes";
    const bool pad = cfg.controller_enabled;
    auto k = [&](TFH::Bind b) { return Key(static_cast<SDL_Scancode>(cfg.keys[static_cast<int>(b)])); };

    // Keyboard + SDL game controller + generic pad, all at the same time.
    // 3DS face buttons are mapped by POSITION (3DS A = right, B = bottom, ...),
    // which matches Xbox and PlayStation pads physically. The generic pad uses
    // the usual Xbox button order (0 = bottom, 1 = right, 2 = left, 3 = top).
    auto bind = [&](TFH::Bind b, SDL_GameControllerButton btn, int raw_button, const char* hat) {
        std::vector<std::string> sources{k(b)};
        if (pad) {
            sources.push_back(PadButton(btn));
            sources.push_back(RawButton(raw_button));
            if (hat) sources.push_back(RawHat(hat));
        }
        return AnyButton(sources);
    };
    using TFH::Bind;

    p.buttons[A] = bind(Bind::A, SDL_CONTROLLER_BUTTON_B, 1, nullptr);
    p.buttons[B] = bind(Bind::B, SDL_CONTROLLER_BUTTON_A, 0, nullptr);
    p.buttons[X] = bind(Bind::X, SDL_CONTROLLER_BUTTON_Y, 3, nullptr);
    p.buttons[Y] = bind(Bind::Y, SDL_CONTROLLER_BUTTON_X, 2, nullptr);
    p.buttons[Up] = bind(Bind::Up, SDL_CONTROLLER_BUTTON_DPAD_UP, 11, "hat_up");
    p.buttons[Down] = bind(Bind::Down, SDL_CONTROLLER_BUTTON_DPAD_DOWN, 12, "hat_down");
    p.buttons[Left] = bind(Bind::Left, SDL_CONTROLLER_BUTTON_DPAD_LEFT, 13, "hat_left");
    p.buttons[Right] = bind(Bind::Right, SDL_CONTROLLER_BUTTON_DPAD_RIGHT, 14, "hat_right");
    p.buttons[L] = bind(Bind::L, SDL_CONTROLLER_BUTTON_LEFTSHOULDER, 4, nullptr);
    p.buttons[R] = bind(Bind::R, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, 5, nullptr);
    p.buttons[Start] = bind(Bind::Start, SDL_CONTROLLER_BUTTON_START, 7, nullptr);
    p.buttons[Select] = bind(Bind::Select, SDL_CONTROLLER_BUTTON_BACK, 6, nullptr);
    p.buttons[ZL] = pad ? AnyButton({k(Bind::ZL), PadTrigger(SDL_CONTROLLER_AXIS_TRIGGERLEFT),
                                     RawTrigger(false)})
                        : AnyButton({k(Bind::ZL)});
    p.buttons[ZR] = pad ? AnyButton({k(Bind::ZR), PadTrigger(SDL_CONTROLLER_AXIS_TRIGGERRIGHT),
                                     RawTrigger(true)})
                        : AnyButton({k(Bind::ZR)});
    p.buttons[Home] = pad ? AnyButton({PadButton(SDL_CONTROLLER_BUTTON_GUIDE), RawButton(10)}) : AnyButton({});

    const std::string keysStick = InputCommon::GenerateAnalogParamFromKeys(
        cfg.keys[static_cast<int>(Bind::StickUp)], cfg.keys[static_cast<int>(Bind::StickDown)],
        cfg.keys[static_cast<int>(Bind::StickLeft)], cfg.keys[static_cast<int>(Bind::StickRight)],
        cfg.keys[static_cast<int>(Bind::StickSlow)], 0.5f);
    p.analogs[CirclePad] = pad ? AnyAnalog({keysStick, PadStick(SDL_CONTROLLER_AXIS_LEFTX, SDL_CONTROLLER_AXIS_LEFTY, cfg.stick_deadzone),
                                            RawStick(false, cfg.stick_deadzone)})
                               : AnyAnalog({keysStick});
    p.analogs[CStick] = pad ? AnyAnalog({PadStick(SDL_CONTROLLER_AXIS_RIGHTX, SDL_CONTROLLER_AXIS_RIGHTY, cfg.stick_deadzone),
                                         RawStick(true, cfg.stick_deadzone)})
                            : AnyAnalog({});

    // Same defaults Azahar's own frontend uses.
    p.motion_device = "engine:motion_emu,update_period:100,sensitivity:0.01,tilt_clamp:90.0";
    p.touch_device = "engine:emu_window"; // mouse on the bottom screen = touch
    p.controller_touch_device = "";
    p.use_touchpad = false;
    p.use_touch_from_button = false;
    p.touch_from_button_map_index = 0;
    p.udp_input_address = "127.0.0.1";
    p.udp_input_port = 26760;
    p.udp_pad_index = 0;
    Settings::values.input_profiles = {p};
    Settings::values.current_input_profile_index = 0;
}

} // namespace TFH::Input
