// SPDX-License-Identifier: GPL-2.0-or-later
#include "game_file.h"

#include <filesystem>

#include "inspect/inspector.hpp"

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#endif

namespace TFH {

namespace {
std::filesystem::path FromUtf8(const std::string& s) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size()));
}
// Tri Force Heroes product codes are CTR-P-EA3x (x = region letter). Only EUR v0 (EA3P) is
// the tested target; the 3-letter game code is shared between regions.
bool IsTfhProductCode(const std::string& pc) {
    return pc.size() >= 10 && pc.compare(0, 9, "CTR-P-EA3") == 0;
}
std::string TitleFrom(const tfh::inspect::Result& r) {
    const tfh::ctr::Smdh* s = r.icon ? &*r.icon : (r.cia && r.cia->metaIcon ? &*r.cia->metaIcon : nullptr);
    if (!s) return {};
    for (int lang : {1, 3, 0})
        if (!s->titles[lang].shortName.empty()) return s->titles[lang].shortName;
    return {};
}
} // namespace

DumpCheck CheckDump(const std::string& path_utf8) {
    DumpCheck c;
    if (path_utf8.empty()) {
        c.state = DumpCheck::State::None;
        c.message = "No game file selected.";
        return c;
    }
    const auto path = FromUtf8(path_utf8);
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) {
        c.state = DumpCheck::State::NotFound;
        c.message = "File not found.";
        return c;
    }
    auto ext = path.extension().string();
    for (auto& ch : ext) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    c.is_cia = ext == ".cia";

    try {
        tfh::FileSource src(path);
        const auto r = tfh::inspect::inspect(src);
        if (r.ncch) {
            c.product_code = r.ncch->productCode;
            c.region = tfh::ctr::regionFromProductCode(r.ncch->productCode);
            c.is_tfh = IsTfhProductCode(r.ncch->productCode);
        } else if (r.cia) {
            c.is_tfh = r.cia->tmd.titleId == tfh::inspect::TargetVersion::kTitleId;
        }
        c.title = TitleFrom(r);
        c.is_target = tfh::inspect::matchesTarget(r).value_or(false);
        switch (r.status) {
        case tfh::inspect::Status::Encrypted:
            c.state = DumpCheck::State::Encrypted;
            c.message = "This file is encrypted. Decrypt it on your own 3DS with GodMode9 (see step 2).";
            break;
        case tfh::inspect::Status::HashMismatch:
            c.state = DumpCheck::State::Corrupt;
            c.message = "This file is damaged or was decrypted incorrectly (checksums do not match).";
            break;
        case tfh::inspect::Status::Complete:
            c.state = DumpCheck::State::Ok;
            if (c.is_target)
                c.message = "Looks good: decrypted Tri Force Heroes (Europe, release version without update) - the tested version.";
            else if (c.is_tfh)
                c.message = "Decrypted Tri Force Heroes, but not the tested version (Europe, no update). It may still work.";
            else
                c.message = "Decrypted 3DS game, but this is not Tri Force Heroes.";
            break;
        }
    } catch (const std::exception& e) {
        c.state = DumpCheck::State::Invalid;
        c.message = std::string("Not a 3DS game file (.cia/.3ds/.cci/.cxi): ") + e.what();
    }
    return c;
}

std::string BrowseForGameFile() {
#ifdef _WIN32
    wchar_t file[MAX_PATH * 4] = {};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = L"3DS game (*.cia;*.3ds;*.cci;*.cxi)\0*.cia;*.3ds;*.cci;*.cxi\0All files\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = static_cast<DWORD>(std::size(file));
    ofn.lpstrTitle = L"Select your decrypted Tri Force Heroes dump";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return {};
    const auto u8 = std::filesystem::path(file).u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
#else
    return {};
#endif
}

} // namespace TFH
