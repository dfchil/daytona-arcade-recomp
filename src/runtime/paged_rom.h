// Bounded, read-only access to locally imported ROM images. No guest timing
// changes: a cache miss completes before the requesting bus read returns.
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace rt {

class PagedRom {
public:
    static constexpr size_t kPageBytes = 4096;

    PagedRom(const std::string &path, size_t expected_bytes, size_t cache_bytes)
        : file_(std::fopen(path.c_str(), "rb"), std::fclose), size_(expected_bytes) {
        if (!file_) throw std::runtime_error("cannot open ROM image " + path);
        if (!size_ || size_ % kPageBytes || !cache_bytes || cache_bytes % kPageBytes)
            throw std::runtime_error("ROM image and cache sizes must be positive multiples of 4096");
        if (size_ / kPageBytes >= UINT32_MAX || cache_bytes / kPageBytes >= UINT32_MAX)
            throw std::runtime_error("ROM image or cache has too many pages");
        if (std::fseek(file_.get(), 0, SEEK_END) || std::ftell(file_.get()) != long(size_))
            throw std::runtime_error("wrong ROM image size: " + path);
        cache_.resize(cache_bytes);
        tags_.assign(cache_bytes / kPageBytes, UINT32_MAX);
        slots_.assign(size_ / kPageBytes, UINT32_MAX);
        ages_.resize(tags_.size());
    }

    size_t size() const { return size_; }
    size_t cache_bytes() const { return cache_.size(); }
    uint64_t misses() const { return misses_; }
    using Clock = uint64_t (*)();
    void set_profile_clock(Clock clock) { clock_ = clock; }
    uint64_t read_ticks() const { return read_ticks_; }
    size_t pinned_bytes() const { return pinned_.size(); }

    // Load a bounded offline working set before gameplay. Contents still come
    // from the imported image; the manifest contains addresses, not game data.
    // A failed load leaves the previous pinned set usable.
    void pin_pages(const std::vector<uint32_t> &pages) {
        if (pages.size() > size_ / kPageBytes || pages.size() > UINT32_MAX - tags_.size())
            throw std::runtime_error("too many pinned ROM pages");
        for (size_t i = 0; i < pages.size(); ++i)
            if (pages[i] >= slots_.size() || (i && pages[i] <= pages[i - 1]))
                throw std::runtime_error("pinned ROM pages must be sorted, unique and in range");
        std::vector<uint8_t> data(pages.size() * kPageBytes);
        for (size_t i = 0; i < pages.size(); ++i)
            std::memcpy(data.data() + i * kPageBytes, page(size_t(pages[i]) * kPageBytes), kPageBytes);
        for (auto &slot : slots_) if (slot >= tags_.size()) slot = UINT32_MAX;
        for (size_t i = 0; i < pages.size(); ++i) {
            const auto slot = slots_[pages[i]];
            if (slot != UINT32_MAX) { tags_[slot] = UINT32_MAX; ages_[slot] = 0; }
            slots_[pages[i]] = uint32_t(tags_.size() + i);
        }
        pinned_.swap(data);
    }

    // Optional host-side working-set measurement, including warm cache hits.
    // No bitmap is allocated unless the segmenter explicitly starts a sample.
    void start_access_sample() { accessed_.assign(size_ / kPageBytes, 0); }
    const std::vector<uint8_t> &accessed_pages() const { return accessed_; }

    // O(1) page lookup, fully associative LRU replacement. The inverse map
    // costs four bytes per ROM page (74 KiB across Daytona's five regions).
    // ponytail: misses still block on storage; offline stage packs/prefetch
    // are needed if the active working set exceeds the bounded payload.
    const uint8_t *page(size_t offset) {
        if (offset >= size_) throw std::runtime_error("ROM read outside imported image");
        const uint32_t index = uint32_t(offset / kPageBytes);
        uint32_t slot = slots_[index];
        if (slot != UINT32_MAX && slot >= tags_.size()) {
            if (!accessed_.empty()) accessed_[index] = 1;
            return pinned_.data() + size_t(slot - tags_.size()) * kPageBytes;
        }
        if (slot == UINT32_MAX) {
            const uint64_t before = clock_ ? clock_() : 0;
            slot = uint32_t(std::min_element(ages_.begin(), ages_.end()) - ages_.begin());
            // Failed reads must never expose a partial page under an old tag.
            if (tags_[slot] != UINT32_MAX) slots_[tags_[slot]] = UINT32_MAX;
            tags_[slot] = UINT32_MAX;
            ages_[slot] = 0;
            if (std::fseek(file_.get(), long(size_t(index) * kPageBytes), SEEK_SET) ||
                std::fread(cache_.data() + size_t(slot) * kPageBytes, 1, kPageBytes, file_.get()) != kPageBytes)
                throw std::runtime_error("cannot read imported ROM page");
            tags_[slot] = index;
            slots_[index] = slot;
            ++misses_;
            if (clock_) {
                const uint64_t after = clock_();
                if (after >= before) read_ticks_ += after - before;
            }
        }
        if (!accessed_.empty()) accessed_[index] = 1;
        ages_[slot] = ++sequence_;
        return cache_.data() + size_t(slot) * kPageBytes;
    }

    template <typename T> T read(size_t offset) {
        static_assert(sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4);
        if (offset % sizeof(T)) throw std::runtime_error("unaligned ROM word read");
        T value;
        std::memcpy(&value, page(offset) + offset % kPageBytes, sizeof value);
        return value;
    }

private:
    std::unique_ptr<FILE, decltype(&std::fclose)> file_;
    size_t size_;
    std::vector<uint8_t> cache_;
    std::vector<uint8_t> pinned_;
    std::vector<uint8_t> accessed_;
    std::vector<uint32_t> tags_, slots_;
    std::vector<uint64_t> ages_;
    uint64_t sequence_ = 0;
    uint64_t misses_ = 0;
    Clock clock_ = nullptr;
    uint64_t read_ticks_ = 0;
};

} // namespace rt
