/* Portable DTP1 validation; no pointers from disk are trusted by enDjinn. */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define DAYTONA_PACK_MAX_BYTES (3u * 1024u * 1024u)
#define DAYTONA_PACK_VRAM_BYTES 1498368u

static inline uint32_t dtp_u32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static inline uint32_t dtp_u16(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8; }
static inline int dtp_range(uint32_t offset, uint32_t length, size_t size, uint32_t start) {
    return !(offset & 31) && offset >= start && offset <= size && length <= size - offset;
}

static inline int daytona_materials_valid(const uint8_t *data, size_t size, uint32_t resources) {
    if (!data || size < 128 || size > DAYTONA_PACK_MAX_BYTES || memcmp(data, "DTM1", 4) ||
        dtp_u32(data + 4) != size || dtp_u32(data + 12) != resources || dtp_u32(data + 16) != 96 ||
        dtp_u32(data + 20) || dtp_u32(data + 24) || dtp_u32(data + 28)) return 0;
    const uint32_t count = dtp_u32(data + 8), start = 32 + count * 96;
    if (!count || count > 8192 || count > (size - 32) / 96) return 0;
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t *r = data + 32 + i * 96;
        const uint32_t h0 = dtp_u16(r), w = 32u << (h0 & 7), h = 32u << ((h0 >> 3) & 7);
        if (!(h0 & 0x4000) || w > 1024 || h > 1024 || dtp_u16(r + 8) > 255 ||
            dtp_u16(r + 10) || dtp_u32(r + 24) || dtp_u32(r + 28) || dtp_u32(r + 12) >= resources ||
            dtp_u32(r + 20) != w * h / 2 || !dtp_range(dtp_u32(r + 16), w * h / 2, size, start) ||
            (i && memcmp(r - 96, r, 10) >= 0)) return 0;
    }
    return 1;
}

static inline int daytona_pack_valid(const void *blob, size_t size) {
    const uint8_t *data = (const uint8_t *)blob;
    if (!data || size < 64 || size > DAYTONA_PACK_MAX_BYTES ||
        (memcmp(data, "DTP1", 4) && memcmp(data, "DTP2", 4)) ||
        dtp_u32(data + 4) != size || dtp_u32(data + 16) != 32 || dtp_u32(data + 28)) return 0;
    const uint32_t count = dtp_u32(data + 8), declared = dtp_u32(data + 12);
    if (!count || count > 2048 || count > (size - 32) / 32 || declared > DAYTONA_PACK_VRAM_BYTES) return 0;
    const uint32_t start = 32 + count * 32;
    const uint32_t materials = dtp_u32(data + 20), material_size = dtp_u32(data + 24);
    if (data[3] == '2') {
        if (!dtp_range(materials, material_size, size, start) || material_size != size - materials ||
            !daytona_materials_valid(data + materials, material_size, count)) return 0;
        const uint8_t *table = data + materials;
        for (uint32_t i = 0; i < dtp_u32(table + 8); ++i) {
            const uint8_t *m = table + 32 + i * 96;
            const uint8_t *t = data + 32 + dtp_u32(m + 12) * 32;
            const uint32_t h0 = dtp_u16(m);
            if (dtp_u32(t + 20) != (32u << (h0 & 7)) || dtp_u32(t + 24) != (32u << ((h0 >> 3) & 7))) return 0;
        }
    } else if (materials || material_size) return 0;
    const size_t resource_end = materials ? materials : size;
    uint32_t vram = 0, palette_offsets[64] = {0}, palette_sizes[64] = {0};
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t *record = data + 32 + i * 32;
        const uint32_t offset = dtp_u32(record), length = dtp_u32(record + 4);
        const uint32_t pal = dtp_u32(record + 8), pal_size = dtp_u32(record + 12), bank = dtp_u32(record + 16);
        const uint32_t w = dtp_u32(record + 20), h = dtp_u32(record + 24);
        if (dtp_u32(record + 28) || length < 64 || (length & 31) || !dtp_range(offset, length, resource_end, start)) return 0;
        const uint8_t *dt = data + offset;
        const uint32_t header = ((uint32_t)dt[9] + 1) * 32, mode = dtp_u32(dt + 16), fmt = (mode >> 27) & 7;
        if (memcmp(dt, "DcTx", 4) || dt[8] || dtp_u32(dt + 4) != length || header >= length ||
            w != dtp_u16(dt + 12) || h != dtp_u16(dt + 14) ||
            w != (8u << ((mode >> 3) & 7)) || h != (8u << (mode & 7)) ||
            (mode & (1u << 31)) || (fmt != 0 && fmt != 1 && fmt != 5)) return 0;
        if (fmt != 5 && (mode & (3u << 25))) return 0; /* twiddled, non-stride only */
        const uint32_t payload = length - header;
        if (payload > DAYTONA_PACK_VRAM_BYTES - vram) return 0;
        vram += payload;
        if (mode & (1u << 30)) {
            const uint32_t entries = (uint32_t)dt[10] + 1, book = entries * 8;
            const uint32_t indices = w * h / (fmt == 5 ? 16 : 4);
            if (book > payload || indices > payload - book) return 0;
            for (uint32_t j = 0; j < indices; ++j)
                if (dt[header + book + j] < 256 - entries) return 0;
        } else if (payload < w * h / (fmt == 5 ? 2 : 1) * (fmt == 5 ? 1 : 2)) return 0;
        if (fmt == 5) {
            if (bank >= 64 || pal_size < 12 || pal_size > 72 || !dtp_range(pal, pal_size, resource_end, start)) return 0;
            const uint8_t *palette = data + pal;
            const uint32_t colors = dtp_u32(palette + 4);
            if (memcmp(palette, "DPAL", 4) || !colors || colors > 16 || pal_size != 8 + colors * 4 || dt[11] + 1u > colors) return 0;
            if (palette_offsets[bank] && (palette_sizes[bank] != pal_size ||
                memcmp(data + palette_offsets[bank], palette, pal_size))) return 0;
            palette_offsets[bank] = pal; palette_sizes[bank] = pal_size;
        } else if (pal || pal_size || bank != UINT32_MAX) return 0;
    }
    return vram == declared;
}
