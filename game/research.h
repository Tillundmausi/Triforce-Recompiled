// SPDX-License-Identifier: GPL-2.0-or-later
// Research access to Tri Force Heroes' own session state (see docs/2PLAYER_DOPPEL.md).
// The game's doppel mode is a flag - DoppelsEnabled - plus the index of the body the
// pad currently drives (ActiveLink). Reading those two while playing tells us how the
// game distinguishes single player from an online session; forcing DoppelsEnabled in a
// session is the experiment for "two players + one doppel".
//
// Addresses are for the supported European build (CTR-P-EA3P v0) and were taken from the
// Tricord project's public address collection; they still have to be confirmed in-game.
#pragma once

namespace TFH::Research {

// u8: 1 = single player (doppels exist), 0 = network session.
constexpr unsigned kDoppelsEnabled = 0x32D81EC6;
// u8: 0/1/2 = hero the pad drives, 3 = all heroes at once.
constexpr unsigned kActiveLink = 0x31A038D1;
// u32 level/stage ids and the network state (0xFFFFFFFD = not connected).
constexpr unsigned kLevelId = 0x3280EE44;
constexpr unsigned kStageId = 0x3280EE48;
constexpr unsigned kHostData = 0x31A03664;
// float X, +4 Y, +8 Z, one block per hero with a stride of 0x10000.
constexpr unsigned kPositionX = 0x32841358;
constexpr unsigned kPlayerStride = 0x10000;

struct Snapshot {
    bool valid = false;
    int doppels_enabled = 0;
    int active_link = 0;
    unsigned level_id = 0;
    unsigned stage_id = 0;
    unsigned host_data = 0;
    float pos[3][3]{}; // [hero][x, y, z]
};

// Reads the values above from the running game (cheap, safe to call every frame).
Snapshot Read();

// Writes DoppelsEnabled = 1. Only touches that one byte; call repeatedly while the
// experiment is enabled, the game resets it on its own.
void ForceDoppelsEnabled();

} // namespace TFH::Research
