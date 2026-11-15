// SPDX-License-Identifier: GPL-2.0-or-later
// Level names for Discord Rich Presence.
//
// Each rule maps a part of a RomFS path to the name shown in Discord. A rule
// only fires when the game loads a file whose path contains `contains`, so only
// files that belong to exactly one level/area may be listed here.
//
// The table starts EMPTY on purpose: the real file names of Tri Force Heroes
// are only known after a play session has been recorded. Play with the launcher
// open, enter areas/levels, then look at user/log/game_files.txt (each line =
// first time a file was loaded, with seconds since start) and add rules, e.g.
//     {"/SomeStageFolder/", "Woodlands - Deku Forest"},
// Never add guessed names.
#pragma once

namespace TFH {

struct LevelRule {
    const char* contains;
    const char* name;
};

inline constexpr LevelRule kLevelRules[] = {
    {nullptr, nullptr}, // terminator (keep last)
};

} // namespace TFH
