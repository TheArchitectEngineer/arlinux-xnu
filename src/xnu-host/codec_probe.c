/* Exercise the production codecs on an owned physical frame. This verifies
 * codec and physical-aperture operations, not the compressor pager lifecycle. */
#include <kern/assert.h>
#include <vm/pmap.h>
#include <vm/WKdm_new.h>
#include <vm/lz4.h>
#include <machine/machine_routines.h>
#include <pexpert/pexpert.h>
#include <string.h>
#include <host/memory.h>

static WK_word saved[PAGE_SIZE / sizeof(WK_word)] __attribute__((aligned(64)));
static WK_word decoded[PAGE_SIZE / sizeof(WK_word)] __attribute__((aligned(PAGE_SIZE)));
static WK_word encoded[2 * PAGE_SIZE / sizeof(WK_word)] __attribute__((aligned(64)));
static WK_word scratch[WKdm_SCRATCH_BUF_SIZE_INTERNAL / sizeof(WK_word)] __attribute__((aligned(64)));
static lz4_hash_entry_t lz4_scratch[LZ4_COMPRESS_HASH_ENTRIES] __attribute__((aligned(64)));

void
xnu_host_codec_bootstrap(unsigned int page)
{
    WK_word *source = pmap_map_compressor_page(page);
    memcpy(saved, source, PAGE_SIZE);
    for (unsigned int mode = 0; mode < 4; mode++) {
        if (mode == 0)
            fill32_dczva((addr64_t)source, PAGE_SIZE);
        else if (mode == 1)
            fill32_nt((addr64_t)source, PAGE_SIZE, 0x12345678);
        else {
            for (unsigned int i = 0; i < PAGE_SIZE / sizeof(*source); i++)
                source[i] = mode == 2 ? ((i % 8) ? 0 : (0x13570000 | (i & 1023))) :
                    (((i / 32) % 8) << 16) | (i & 1023);
        }
        int size = WKdm_compress_16k(source, encoded, scratch, PAGE_SIZE);
        if (mode < 2) {
            /* Original WKdm represents zero/single-value pages by return 0;
             * the compressor reconstructs those using the pattern-fill path. */
            assert(size == 0);
            fill32_nt((addr64_t)decoded, PAGE_SIZE, source[0]);
        } else {
            assert(size > 0 && size < PAGE_SIZE);
            WKdm_decompress_16k(encoded, decoded, scratch, size);
        }
        assert(memcmp(source, decoded, PAGE_SIZE) == 0);
        size_t lzsize = lz4raw_encode_buffer((uint8_t *)encoded, sizeof(encoded),
            (const uint8_t *)source, PAGE_SIZE, lz4_scratch);
        assert(lzsize > 0);
        assert(lz4raw_decode_buffer((uint8_t *)decoded, PAGE_SIZE,
            (const uint8_t *)encoded, lzsize, NULL) == PAGE_SIZE);
        assert(memcmp(source, decoded, PAGE_SIZE) == 0);
    }
    assert(WKdm_compress_16k(source, encoded, scratch, 1) == -1);
    memcpy(source, saved, PAGE_SIZE);
    pmap_unmap_compressor_page(page, source);
    assert(memcmp(pmap_map_compressor_page(page), saved, PAGE_SIZE) == 0);
    pmap_unmap_compressor_page(page, source);
    kprintf("XNU_HOST_CODEC WKdm16k/LZ4 zero/single/sparse/dictionary round trips and budget rejection passed (pager not initialized)\n");
}
