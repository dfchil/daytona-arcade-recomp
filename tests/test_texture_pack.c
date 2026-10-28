#include "../tools/common/dreamcast/texture_pack_format.h"
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static void put(uint8_t *p, uint32_t n) {
    for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(n >> (i * 8));
}
int main(int argc, char **argv) {
    uint8_t data[224] = {0};
    memcpy(data, "DTP1", 4); put(data + 4, sizeof data); put(data + 8, 1);
    put(data + 12, 128); put(data + 16, 32);
    put(data + 32, 64); put(data + 36, 160); put(data + 48, UINT32_MAX);
    put(data + 52, 8); put(data + 56, 8);
    memcpy(data + 64, "DcTx", 4); put(data + 68, 160);
    put(data + 76, 8 | (8 << 16)); put(data + 80, 1u << 27);
    assert(daytona_pack_valid(data, sizeof data));
    for (size_t size = 0; size < sizeof data; ++size) assert(!daytona_pack_valid(data, size));
    const unsigned offsets[] = {0, 4, 8, 12, 16, 20, 32, 36, 40, 44, 48, 52, 56, 60, 64, 68, 72, 76, 80};
    for (unsigned i = 0; i < sizeof offsets / sizeof *offsets; ++i) {
        uint8_t saved = data[offsets[i]];
        data[offsets[i]] ^= 0xff;
        assert(!daytona_pack_valid(data, sizeof data));
        data[offsets[i]] = saved;
    }
    /* Small VQ books occupy the end of the hardware's 256-entry range. */
    put(data + 80, (1u << 30) | (1u << 27)); data[74] = 1;
    memset(data + 112, 254, 16);
    assert(daytona_pack_valid(data, sizeof data));
    data[112] = 253; assert(!daytona_pack_valid(data, sizeof data));
    data[112] = 255; assert(daytona_pack_valid(data, sizeof data));
    /* DTP2 embeds its material proofs and ties their indices/dimensions to
     * this pack's resource table, not a separately selected manifest. */
    uint8_t linked[2784] = {0};
    memcpy(linked, "DTP2", 4); put(linked + 4, sizeof linked); put(linked + 8, 1);
    put(linked + 12, 2048); put(linked + 16, 32); put(linked + 20, 2144); put(linked + 24, 640);
    put(linked + 32, 64); put(linked + 36, 2080); put(linked + 48, UINT32_MAX);
    put(linked + 52, 32); put(linked + 56, 32);
    memcpy(linked + 64, "DcTx", 4); put(linked + 68, 2080);
    put(linked + 76, 32 | (32 << 16)); put(linked + 80, (1u << 27) | 18);
    uint8_t *proof = linked + 2144;
    memcpy(proof, "DTM1", 4); put(proof + 4, 640); put(proof + 8, 1);
    put(proof + 12, 1); put(proof + 16, 96); put(proof + 32, 0x4000);
    put(proof + 48, 128); put(proof + 52, 512);
    assert(daytona_pack_valid(linked, sizeof linked));
    put(proof + 44, 1); assert(!daytona_pack_valid(linked, sizeof linked)); put(proof + 44, 0);
    put(linked + 20, 2112); assert(!daytona_pack_valid(linked, sizeof linked)); put(linked + 20, 2144);
    linked[3] = '1'; assert(!daytona_pack_valid(linked, sizeof linked));
    for (int i = 1; i < argc; ++i) {
        FILE *file = fopen(argv[i], "rb"); assert(file);
        assert(!fseek(file, 0, SEEK_END)); long size = ftell(file); assert(size > 0);
        rewind(file); uint8_t *bytes = malloc((size_t)size); assert(bytes);
        assert(fread(bytes, 1, (size_t)size, file) == (size_t)size);
        assert(daytona_pack_valid(bytes, (size_t)size)); free(bytes); fclose(file);
    }
    puts("PASS: DTP1/DTP2 bounds, material/resource linkage and supplied packs");
}
