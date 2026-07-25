#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "pocketjs_core";

void *pocketjs_rust_alloc(size_t size, size_t alignment)
{
    if (size == 0) {
        return NULL;
    }

    if (alignment < sizeof(void *)) {
        alignment = sizeof(void *);
    }

    void *ptr = heap_caps_aligned_alloc(
        alignment,
        size,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );
    if (ptr == NULL) {
        ptr = heap_caps_aligned_alloc(
            alignment,
            size,
            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT
        );
    }
    return ptr;
}

void pocketjs_rust_dealloc(void *ptr, size_t size, size_t alignment)
{
    (void)size;
    (void)alignment;
    heap_caps_free(ptr);
}

void pocketjs_rust_panic(void)
{
    ESP_LOGE(TAG, "PocketJS Rust core panicked");
    abort();
}

/*
 * Rust's bare-metal staticlib bundles compiler-rt popcount objects built
 * without the ESP toolchain's ilp32f ELF attributes. Providing the two tiny
 * integer helpers here keeps those duplicate soft-float objects out of the
 * final link; this file is compiled with ESP-IDF's matching hard-float ABI.
 *
 * O0 deliberately prevents GCC from recognizing the implementation as a
 * popcount idiom and replacing it with a recursive call to the same symbol.
 */
__attribute__((optimize("O0")))
int __popcountsi2(unsigned int value)
{
    int count = 0;
    while (value != 0) {
        count += (int)(value & 1U);
        value >>= 1;
    }
    return count;
}

__attribute__((optimize("O0")))
int __popcountdi2(unsigned long long value)
{
    int count = 0;
    while (value != 0) {
        count += (int)(value & 1ULL);
        value >>= 1;
    }
    return count;
}
