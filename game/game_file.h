// SPDX-License-Identifier: GPL-2.0-or-later
// Game dump handling for the setup assistant: file picker + validation using the
// project's own container parser (src/inspect, same code as ctrinspect).
#pragma once

#include <string>

namespace TFH {

struct DumpCheck {
    enum class State {
        None,       // no file selected
        NotFound,   // path does not exist
        Invalid,    // not a 3DS game file / unreadable
        Encrypted,  // valid container but encrypted
        Corrupt,    // decrypted but hashes don't match
        Ok,         // decrypted, verified
    };
    State state = State::None;
    bool is_cia = false;
    bool is_target = false;    // Tri Force Heroes EUR v0.0.0 (the tested version)
    bool is_tfh = false;       // any Tri Force Heroes title id
    std::string title;         // English title from the icon, if available
    std::string product_code;  // e.g. CTR-P-EA3P
    std::string region;
    std::string message;       // human readable summary (English)
};

DumpCheck CheckDump(const std::string& path_utf8);

// Opens the native "open file" dialog. Returns empty string if cancelled or
// unsupported on this platform.
std::string BrowseForGameFile();

} // namespace TFH
