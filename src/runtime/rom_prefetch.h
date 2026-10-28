// Replay-derived address hints only. Unlisted pages keep the ordinary ROM path.
#pragma once
#include "runtime/paged_rom.h"
#include <array>
#include <charconv>

namespace rt {
inline size_t load_rom_pages(const std::string &path, const std::array<PagedRom *, 5> &roms,
                             size_t budget = 1024 * 1024) {
    std::unique_ptr<FILE, decltype(&std::fclose)> file(std::fopen(path.c_str(), "r"), std::fclose);
    if (!file) throw std::runtime_error("cannot open ROM page manifest " + path);
    char magic[32]{};
    if (std::fscanf(file.get(), "%31s", magic) != 1 || std::strcmp(magic, "DRP1"))
        throw std::runtime_error("invalid ROM page manifest");
    const auto number = [&]() {
        char token[32]; unsigned value = 0;
        if (std::fscanf(file.get(), "%31s", token) != 1) throw std::runtime_error("truncated ROM page manifest");
        const auto end = token + std::strlen(token);
        const auto result = std::from_chars(token, end, value);
        if (result.ec != std::errc{} || result.ptr != end) throw std::runtime_error("invalid ROM page number");
        return value;
    };
    std::array<std::vector<uint32_t>, 5> pages;
    size_t total = 0;
    // Fixed region order: program, main, copro, polygon, texture.
    for (unsigned region = 0; region < pages.size(); ++region) {
        const unsigned count = number();
        if (count > (budget - total) / PagedRom::kPageBytes)
            throw std::runtime_error("ROM page manifest exceeds RAM budget");
        total += size_t(count) * PagedRom::kPageBytes;
        pages[region].resize(count);
        for (size_t i = 0; i < count; ++i) {
            const unsigned page = number();
            if (!roms[region] ||
                page >= roms[region]->size() / PagedRom::kPageBytes ||
                (i && page <= pages[region][i - 1]))
                throw std::runtime_error("invalid ROM page manifest address");
            pages[region][i] = page;
        }
    }
    char trailing;
    if (std::fscanf(file.get(), " %c", &trailing) != EOF || std::ferror(file.get()))
        throw std::runtime_error("trailing ROM page manifest data");
    for (unsigned i = 0; i < pages.size(); ++i) if (roms[i]) roms[i]->pin_pages(pages[i]);
    return total;
}
} // namespace rt
