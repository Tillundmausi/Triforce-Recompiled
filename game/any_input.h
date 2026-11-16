// SPDX-License-Identifier: GPL-2.0-or-later
// "tfh_any" input engine: one 3DS button/stick can be driven by several sources
// at once (keyboard AND controller). Azahar only supports one source per input.
#pragma once

#include <filesystem>
#include <initializer_list>
#include <string>
#include <vector>

namespace TFH { struct AppConfig; }

namespace TFH::Input {

// Registers the "tfh_any" button and analog factories with Azahar's input system.
// Call after InputCommon::Init().
void RegisterAnyFactories();
void UnregisterAnyFactories();

// Builds a param string that ORs several button params.
std::string AnyButton(std::initializer_list<std::string> params);
std::string AnyButton(const std::vector<std::string>& params);
// Builds a param string that picks the strongest of several analog params.
std::string AnyAnalog(std::initializer_list<std::string> params);
std::string AnyAnalog(const std::vector<std::string>& params);

// Some pads (vJoy, emulators that fake a controller, Windows' "synthetic"
// Xbox pads, ...) show up in SDL as a plain joystick that SDL does not map to
// a game controller. Azahar's SDL engine only reads game controllers, so
// those pads would do nothing in the game. These keep a list of such pads
// open and read them with a standard Xbox-like layout. Call RefreshPads()
// regularly from the main thread; the devices themselves may be read from
// the emulation thread.
void RefreshPads();

// Adds SDL controller mappings from a file (user/gamecontrollerdb.txt).
// Call after SDL_Init() and before InputCommon::Init().
void LoadControllerMappings(const std::filesystem::path& file);

// Writes keyboard (from the config) + controller bindings into
// Settings::values.current_input_profile.
void ApplyBindings(const AppConfig& cfg);

} // namespace TFH::Input
