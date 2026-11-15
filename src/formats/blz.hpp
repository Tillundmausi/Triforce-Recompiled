// SPDX-License-Identifier: GPL-2.0-or-later
// "Bottom LZ" / reverse LZSS used for compressed ExeFS .code on the 3DS.
#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace tfh::blz {

// Decompresses a BLZ buffer. Throws FormatError on malformed input.
std::vector<std::uint8_t> decompress(std::span<const std::uint8_t> compressed);

// Greedy BLZ compressor. Only meant for tests and tooling; it is NOT tuned to
// reproduce Nintendo's encoder byte-for-byte.
std::vector<std::uint8_t> compress(std::span<const std::uint8_t> data);

} // namespace tfh::blz
