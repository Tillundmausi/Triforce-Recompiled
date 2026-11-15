// SPDX-License-Identifier: GPL-2.0-or-later
// Random-access byte sources so we never need to load a 500+ MB dump into RAM.
#pragma once

#include "../common/bytes.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <vector>

namespace tfh {

class Source {
public:
    virtual ~Source() = default;
    virtual u64 size() const = 0;
    virtual void read(u64 offset, void* dst, std::size_t n) const = 0;

    std::vector<u8> readVec(u64 offset, std::size_t n) const {
        std::vector<u8> v(n);
        if (n) read(offset, v.data(), n);
        return v;
    }
};

class FileSource final : public Source {
public:
    explicit FileSource(const std::filesystem::path& path)
        : file_(path, std::ios::binary) {
        require(file_.good(), "cannot open file: " + path.string());
        size_ = std::filesystem::file_size(path);
    }
    u64 size() const override { return size_; }
    void read(u64 offset, void* dst, std::size_t n) const override {
        require(offset <= size_ && n <= size_ - offset, "file read out of range");
        file_.clear();
        file_.seekg(static_cast<std::streamoff>(offset));
        file_.read(static_cast<char*>(dst), static_cast<std::streamsize>(n));
        require(file_.gcount() == static_cast<std::streamsize>(n), "short file read");
    }

private:
    mutable std::ifstream file_;
    u64 size_ = 0;
};

class MemorySource final : public Source {
public:
    explicit MemorySource(std::vector<u8> data) : data_(std::move(data)) {}
    u64 size() const override { return data_.size(); }
    void read(u64 offset, void* dst, std::size_t n) const override {
        require(offset <= data_.size() && n <= data_.size() - offset, "memory read out of range");
        std::copy_n(data_.data() + offset, n, static_cast<u8*>(dst));
    }

private:
    std::vector<u8> data_;
};

// A window into another source. The parent must outlive the SubSource.
class SubSource final : public Source {
public:
    SubSource(const Source& parent, u64 base, u64 length) : parent_(parent), base_(base), len_(length) {
        require(base <= parent.size() && length <= parent.size() - base, "sub-range exceeds parent");
    }
    u64 size() const override { return len_; }
    void read(u64 offset, void* dst, std::size_t n) const override {
        require(offset <= len_ && n <= len_ - offset, "sub-range read out of range");
        parent_.read(base_ + offset, dst, n);
    }

private:
    const Source& parent_;
    u64 base_, len_;
};

} // namespace tfh
