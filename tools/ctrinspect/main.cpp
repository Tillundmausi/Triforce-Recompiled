// SPDX-License-Identifier: GPL-2.0-or-later
// ctrinspect - validates a 3DS game dump and extracts the ARM11 executable.
//
//   ctrinspect <dump.cia|.3ds|.cci|.cxi> [--out <dir>]
//
// Exit codes: 0 = complete, 2 = encrypted (partial report), 3 = hash mismatch, 1 = error.
#include "../../src/inspect/report.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace fs = std::filesystem;
using namespace tfh;

namespace {
void writeFile(const fs::path& p, std::span<const u8> data) {
    std::ofstream f(p, std::ios::binary);
    require(f.good(), "cannot write " + p.string());
    f.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
}

int usage() {
    std::cerr << "Usage: ctrinspect <dump.cia|.3ds|.cci|.cxi> [--out <dir>]\n";
    return 1;
}
} // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    if (argc < 2) return usage();
    fs::path input = argv[1];
    fs::path outDir = "game_data";
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--out" && i + 1 < argc) outDir = argv[++i];
        else return usage();
    }

    try {
        FileSource src(input);
        const auto result = inspect::inspect(src);
        inspect::writeSummary(result, std::cout);

        fs::create_directories(outDir);
        {
            std::ofstream rep(outDir / "report.json");
            inspect::writeJsonReport(result, input.filename().string(), rep);
        }
        if (!result.exheaderRaw.empty()) writeFile(outDir / "exheader.bin", result.exheaderRaw);
        if (!result.code.empty()) {
            writeFile(outDir / "code.bin", result.code);
            for (const auto& s : result.segments) {
                if (s.fileOffset >= result.code.size()) continue;
                const std::size_t n = std::min<std::size_t>(s.size, result.code.size() - s.fileOffset);
                writeFile(outDir / (s.name.substr(1) + ".bin"),
                          std::span<const u8>(result.code.data() + s.fileOffset, n));
            }
        }
        std::cout << "Output:         " << fs::absolute(outDir).string() << '\n';

        switch (result.status) {
        case inspect::Status::Complete: return 0;
        case inspect::Status::Encrypted: return 2;
        case inspect::Status::HashMismatch: return 3;
        }
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << '\n';
        return 1;
    }
}
