// SPDX-License-Identifier: GPL-2.0-or-later
#include "research.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "core/core.h"
#include "core/memory.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/kernel/process.h"

namespace TFH::Research {

namespace {

float ReadFloat(Memory::MemorySystem& memory, unsigned addr) {
    const unsigned bits = memory.Read32(addr);
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

struct Range {
    const char* name;
    unsigned start;
    unsigned size;
};

// The game keeps its runtime structures in the linear heap (0x30000000 and up in the New 3DS
// memory layout). The normal application heap at 0x08000000 is written as well, in case this
// build allocates its session state there; the code head at 0x00100000 is included so a dump
// can be checked against code.bin.
constexpr Range kRanges[] = {
    {"code", 0x00100000, 0x00010000},    // exefs .text head - lets you verify the dump is real
    {"heap300", 0x30000000, 0x04000000}, // linear heap, New 3DS layout
    {"heap080", 0x08000000, 0x02000000}, // application heap
};

} // namespace

Snapshot Read() {
    Snapshot s;
    Memory::MemorySystem& memory = Core::System::GetInstance().Memory();
    for (int slot = 0; slot < 3; ++slot) {
        const unsigned base =
            kSlotBase + static_cast<unsigned>(slot) * kSlotStride;
        const unsigned pos = base + kSlotPosition;
        s.pos[slot][0] = ReadFloat(memory, pos);
        s.pos[slot][1] = ReadFloat(memory, pos + 4);
        s.pos[slot][2] = ReadFloat(memory, pos + 8);
        s.in_use[slot] = memory.Read32(base + kSlotInUse) != 0x80000000;
    }
    s.valid = true;
    return s;
}

std::string Peek(unsigned addr, int count) {
    Memory::MemorySystem& memory = Core::System::GetInstance().Memory();
    std::string out;
    char line[96];
    for (int row = 0; row < count; row += 16) {
        std::snprintf(line, sizeof(line), "0x%08X:", addr + row);
        out += line;
        for (int i = 0; i < 16 && row + i < count; ++i) {
            std::snprintf(line, sizeof(line), " %02X", memory.Read8(addr + row + i));
            out += line;
        }
        out += '\n';
    }
    return out;
}

void Poke(unsigned addr, unsigned char value) {
    Core::System::GetInstance().Memory().Write8(addr, value);
}

std::string DumpMemory(const std::string& user_dir) {
    auto& system = Core::System::GetInstance();
    Kernel::KernelSystem& kernel = system.Kernel();
    if (kernel.GetCurrentProcess() == nullptr)
        return "The game is not running.";
    const Kernel::Process& process = *kernel.GetCurrentProcess();
    Memory::MemorySystem& memory = system.Memory();

    const std::filesystem::path dir = std::filesystem::path(user_dir) / "memdump";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    int index = 1;
    for (; index < 1000; ++index) {
        char probe[32];
        std::snprintf(probe, sizeof(probe), "mem_%03d.txt", index);
        if (!std::filesystem::exists(dir / probe))
            break;
    }
    char stem[32];
    std::snprintf(stem, sizeof(stem), "mem_%03d", index);

    const Snapshot s = Read();
    std::string summary;

    std::vector<unsigned char> buffer(0x1000);
    for (const Range& range : kRanges) {
        const std::filesystem::path file = dir / (std::string(stem) + "_" + range.name + ".bin");
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        if (!out) {
            return "Cannot write " + file.string();
        }
        unsigned written = 0;
        while (written < range.size) {
            const unsigned chunk = std::min<unsigned>(0x1000, range.size - written);
            const unsigned addr = range.start + written;
            if (memory.IsValidVirtualAddress(process, addr)) {
                memory.ReadBlock(addr, buffer.data(), chunk);
            } else {
                std::memset(buffer.data(), 0, chunk);
            }
            out.write(reinterpret_cast<const char*>(buffer.data()), chunk);
            written += chunk;
        }
        out.close();
        char line[96];
        std::snprintf(line, sizeof(line), "%s 0x%08X size 0x%X\n", range.name, range.start,
                      range.size);
        summary += line;
    }

    const std::filesystem::path txt = dir / (std::string(stem) + ".txt");
    std::ofstream out(txt, std::ios::trunc);
    if (!out)
        return "Cannot write " + txt.string();
    out << "memory dump " << stem << "\n\n" << summary << "\n";
    out << "hero bodies (found for this build):\n";
    for (int slot = 0; slot < 3; ++slot) {
        out << "slot" << (slot + 1) << "=" << (s.in_use[slot] ? "in use" : "empty") << ' '
            << s.pos[slot][0] << ',' << s.pos[slot][1] << ',' << s.pos[slot][2] << '\n';
    }
    out.close();
    return txt.string();
}

} // namespace TFH::Research
