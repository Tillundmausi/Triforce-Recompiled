// SPDX-License-Identifier: GPL-2.0-or-later
// Watches which RomFS files the game reads (via the Azahar RomFS read hook from
// patches/azahar-romfs-read-hook.patch) and derives the current level from them.
// Also writes user/log/game_files.txt: every game file the first time it is
// loaded, with a timestamp. That log is how the level table (levels.h) is filled.
#pragma once

#include <filesystem>
#include <string>

namespace TFH::RomfsWatch {

void Install(const std::filesystem::path& log_dir); // call before System::Load
void Uninstall();                                   // call after the emulation thread stopped

struct Snapshot {
    std::string last_file;  // most recently read game file
    std::string level;      // display name from levels.h, empty if not known
    std::size_t files_seen = 0;
};
Snapshot Get();

std::string LogPath();

// Pure helper (tested): level name for a RomFS path, or nullptr.
const char* LevelForPath(const std::string& path);

} // namespace TFH::RomfsWatch
