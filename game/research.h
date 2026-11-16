// SPDX-License-Identifier: GPL-2.0-or-later
// Research access to Tri Force Heroes' own player state (see docs/2PLAYER_DOPPEL.md).
//
// The addresses below were found by dumping the game's memory and comparing it while
// playing (European dump, CTR-P-EA3P v0). They are the three hero bodies the game keeps,
// one block per player slot, 0x10000 apart. Everything that is still unknown - above all
// the flag that decides single player (doppels) versus a network session - is deliberately
// not shown here instead of guessing.
#pragma once

#include <string>

namespace TFH::Research {

// First player block. The other two are at +0x10000 and +0x20000.
constexpr unsigned kSlotBase = 0x3281A808;
constexpr unsigned kSlotStride = 0x10000;
// float X, +4 Y, +8 Z of the body in that slot.
constexpr unsigned kSlotPosition = 0x1AC;
// 1 when the slot holds a body, 0x80000000 (-0.0 as float) when it does not.
constexpr unsigned kSlotInUse = 0x1C8;

struct Snapshot {
    bool valid = false;
    float pos[3][3]{}; // [slot][x, y, z]
    bool in_use[3]{};
};

// Reads the values above from the running game (cheap, safe to call every frame).
Snapshot Read();

// Reads <count> bytes at addr as hex (16 per line), for trying candidate addresses.
std::string Peek(unsigned addr, int count = 32);

// Writes one byte at addr.
void Poke(unsigned addr, unsigned char value);

// Writes the parts of the game's memory that hold its session state to
// <user_dir>/memdump/mem_NNN_*.bin (plus a .txt with the layout and the snapshot above),
// so the addresses can be found for this build without a debugger. Returns the path of the
// .txt file, or an error message if the game is not running.
std::string DumpMemory(const std::string& user_dir);

} // namespace TFH::Research
