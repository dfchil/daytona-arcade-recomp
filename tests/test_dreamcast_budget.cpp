#include "../tools/common/dreamcast_budget.h"

#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <vector>

namespace {

rt::GeoPoly textured(uint16_t h0, uint16_t h1, uint16_t h2, uint16_t h3, uint8_t luma) {
    rt::GeoPoly poly;
    poly.num_vertices = 4;
    poly.texheader[0] = h0;
    poly.texheader[1] = h1;
    poly.texheader[2] = h2;
    poly.texheader[3] = h3;
    poly.luma = luma;
    return poly;
}

} // namespace

int main() {
    using namespace tools::dreamcast;
    static_assert(kTaBytes == 2687744);
    static_assert(kScanoutBytes == 1843200);
    static_assert(kTextureCacheBytes == 1498368);

    RomUsage rom;
    assert(rom.add_frame({1, 0, 1, 1, 0}) == 3);
    assert(rom.peak_pages() == 3 && rom.union_pages() == 3 && rom.contiguous_runs() == 2);
    assert(rom.add_frame({0, 1, 0, 0, 0}) == 1);
    assert(rom.peak_pages() == 3 && rom.union_pages() == 4 && rom.contiguous_runs() == 1);
    assert(rom.add_frame({1, 0, 0, 0, 1}) == 2);
    assert(rom.union_pages() == 5 && rom.contiguous_runs() == 1);
    assert(rom.add_frame({0, 0, 0, 0, 0}) == 0 && rom.union_pages() == 5);

    std::vector<rt::GeoPoly> frame;
    frame.push_back(textured(0x4000, 0x01, 0x0000, 0x0000, 0x10)); // 32x32, 4bpp
    frame.push_back(textured(0x4000, 0x02, 0x0000, 0x0040, 0x20)); // same source, second material
    rt::GeoPoly solid = textured(0x0000, 0, 0, 0, 0);
    frame.push_back(solid);
    const FrameUsage usage = analyze_frame(frame, 0);
    assert(usage.polygons == 3);
    assert(usage.textured_polygons == 2);
    assert(usage.solid_polygons == 1);
    assert(usage.triangles == 6);
    assert(usage.sources == 1 && usage.source_bytes == 512);
    assert(usage.materials == 2);

    StageUsage stage;
    stage.add_frame(frame, 0);
    frame.push_back(textured(0x4001, 0, 0x0001, 0, 0)); // 64x32, a second source
    stage.add_frame(frame, 0);
    assert(stage.frames() == 2);
    assert(stage.source_count() == 2 && stage.stage_source_bytes() == 1536);
    assert(stage.peak_source_bytes() == 1536);
    assert(stage.fits_texture_cache());
    std::puts("PASS: Dreamcast source and PVR budget accounting");
}
