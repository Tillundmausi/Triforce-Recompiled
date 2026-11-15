// SPDX-License-Identifier: GPL-2.0-or-later
#include "report.hpp"

#include "../common/json.hpp"
#include "../formats/svc_names.hpp"

namespace tfh::inspect {

namespace {
const char* kLang[16] = {"ja", "en", "fr", "de", "it", "es", "zh_hans", "ko",
                         "nl", "pt", "ru", "zh_hant", "l12", "l13", "l14", "l15"};

void writeSmdh(JsonWriter& j, std::string_view key, const ctr::Smdh& s) {
    j.beginObject(key);
    j.field("region_lockout", ctr::regionLockoutString(s.regionLockout));
    j.beginObject("titles");
    for (int i = 0; i < 16; ++i) {
        const auto& t = s.titles[i];
        if (t.shortName.empty() && t.longName.empty()) continue;
        j.beginObject(kLang[i]);
        j.field("short", t.shortName);
        j.field("long", t.longName);
        j.field("publisher", t.publisher);
        j.endObject();
    }
    j.endObject();
    j.endObject();
}

std::string bestTitle(const ctr::Smdh& s) {
    for (int lang : {3, 1, 0}) // de, en, ja
        if (!s.titles[lang].shortName.empty()) return s.titles[lang].shortName;
    return {};
}
} // namespace

void writeJsonReport(const Result& r, const std::string& inputName, std::ostream& os) {
    JsonWriter j(os);
    j.beginObject();
    j.field("tool", "ctrinspect");
    j.field("input", inputName);
    j.field("container", containerName(r.container));
    j.field("status", statusName(r.status));
    j.field("status_message", r.statusMessage);
    if (auto m = matchesTarget(r)) j.field("is_target_version", *m);
    j.beginArray("warnings");
    for (const auto& w : r.warnings) j.value(w);
    j.endArray();

    if (r.cia) {
        const auto& c = *r.cia;
        j.beginObject("cia");
        j.field("title_id", hex64(c.tmd.titleId));
        j.field("title_version", unsigned(c.tmd.titleVersion));
        j.field("tmd_issuer", c.tmd.issuer);
        j.field("ticket_issuer", c.ticket.issuer);
        j.field("ticket_id", hex64(c.ticket.ticketId));
        j.field("ticket_console_id", hex32(c.ticket.consoleId));
        j.field("ticket_common_key_index", unsigned(c.ticket.commonKeyIndex));
        j.field("save_data_size", unsigned(c.tmd.saveDataSize));
        j.beginArray("contents");
        for (std::size_t i = 0; i < c.tmd.contents.size(); ++i) {
            const auto& ch = c.tmd.contents[i];
            j.beginObject();
            j.field("index", unsigned(ch.index));
            j.field("id", hex32(ch.id));
            j.field("type", hex32(ch.type));
            j.field("encrypted", ch.encrypted());
            j.field("optional", bool(ch.type & ctr::kContentOptional));
            j.field("size", ch.size);
            j.field("present", c.contentOffsets[i].has_value());
            j.field("sha256", toHex(ch.hash));
            j.endObject();
        }
        j.endArray();
        if (c.metaCoreVersion) j.field("meta_core_version", unsigned(*c.metaCoreVersion));
        j.beginArray("meta_dependencies");
        for (u64 d : c.metaDependencies) j.value(hex64(d));
        j.endArray();
        if (c.metaIcon) writeSmdh(j, "meta_icon", *c.metaIcon);
        j.endObject();
    }

    if (r.ncch) {
        const auto& n = *r.ncch;
        j.beginObject("ncch");
        j.field("program_id", hex64(n.programId));
        j.field("partition_id", hex64(n.partitionId));
        j.field("product_code", n.productCode);
        j.field("region", ctr::regionFromProductCode(n.productCode));
        j.field("maker_code", n.makerCode);
        j.field("ncch_version", unsigned(n.version));
        j.field("media_unit", unsigned(n.mediaUnit()));
        j.field("content_size", u64(n.contentSizeMU) * n.mediaUnit());
        j.field("crypto_method", hex32(n.cryptoMethod()));
        j.field("no_crypto", n.noCrypto());
        j.field("fixed_key", n.fixedKey());
        j.field("uses_seed", n.usesSeed());
        j.field("executable", n.isExecutable());
        j.field("exefs_size", u64(n.exefsSize) * n.mediaUnit());
        j.field("romfs_size", u64(n.romfsSize) * n.mediaUnit());
        j.endObject();
    }

    if (r.exheader) {
        const auto& e = *r.exheader;
        j.beginObject("exheader");
        j.field("hash_ok", r.exheaderHashOk);
        j.field("app_title", e.appTitle);
        j.field("compressed_code", e.compressedCode);
        j.field("sd_application", e.sdApplication);
        j.field("remaster_version", unsigned(e.remasterVersion));
        j.field("stack_size", hex32(e.stackSize));
        j.field("bss_size", hex32(e.bssSize));
        j.beginArray("segments");
        for (const auto& s : r.segments) {
            j.beginObject();
            j.field("name", s.name);
            j.field("address", hex32(s.address));
            j.field("size", hex32(s.size));
            j.field("mapped_size", hex32(s.mappedSize));
            j.field("code_file_offset", hex32(s.fileOffset));
            j.endObject();
        }
        j.endArray();
        j.field("save_data_size", e.saveDataSize);
        j.field("jump_id", hex64(e.jumpId));
        j.field("program_id", hex64(e.programId));
        j.field("core_version", hex32(e.coreVersion));
        j.field("priority", unsigned(e.priority));
        j.field("ideal_processor", unsigned(e.flag0 & 3));
        j.field("affinity_mask", unsigned((e.flag0 >> 2) & 3));
        j.field("old3ds_system_mode", unsigned(e.flag0 >> 4));
        j.field("new3ds_system_mode", unsigned(e.flag2 & 0xF));
        j.field("new3ds_l2_cache", bool(e.flag1 & 1));
        j.field("new3ds_804mhz", bool(e.flag1 & 2));
        j.field("extdata_id", hex64(e.extdataId));
        j.beginArray("dependencies");
        for (u64 d : e.dependencies) j.value(hex64(d));
        j.endArray();
        j.beginArray("services");
        for (const auto& s : e.services) j.value(s);
        j.endArray();
        j.beginObject("kernel_caps");
        j.field("kernel_version", std::to_string(e.kernel.kernelMajor) + "." + std::to_string(e.kernel.kernelMinor));
        j.field("handle_table_size", e.kernel.handleTableSize);
        if (e.kernel.kernelFlags) j.field("kernel_flags", hex32(*e.kernel.kernelFlags));
        j.beginArray("allowed_svcs");
        for (u32 s : e.kernel.allowedSvcs) {
            j.beginObject();
            j.field("id", hex32(s));
            j.field("name", ctr::svcName(s));
            j.endObject();
        }
        j.endArray();
        j.beginArray("mapped_ranges");
        for (auto [a, b] : e.kernel.mappedRanges) j.value(hex32(a) + "-" + hex32(b));
        j.endArray();
        j.beginArray("mapped_pages");
        for (u32 p : e.kernel.mappedPages) j.value(hex32(p));
        j.endArray();
        j.beginArray("unknown_descriptors");
        for (u32 d : e.kernel.unknown) j.value(hex32(d));
        j.endArray();
        j.endObject();
        j.endObject();
    }

    if (!r.exefs.empty()) {
        j.field("exefs_superblock_hash_ok", r.exefsSuperblockOk);
        j.beginArray("exefs");
        for (std::size_t i = 0; i < r.exefs.size(); ++i) {
            j.beginObject();
            j.field("name", r.exefs[i].name);
            j.field("offset", hex32(r.exefs[i].offset));
            j.field("size", unsigned(r.exefs[i].size));
            j.field("hash_ok", bool(r.exefsHashOk[i]));
            j.endObject();
        }
        j.endArray();
    }
    if (r.icon) writeSmdh(j, "icon", *r.icon);
    if (!r.code.empty()) {
        j.beginObject("code");
        j.field("size", u64(r.code.size()));
        j.field("sha256", toHex(r.codeSha256));
        j.endObject();
    }
    j.endObject();
    j.finish();
}

void writeSummary(const Result& r, std::ostream& os) {
    os << "Container:      " << containerName(r.container) << '\n';
    if (r.cia) {
        os << "Title-ID:       " << hex64(r.cia->tmd.titleId) << "  (Version " << r.cia->tmd.titleVersion << ")\n";
        os << "Contents:       " << r.cia->tmd.contents.size() << '\n';
        for (std::size_t i = 0; i < r.cia->tmd.contents.size(); ++i) {
            const auto& c = r.cia->tmd.contents[i];
            os << "  [" << c.index << "] " << c.size << " bytes"
               << (c.encrypted() ? ", encrypted (title key)" : ", not encrypted")
               << (r.cia->contentOffsets[i] ? "" : ", NOT included") << '\n';
        }
        if (r.cia->metaIcon) os << "Title (meta):   " << bestTitle(*r.cia->metaIcon) << '\n';
    }
    if (r.ncch) {
        os << "Product code:   " << r.ncch->productCode << "  -> region " << ctr::regionFromProductCode(r.ncch->productCode) << '\n';
        os << "Program ID:     " << hex64(r.ncch->programId) << '\n';
        os << "NCCH crypto:    " << (r.ncch->noCrypto() ? "none (decrypted)" : "ENCRYPTED") << '\n';
    }
    if (r.exheader) {
        const auto& e = *r.exheader;
        os << "ExHeader:       " << e.appTitle << ", hash " << (r.exheaderHashOk ? "OK" : "MISMATCH") << '\n';
        for (const auto& s : r.segments)
            os << "  " << s.name << "  @ " << hex32(s.address) << "  size " << hex32(s.size) << '\n';
        os << "  .bss    size " << hex32(e.bssSize) << ", Stack " << hex32(e.stackSize) << '\n';
        os << "Services:       " << e.services.size() << "  SVCs: " << e.kernel.allowedSvcs.size() << '\n';
    }
    if (r.icon) os << "Title:          " << bestTitle(*r.icon) << '\n';
    if (!r.code.empty()) os << ".code:          " << r.code.size() << " bytes, SHA-256 " << toHex(r.codeSha256) << '\n';
    if (auto m = matchesTarget(r))
        os << "Target version: " << (*m ? "YES (EUR v0.0.0)" : "NO - the project targets CTR-P-EA3P v0.0.0") << '\n';
    for (const auto& w : r.warnings) os << "WARNING: " << w << '\n';
    os << "Status:         " << statusName(r.status) << " - " << r.statusMessage << '\n';
}

} // namespace tfh::inspect
