#include "../tools/common/dreamcast/material_pack.hpp"
#include "../tools/common/dreamcast_assets.h"
#undef NDEBUG
#include <cassert>

static void put(std::vector<uint8_t> &v, size_t at, uint32_t n) {
    for (unsigned i = 0; i < 4; ++i) v[at + i] = uint8_t(n >> (i * 8));
}
int main() {
    std::vector<uint8_t> palette(0x4000), xlat(0xc000), luma(0x20000, 255);
    for (unsigned bank = 0; bank < 3; ++bank) for (unsigned level = 0; level < 64; ++level)
        xlat[bank * 0x4000 + level * 2] = uint8_t(64 + level * 3);
    const auto original_xlat = xlat;
    std::vector<uint32_t> sheet(0x80000, 0x01234567);
    rt::VideoMem mem{palette.data(), xlat.data(), luma.data(), sheet.data(), sheet.data()};
    rt::GeoPoly poly{}; poly.texheader[0] = 0x4000; poly.luma = 255;
    const auto state = tools::dreamcast::material_state(poly, mem);
    std::vector<uint8_t> blob(128 + state.size() - 64);
    std::memcpy(blob.data(), "DTM1", 4);
    put(blob, 4, unsigned(blob.size())); put(blob, 8, 1); put(blob, 12, 1); put(blob, 16, 96);
    put(blob, 32, 0x4000); put(blob, 40, 255); put(blob, 48, 128); put(blob, 52, unsigned(state.size() - 64));
    std::copy_n(state.data(), 64, blob.data() + 64);
    std::copy(state.begin() + 64, state.end(), blob.begin() + 128);
    daytona::PreparedMaterials materials;
    assert(materials.set(blob.data(), blob.size(), 1));
    assert(materials.find(poly, mem) == 0);
    ++poly.luma; assert(materials.find(poly, mem) == -1); --poly.luma;
    ++poly.texheader[3]; assert(materials.find(poly, mem) == -1); --poly.texheader[3];
    xlat.assign(xlat.size(), 0); assert(materials.find(poly, mem) == -1);
    xlat = original_xlat; assert(materials.find(poly, mem) == 0);
    luma[0] = 0; assert(materials.find(poly, mem) == -1);
    luma[0] = 255; assert(materials.find(poly, mem) == 0);
    palette[0x2000] = 1; assert(materials.find(poly, mem) == -1);
    palette[0x2000] = 0; assert(materials.find(poly, mem) == 0);
    sheet[0] ^= 1; ++mem.tex_generation; assert(materials.find(poly, mem) == -1);
    sheet[0] ^= 1; ++mem.tex_generation; assert(materials.find(poly, mem) == 0);
    auto other = sheet; other[0] ^= 1; mem.tex0 = other.data();
    assert(materials.find(poly, mem) == -1); // new sheet, even with the same generation
    mem.tex0 = sheet.data(); assert(materials.find(poly, mem) == 0);
    for (size_t n = 0; n < blob.size(); ++n) assert(!daytona_materials_valid(blob.data(), n, 1));
    for (unsigned offset : {0u, 4u, 8u, 12u, 16u, 20u, 28u, 32u, 41u, 42u, 44u, 48u, 52u, 56u}) {
        blob[offset] ^= 255;
        assert(!daytona_materials_valid(blob.data(), blob.size(), 1));
        blob[offset] ^= 255;
    }
    assert(!materials.set(blob.data(), blob.size(), 0));
    assert(materials.find(poly, mem) == -1);
    // Two material keys share one live source. A different proof range or
    // different live origin must remain independent, even if bytes coincide.
    std::vector<uint8_t> shared(416 + 2 * 512);
    std::copy_n(blob.data(), 32, shared.data());
    put(shared, 4, unsigned(shared.size())); put(shared, 8, 4);
    for (unsigned i = 0; i < 4; ++i) {
        const unsigned at = 32 + i * 96;
        std::copy_n(blob.data() + 32, 96, shared.data() + at);
        shared[at + 6] = uint8_t(i < 3 ? i : 0);
        shared[at + 4] = uint8_t(i == 3);
        put(shared, at + 16, i == 2 ? 928 : 416);
    }
    std::copy_n(blob.data() + 128, 512, shared.data() + 416);
    std::copy_n(blob.data() + 128, 512, shared.data() + 928);
    shared[928] ^= 1;
    assert(materials.set(shared.data(), shared.size(), 1));
    assert(materials.size() == 4 && materials.source_count() == 3);
    assert(materials.find(poly, mem) == 0 && materials.source_checks() == 1);
    poly.texheader[3] = 1;
    assert(materials.find(poly, mem) == 0 && materials.source_checks() == 1);
    poly.texheader[3] = 2;
    assert(materials.find(poly, mem) == -1 && materials.source_checks() == 2);
    poly.texheader[3] = 0; poly.texheader[2] = 1;
    assert(materials.find(poly, mem) == 0 && materials.source_checks() == 3);
    sheet[0] ^= 1; ++mem.tex_generation;
    poly.texheader[2] = 0; poly.texheader[3] = 1;
    assert(materials.find(poly, mem) == -1 && materials.source_checks() == 4);
    poly.texheader[3] = 0;
    assert(materials.find(poly, mem) == -1 && materials.source_checks() == 4);
    poly.texheader[2] = 1;
    assert(materials.find(poly, mem) == 0 && materials.source_checks() == 5);
    // Failure and success are both invalidated by a different sheet pointer.
    auto restored = sheet; restored[0] ^= 1; mem.tex0 = restored.data();
    poly.texheader[2] = 0;
    assert(materials.find(poly, mem) == 0 && materials.source_checks() == 6);
    std::puts("PASS: exact material keys, colours, changed sheets, bounds and cache invalidation");
}
