// SPDX-License-Identifier: GPL-2.0-or-later
// Nintendo 3DS ("CTR") container formats: CIA, ticket, TMD, NCSD, NCCH,
// ExHeader, ExeFS, SMDH. Layouts follow the public 3dbrew documentation.
#pragma once

#include "source.hpp"

#include <array>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace tfh::ctr {

using Hash = std::array<u8, 32>;

constexpr u32 kMediaUnit = 0x200;

// Size of a signature block (type + signature + padding) for a signature type.
std::size_t signatureBlockSize(u32 sigType);

// ----- SMDH (icon / title metadata) ------------------------------------------
struct SmdhTitle {
    std::string shortName, longName, publisher;
};
struct Smdh {
    std::array<SmdhTitle, 16> titles; // index = language (0 JP, 1 EN, 2 FR, 3 DE, ...)
    u32 regionLockout = 0;
};
constexpr std::size_t kSmdhSize = 0x36C0;
std::optional<Smdh> parseSmdh(std::span<const u8> data); // nullopt if magic mismatches
std::string regionLockoutString(u32 mask);

// ----- Ticket / TMD -----------------------------------------------------------
struct Ticket {
    u32 sigType = 0;
    std::string issuer;
    u64 ticketId = 0;
    u32 consoleId = 0;
    u64 titleId = 0;
    u16 titleVersion = 0;
    u8 commonKeyIndex = 0;
};
Ticket parseTicket(std::span<const u8> data);

enum ContentType : u16 {
    kContentEncrypted = 0x0001,
    kContentDisc = 0x0002,
    kContentOptional = 0x4000,
    kContentShared = 0x8000,
};

struct ContentChunk {
    u32 id = 0;
    u16 index = 0;
    u16 type = 0;
    u64 size = 0;
    Hash hash{};
    bool encrypted() const { return type & kContentEncrypted; }
};

struct Tmd {
    u32 sigType = 0;
    std::string issuer;
    u64 titleId = 0;
    u32 titleType = 0;
    u32 saveDataSize = 0;
    u16 titleVersion = 0;
    std::vector<ContentChunk> contents;
};
Tmd parseTmd(std::span<const u8> data);

// ----- CIA ----------------------------------------------------------------
struct Cia {
    u32 headerSize = 0;
    u16 type = 0, version = 0;
    u32 certSize = 0, ticketSize = 0, tmdSize = 0, metaSize = 0;
    u64 contentSize = 0;
    u64 certOffset = 0, ticketOffset = 0, tmdOffset = 0, contentOffset = 0, metaOffset = 0;
    Ticket ticket;
    Tmd tmd;
    // Absolute offset of each TMD content inside the CIA, or nullopt if the
    // content is not present in this CIA (content index bitfield).
    std::vector<std::optional<u64>> contentOffsets;
    std::vector<u64> metaDependencies;
    std::optional<u32> metaCoreVersion;
    std::optional<Smdh> metaIcon;
};
Cia parseCia(const Source& src);
bool looksLikeCia(const Source& src);

// ----- NCSD (.3ds / .cci) --------------------------------------------------
struct NcsdPartition {
    u64 offset = 0, size = 0; // bytes
};
struct Ncsd {
    u64 mediaId = 0;
    std::array<NcsdPartition, 8> partitions;
};
bool looksLikeNcsd(const Source& src);
Ncsd parseNcsd(const Source& src);

// ----- NCCH ---------------------------------------------------------------
struct NcchHeader {
    u32 contentSizeMU = 0;
    u64 partitionId = 0, programId = 0;
    std::string makerCode, productCode;
    u16 version = 0;
    Hash exheaderHash{}, exefsSuperblockHash{}, romfsSuperblockHash{};
    u32 exheaderSize = 0;
    std::array<u8, 8> flags{};
    u32 plainOff = 0, plainSize = 0, logoOff = 0, logoSize = 0;
    u32 exefsOff = 0, exefsSize = 0, exefsHashRegionSize = 0;
    u32 romfsOff = 0, romfsSize = 0, romfsHashRegionSize = 0;

    u32 mediaUnit() const { return kMediaUnit << flags[6]; }
    u8 cryptoMethod() const { return flags[3]; }
    bool fixedKey() const { return flags[7] & 0x01; }
    bool noRomfs() const { return flags[7] & 0x02; }
    bool noCrypto() const { return flags[7] & 0x04; }
    bool usesSeed() const { return flags[7] & 0x20; }
    bool isData() const { return flags[5] & 0x01; }
    bool isExecutable() const { return flags[5] & 0x02; }
};
bool looksLikeNcch(const Source& src);
NcchHeader parseNcchHeader(const Source& src);
std::string regionFromProductCode(const std::string& productCode);

// ----- ExHeader -------------------------------------------------------------
struct CodeSetInfo {
    u32 address = 0, numPages = 0, size = 0;
};

struct KernelCaps {
    std::vector<u32> allowedSvcs;
    std::vector<u32> interrupts;
    int kernelMajor = -1, kernelMinor = -1;
    int handleTableSize = -1;
    std::optional<u32> kernelFlags;
    std::vector<std::pair<u32, u32>> mappedRanges; // [start, end) page-aligned, from descriptor pairs
    std::vector<u32> mappedPages;
    std::vector<u32> unknown;
};
KernelCaps decodeKernelCaps(std::span<const u32> descriptors);

struct ExHeader {
    std::string appTitle;
    bool compressedCode = false;
    bool sdApplication = false;
    u16 remasterVersion = 0;
    CodeSetInfo text, ro, data;
    u32 stackSize = 0, bssSize = 0;
    std::vector<u64> dependencies;
    u64 saveDataSize = 0, jumpId = 0;
    // ARM11 local capabilities
    u64 programId = 0;
    u32 coreVersion = 0;
    u8 flag0 = 0, flag1 = 0, flag2 = 0, priority = 0;
    u64 extdataId = 0;
    std::vector<std::string> services;
    KernelCaps kernel;
};
constexpr std::size_t kExHeaderSize = 0x400;      // SCI + ACI (hashed part)
constexpr std::size_t kAccessDescSize = 0x400;
ExHeader parseExHeader(std::span<const u8> data);

// ----- ExeFS --------------------------------------------------------------
struct ExeFsEntry {
    std::string name;
    u32 offset = 0, size = 0;
    Hash hash{};
};
constexpr std::size_t kExeFsHeaderSize = 0x200;
std::vector<ExeFsEntry> parseExeFsHeader(std::span<const u8> data);

} // namespace tfh::ctr
