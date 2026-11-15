// SPDX-License-Identifier: GPL-2.0-or-later
// 3DS RomFS (IVFC level 3) file table. Layout follows the public 3dbrew
// documentation ("RomFS"). Used to turn a read offset into a file path, so the
// frontend can tell which game files are being loaded (Discord level display).
#pragma once

#include "source.hpp"

#include <string>
#include <vector>

namespace tfh::romfs {

struct File {
    std::string path; // "/dir/sub/name.ext"
    u64 offset = 0;   // relative to the start of level 3 (the level 3 header)
    u64 size = 0;
};

class Index {
public:
    Index() = default;
    explicit Index(std::vector<File> files); // sorts by offset

    // File containing this level-3 offset, or nullptr (headers, tables, padding).
    const File* find(u64 offset) const;
    const std::vector<File>& files() const { return files_; }
    bool empty() const { return files_.empty(); }

private:
    std::vector<File> files_;
};

// Parses the level 3 header + directory/file metadata tables. `level3` must
// start at the level 3 header (this is how Azahar exposes a title's RomFS).
Index parseLevel3(const Source& level3);

// Offset of level 3 inside a RomFS section that starts with the IVFC header.
u64 level3OffsetFromIvfc(const Source& romfsSection);

} // namespace tfh::romfs
