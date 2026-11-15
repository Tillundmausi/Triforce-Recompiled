// SPDX-License-Identifier: GPL-2.0-or-later
#include "romfs.hpp"

#include <algorithm>
#include <unordered_set>
#include <utility>

namespace tfh::romfs {

namespace {
constexpr u32 kNone = 0xFFFFFFFFu;
constexpr u64 kMaxTable = 64ull << 20; // sanity limit for metadata tables

void appendUtf8(std::string& out, u32 cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

std::string utf16Name(std::span<const u8> d, std::size_t off, u32 bytes) {
    checkRange(d, off, bytes);
    std::string s;
    for (u32 i = 0; i + 1 < bytes; i += 2) {
        u32 c = le16(d, off + i);
        if (c >= 0xD800 && c < 0xDC00 && i + 3 < bytes) {
            const u32 lo = le16(d, off + i + 2);
            if (lo >= 0xDC00 && lo < 0xE000) {
                c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
                i += 2;
            }
        }
        appendUtf8(s, c);
    }
    return s;
}
} // namespace

Index::Index(std::vector<File> files) : files_(std::move(files)) {
    std::sort(files_.begin(), files_.end(), [](const File& a, const File& b) { return a.offset < b.offset; });
}

const File* Index::find(u64 offset) const {
    auto it = std::upper_bound(files_.begin(), files_.end(), offset,
                               [](u64 o, const File& f) { return o < f.offset; });
    if (it == files_.begin()) return nullptr;
    --it;
    return offset < it->offset + std::max<u64>(it->size, 1) ? &*it : nullptr;
}

Index parseLevel3(const Source& l3) {
    const auto hdr = l3.readVec(0, 0x28);
    const u32 headerLen = le32(hdr, 0x00);
    require(headerLen == 0x28, "RomFS: unexpected level 3 header size");
    const u32 dirMetaOff = le32(hdr, 0x0C), dirMetaSize = le32(hdr, 0x10);
    const u32 fileMetaOff = le32(hdr, 0x1C), fileMetaSize = le32(hdr, 0x20);
    const u32 dataOff = le32(hdr, 0x24);
    require(dirMetaSize <= kMaxTable && fileMetaSize <= kMaxTable, "RomFS: metadata table too large");
    require(dirMetaSize >= 0x18, "RomFS: no root directory");
    const auto dirs = l3.readVec(dirMetaOff, dirMetaSize);
    const auto filesMeta = l3.readVec(fileMetaOff, fileMetaSize);

    std::vector<File> out;
    std::unordered_set<u32> seenDirs, seenFiles;
    // (directory offset, path of that directory without trailing slash)
    std::vector<std::pair<u32, std::string>> stack{{0u, std::string{}}};
    while (!stack.empty()) {
        auto [dOff, dPath] = std::move(stack.back());
        stack.pop_back();
        require(seenDirs.insert(dOff).second, "RomFS: directory loop");
        require(u64(dOff) + 0x18 <= dirs.size(), "RomFS: directory entry out of range");

        // Files of this directory.
        for (u32 f = le32(dirs, dOff + 0x0C); f != kNone; f = le32(filesMeta, f + 0x04)) {
            require(u64(f) + 0x20 <= filesMeta.size(), "RomFS: file entry out of range");
            require(seenFiles.insert(f).second, "RomFS: file loop");
            const u32 nameLen = le32(filesMeta, f + 0x1C);
            File file;
            file.path = dPath + "/" + utf16Name(filesMeta, f + 0x20, nameLen);
            file.offset = u64(dataOff) + le64(filesMeta, f + 0x08);
            file.size = le64(filesMeta, f + 0x10);
            out.push_back(std::move(file));
        }
        // Sub-directories.
        for (u32 c = le32(dirs, dOff + 0x08); c != kNone; c = le32(dirs, c + 0x04)) {
            require(u64(c) + 0x18 <= dirs.size(), "RomFS: directory entry out of range");
            const u32 nameLen = le32(dirs, c + 0x14);
            stack.emplace_back(c, dPath + "/" + utf16Name(dirs, c + 0x18, nameLen));
            require(stack.size() < 100000, "RomFS: too many directories");
        }
    }
    return Index(std::move(out));
}

u64 level3OffsetFromIvfc(const Source& sec) {
    const auto h = sec.readVec(0, 0x60);
    require(h[0] == 'I' && h[1] == 'V' && h[2] == 'F' && h[3] == 'C', "RomFS: missing IVFC magic");
    const u32 masterHashSize = le32(h, 0x08);
    const u32 l3BlockLog2 = le32(h, 0x4C);
    require(l3BlockLog2 < 31, "RomFS: bad level 3 block size");
    const u64 block = u64(1) << l3BlockLog2;
    const u64 start = 0x60 + u64(masterHashSize);
    return (start + block - 1) / block * block;
}

} // namespace tfh::romfs
