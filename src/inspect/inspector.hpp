// SPDX-License-Identifier: GPL-2.0-or-later
// Phase 1: validates a game dump and extracts the ARM11 executable.
#pragma once

#include "../formats/ctr.hpp"

#include <optional>
#include <string>
#include <vector>

namespace tfh::inspect {

enum class Container { Cia, Ncsd, Ncch };
enum class Status {
    Complete,       // fully parsed, all hashes verified, code extracted
    Encrypted,      // container parsed, but the executable is encrypted
    HashMismatch,   // decrypted data present, but hashes do not verify
};

struct Segment {
    std::string name;
    u32 address = 0;
    u32 fileOffset = 0;  // offset inside the decompressed .code
    u32 size = 0;        // bytes as listed in the ExHeader
    u32 mappedSize = 0;  // numPages * 0x1000
};

struct Result {
    Container container = Container::Ncch;
    Status status = Status::Complete;
    std::string statusMessage;
    std::vector<std::string> warnings;

    std::optional<ctr::Cia> cia;
    std::optional<ctr::Ncsd> ncsd;
    std::optional<ctr::NcchHeader> ncch;

    std::optional<ctr::ExHeader> exheader;
    std::vector<u8> exheaderRaw; // 0x800 bytes (ExHeader + AccessDesc)
    bool exheaderHashOk = false;

    std::vector<ctr::ExeFsEntry> exefs;
    std::vector<bool> exefsHashOk;
    bool exefsSuperblockOk = false;
    std::optional<ctr::Smdh> icon;

    std::vector<u8> code; // decompressed .code (text | ro | data)
    ctr::Hash codeSha256{};
    std::vector<Segment> segments;
};

Result inspect(const Source& src);

// The single executable this project targets (decision 2026-11-15).
struct TargetVersion {
    static constexpr u64 kTitleId = 0x0004000000177000ull;
    static constexpr const char* kProductCode = "CTR-P-EA3P";
    static constexpr u16 kTitleVersion = 0;
};
// nullopt = not enough information (e.g. NCCH header unreadable).
std::optional<bool> matchesTarget(const Result& r);

const char* containerName(Container c);
const char* statusName(Status s);

} // namespace tfh::inspect
