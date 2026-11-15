// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <span>

namespace tfh {

class Sha256 {
public:
    using Digest = std::array<std::uint8_t, 32>;
    Sha256();
    void update(std::span<const std::uint8_t> data);
    Digest finish();
    static Digest hash(std::span<const std::uint8_t> data);

private:
    void block(const std::uint8_t* p);
    std::array<std::uint32_t, 8> h_;
    std::array<std::uint8_t, 64> buf_{};
    std::uint64_t total_ = 0;
    std::size_t bufLen_ = 0;
};

} // namespace tfh
