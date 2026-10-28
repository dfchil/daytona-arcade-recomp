#pragma once
#include "texture_pack_format.h"
#include "runtime/material.h"
#include <cstring>
#include <map>

namespace daytona {
// Local exact proofs, not hashes or frame-number guesses. Missing material
// states stay misses; callers must not choose the nearest lighting variant.
class PreparedMaterials {
public:
    void clear() { *this = PreparedMaterials{}; }
    bool set(const uint8_t *data, size_t size, unsigned resources) {
        clear();
        if (!daytona_materials_valid(data, size, resources)) return false;
        bytes_.assign(data, data + size);
        source_ids_.resize(dtp_u32(data + 8));
        std::map<std::array<uint32_t, 3>, unsigned> sources;
        for (unsigned i = 0; i < source_ids_.size(); ++i) {
            const uint8_t *record = data + 32 + i * 96;
            // Sharing requires the same dimensions, sheet/origin and exact
            // proof range. Equal bytes at a different live origin are not proof.
            const std::array<uint32_t, 3> key{dtp_u16(record) & 63,
                dtp_u16(record + 4) & 0x1fff, dtp_u32(record + 16)};
            source_ids_[i] = sources.try_emplace(key, unsigned(sources.size())).first->second;
        }
        source_matches_.resize(sources.size());
        return true;
    }
    unsigned size() const { return unsigned(source_ids_.size()); }
    unsigned source_count() const { return unsigned(source_matches_.size()); }
    uint64_t source_checks() const { return source_checks_; }
    int find(const rt::GeoPoly &poly, const rt::VideoMem &mem) {
        if (bytes_.empty() || !mem.tex0 || !mem.tex1 || !mem.palram || !mem.colorxlat || !mem.lumaram) return -1;
        uint8_t key[10];
        for (unsigned i = 0; i < 5; ++i) {
            const uint16_t n = i < 4 ? poly.texheader[i] : poly.luma;
            key[i * 2] = uint8_t(n); key[i * 2 + 1] = uint8_t(n >> 8);
        }
        size_t lo = 0, hi = size();
        while (lo < hi) {
            const size_t mid = lo + (hi - lo) / 2;
            if (std::memcmp(bytes_.data() + 32 + mid * 96, key, sizeof key) < 0) lo = mid + 1;
            else hi = mid;
        }
        if (lo == size()) return -1;
        const uint8_t *record = bytes_.data() + 32 + lo * 96;
        if (std::memcmp(record, key, sizeof key)) return -1;
        const auto colors = rt::material_colors(poly, mem);
        for (unsigned i = 0; i < 16; ++i) if (colors[i] != dtp_u32(record + 32 + i * 4)) return -1;
        if (mem.tex_generation != generation_ || mem.tex0 != tex0_ || mem.tex1 != tex1_) {
            std::fill(source_matches_.begin(), source_matches_.end(), uint8_t(0));
            generation_ = mem.tex_generation; tex0_ = mem.tex0; tex1_ = mem.tex1;
        }
        auto &match = source_matches_[source_ids_[lo]];
        if (!match) {
            ++source_checks_;
            const unsigned w = 32u << (poly.texheader[0] & 7), h = 32u << ((poly.texheader[0] >> 3) & 7);
            const uint8_t *source = bytes_.data() + dtp_u32(record + 16);
            bool same = true;
            // ponytail: invalidate all shared sources after any sheet write;
            // dirty-page tracking is the next step if this still costs a frame.
            for (unsigned y = 0; y < h && same; y += 2) for (unsigned x = 0; x < w; x += 2) {
                if (rt::material_cell(poly, mem, x, y) != dtp_u16(source)) { same = false; break; }
                source += 2;
            }
            match = same ? 1 : 2;
        }
        return match == 1 ? int(dtp_u32(record + 12)) : -1;
    }
private:
    std::vector<uint8_t> bytes_, source_matches_;
    std::vector<unsigned> source_ids_;
    uint64_t source_checks_ = 0;
    uint64_t generation_ = ~uint64_t(0);
    const uint32_t *tex0_ = nullptr, *tex1_ = nullptr;
};
} // namespace daytona
