// SPDX-License-Identifier: GPL-2.0-or-later
#include "blz.hpp"

#include "../common/bytes.hpp"

#include <algorithm>
#include <cstring>

namespace tfh::blz {

std::vector<u8> decompress(std::span<const u8> in) {
    require(in.size() >= 8, "BLZ: buffer too small");
    const u32 size = static_cast<u32>(in.size());
    const u32 topBottom = le32(in, size - 8);
    const u32 extra = le32(in, size - 4);
    const u32 headerLen = topBottom >> 24;
    const u32 compLen = topBottom & 0xFFFFFF;
    require(headerLen >= 8 && headerLen <= size, "BLZ: bad header length");
    require(compLen <= size && compLen >= headerLen, "BLZ: bad compressed length");

    const u64 outSize = static_cast<u64>(size) + extra;
    require(outSize < (1ull << 31), "BLZ: output too large");
    std::vector<u8> out(static_cast<std::size_t>(outSize), 0);
    std::memcpy(out.data(), in.data(), size);

    u32 index = size - headerLen;
    const u32 stop = size - compLen;
    u64 pos = outSize;

    while (index > stop) {
        u8 control = in[--index];
        for (int bit = 0; bit < 8; ++bit) {
            if (index <= stop || pos == 0) break;
            if (control & 0x80) {
                require(index >= stop + 2, "BLZ: truncated back-reference");
                index -= 2;
                u32 seg = in[index] | (in[index + 1] << 8);
                const u32 len = ((seg >> 12) & 0xF) + 3;
                const u32 disp = (seg & 0x0FFF) + 2;
                require(pos >= len, "BLZ: back-reference underflows output");
                for (u32 j = 0; j < len; ++j) {
                    require(pos + disp < outSize, "BLZ: back-reference past end");
                    const u8 v = out[pos + disp];
                    out[--pos] = v;
                }
            } else {
                out[--pos] = in[--index];
            }
            control = static_cast<u8>(control << 1);
        }
    }
    return out;
}

std::vector<u8> compress(std::span<const u8> data) {
    // Build the token stream in *read order* (the decoder reads downwards).
    std::vector<u8> stream;
    const std::size_t n = data.size();
    std::size_t p = n; // number of bytes still to produce (we produce data[p-1] next)
    while (p > 0) {
        const std::size_t ctrlPos = stream.size();
        stream.push_back(0);
        u8 ctrl = 0;
        for (int bit = 0; bit < 8 && p > 0; ++bit) {
            const std::size_t dst = p - 1; // byte being written
            std::size_t bestLen = 0, bestDisp = 0;
            for (std::size_t d = 0; d <= 0xFFF; ++d) {
                const std::size_t src = dst + d + 3;
                if (src >= n) break;
                std::size_t len = 0;
                while (len < 18 && len <= dst && data[dst - len] == data[src - len]) ++len;
                if (len > bestLen) { bestLen = len; bestDisp = d; if (len == 18) break; }
            }
            if (bestLen >= 3) {
                ctrl |= static_cast<u8>(0x80 >> bit);
                const u16 seg = static_cast<u16>(((bestLen - 3) << 12) | bestDisp);
                stream.push_back(static_cast<u8>(seg >> 8)); // read first (higher address)
                stream.push_back(static_cast<u8>(seg & 0xFF));
                p -= bestLen;
            } else {
                stream.push_back(data[dst]);
                p -= 1;
            }
        }
        stream[ctrlPos] = ctrl;
    }
    std::reverse(stream.begin(), stream.end());
    // Layout: [token stream][0xFF padding][8-byte footer]. Padding sits between
    // stream and footer and is accounted for by the header length.
    std::size_t pad = 0;
    while ((stream.size() + pad) % 4) ++pad;
    const std::size_t total = stream.size() + pad + 8;
    require(total <= n, "BLZ: data is not compressible enough for in-place BLZ");
    std::vector<u8> out(stream);
    out.insert(out.end(), pad, 0xFF);
    out.resize(total);
    putLe32(out.data() + total - 8, static_cast<u32>(((pad + 8) << 24) | total));
    putLe32(out.data() + total - 4, static_cast<u32>(n - total));
    return out;
}

} // namespace tfh::blz
