// Tri Force Heroes Recomp - byte helpers
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstdint>
#include <cstdio>
#include <span>
#include <stdexcept>
#include <string>

namespace tfh {

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

struct FormatError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

inline void require(bool cond, const std::string& msg) {
    if (!cond) throw FormatError(msg);
}

inline void checkRange(std::span<const u8> d, std::size_t off, std::size_t n) {
    require(off <= d.size() && n <= d.size() - off, "read out of range");
}

inline u16 le16(std::span<const u8> d, std::size_t o) {
    checkRange(d, o, 2);
    return static_cast<u16>(d[o] | (d[o + 1] << 8));
}
inline u32 le32(std::span<const u8> d, std::size_t o) {
    checkRange(d, o, 4);
    return static_cast<u32>(d[o]) | (static_cast<u32>(d[o + 1]) << 8) |
           (static_cast<u32>(d[o + 2]) << 16) | (static_cast<u32>(d[o + 3]) << 24);
}
inline u64 le64(std::span<const u8> d, std::size_t o) {
    return static_cast<u64>(le32(d, o)) | (static_cast<u64>(le32(d, o + 4)) << 32);
}
inline u16 be16(std::span<const u8> d, std::size_t o) {
    checkRange(d, o, 2);
    return static_cast<u16>((d[o] << 8) | d[o + 1]);
}
inline u32 be32(std::span<const u8> d, std::size_t o) {
    checkRange(d, o, 4);
    return (static_cast<u32>(d[o]) << 24) | (static_cast<u32>(d[o + 1]) << 16) |
           (static_cast<u32>(d[o + 2]) << 8) | static_cast<u32>(d[o + 3]);
}
inline u64 be64(std::span<const u8> d, std::size_t o) {
    return (static_cast<u64>(be32(d, o)) << 32) | static_cast<u64>(be32(d, o + 4));
}

inline void putLe32(u8* p, u32 v) {
    p[0] = static_cast<u8>(v); p[1] = static_cast<u8>(v >> 8);
    p[2] = static_cast<u8>(v >> 16); p[3] = static_cast<u8>(v >> 24);
}

// Reads a fixed-size ASCII field, stopping at the first NUL.
inline std::string asciiField(std::span<const u8> d, std::size_t o, std::size_t n) {
    checkRange(d, o, n);
    std::string s;
    for (std::size_t i = 0; i < n && d[o + i] != 0; ++i) {
        const u8 c = d[o + i];
        s.push_back((c >= 0x20 && c < 0x7F) ? static_cast<char>(c) : '?');
    }
    return s;
}

inline std::string toHex(std::span<const u8> d) {
    static const char* digits = "0123456789abcdef";
    std::string s;
    s.reserve(d.size() * 2);
    for (u8 b : d) { s.push_back(digits[b >> 4]); s.push_back(digits[b & 15]); }
    return s;
}

inline std::string hex64(u64 v) {
    char buf[24];
    std::snprintf(buf, sizeof buf, "%016llX", static_cast<unsigned long long>(v));
    return buf;
}
inline std::string hex32(u32 v) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "0x%08X", v);
    return buf;
}

constexpr u64 alignUp(u64 v, u64 a) { return (v + a - 1) / a * a; }

} // namespace tfh
