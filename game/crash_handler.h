// SPDX-License-Identifier: GPL-2.0-or-later
// Writes <dir>/crash.txt (+ crash.dmp on Windows) when the game crashes or an
// uncaught C++ exception terminates it. The text lists module+RVA per frame so
// the addresses can be symbolized later with the build's PDB.
#pragma once

#include <filesystem>

namespace TFH {

void InstallCrashHandler(const std::filesystem::path& dir);

} // namespace TFH
