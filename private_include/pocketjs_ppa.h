#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Register the blocking PPA FILL, BLEND, and SRM clients used by the
 * PocketJS RGB565 renderer. A failed or unavailable operation is reported to
 * Rust, which preserves DrawList order with the RGB565 software fallback.
 */
esp_err_t pocketjs_ppa_init(void);

void pocketjs_ppa_deinit(void);

/*
 * Narrow C ABI consumed by the no_std Rust backend. Application code should
 * use pocketjs_ppa_init()/pocketjs_ppa_deinit() rather than calling these
 * bridge functions directly.
 */
int pocketjs_ppa_fill_rgb565_bridge(
    uint16_t *destination,
    size_t destination_pixels,
    uint32_t width,
    uint32_t height,
    uint32_t x,
    uint32_t y,
    uint32_t rect_width,
    uint32_t rect_height,
    uint16_t color
);

int pocketjs_ppa_blend_a8_rgb565_bridge(
    uint16_t *destination,
    size_t destination_pixels,
    uint32_t width,
    uint32_t height,
    const uint8_t *mask,
    size_t mask_len,
    uint32_t x,
    uint32_t y,
    uint32_t rect_width,
    uint32_t rect_height,
    uint8_t red,
    uint8_t green,
    uint8_t blue,
    uint8_t global_alpha
);

int pocketjs_ppa_srm_psm5650_rgb565_bridge(
    uint16_t *destination,
    size_t destination_pixels,
    uint32_t width,
    uint32_t height,
    const uint8_t *source,
    size_t source_len,
    uint32_t source_width,
    uint32_t source_height,
    uint32_t source_x,
    uint32_t source_y,
    uint32_t source_rect_width,
    uint32_t source_rect_height,
    uint32_t destination_x,
    uint32_t destination_y,
    uint32_t destination_rect_width,
    uint32_t destination_rect_height,
    uint32_t quarter_turn,
    int mirror_x,
    int mirror_y
);

#ifdef __cplusplus
}
#endif
