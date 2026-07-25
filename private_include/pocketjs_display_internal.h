#pragma once

#include "pocketjs_display.h"
#include "pocketjs_core.h"

esp_err_t pocketjs_display_attach_internal(pocketjs_display_t *display);
void pocketjs_display_detach_internal(pocketjs_display_t *display);

esp_err_t pocketjs_display_render_internal(
    pocketjs_display_t *display,
    pocketjs_core_t *core,
    pocketjs_render_stats_t *out_stats,
    uint32_t *out_flush_wait_us
);
