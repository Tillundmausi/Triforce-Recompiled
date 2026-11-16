// SPDX-License-Identifier: GPL-2.0-or-later
#include "research.h"

#include <cstring>

#include "core/core.h"
#include "core/memory.h"

namespace TFH::Research {

namespace {

float ReadFloat(Memory::MemorySystem& memory, unsigned addr) {
    const unsigned bits = memory.Read32(addr);
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

} // namespace

Snapshot Read() {
    Snapshot s;
    Memory::MemorySystem& memory = Core::System::GetInstance().Memory();
    s.doppels_enabled = memory.Read8(kDoppelsEnabled);
    s.active_link = memory.Read8(kActiveLink);
    s.level_id = memory.Read32(kLevelId);
    s.stage_id = memory.Read32(kStageId);
    s.host_data = memory.Read32(kHostData);
    for (int hero = 0; hero < 3; ++hero) {
        const unsigned base = kPositionX + static_cast<unsigned>(hero) * kPlayerStride;
        s.pos[hero][0] = ReadFloat(memory, base);
        s.pos[hero][1] = ReadFloat(memory, base + 4);
        s.pos[hero][2] = ReadFloat(memory, base + 8);
    }
    s.valid = true;
    return s;
}

void ForceDoppelsEnabled() {
    Core::System::GetInstance().Memory().Write8(kDoppelsEnabled, 1);
}

} // namespace TFH::Research
