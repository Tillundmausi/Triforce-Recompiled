// SPDX-License-Identifier: GPL-2.0-or-later
#include "any_input.h"
#include "config.h"

#include <cmath>
#include <memory>
#include <tuple>
#include <vector>

#include <SDL.h>

#include "common/param_package.h"
#include "common/settings.h"
#include "core/frontend/input.h"
#include "input_common/main.h"

namespace TFH::Input {
namespace {

constexpr int kMaxSources = 8;

std::string Combine(const char* engine, std::initializer_list<std::string> params) {
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

} // namespace

void RegisterAnyFactories() {
    ::Input::RegisterFactory<::Input::ButtonDevice>("tfh_any", std::make_shared<AnyButtonFactory>());
    ::Input::RegisterFactory<::Input::AnalogDevice>("tfh_any", std::make_shared<AnyAnalogFactory>());
}

void UnregisterAnyFactories() {
    ::Input::UnregisterFactory<::Input::ButtonDevice>("tfh_any");
    ::Input::UnregisterFactory<::Input::AnalogDevice>("tfh_any");
}

std::string AnyButton(std::initializer_list<std::string> params) { return Combine("tfh_any", params); }
std::string AnyAnalog(std::initializer_list<std::string> params) { return Combine("tfh_any", params); }

void ApplyBindings(const AppConfig& cfg) {
    using namespace Settings::NativeButton;
    using namespace Settings::NativeAnalog;
    auto& p = Settings::values.current_input_profile;
    p.name = "Tri Force Heroes";
    const bool pad = cfg.controller_enabled;
    auto k = [&](TFH::Bind b) { return Key(static_cast<SDL_Scancode>(cfg.keys[static_cast<int>(b)])); };
    auto both = [&](TFH::Bind b, SDL_GameControllerButton btn) {
        return pad ? AnyButton({k(b), PadButton(btn)}) : AnyButton({k(b)});
    };
    using TFH::Bind;

    // 3DS face buttons are mapped by POSITION (3DS A = right, B = bottom, ...),
    // which matches Xbox and PlayStation pads physically.
    p.buttons[A] = both(Bind::A, SDL_CONTROLLER_BUTTON_B);
    p.buttons[B] = both(Bind::B, SDL_CONTROLLER_BUTTON_A);
    p.buttons[X] = both(Bind::X, SDL_CONTROLLER_BUTTON_Y);
    p.buttons[Y] = both(Bind::Y, SDL_CONTROLLER_BUTTON_X);
    p.buttons[Up] = both(Bind::Up, SDL_CONTROLLER_BUTTON_DPAD_UP);
    p.buttons[Down] = both(Bind::Down, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    p.buttons[Left] = both(Bind::Left, SDL_CONTROLLER_BUTTON_DPAD_LEFT);
    p.buttons[Right] = both(Bind::Right, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
    p.buttons[L] = both(Bind::L, SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
    p.buttons[R] = both(Bind::R, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
    p.buttons[Start] = both(Bind::Start, SDL_CONTROLLER_BUTTON_START);
    p.buttons[Select] = both(Bind::Select, SDL_CONTROLLER_BUTTON_BACK);
    p.buttons[ZL] = pad ? AnyButton({k(Bind::ZL), PadTrigger(SDL_CONTROLLER_AXIS_TRIGGERLEFT)}) : AnyButton({k(Bind::ZL)});
    p.buttons[ZR] = pad ? AnyButton({k(Bind::ZR), PadTrigger(SDL_CONTROLLER_AXIS_TRIGGERRIGHT)}) : AnyButton({k(Bind::ZR)});
    p.buttons[Home] = pad ? AnyButton({PadButton(SDL_CONTROLLER_BUTTON_GUIDE)}) : AnyButton({});

    const std::string keysStick = InputCommon::GenerateAnalogParamFromKeys(
        cfg.keys[static_cast<int>(Bind::StickUp)], cfg.keys[static_cast<int>(Bind::StickDown)],
        cfg.keys[static_cast<int>(Bind::StickLeft)], cfg.keys[static_cast<int>(Bind::StickRight)],
        cfg.keys[static_cast<int>(Bind::StickSlow)], 0.5f);
    p.analogs[CirclePad] = pad ? AnyAnalog({keysStick, PadStick(SDL_CONTROLLER_AXIS_LEFTX, SDL_CONTROLLER_AXIS_LEFTY, cfg.stick_deadzone)})
                               : AnyAnalog({keysStick});
    p.analogs[CStick] = pad ? AnyAnalog({PadStick(SDL_CONTROLLER_AXIS_RIGHTX, SDL_CONTROLLER_AXIS_RIGHTY, cfg.stick_deadzone)})
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
