// Synthetic image: exercise LRU eviction, boundary reads and failures.
#include "runtime/paged_rom.h"
#include "runtime/rom_prefetch.h"
#include "runtime/geo.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

static void require(bool ok) { if (!ok) std::abort(); }
static uint64_t clock_tick() { static uint64_t ticks = 0; return ticks += 7; }

int main(int argc, char **argv) {
    require(argc == 2);
    const std::string path = std::string(argv[1]) + "/paged-rom-test.bin";
    std::array<uint32_t, 3072> expected{};
    for (size_t i = 0; i < expected.size(); ++i) expected[i] = uint32_t(i * 0x10203u) ^ 0x12345678u;
    FILE *out = std::fopen(path.c_str(), "wb");
    require(out && std::fwrite(expected.data(), sizeof(uint32_t), expected.size(), out) == expected.size());
    require(std::fclose(out) == 0);
    {
        rt::PagedRom rom(path, sizeof expected, 4096);
        rom.pin_pages({0, 2});
        require(rom.pinned_bytes() == 8192 && rom.misses() == 2);
        rom.start_access_sample();
        for (unsigned pass = 0; pass < 4; ++pass)
            for (size_t i = 0; i < expected.size(); ++i) require(rom.read<uint32_t>(i * 4) == expected[i]);
        require(rom.misses() == 3); // pinned pages survive repeated fallback reads
        require(rom.accessed_pages() == std::vector<uint8_t>({1, 1, 1}));
        for (const auto &bad : {std::vector<uint32_t>{2, 0}, {0, 0}, {3}}) {
            bool rejected = false;
            try { rom.pin_pages(bad); } catch (const std::runtime_error &) { rejected = true; }
            require(rejected && rom.pinned_bytes() == 8192);
        }
        rom.pin_pages({1});
        require(rom.pinned_bytes() == 4096);
        require(rom.read<uint32_t>(0) == expected[0]);
        require(rom.read<uint32_t>(4096) == expected[1024]);
        rom.pin_pages({});
        require(rom.pinned_bytes() == 0);
        for (size_t i = 0; i < expected.size(); ++i) require(rom.read<uint32_t>(i * 4) == expected[i]);

        const std::string manifest = path + ".pages";
        const auto write_manifest = [&](const char *text) {
            FILE *f = std::fopen(manifest.c_str(), "wb");
            require(f && std::fputs(text, f) >= 0 && std::fclose(f) == 0);
        };
        const std::array<rt::PagedRom *, 5> roms{&rom, nullptr, nullptr, nullptr, nullptr};
        write_manifest("DRP1\n2 0 2\n0\n0\n0\n0\n");
        require(rt::load_rom_pages(manifest, roms) == 8192);
        bool rejected = false;
        try { rt::load_rom_pages(manifest, roms, 4096); } catch (const std::runtime_error &) { rejected = true; }
        require(rejected && rom.pinned_bytes() == 8192);
        for (const char *bad : {"DRP2", "DRP1 0", "DRP1 -1", "DRP1 4294967296",
                "DRP1 99999999999999999999999999999999999999999999999",
                "DRP1 2 2 0 0 0 0 0", "DRP1 2 0 0 0 0 0 0", "DRP1 1 3 0 0 0 0",
                "DRP1 0 1 0 0 0 0", "DRP1 0 0 0 0 0 garbage"}) {
            write_manifest(bad); rejected = false;
            try { rt::load_rom_pages(manifest, roms); } catch (const std::runtime_error &) { rejected = true; }
            require(rejected && rom.pinned_bytes() == 8192);
            require(rom.read<uint32_t>(8192) == expected[2048]);
        }
        std::filesystem::remove(manifest);
    }
    {
        rt::PagedRom rom(path, sizeof expected, 8192);
        for (unsigned pass = 0; pass < 8; ++pass) {
            require(rom.read<uint32_t>(0) == expected[0]);
            require(rom.read<uint32_t>(8192) == expected[2048]);
        }
        require(rom.misses() == 2); // These two pages collided in a direct map.
        require(rom.accessed_pages().empty()); // Default runtime has no tracking allocation.
        rom.start_access_sample();
        require(rom.read<uint32_t>(0) == expected[0]);
        require(rom.read<uint32_t>(8192) == expected[2048]);
        require(rom.read<uint32_t>(8192) == expected[2048]);
        require(rom.misses() == 2); // Warm hits still belong to the working set.
        require(rom.accessed_pages() == std::vector<uint8_t>({1, 0, 1}));
        rom.start_access_sample();
        require(rom.accessed_pages() == std::vector<uint8_t>({0, 0, 0}));
        require(rom.read<uint32_t>(4096) == expected[1024]); // evicts page 0
        require(rom.accessed_pages() == std::vector<uint8_t>({0, 1, 0}));
        require(rom.read<uint32_t>(8192) == expected[2048]); // most recent page survives
        require(rom.misses() == 3);
        require(rom.read<uint32_t>(0) == expected[0]);
        require(rom.misses() == 4);
    }
    {
        rt::PagedRom rom(path, sizeof expected, 4096);
        rom.set_profile_clock(clock_tick);
        for (unsigned pass = 0; pass < 2; ++pass)
            for (size_t i = 0; i < expected.size(); ++i) require(rom.read<uint32_t>(i * 4) == expected[i]);
        require(rom.misses() == 6 && rom.cache_bytes() == 4096);
        require(rom.read_ticks() == 6 * 7); // Hits never call the profiling clock.
        // A cursor wrapping at the last word must fetch the first page again.
        rt::GeoPtr cursor{nullptr, 2048, 2047, &rom};
        require(*cursor++ == expected[2047]);
        require(*cursor == expected[0]);
        bool rejected = false;
        try { rom.read<uint32_t>(sizeof expected); } catch (const std::runtime_error &) { rejected = true; }
        require(rejected);
        rejected = false;
        try { rom.read<uint32_t>(1); } catch (const std::runtime_error &) { rejected = true; }
        require(rejected);
        rejected = false;
        try { rt::PagedRom bad(path, 4096, 4096); } catch (const std::runtime_error &) { rejected = true; }
        require(rejected);
        // Truncate a previously accepted file: a miss must fail, not expose
        // the partially overwritten cache under its previous valid tag.
        std::filesystem::resize_file(path, 4096);
        rejected = false;
        try { rom.read<uint32_t>(8192); } catch (const std::runtime_error &) { rejected = true; }
        require(rejected);
        // A failed replacement must retain the already pinned original page.
        rom.pin_pages({0});
        rejected = false;
        try { rom.pin_pages({0, 2}); } catch (const std::runtime_error &) { rejected = true; }
        require(rejected && rom.pinned_bytes() == 4096);
        require(rom.read<uint32_t>(0) == expected[0]);
    }
    std::filesystem::remove(path);
    std::puts("PASS: bounded ROM cache, eviction, cursor wrap and failed reads");
}
