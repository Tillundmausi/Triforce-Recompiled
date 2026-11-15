// SPDX-License-Identifier: GPL-2.0-or-later
// Minimal streaming JSON writer (no dependencies).
#pragma once

#include <cstdint>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace tfh {

class JsonWriter {
public:
    explicit JsonWriter(std::ostream& os) : os_(os) {}

    void beginObject(std::string_view key = {}) { open(key, '{'); }
    void endObject() { close('}'); }
    void beginArray(std::string_view key = {}) { open(key, '['); }
    void endArray() { close(']'); }

    void field(std::string_view key, std::string_view v) { prefix(key); str(v); }
    void field(std::string_view key, const char* v) { field(key, std::string_view(v)); }
    void field(std::string_view key, const std::string& v) { field(key, std::string_view(v)); }
    void field(std::string_view key, bool v) { prefix(key); os_ << (v ? "true" : "false"); }
    void field(std::string_view key, std::int64_t v) { prefix(key); os_ << v; }
    void field(std::string_view key, std::uint64_t v) { prefix(key); os_ << v; }
    void field(std::string_view key, int v) { field(key, std::int64_t(v)); }
    void field(std::string_view key, unsigned v) { field(key, std::uint64_t(v)); }
    void value(std::string_view v) { field({}, v); }
    void value(std::uint64_t v) { field({}, v); }

    void finish() { os_ << '\n'; }

private:
    void str(std::string_view s) {
        os_ << '"';
        for (char ch : s) {
            const unsigned char c = static_cast<unsigned char>(ch);
            switch (c) {
            case '"': os_ << "\\\""; break;
            case '\\': os_ << "\\\\"; break;
            case '\n': os_ << "\\n"; break;
            case '\r': os_ << "\\r"; break;
            case '\t': os_ << "\\t"; break;
            default:
                if (c < 0x20) { const char* hx = "0123456789abcdef"; os_ << "\\u00" << hx[c >> 4] << hx[c & 15]; }
                else os_ << ch;
            }
        }
        os_ << '"';
    }
    void prefix(std::string_view key) {
        if (!first_.empty()) {
            if (!first_.back()) os_ << ',';
            first_.back() = false;
            os_ << '\n' << std::string(first_.size() * 2, ' ');
        }
        if (!key.empty()) { str(key); os_ << ": "; }
    }
    void open(std::string_view key, char c) { prefix(key); os_ << c; first_.push_back(true); }
    void close(char c) {
        const bool empty = first_.back();
        first_.pop_back();
        if (!empty) os_ << '\n' << std::string(first_.size() * 2, ' ');
        os_ << c;
    }

    std::ostream& os_;
    std::vector<bool> first_;
};

} // namespace tfh
