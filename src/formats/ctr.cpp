// SPDX-License-Identifier: GPL-2.0-or-later
#include "ctr.hpp"

#include <bit>
#include <cstring>

namespace tfh::ctr {

std::size_t signatureBlockSize(u32 sigType) {
    switch (sigType) {
    case 0x010000: case 0x010003: return 4 + 0x200 + 0x3C; // RSA-4096
    case 0x010001: case 0x010004: return 4 + 0x100 + 0x3C; // RSA-2048
    case 0x010002: case 0x010005: return 4 + 0x3C + 0x40;  // ECDSA
    default: throw FormatError("unknown signature type " + hex32(sigType));
    }
}

// ----- SMDH ---------------------------------------------------------------
namespace {
std::string utf16leToUtf8(std::span<const u8> d, std::size_t off, std::size_t bytes) {
    std::string out;
    for (std::size_t i = 0; i + 1 < bytes; i += 2) {
        u32 c = le16(d, off + i);
        if (c == 0) break;
        if (c >= 0xD800 && c <= 0xDBFF && i + 3 < bytes) {
            const u32 lo = le16(d, off + i + 2);
            if (lo >= 0xDC00 && lo <= 0xDFFF) { c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00); i += 2; }
        }
        if (c < 0x80) out.push_back(static_cast<char>(c));
        else if (c < 0x800) { out.push_back(char(0xC0 | (c >> 6))); out.push_back(char(0x80 | (c & 0x3F))); }
        else if (c < 0x10000) {
            out.push_back(char(0xE0 | (c >> 12))); out.push_back(char(0x80 | ((c >> 6) & 0x3F)));
            out.push_back(char(0x80 | (c & 0x3F)));
        } else {
            out.push_back(char(0xF0 | (c >> 18))); out.push_back(char(0x80 | ((c >> 12) & 0x3F)));
            out.push_back(char(0x80 | ((c >> 6) & 0x3F))); out.push_back(char(0x80 | (c & 0x3F)));
        }
    }
    return out;
}
} // namespace

std::optional<Smdh> parseSmdh(std::span<const u8> d) {
    if (d.size() < kSmdhSize || std::memcmp(d.data(), "SMDH", 4) != 0) return std::nullopt;
    Smdh s;
    for (std::size_t i = 0; i < 16; ++i) {
        const std::size_t base = 0x8 + i * 0x200;
        s.titles[i].shortName = utf16leToUtf8(d, base, 0x80);
        s.titles[i].longName = utf16leToUtf8(d, base + 0x80, 0x100);
        s.titles[i].publisher = utf16leToUtf8(d, base + 0x180, 0x80);
    }
    s.regionLockout = le32(d, 0x2018);
    return s;
}

std::string regionLockoutString(u32 m) {
    if ((m & 0x7F) == 0x7F) return "region-free";
    static const char* names[] = {"JPN", "USA", "EUR", "AUS", "CHN", "KOR", "TWN"};
    std::string s;
    for (int i = 0; i < 7; ++i)
        if (m & (1u << i)) { if (!s.empty()) s += ","; s += names[i]; }
    return s.empty() ? "none" : s;
}

// ----- Ticket / TMD -----------------------------------------------------------
Ticket parseTicket(std::span<const u8> d) {
    Ticket t;
    t.sigType = be32(d, 0);
    const std::size_t s = signatureBlockSize(t.sigType);
    t.issuer = asciiField(d, s, 0x40);
    t.ticketId = be64(d, s + 0x90);
    t.consoleId = be32(d, s + 0x98);
    t.titleId = be64(d, s + 0x9C);
    t.titleVersion = be16(d, s + 0xA6);
    checkRange(d, s + 0xB1, 1);
    t.commonKeyIndex = d[s + 0xB1];
    return t;
}

Tmd parseTmd(std::span<const u8> d) {
    Tmd t;
    t.sigType = be32(d, 0);
    const std::size_t s = signatureBlockSize(t.sigType);
    t.issuer = asciiField(d, s, 0x40);
    t.titleId = be64(d, s + 0x4C);
    t.titleType = be32(d, s + 0x54);
    t.saveDataSize = le32(d, s + 0x5A); // documented as little-endian
    t.titleVersion = be16(d, s + 0x9C);
    const u16 count = be16(d, s + 0x9E);
    const std::size_t chunks = s + 0xC4 + 64 * 0x24;
    for (u16 i = 0; i < count; ++i) {
        const std::size_t o = chunks + std::size_t(i) * 0x30;
        ContentChunk c;
        c.id = be32(d, o);
        c.index = be16(d, o + 4);
        c.type = be16(d, o + 6);
        c.size = be64(d, o + 8);
        checkRange(d, o + 0x10, 32);
        std::memcpy(c.hash.data(), d.data() + o + 0x10, 32);
        t.contents.push_back(c);
    }
    return t;
}

// ----- CIA ----------------------------------------------------------------
bool looksLikeCia(const Source& src) {
    if (src.size() < 0x2020) return false;
    const auto h = src.readVec(0, 0x20);
    return le32(h, 0) == 0x2020;
}

Cia parseCia(const Source& src) {
    require(looksLikeCia(src), "not a CIA (header size != 0x2020)");
    const auto h = src.readVec(0, 0x2020);
    Cia c;
    c.headerSize = le32(h, 0x00);
    c.type = le16(h, 0x04);
    c.version = le16(h, 0x06);
    c.certSize = le32(h, 0x08);
    c.ticketSize = le32(h, 0x0C);
    c.tmdSize = le32(h, 0x10);
    c.metaSize = le32(h, 0x14);
    c.contentSize = le64(h, 0x18);

    c.certOffset = alignUp(c.headerSize, 64);
    c.ticketOffset = alignUp(c.certOffset + c.certSize, 64);
    c.tmdOffset = alignUp(c.ticketOffset + c.ticketSize, 64);
    c.contentOffset = alignUp(c.tmdOffset + c.tmdSize, 64);
    c.metaOffset = alignUp(c.contentOffset + c.contentSize, 64);
    require(c.contentOffset + c.contentSize <= src.size(), "CIA content section exceeds file size");

    c.ticket = parseTicket(src.readVec(c.ticketOffset, c.ticketSize));
    c.tmd = parseTmd(src.readVec(c.tmdOffset, c.tmdSize));

    // Content index bitfield (bit 7 of byte 0 = content index 0).
    u64 off = c.contentOffset;
    for (const auto& chunk : c.tmd.contents) {
        const bool present = (h[0x20 + chunk.index / 8] >> (7 - chunk.index % 8)) & 1;
        if (present) {
            require(off + chunk.size <= c.contentOffset + c.contentSize, "CIA content exceeds content section");
            c.contentOffsets.emplace_back(off);
            off += chunk.size;
        } else {
            c.contentOffsets.emplace_back(std::nullopt);
        }
    }

    if (c.metaSize >= 0x400 && c.metaOffset + c.metaSize <= src.size()) {
        const auto meta = src.readVec(c.metaOffset, c.metaSize);
        for (std::size_t i = 0; i < 0x30; ++i)
            if (u64 id = le64(meta, i * 8)) c.metaDependencies.push_back(id);
        c.metaCoreVersion = le32(meta, 0x300);
        if (meta.size() >= 0x400 + kSmdhSize)
            c.metaIcon = parseSmdh(std::span<const u8>(meta).subspan(0x400, kSmdhSize));
    }
    return c;
}

// ----- NCSD ---------------------------------------------------------------
bool looksLikeNcsd(const Source& src) {
    if (src.size() < 0x200) return false;
    const auto m = src.readVec(0x100, 4);
    return std::memcmp(m.data(), "NCSD", 4) == 0;
}

Ncsd parseNcsd(const Source& src) {
    require(looksLikeNcsd(src), "not an NCSD image");
    const auto h = src.readVec(0, 0x200);
    Ncsd n;
    n.mediaId = le64(h, 0x108);
    const u64 mu = u64(kMediaUnit) << h[0x188 + 6];
    for (std::size_t i = 0; i < 8; ++i) {
        n.partitions[i].offset = u64(le32(h, 0x120 + i * 8)) * mu;
        n.partitions[i].size = u64(le32(h, 0x124 + i * 8)) * mu;
    }
    return n;
}

// ----- NCCH ---------------------------------------------------------------
bool looksLikeNcch(const Source& src) {
    if (src.size() < 0x200) return false;
    const auto m = src.readVec(0x100, 4);
    return std::memcmp(m.data(), "NCCH", 4) == 0;
}

NcchHeader parseNcchHeader(const Source& src) {
    require(looksLikeNcch(src), "NCCH magic not found (content encrypted or not an NCCH)");
    const auto h = src.readVec(0, 0x200);
    NcchHeader n;
    n.contentSizeMU = le32(h, 0x104);
    n.partitionId = le64(h, 0x108);
    n.makerCode = asciiField(h, 0x110, 2);
    n.version = le16(h, 0x112);
    n.programId = le64(h, 0x118);
    n.productCode = asciiField(h, 0x150, 0x10);
    std::memcpy(n.exheaderHash.data(), h.data() + 0x160, 32);
    n.exheaderSize = le32(h, 0x180);
    std::memcpy(n.flags.data(), h.data() + 0x188, 8);
    n.plainOff = le32(h, 0x190); n.plainSize = le32(h, 0x194);
    n.logoOff = le32(h, 0x198); n.logoSize = le32(h, 0x19C);
    n.exefsOff = le32(h, 0x1A0); n.exefsSize = le32(h, 0x1A4); n.exefsHashRegionSize = le32(h, 0x1A8);
    n.romfsOff = le32(h, 0x1B0); n.romfsSize = le32(h, 0x1B4); n.romfsHashRegionSize = le32(h, 0x1B8);
    std::memcpy(n.exefsSuperblockHash.data(), h.data() + 0x1C0, 32);
    std::memcpy(n.romfsSuperblockHash.data(), h.data() + 0x1E0, 32);
    return n;
}

std::string regionFromProductCode(const std::string& pc) {
    // Format: CTR-P-XXXR, last character = region.
    if (pc.size() < 10) return "unknown";
    switch (pc[9]) {
    case 'J': return "Japan";
    case 'E': return "North America";
    case 'P': return "Europe";
    case 'U': return "Australia";
    case 'K': return "Korea";
    case 'C': return "China";
    case 'W': return "Taiwan";
    case 'A': return "Region-free";
    default: return std::string("unknown ('") + pc[9] + "')";
    }
}

// ----- ExHeader -------------------------------------------------------------
KernelCaps decodeKernelCaps(std::span<const u32> desc) {
    KernelCaps k;
    std::optional<u32> pendingRangeStart;
    for (u32 d : desc) {
        const int ones = std::countl_one(d);
        if (ones == 32) continue; // unused slot
        if (ones == 3) {
            k.interrupts.push_back(d);
        } else if (ones == 4) {
            const u32 table = (d >> 24) & 7;
            for (u32 b = 0; b < 24; ++b)
                if (d & (1u << b)) k.allowedSvcs.push_back(table * 24 + b);
        } else if (ones == 6) {
            k.kernelMajor = int((d >> 8) & 0xFF);
            k.kernelMinor = int(d & 0xFF);
        } else if (ones == 7) {
            k.handleTableSize = int(d & 0x7FFFF);
        } else if (ones == 8) {
            k.kernelFlags = d & 0x7FFFFF;
        } else if (ones == 9 && !(d & (1u << 21))) {
            const u32 addr = (d & 0xFFFFF) << 12;
            if (!pendingRangeStart) pendingRangeStart = addr;
            else { k.mappedRanges.emplace_back(*pendingRangeStart, addr); pendingRangeStart.reset(); }
        } else if (ones == 11) {
            k.mappedPages.push_back((d & 0xFFFFF) << 12);
        } else {
            k.unknown.push_back(d);
        }
    }
    if (pendingRangeStart) k.unknown.push_back(*pendingRangeStart);
    std::sort(k.allowedSvcs.begin(), k.allowedSvcs.end());
    return k;
}

ExHeader parseExHeader(std::span<const u8> d) {
    require(d.size() >= kExHeaderSize, "ExHeader too small");
    ExHeader e;
    e.appTitle = asciiField(d, 0x00, 8);
    e.compressedCode = d[0x0D] & 1;
    e.sdApplication = d[0x0D] & 2;
    e.remasterVersion = le16(d, 0x0E);
    auto cs = [&](std::size_t o) { return CodeSetInfo{le32(d, o), le32(d, o + 4), le32(d, o + 8)}; };
    e.text = cs(0x10);
    e.stackSize = le32(d, 0x1C);
    e.ro = cs(0x20);
    e.data = cs(0x30);
    e.bssSize = le32(d, 0x3C);
    for (std::size_t i = 0; i < 48; ++i)
        if (u64 id = le64(d, 0x40 + i * 8)) e.dependencies.push_back(id);
    e.saveDataSize = le64(d, 0x1C0);
    e.jumpId = le64(d, 0x1C8);

    e.programId = le64(d, 0x200);
    e.coreVersion = le32(d, 0x208);
    e.flag1 = d[0x20C];
    e.flag2 = d[0x20D];
    e.flag0 = d[0x20E];
    e.priority = d[0x20F];
    e.extdataId = le64(d, 0x230);
    for (std::size_t i = 0; i < 34; ++i) { // 32 regular + 2 extended entries
        std::string s = asciiField(d, 0x250 + i * 8, 8);
        if (!s.empty()) e.services.push_back(std::move(s));
    }
    std::array<u32, 28> kc{};
    for (std::size_t i = 0; i < kc.size(); ++i) kc[i] = le32(d, 0x370 + i * 4);
    e.kernel = decodeKernelCaps(kc);
    return e;
}

// ----- ExeFS --------------------------------------------------------------
std::vector<ExeFsEntry> parseExeFsHeader(std::span<const u8> d) {
    require(d.size() >= kExeFsHeaderSize, "ExeFS header too small");
    std::vector<ExeFsEntry> out;
    for (std::size_t i = 0; i < 10; ++i) {
        ExeFsEntry e;
        e.name = asciiField(d, i * 16, 8);
        e.offset = le32(d, i * 16 + 8);
        e.size = le32(d, i * 16 + 12);
        if (e.name.empty() && e.size == 0) continue;
        std::memcpy(e.hash.data(), d.data() + 0x200 - 32 * (i + 1), 32);
        out.push_back(std::move(e));
    }
    return out;
}

} // namespace tfh::ctr
