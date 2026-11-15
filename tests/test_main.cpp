// SPDX-License-Identifier: GPL-2.0-or-later
// Unit tests for the Phase-1 tooling. No external test framework so the
// project builds offline.
#include "../src/common/sha256.hpp"
#include "../src/formats/blz.hpp"
#include "../src/formats/ctr.hpp"
#include "../src/formats/romfs.hpp"
#include "../src/inspect/inspector.hpp"
#include "../src/inspect/report.hpp"

#include <cstdio>
#include <cstring>
#include <functional>
#include <iostream>
#include <sstream>

using namespace tfh;

namespace {
int g_failures = 0;
int g_checks = 0;
#define CHECK(cond)                                                                    \
    do {                                                                               \
        ++g_checks;                                                                    \
        if (!(cond)) { ++g_failures; std::cerr << __FILE__ << ":" << __LINE__ << ": CHECK failed: " #cond "\n"; } \
    } while (0)

template <class F>
bool throws(F&& f) {
    try { f(); } catch (const FormatError&) { return true; }
    return false;
}

void putBe16(std::vector<u8>& v, std::size_t o, u16 x) { v[o] = u8(x >> 8); v[o + 1] = u8(x); }
void putBe32(std::vector<u8>& v, std::size_t o, u32 x) { for (int i = 0; i < 4; ++i) v[o + i] = u8(x >> (24 - 8 * i)); }
void putBe64(std::vector<u8>& v, std::size_t o, u64 x) { putBe32(v, o, u32(x >> 32)); putBe32(v, o + 4, u32(x)); }
void putLe16(std::vector<u8>& v, std::size_t o, u16 x) { v[o] = u8(x); v[o + 1] = u8(x >> 8); }
void putLe32v(std::vector<u8>& v, std::size_t o, u32 x) { putLe32(v.data() + o, x); }
void putLe64(std::vector<u8>& v, std::size_t o, u64 x) { putLe32v(v, o, u32(x)); putLe32v(v, o + 4, u32(x >> 32)); }
void putStr(std::vector<u8>& v, std::size_t o, const char* s) { std::memcpy(v.data() + o, s, std::strlen(s)); }
void putUtf16(std::vector<u8>& v, std::size_t o, const char* s) { for (std::size_t i = 0; s[i]; ++i) putLe16(v, o + i * 2, u8(s[i])); }

// ---------------------------------------------------------------------------
void testSha256() {
    const auto empty = Sha256::hash({});
    CHECK(toHex(empty) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    const u8 abc[] = {'a', 'b', 'c'};
    CHECK(toHex(Sha256::hash(abc)) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    std::string longMsg = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    CHECK(toHex(Sha256::hash(std::span<const u8>(reinterpret_cast<const u8*>(longMsg.data()), longMsg.size()))) ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    // incremental == one-shot
    std::vector<u8> big(100000);
    for (std::size_t i = 0; i < big.size(); ++i) big[i] = u8(i * 7 + (i >> 5));
    Sha256 s;
    for (std::size_t i = 0; i < big.size(); i += 777)
        s.update(std::span<const u8>(big).subspan(i, std::min<std::size_t>(777, big.size() - i)));
    CHECK(s.finish() == Sha256::hash(big));
}

std::vector<u8> makeCodeLike(std::size_t n) {
    std::vector<u8> v(n);
    u32 state = 0x12345678;
    for (std::size_t i = 0; i < n; i += 4) {
        state = state * 1103515245u + 12345u;
        // mostly repeated ARM-ish words with some noise -> compressible
        const u32 w = (state >> 28) < 10 ? 0xE92D4010u + ((i / 64) & 3) : state;
        putLe32(v.data() + i, w);
    }
    return v;
}

void testBlz() {
    for (std::size_t n : {64u, 1000u, 4096u, 0x4000u}) {
        const auto data = makeCodeLike(n);
        const auto comp = blz::compress(data);
        CHECK(comp.size() < data.size());
        const auto dec = blz::decompress(comp);
        CHECK(dec == data);
    }
    // Overlapping runs (long run of the same byte).
    std::vector<u8> run(3000, 0xAA);
    run[0] = 1; run[2999] = 2;
    CHECK(blz::decompress(blz::compress(run)) == run);
    // Raw (uncompressed) prefix below the compressed region must be preserved:
    // build footer manually with compLen < size.
    {
        auto data = makeCodeLike(2048);
        auto comp = blz::compress(std::span<const u8>(data).subspan(512));
        std::vector<u8> withPrefix(data.begin(), data.begin() + 512);
        withPrefix.insert(withPrefix.end(), comp.begin(), comp.end());
        CHECK(blz::decompress(withPrefix) == data);
    }
    CHECK(throws([] { std::vector<u8> tiny(4); blz::decompress(tiny); }));
    CHECK(throws([] { std::vector<u8> bad(16, 0); putLe32(bad.data() + 8, 0xFF000010); blz::decompress(bad); }));
}

void testKernelCaps() {
    const u32 desc[] = {
        0xF0000006u,              // table 0: svc 1, 2
        0xF1000001u,              // table 1: svc 24
        0xFC00022Eu,              // kernel 2.46
        0xFE000200u,              // handle table 0x200
        0xFF800000u | 0x1F000,    // map range start 0x1F000000
        0xFF800000u | 0x1F600,    // map range end   0x1F600000
        0xFFE00000u | 0x1EC00,    // map page 0x1EC00000
        0xFFFFFFFFu,
    };
    const auto k = ctr::decodeKernelCaps(desc);
    CHECK((k.allowedSvcs == std::vector<u32>{1, 2, 24}));
    CHECK(k.kernelMajor == 2 && k.kernelMinor == 46);
    CHECK(k.handleTableSize == 0x200);
    CHECK(k.mappedRanges.size() == 1 && k.mappedRanges[0].first == 0x1F000000u && k.mappedRanges[0].second == 0x1F600000u);
    CHECK(k.mappedPages.size() == 1 && k.mappedPages[0] == 0x1EC00000u);
    CHECK(k.unknown.empty());
}

// ---------------------------------------------------------------------------
// Synthetic, fully self-made CIA (no Nintendo data involved).
struct FakeCiaOptions {
    bool ncchNoCrypto = true;
    bool titleKeyEncrypted = false;
    bool corruptCode = false;
};

constexpr u64 kTitleId = 0x0004000000ABC000ull;

std::vector<u8> buildSmdh() {
    std::vector<u8> s(ctr::kSmdhSize, 0);
    putStr(s, 0, "SMDH");
    putUtf16(s, 0x8 + 3 * 0x200, "Testspiel");          // de short
    putUtf16(s, 0x8 + 3 * 0x200 + 0x80, "Ein Testspiel"); // de long
    putUtf16(s, 0x8 + 1 * 0x200, "Test Game");           // en short
    putLe32v(s, 0x2018, 0x04);                             // EUR
    return s;
}

std::vector<u8> buildCia(const FakeCiaOptions& opt, std::vector<u8>* plainCodeOut = nullptr) {
    // --- code: text 2 pages, ro 1 page, data 1 page
    auto code = makeCodeLike(0x4000);
    if (plainCodeOut) *plainCodeOut = code;
    auto comp = blz::compress(code);

    // --- ExeFS
    const auto smdh = buildSmdh();
    const u32 iconOff = u32(alignUp(comp.size(), 0x200));
    std::vector<u8> exefs(ctr::kExeFsHeaderSize + alignUp(iconOff + smdh.size(), 0x200), 0);
    putStr(exefs, 0, ".code");
    putLe32v(exefs, 8, 0); putLe32v(exefs, 12, u32(comp.size()));
    putStr(exefs, 16, "icon");
    putLe32v(exefs, 24, iconOff); putLe32v(exefs, 28, u32(smdh.size()));
    std::memcpy(exefs.data() + 0x200, comp.data(), comp.size());
    std::memcpy(exefs.data() + 0x200 + iconOff, smdh.data(), smdh.size());
    auto h0 = Sha256::hash(comp);
    auto h1 = Sha256::hash(smdh);
    std::memcpy(exefs.data() + 0x200 - 32, h0.data(), 32);
    std::memcpy(exefs.data() + 0x200 - 64, h1.data(), 32);
    if (opt.corruptCode) exefs[0x200 + 3] ^= 0xFF;

    // --- ExHeader
    std::vector<u8> exh(0x800, 0);
    putStr(exh, 0, "TESTAPP");
    exh[0x0D] = 1; // compressed code
    auto cs = [&](std::size_t o, u32 a, u32 p, u32 s) { putLe32v(exh, o, a); putLe32v(exh, o + 4, p); putLe32v(exh, o + 8, s); };
    cs(0x10, 0x00100000, 2, 0x1800);
    putLe32v(exh, 0x1C, 0x4000);
    cs(0x20, 0x00102000, 1, 0x400);
    cs(0x30, 0x00103000, 1, 0x200);
    putLe32v(exh, 0x3C, 0x100);
    putLe64(exh, 0x40, 0x0004013000001502ull);
    putLe64(exh, 0x200, kTitleId);
    exh[0x20F] = 0x30;
    putStr(exh, 0x250, "fs:USER");
    putStr(exh, 0x258, "gsp::Gpu");
    putStr(exh, 0x260, "nwm::UDS");
    const u32 kc[] = {0xF0000006u, 0xF1000001u, 0xFC00022Eu, 0xFE000200u};
    for (std::size_t i = 0; i < 28; ++i) putLe32v(exh, 0x370 + i * 4, i < 4 ? kc[i] : 0xFFFFFFFFu);

    // --- NCCH
    const u32 exefsOffMU = (0x200 + 0x800) / 0x200;
    std::vector<u8> ncch(0x200 + 0x800, 0);
    ncch.insert(ncch.end(), exefs.begin(), exefs.end());
    putStr(ncch, 0x100, "NCCH");
    putLe32v(ncch, 0x104, u32(ncch.size() / 0x200));
    putLe64(ncch, 0x108, kTitleId);
    putStr(ncch, 0x110, "01");
    putLe16(ncch, 0x112, 2);
    putLe64(ncch, 0x118, kTitleId);
    putStr(ncch, 0x150, "CTR-P-TSTP");
    auto exhHash = Sha256::hash(std::span<const u8>(exh.data(), 0x400));
    std::memcpy(ncch.data() + 0x160, exhHash.data(), 32);
    putLe32v(ncch, 0x180, 0x400);
    ncch[0x188 + 5] = 0x03;
    ncch[0x188 + 7] = opt.ncchNoCrypto ? 0x04 : 0x00;
    putLe32v(ncch, 0x1A0, exefsOffMU);
    putLe32v(ncch, 0x1A4, u32(exefs.size() / 0x200));
    putLe32v(ncch, 0x1A8, 1);
    auto superHash = Sha256::hash(std::span<const u8>(exefs.data(), 0x200));
    std::memcpy(ncch.data() + 0x1C0, superHash.data(), 32);
    std::memcpy(ncch.data() + 0x200, exh.data(), exh.size());
    if (opt.titleKeyEncrypted) std::memset(ncch.data() + 0x100, 0x5A, 4); // simulate ciphertext

    // --- Ticket (RSA-2048 signature)
    const std::size_t sig = 4 + 0x100 + 0x3C;
    std::vector<u8> ticket(sig + 0x210, 0);
    putBe32(ticket, 0, 0x010004);
    putStr(ticket, sig, "Root-CA00000003-XS0000000c");
    putBe64(ticket, sig + 0x9C, kTitleId);

    // --- TMD
    std::vector<u8> tmd(sig + 0xC4 + 0x900 + 0x30, 0);
    putBe32(tmd, 0, 0x010004);
    putStr(tmd, sig, "Root-CA00000003-CP0000000b");
    putBe64(tmd, sig + 0x4C, kTitleId);
    putBe16(tmd, sig + 0x9C, 0);
    putBe16(tmd, sig + 0x9E, 1);
    const std::size_t ch = sig + 0xC4 + 0x900;
    putBe32(tmd, ch, 0);
    putBe16(tmd, ch + 4, 0);
    putBe16(tmd, ch + 6, opt.titleKeyEncrypted ? 1 : 0);
    putBe64(tmd, ch + 8, ncch.size());
    auto ncchHash = Sha256::hash(ncch);
    std::memcpy(tmd.data() + ch + 0x10, ncchHash.data(), 32);

    // --- CIA
    std::vector<u8> cia(0x2020, 0);
    putLe32v(cia, 0, 0x2020);
    putLe32v(cia, 0x08, 0);
    putLe32v(cia, 0x0C, u32(ticket.size()));
    putLe32v(cia, 0x10, u32(tmd.size()));
    putLe32v(cia, 0x14, 0);
    putLe64(cia, 0x18, ncch.size());
    cia[0x20] = 0x80;
    auto appendAligned = [&](const std::vector<u8>& part) {
        cia.resize(alignUp(cia.size(), 64), 0);
        cia.insert(cia.end(), part.begin(), part.end());
    };
    appendAligned({}); // cert chain (empty)
    appendAligned(ticket);
    appendAligned(tmd);
    appendAligned(ncch);
    return cia;
}

void testInspectComplete() {
    std::vector<u8> plain;
    MemorySource src(buildCia({}, &plain));
    const auto r = inspect::inspect(src);
    CHECK(r.container == inspect::Container::Cia);
    CHECK(r.status == inspect::Status::Complete);
    CHECK(r.cia && r.cia->tmd.titleId == kTitleId && r.cia->ticket.titleId == kTitleId);
    CHECK(r.cia->tmd.contents.size() == 1);
    CHECK(r.ncch && r.ncch->productCode == "CTR-P-TSTP");
    CHECK(ctr::regionFromProductCode(r.ncch->productCode) == "Europe");
    CHECK(r.exheaderHashOk && r.exefsSuperblockOk);
    CHECK(r.exefsHashOk.size() == 2 && r.exefsHashOk[0] && r.exefsHashOk[1]);
    CHECK(r.exheader && r.exheader->appTitle == "TESTAPP");
    CHECK(r.exheader->text.address == 0x00100000u && r.exheader->data.size == 0x200u);
    CHECK((r.exheader->services == std::vector<std::string>{"fs:USER", "gsp::Gpu", "nwm::UDS"}));
    CHECK((r.exheader->kernel.allowedSvcs == std::vector<u32>{1, 2, 24}));
    CHECK(r.exheader->dependencies.size() == 1);
    CHECK(r.code == plain);
    CHECK(r.segments.size() == 3 && r.segments[1].fileOffset == 0x2000u && r.segments[2].fileOffset == 0x3000u);
    CHECK(r.warnings.empty());
    CHECK(r.icon && r.icon->titles[3].shortName == "Testspiel" && r.icon->titles[3].longName == "Ein Testspiel");
    CHECK(ctr::regionLockoutString(r.icon->regionLockout) == "EUR");
    CHECK(inspect::matchesTarget(r) == std::optional<bool>(false)); // synthetic title is not TFH

    std::ostringstream json;
    inspect::writeJsonReport(r, "fake.cia", json);
    const std::string js = json.str();
    CHECK(js.find("\"status\": \"complete\"") != std::string::npos);
    CHECK(js.find("\"nwm::UDS\"") != std::string::npos);
    CHECK(js.find("\"name\": \"ControlMemory\"") != std::string::npos);
    CHECK(js.find("0004000000ABC000") != std::string::npos);
    std::ostringstream summary;
    inspect::writeSummary(r, summary);
    CHECK(summary.str().find("Europe") != std::string::npos);
}

void testInspectEncrypted() {
    {
        FakeCiaOptions o; o.ncchNoCrypto = false;
        MemorySource src(buildCia(o));
        const auto r = inspect::inspect(src);
        CHECK(r.status == inspect::Status::Encrypted);
        CHECK(r.ncch.has_value());
        CHECK(!r.exheader.has_value() && r.code.empty());
    }
    {
        FakeCiaOptions o; o.titleKeyEncrypted = true;
        MemorySource src(buildCia(o));
        const auto r = inspect::inspect(src);
        CHECK(r.status == inspect::Status::Encrypted);
        CHECK(!r.ncch.has_value());
        CHECK(r.cia && r.cia->tmd.contents[0].encrypted());
    }
}

void testInspectCorrupt() {
    FakeCiaOptions o; o.corruptCode = true;
    MemorySource src(buildCia(o));
    bool threwOrMismatch = false;
    try {
        const auto r = inspect::inspect(src);
        threwOrMismatch = r.status == inspect::Status::HashMismatch && !r.exefsHashOk[0];
    } catch (const FormatError&) {
        threwOrMismatch = true; // corrupted BLZ stream may also be rejected outright
    }
    CHECK(threwOrMismatch);
}

void testGarbage() {
    MemorySource src(std::vector<u8>(0x4000, 0x11));
    CHECK(throws([&] { inspect::inspect(src); }));
}
// Synthetic RomFS level 3: "/a.bin" and "/Stage/Forest.arc".
std::vector<u8> buildRomfsL3() {
    auto align4 = [](std::size_t n) { return (n + 3) & ~std::size_t(3); };
    // directory metadata: root (0x00), "Stage" (0x18)
    std::vector<u8> dirs(0x18 + 0x18 + align4(10), 0);
    auto dir = [&](std::size_t o, u32 parent, u32 sib, u32 child, u32 file, const char* name) {
        putLe32v(dirs, o, parent); putLe32v(dirs, o + 4, sib); putLe32v(dirs, o + 8, child);
        putLe32v(dirs, o + 12, file); putLe32v(dirs, o + 16, 0xFFFFFFFF);
        putLe32v(dirs, o + 20, u32(std::strlen(name) * 2)); putUtf16(dirs, o + 24, name);
    };
    const u32 fileA = 0, fileB = u32(0x20 + align4(10));
    dir(0x00, 0, 0xFFFFFFFF, 0x18, fileA, "");
    dir(0x18, 0, 0xFFFFFFFF, 0xFFFFFFFF, fileB, "Stage");
    std::vector<u8> files(fileB + 0x20 + align4(20), 0);
    auto file = [&](std::size_t o, u32 parent, u64 off, u64 size, const char* name) {
        putLe32v(files, o, parent); putLe32v(files, o + 4, 0xFFFFFFFF);
        putLe64(files, o + 8, off); putLe64(files, o + 16, size); putLe32v(files, o + 24, 0xFFFFFFFF);
        putLe32v(files, o + 28, u32(std::strlen(name) * 2)); putUtf16(files, o + 32, name);
    };
    file(fileA, 0x00, 0x00, 0x10, "a.bin");
    file(fileB, 0x18, 0x10, 0x30, "Forest.arc");
    const u32 dirOff = 0x28, fileOff = u32(dirOff + dirs.size()), dataOff = u32(fileOff + files.size() + 0x10);
    std::vector<u8> l3(dataOff + 0x40, 0);
    putLe32v(l3, 0x00, 0x28);
    putLe32v(l3, 0x04, dirOff); putLe32v(l3, 0x08, 0);           // dir hash table (empty)
    putLe32v(l3, 0x0C, dirOff); putLe32v(l3, 0x10, u32(dirs.size()));
    putLe32v(l3, 0x14, fileOff); putLe32v(l3, 0x18, 0);          // file hash table (empty)
    putLe32v(l3, 0x1C, fileOff); putLe32v(l3, 0x20, u32(files.size()));
    putLe32v(l3, 0x24, dataOff);
    std::memcpy(l3.data() + dirOff, dirs.data(), dirs.size());
    std::memcpy(l3.data() + fileOff, files.data(), files.size());
    return l3;
}

void testRomfs() {
    const auto l3 = buildRomfsL3();
    MemorySource src(l3);
    const auto idx = romfs::parseLevel3(src);
    CHECK(idx.files().size() == 2);
    const u64 dataOff = le32(l3, 0x24);
    const auto* a = idx.find(dataOff + 0x0F);
    CHECK(a && a->path == "/a.bin");
    const auto* b = idx.find(dataOff + 0x10);
    CHECK(b && b->path == "/Stage/Forest.arc" && b->size == 0x30);
    CHECK(idx.find(dataOff + 0x3F) == b);
    CHECK(idx.find(dataOff + 0x40) == nullptr);
    CHECK(idx.find(0x10) == nullptr); // header area
    // A directory loop must be rejected, not hang.
    auto bad = l3;
    putLe32v(bad, 0x28 + 0x18 + 8, 0x18); // "Stage" lists itself as child
    MemorySource badSrc(bad);
    CHECK(throws([&] { romfs::parseLevel3(badSrc); }));
    // IVFC: level 3 starts at align(0x60 + master hash size, level-3 block size).
    std::vector<u8> ivfc(0x60, 0);
    putStr(ivfc, 0, "IVFC");
    putLe32v(ivfc, 0x08, 0x20);
    putLe32v(ivfc, 0x4C, 12);
    MemorySource ivfcSrc(ivfc);
    CHECK(romfs::level3OffsetFromIvfc(ivfcSrc) == 0x1000);
}
} // namespace

int main(int argc, char** argv) {
    // Helper for CLI end-to-end tests: write the synthetic CIA to a file.
    if (argc == 3 && std::string(argv[1]) == "--write-fake-cia") {
        const auto cia = buildCia({});
        std::FILE* f = std::fopen(argv[2], "wb");
        if (!f) return 1;
        std::fwrite(cia.data(), 1, cia.size(), f);
        std::fclose(f);
        return 0;
    }
    const std::pair<const char*, std::function<void()>> tests[] = {
        {"sha256", testSha256},
        {"blz", testBlz},
        {"kernel_caps", testKernelCaps},
        {"inspect_complete", testInspectComplete},
        {"inspect_encrypted", testInspectEncrypted},
        {"inspect_corrupt", testInspectCorrupt},
        {"garbage", testGarbage},
        {"romfs", testRomfs},
    };
    for (const auto& [name, fn] : tests) {
        const int before = g_failures;
        try { fn(); } catch (const std::exception& e) {
            ++g_failures; std::cerr << name << ": unexpected exception: " << e.what() << '\n';
        }
        std::cout << (g_failures == before ? "[ OK ] " : "[FAIL] ") << name << '\n';
    }
    std::cout << g_checks << " checks, " << g_failures << " failures\n";
    return g_failures ? 1 : 0;
}
