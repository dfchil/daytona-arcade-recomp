// Dreamcast PVR working-set accounting for a native Model 2 frame.
// It deliberately records headers and byte counts only: no game data leaves
// the user's locally generated images.
#pragma once

#include "runtime/geo.h"

#include <algorithm>
#include <cstdint>
#include <set>
#include <vector>

namespace tools::dreamcast {

constexpr uint64_t kKiB = 1024;
constexpr uint64_t kMiB = 1024 * kKiB;
constexpr uint64_t kPvrVramBytes = 8 * kMiB;
// Packed 24-bit RGB scanout (KOS PM_RGB888P), no dithering.
// PVR frame_size uses exactly three bytes per pixel, in both framebuffers.
constexpr uint64_t kScanoutBytes = 2 * 640 * 480 * 3;
// KOS pvr_allocate_buffers with enDjinn's defaults: two 1 MiB vertex
// buffers, five 16-word bins per 32x32 tile, two overflow sets, and tile
// matrices (72-byte header + 6 words + 6 words per tile, aligned to 128).
constexpr uint64_t kTaVertexBytes = 2 * kMiB;
constexpr uint64_t kTaOpbBytes = 2 * 5 * 16 * 4 * (640 / 32) * (480 / 32) * 3;
constexpr uint64_t kTaTileBytes = 2 * ((72 + 24 + 24 * (640 / 32) * (480 / 32) + 127) / 128 * 128);
constexpr uint64_t kTaBytes = kTaVertexBytes + kTaOpbBytes + kTaTileBytes;
// Conservative System24 reservation, not approval of PAL8 (excluded by the
// target format constraint). Four raw PAL4 layers need 512 KiB, but palette
// groups/duplication and geometry still need measured accounting. Keep slack
// until that layout is established; do not spend the apparent saving twice.
constexpr uint64_t kSystem24Bytes = 2 * 512 * 512 * 2;
constexpr uint64_t kUiBytes = 256 * kKiB;
constexpr uint64_t kReserveBytes = 1 * kMiB;
constexpr uint64_t kTextureCacheBytes = kPvrVramBytes - kScanoutBytes - kTaBytes - kSystem24Bytes - kUiBytes - kReserveBytes;

// RAM residency, not PVR texture residency. Samples contain one byte per ROM
// page; the stage union is a replay-derived lower bound, not coverage proof.
class RomUsage {
public:
    uint64_t add_frame(const std::vector<uint8_t> &accessed) {
        if (pages_.size() < accessed.size()) pages_.resize(accessed.size());
        uint64_t count = 0;
        for (size_t i = 0; i < accessed.size(); ++i) if (accessed[i]) {
            ++count;
            if (!pages_[i]) { pages_[i] = 1; ++union_pages_; }
        }
        peak_pages_ = std::max(peak_pages_, count);
        return count;
    }
    uint64_t peak_pages() const { return peak_pages_; }
    uint64_t union_pages() const { return union_pages_; }
    const std::vector<uint8_t> &pages() const { return pages_; }
    uint64_t contiguous_runs() const {
        uint64_t runs = 0;
        for (size_t i = 0; i < pages_.size(); ++i)
            if (pages_[i] && (!i || !pages_[i - 1])) ++runs;
        return runs;
    }
private:
    std::vector<uint8_t> pages_;
    uint64_t peak_pages_ = 0, union_pages_ = 0;
};

struct SourceKey {
    uint16_t h0 = 0, h2 = 0;
    bool operator<(const SourceKey &other) const {
        return h0 != other.h0 ? h0 < other.h0 : h2 < other.h2;
    }
};

struct MaterialKey {
    uint16_t h0 = 0, h1 = 0, h2 = 0, h3 = 0;
    uint8_t luma = 0;
    bool operator<(const MaterialKey &other) const {
        if (h0 != other.h0) return h0 < other.h0;
        if (h1 != other.h1) return h1 < other.h1;
        if (h2 != other.h2) return h2 < other.h2;
        return h3 != other.h3 ? h3 < other.h3 : luma < other.luma;
    }
};

inline SourceKey source_key(const rt::GeoPoly &poly) {
    // Same source identity used by the current indexed-texture renderer.
    return {uint16_t(poly.texheader[0] & 0x23ff), uint16_t(poly.texheader[2] & 0x1fff)};
}

inline MaterialKey material_key(const rt::GeoPoly &poly) {
    return {uint16_t(poly.texheader[0] & 0x23ff), uint16_t(poly.texheader[1] & 0x00ff),
            uint16_t(poly.texheader[2] & 0x1fff), uint16_t(poly.texheader[3] & 0xffc0), uint8_t(poly.luma & 0xf0)};
}

inline uint64_t source_bytes(SourceKey key) {
    const uint64_t width = 32ull << (key.h0 & 7);
    const uint64_t height = 32ull << ((key.h0 >> 3) & 7);
    return width * height / 2; // PVR paletted 4bpp, prior to optional compression.
}

struct FrameUsage {
    uint32_t polygons = 0;
    uint32_t textured_polygons = 0;
    uint32_t solid_polygons = 0;
    uint32_t triangles = 0;
    uint32_t sources = 0;
    uint32_t materials = 0;
    uint64_t source_bytes = 0;
};

inline FrameUsage analyze_frame(const std::vector<rt::GeoPoly> &polys, int windows) {
    FrameUsage out;
    std::set<SourceKey> sources;
    std::set<MaterialKey> materials;
    for (const rt::GeoPoly &poly : polys) {
        if (poly.window > windows || poly.num_vertices < 3 || poly.num_vertices > 8) continue;
        const unsigned renderer = (poly.texheader[0] >> 13) & 3;
        if (renderer == 1) continue; // The reference rasterizer draws no translucent solid polygon.
        ++out.polygons;
        out.triangles += poly.num_vertices - 2;
        if (renderer & 2) {
            ++out.textured_polygons;
            sources.insert(source_key(poly));
            materials.insert(material_key(poly));
        } else {
            ++out.solid_polygons;
        }
    }
    out.sources = uint32_t(sources.size());
    out.materials = uint32_t(materials.size());
    for (const SourceKey key : sources) out.source_bytes += source_bytes(key);
    return out;
}

class StageUsage {
public:
    void add_frame(const std::vector<rt::GeoPoly> &polys, int windows) {
        const FrameUsage frame = analyze_frame(polys, windows);
        ++frames_;
        peak_source_bytes_ = std::max(peak_source_bytes_, frame.source_bytes);
        peak_polygons_ = std::max(peak_polygons_, frame.polygons);
        peak_triangles_ = std::max(peak_triangles_, frame.triangles);
        for (const rt::GeoPoly &poly : polys) {
            if (poly.window > windows || poly.num_vertices < 3 || poly.num_vertices > 8 ||
                ((poly.texheader[0] >> 13) & 3) == 1 || !(((poly.texheader[0] >> 13) & 3) & 2))
                continue;
            const SourceKey source = source_key(poly);
            if (sources_.insert(source).second) stage_source_bytes_ += source_bytes(source);
            materials_.insert(material_key(poly));
        }
    }

    uint64_t frames() const { return frames_; }
    uint64_t peak_source_bytes() const { return peak_source_bytes_; }
    uint32_t peak_polygons() const { return peak_polygons_; }
    uint32_t peak_triangles() const { return peak_triangles_; }
    uint64_t stage_source_bytes() const { return stage_source_bytes_; }
    size_t source_count() const { return sources_.size(); }
    size_t material_count() const { return materials_.size(); }
    bool fits_texture_cache() const { return peak_source_bytes_ <= kTextureCacheBytes; }

private:
    uint64_t frames_ = 0, peak_source_bytes_ = 0, stage_source_bytes_ = 0;
    uint32_t peak_polygons_ = 0, peak_triangles_ = 0;
    std::set<SourceKey> sources_;
    std::set<MaterialKey> materials_;
};

} // namespace tools::dreamcast
