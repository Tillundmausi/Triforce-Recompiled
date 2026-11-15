// SPDX-License-Identifier: GPL-2.0-or-later
// "tfh_any" input engine: one 3DS button/stick can be driven by several sources
// at once (keyboard AND controller). Azahar only supports one source per input.
#pragma once

#include <initializer_list>
#include <string>

namespace TFH { struct AppConfig; }

namespace TFH::Input {

// Registers the "tfh_any" button and analog factories with Azahar's input system.
// Call after InputCommon::Init().
void RegisterAnyFactories();
void UnregisterAnyFactories();

// Builds a param string that ORs several button params.
std::string AnyButton(std::initializer_list<std::string> params);
// Builds a param string that picks the strongest of several analog params.
std::string AnyAnalog(std::initializer_list<std::string> params);

// Writes keyboard (from the config) + controller bindings into
// Settings::values.current_input_profile.
void ApplyBindings(const AppConfig& cfg);

} // namespace TFH::Input
