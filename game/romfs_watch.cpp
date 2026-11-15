// SPDX-License-Identifier: GPL-2.0-or-later
#include "romfs_watch.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <map>
#include <mutex>
#include <unordered_set>

#include "common/logging/log.h"
#include "core/file_sys/ivfc_archive.h"
#include "core/file_sys/romfs_reader.h"
#include "formats/romfs.hpp"
#include "levels.h"

namespace TFH::RomfsWatch {

namespace {

// tfh::Source view of an Azahar RomFS reader (offset 0 = level 3 header).
class ReaderSource final : public tfh::Source {
public:
    explicit ReaderSource(FileSys::RomFSReader& r) : reader(r) {}
    tfh::u64 size() const override { return reader.GetSize(); }
    void read(tfh::u64 offset, void* dst, std::size_t n) const override {
        tfh::require(offset <= size() && n <= size() - offset, "RomFS read out of range");
        const std::size_t got = reader.ReadFile(static_cast<std::size_t>(offset), n, static_cast<tfh::u8*>(dst));
        tfh::require(got == n, "short RomFS read");
    }

private:
    FileSys::RomFSReader& reader;
};

struct State {
    std::mutex mutex;
    bool installed = false;
    std::filesystem::path log_dir;
    std::ofstream log;
    std::chrono::steady_clock::time_point start;
    std::map<FileSys::RomFSReader*, tfh::romfs::Index> indexes;
    std::unordered_set<std::string> seen;
    const tfh::romfs::File* last = nullptr;
    Snapshot snap;
};
State g;

double Seconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - g.start).count();
}

const tfh::romfs::Index& IndexFor(FileSys::RomFSReader* reader) {
    auto it = g.indexes.find(reader);
    if (it != g.indexes.end()) return it->second;
    tfh::romfs::Index idx;
    try {
        ReaderSource src(*reader);
        idx = tfh::romfs::parseLevel3(src);
        LOG_INFO(Frontend, "RomFS index: {} files", idx.files().size());
        std::ofstream list(g.log_dir / "romfs_index.txt", std::ios::trunc);
        list << "# All files in the game's RomFS (offset, size, path)\n";
        for (const auto& f : idx.files()) {
            char buf[48];
            std::snprintf(buf, sizeof(buf), "%010llx %10llu ", static_cast<unsigned long long>(f.offset),
                          static_cast<unsigned long long>(f.size));
            list << buf << f.path << '\n';
        }
    } catch (const std::exception& e) {
        LOG_WARNING(Frontend, "RomFS index could not be read: {}", e.what());
    }
    return g.indexes.emplace(reader, std::move(idx)).first->second;
}

void OnRead(FileSys::RomFSReader* reader, u64 offset, std::size_t /*length*/) {
    std::lock_guard lk(g.mutex);
    if (!g.installed || !reader) return;
    const auto* file = IndexFor(reader).find(offset);
    if (!file || file == g.last) return;
    g.last = file;
    g.snap.last_file = file->path;
    if (const char* level = LevelForPath(file->path)) g.snap.level = level;
    if (g.seen.insert(file->path).second) {
        g.snap.files_seen = g.seen.size();
        if (g.log) {
            char t[32];
            std::snprintf(t, sizeof(t), "[%8.1fs] ", Seconds());
            g.log << t << file->path << '\n';
            g.log.flush();
        }
    }
}

} // namespace

const char* LevelForPath(const std::string& path) {
    for (const auto& r : kLevelRules) {
        if (!r.contains) break;
        if (path.find(r.contains) != std::string::npos) return r.name;
    }
    return nullptr;
}

void Install(const std::filesystem::path& log_dir) {
    {
        std::lock_guard lk(g.mutex);
        g.indexes.clear();
        g.seen.clear();
        g.last = nullptr;
        g.snap = {};
        g.log_dir = log_dir;
        std::error_code ec;
        std::filesystem::create_directories(log_dir, ec);
        g.log = std::ofstream(log_dir / "game_files.txt", std::ios::trunc);
        g.log << "# Game files in the order they were first loaded (seconds since the game started).\n"
                 "# Note down when you enter an area or level - this is how level names get added.\n";
        g.start = std::chrono::steady_clock::now();
        g.installed = true;
    }
    FileSys::SetRomFSReadHook(&OnRead);
}

void Uninstall() {
    FileSys::SetRomFSReadHook(nullptr);
    std::lock_guard lk(g.mutex);
    g.installed = false;
    g.indexes.clear();
    g.last = nullptr;
    g.log.close();
    g.snap.level.clear();
}

Snapshot Get() {
    std::lock_guard lk(g.mutex);
    return g.snap;
}

std::string LogPath() {
    std::lock_guard lk(g.mutex);
    return (g.log_dir / "game_files.txt").string();
}

} // namespace TFH::RomfsWatch
