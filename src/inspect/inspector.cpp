// SPDX-License-Identifier: GPL-2.0-or-later
#include "inspector.hpp"

#include "../common/sha256.hpp"
#include "../formats/blz.hpp"

#include <memory>

namespace tfh::inspect {

const char* containerName(Container c) {
    switch (c) {
    case Container::Cia: return "CIA";
    case Container::Ncsd: return "NCSD (.3ds/.cci)";
    case Container::Ncch: return "NCCH (.cxi)";
    }
    return "?";
}

const char* statusName(Status s) {
    switch (s) {
    case Status::Complete: return "complete";
    case Status::Encrypted: return "encrypted";
    case Status::HashMismatch: return "hash_mismatch";
    }
    return "?";
}

namespace {

ctr::Hash hashRange(const Source& src, u64 off, u64 len) {
    Sha256 sha;
    std::vector<u8> buf(1 << 20);
    while (len) {
        const std::size_t n = static_cast<std::size_t>(std::min<u64>(len, buf.size()));
        src.read(off, buf.data(), n);
        sha.update(std::span<const u8>(buf.data(), n));
        off += n; len -= n;
    }
    return sha.finish();
}

void inspectNcch(const Source& ncchSrc, Result& r) {
    r.ncch = ctr::parseNcchHeader(ncchSrc);
    const auto& n = *r.ncch;
    if (!n.noCrypto()) {
        r.status = Status::Encrypted;
        r.statusMessage = "NCCH is encrypted (crypto method " + hex32(n.cryptoMethod()) +
                          (n.fixedKey() ? ", fixed key" : "") + (n.usesSeed() ? ", uses seed" : "") +
                          "). Decrypt the dump on your own console (e.g. GodMode9) and run again.";
        return;
    }
    if (!n.isExecutable()) r.warnings.push_back("NCCH is not flagged as executable");

    // --- ExHeader -----------------------------------------------------------
    require(n.exheaderSize == ctr::kExHeaderSize, "unexpected ExHeader size " + hex32(n.exheaderSize));
    r.exheaderRaw = ncchSrc.readVec(0x200, ctr::kExHeaderSize + ctr::kAccessDescSize);
    r.exheaderHashOk =
        Sha256::hash(std::span<const u8>(r.exheaderRaw.data(), ctr::kExHeaderSize)) == n.exheaderHash;
    r.exheader = ctr::parseExHeader(r.exheaderRaw);
    if (r.exheader->programId != n.programId)
        r.warnings.push_back("ExHeader program ID differs from NCCH program ID");

    // --- ExeFS --------------------------------------------------------------
    require(n.exefsOff != 0 && n.exefsSize != 0, "NCCH has no ExeFS");
    const u64 mu = n.mediaUnit();
    const u64 exefsBase = u64(n.exefsOff) * mu;
    const u64 exefsLen = u64(n.exefsSize) * mu;
    SubSource exefs(ncchSrc, exefsBase, exefsLen);
    r.exefsSuperblockOk = hashRange(exefs, 0, u64(n.exefsHashRegionSize) * mu) == n.exefsSuperblockHash;

    const auto header = exefs.readVec(0, ctr::kExeFsHeaderSize);
    r.exefs = ctr::parseExeFsHeader(header);
    const ctr::ExeFsEntry* codeEntry = nullptr;
    for (const auto& e : r.exefs) {
        require(u64(ctr::kExeFsHeaderSize) + e.offset + e.size <= exefsLen, "ExeFS file '" + e.name + "' out of range");
        r.exefsHashOk.push_back(hashRange(exefs, ctr::kExeFsHeaderSize + e.offset, e.size) == e.hash);
        if (e.name == ".code") codeEntry = &e;
        if (e.name == "icon" && e.size >= ctr::kSmdhSize)
            r.icon = ctr::parseSmdh(exefs.readVec(ctr::kExeFsHeaderSize + e.offset, ctr::kSmdhSize));
    }
    require(codeEntry != nullptr, "ExeFS contains no .code");

    // --- .code ----------------------------------------------------------------
    auto raw = exefs.readVec(ctr::kExeFsHeaderSize + codeEntry->offset, codeEntry->size);
    r.code = r.exheader->compressedCode ? blz::decompress(raw) : std::move(raw);
    r.codeSha256 = Sha256::hash(r.code);

    const auto& ex = *r.exheader;
    const u32 textOff = 0;
    const u32 roOff = textOff + ex.text.numPages * 0x1000;
    const u32 dataOff = roOff + ex.ro.numPages * 0x1000;
    r.segments = {
        {".text", ex.text.address, textOff, ex.text.size, ex.text.numPages * 0x1000},
        {".rodata", ex.ro.address, roOff, ex.ro.size, ex.ro.numPages * 0x1000},
        {".data", ex.data.address, dataOff, ex.data.size, ex.data.numPages * 0x1000},
    };
    const u64 expected = u64(dataOff) + ex.data.numPages * 0x1000ull;
    if (r.code.size() < u64(dataOff) + ex.data.size)
        r.warnings.push_back("decompressed .code (" + std::to_string(r.code.size()) +
                             " bytes) is smaller than the ExHeader segments require");
    else if (r.code.size() != expected)
        r.warnings.push_back("decompressed .code size " + std::to_string(r.code.size()) +
                             " differs from page-aligned ExHeader layout " + std::to_string(expected));

    bool allOk = r.exheaderHashOk && r.exefsSuperblockOk;
    for (bool ok : r.exefsHashOk) allOk = allOk && ok;
    if (allOk) {
        r.status = Status::Complete;
        r.statusMessage = "All hashes verified; executable extracted.";
    } else {
        r.status = Status::HashMismatch;
        r.statusMessage = "Hash verification failed - dump is corrupted or was decrypted incorrectly.";
    }
}

} // namespace

std::optional<bool> matchesTarget(const Result& r) {
    if (!r.ncch) return std::nullopt;
    bool ok = r.ncch->programId == TargetVersion::kTitleId && r.ncch->productCode == TargetVersion::kProductCode;
    if (r.cia) ok = ok && r.cia->tmd.titleVersion == TargetVersion::kTitleVersion;
    return ok;
}

Result inspect(const Source& src) {
    Result r;
    if (ctr::looksLikeCia(src)) {
        r.container = Container::Cia;
        r.cia = ctr::parseCia(src);
        const auto& contents = r.cia->tmd.contents;
        std::size_t main = contents.size();
        for (std::size_t i = 0; i < contents.size(); ++i)
            if (contents[i].index == 0) { main = i; break; }
        require(main < contents.size() && r.cia->contentOffsets[main], "CIA has no content with index 0");
        if (r.cia->ticket.titleId != r.cia->tmd.titleId)
            r.warnings.push_back("ticket title ID differs from TMD title ID");
        SubSource ncch(src, *r.cia->contentOffsets[main], contents[main].size);
        if (contents[main].encrypted() && !ctr::looksLikeNcch(ncch)) {
            r.status = Status::Encrypted;
            r.statusMessage = "CIA content 0 is title-key encrypted. Decrypt the CIA on your own console "
                              "(e.g. GodMode9) and run again.";
            return r;
        }
        if (contents[main].encrypted())
            r.warnings.push_back("content 0 is flagged encrypted but is readable (already decrypted?)");
        inspectNcch(ncch, r);
    } else if (ctr::looksLikeNcsd(src)) {
        r.container = Container::Ncsd;
        r.ncsd = ctr::parseNcsd(src);
        const auto& p0 = r.ncsd->partitions[0];
        require(p0.size != 0, "NCSD partition 0 is empty");
        SubSource ncch(src, p0.offset, p0.size);
        inspectNcch(ncch, r);
    } else if (ctr::looksLikeNcch(src)) {
        r.container = Container::Ncch;
        inspectNcch(src, r);
    } else {
        throw FormatError("unrecognised file: not a CIA, NCSD or NCCH");
    }
    return r;
}

} // namespace tfh::inspect
