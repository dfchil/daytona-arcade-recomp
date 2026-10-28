// ROM-free checks for the memory reductions used by the Dreamcast frontend.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "runtime/lockstep.h"
#include "runtime/video.h"

#include <cassert>
#include <cstdio>
#include <memory>
#include <cstring>

int main() {
    // Observation must precede register-cache/stack mutation, including IRQ entry.
    rt::Cpu plain(nullptr), observed(nullptr);
    struct Observation { const rt::Cpu *before; unsigned calls; uint32_t target; int type; } observation{&plain,0,0,0};
    observed.on_call_ctx = &observation;
    observed.on_call = [](void *context, const rt::Cpu &current, uint32_t target, int type) {
        auto &o = *static_cast<Observation *>(context);
        assert(target == o.target && type == o.type);
        assert(current.m_IP == o.before->m_IP && current.m_rcache_pos == o.before->m_rcache_pos);
        assert(!std::memcmp(current.m_r,o.before->m_r,sizeof(current.m_r)));
        ++o.calls;
    };
    for (unsigned i = 0; i < 4; ++i) {
        observation.target = 0x1000+4*i; observation.type = i == 3 ? 7 : 0;
        observed.do_call(observation.target,observation.type,0x8000);
        plain.do_call(observation.target,observation.type,0x8000);
        assert(observation.calls == i+1 && observed.m_IP == plain.m_IP && observed.m_rcache_pos == plain.m_rcache_pos);
        assert(!std::memcmp(observed.m_r,plain.m_r,sizeof(plain.m_r)));
        assert(!std::memcmp(observed.m_rcache,plain.m_rcache,sizeof(plain.m_rcache)));
        assert(!std::memcmp(observed.m_rcache_frame_addr,plain.m_rcache_frame_addr,sizeof(plain.m_rcache_frame_addr)));
    }
    // Remove the simulated IRQ frame without invoking its bus-dependent exit.
    plain.do_ret_0(); observed.do_ret_0();
    observation.calls = 0;
    observed.on_return_ctx = &observation;
    observed.on_return = [](void *context, const rt::Cpu &current, uint32_t source) {
        auto &o = *static_cast<Observation *>(context);
        assert(source == o.target && current.m_IP == o.before->m_IP);
        assert(current.m_rcache_pos == o.before->m_rcache_pos);
        assert(!std::memcmp(current.m_r,o.before->m_r,sizeof(current.m_r)));
        ++o.calls;
    };
    for (unsigned i = 0; i < 3; ++i) {
        observation.target = observed.m_IP;
        plain.do_ret(); observed.do_ret();
        assert(observation.calls == i+1 && observed.m_IP == plain.m_IP);
        assert(!std::memcmp(observed.m_r,plain.m_r,sizeof(plain.m_r)));
    }
    rt::Cpu cpu(nullptr);
    rt::Lockstep scheduler(cpu);
    unsigned called = 0;
    for (unsigned i = 0; i < 10000; ++i) {
        auto capture = std::make_shared<unsigned>(i);
        std::weak_ptr<unsigned> lifetime = capture;
        scheduler.add_callback(i, [&, capture] {
            assert(*capture == i);
            ++called;
            // Re-enqueue while the old callback is executing in its reused slot.
            scheduler.add_callback(i, [&] { ++called; });
        });
        capture.reset();
        scheduler.count = i;
        scheduler.boundary();
        assert(lifetime.expired());
    }
    assert(called == 20000);

    std::vector<uint8_t> tiles(0x10000), chars(0x80000);
    rt::Video gpu(tiles.data(), chars.data(), true);
    rt::Video reference(tiles.data(), chars.data());
    std::vector<rt::GeoPoly> polys;
    const rt::VideoMem mem{};
    gpu.screen_update(polys, 0, mem);
    assert(gpu.screen().empty() && gpu.background_layer().empty() && gpu.foreground_layer().empty());
    // Raw character decoding must match every cached tile, including packed
    // nibble/word order, palette selection and wrapping of tile coordinates.
    for (size_t i = 0; i < tiles.size(); ++i) tiles[i] = uint8_t(i * 37 + i / 17);
    for (size_t i = 0; i < chars.size(); ++i) chars[i] = uint8_t(i * 13 + i / 31);
    gpu.screen_update(polys, 0, mem);
    for (int layer = 0; layer < 4; ++layer)
        for (unsigned y = 0; y < 512; ++y)
            for (unsigned x = 0; x < 512; ++x)
                assert(gpu.system24_pixel(layer, x + 512, y + 512) == gpu.system24_pixels(layer)[y * 512 + x]);
    gpu.set_system24_cache(false);
    const auto generation = gpu.system24_texture_generation();
    gpu.screen_update(polys, 0, mem);
    assert(gpu.last_profile().tiles_rebuilt == 0);
    assert(gpu.system24_texture_generation() > generation);
    for (int layer = 0; layer < 4; ++layer)
        assert(!gpu.system24_pixels(layer) && !gpu.system24_flags(layer));
    gpu.enable_write_tracking();
    const auto stable = gpu.system24_texture_generation();
    gpu.screen_update(polys, 0, mem);
    assert(gpu.system24_texture_generation() == stable);
    tiles[0] ^= 1; gpu.tile_memory_w();
    gpu.screen_update(polys, 0, mem);
    assert(gpu.system24_texture_generation() == stable + 1);
    gpu.screen_update(polys, 0, mem);
    assert(gpu.system24_texture_generation() == stable + 1);
    chars[0] ^= 1; gpu.character_memory_w();
    gpu.screen_update(polys, 0, mem);
    assert(gpu.system24_texture_generation() == stable + 2);
    // Upstream's desktop GPU path needs decoded pixmaps plus register/pen
    // snapshots, even when this Video began as a raw Dreamcast-only instance.
    gpu.set_external_3d(true, true);
    gpu.screen_update(polys, 0, mem);
    assert(gpu.system24_pixels(0) && gpu.system24_flags(0));
    for (unsigned i = 0; i < rt::Video::kGpuTileWords; ++i)
        assert(gpu.gpu_tile_words()[i] == gpu.system24_word(rt::Video::kGpuTileFirst + i));
    for (unsigned i = 0; i < rt::Video::kGpuPens; ++i)
        assert(gpu.gpu_pens()[i] == gpu.system24_pen(i));
    gpu.set_system24_cache(false);
    gpu.set_external_3d(true, false);
    bool rejected = false;
    try { gpu.raster_hash(); } catch (const std::runtime_error &) { rejected = true; }
    assert(rejected);
    gpu.set_external_3d(false);
    gpu.screen_update(polys, 0, mem);
    reference.screen_update(polys, 0, mem);
    assert(gpu.screen().size() == size_t(rt::Video::W) * rt::Video::H);
    assert(gpu.screen() == reference.screen());
    assert(gpu.raster_hash() == reference.raster_hash());
    std::puts("PASS: callback release, raw/cached tile parity, cache-free GPU mode and exact CPU restoration");
}
