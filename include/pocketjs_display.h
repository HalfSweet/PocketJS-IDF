#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

#define POCKETJS_DRAW_BUFFER_ALIGNMENT 128U
#define POCKETJS_DISPLAY_DEFAULT_FLUSH_TIMEOUT_MS 1000U

typedef struct pocketjs_display pocketjs_display_t;

typedef enum {
    POCKETJS_DISPLAY_RENDER_MODE_PARTIAL = 0,
    POCKETJS_DISPLAY_RENDER_MODE_DIRECT = 1,
    POCKETJS_DISPLAY_RENDER_MODE_FULL = 2,
} pocketjs_display_render_mode_t;

typedef enum {
    POCKETJS_PIXEL_FORMAT_RGB565 = 0,
} pocketjs_pixel_format_t;

/** Half-open logical pixel bounds: [x1, x2) x [y1, y2). */
typedef struct {
    uint32_t x1;
    uint32_t y1;
    uint32_t x2;
    uint32_t y2;
} pocketjs_display_area_t;

typedef struct {
    pocketjs_display_area_t area;
    const void *pixels;
    size_t size_bytes;
    size_t stride_bytes;
    pocketjs_pixel_format_t format;
    uint32_t target_id;
    bool is_last;
} pocketjs_display_flush_t;

/**
 * A board backend may override the default logical target ids and request
 * source-boundary alignment for its scaler. Alignments must be powers of two.
 */
typedef struct {
    uint32_t front_target_id;
    uint32_t back_target_id;
    uint32_t x_alignment;
    uint32_t y_alignment;
} pocketjs_display_frame_info_t;

typedef esp_err_t (*pocketjs_display_begin_frame_cb_t)(
    pocketjs_display_t *display,
    pocketjs_display_frame_info_t *frame,
    void *user_data
);

/**
 * Submit one immutable RGB565 map. The driver must complete it either by
 * calling pocketjs_display_flush_ready[_from_isr]() or by implementing
 * flush_wait_cb. The pixel storage remains owned by the application.
 */
typedef esp_err_t (*pocketjs_display_flush_cb_t)(
    pocketjs_display_t *display,
    const pocketjs_display_flush_t *flush,
    void *user_data
);

/**
 * Optional driver-owned wait path. When present it replaces the component's
 * internal semaphore wait and returns the final status of the submitted map.
 */
typedef esp_err_t (*pocketjs_display_flush_wait_cb_t)(
    pocketjs_display_t *display,
    uint32_t timeout_ms,
    void *user_data
);

typedef esp_err_t (*pocketjs_display_end_frame_cb_t)(
    pocketjs_display_t *display,
    const pocketjs_display_frame_info_t *frame,
    void *user_data
);

typedef void (*pocketjs_display_abort_frame_cb_t)(
    pocketjs_display_t *display,
    const pocketjs_display_frame_info_t *frame,
    void *user_data
);

typedef struct {
    /**
     * Optional frame-transaction callbacks. When begin_frame is set,
     * end_frame and abort_frame must also be set. Every successful begin is
     * paired with exactly one end (presented frame) or abort (failed or
     * unchanged frame).
     */
    pocketjs_display_begin_frame_cb_t begin_frame;
    pocketjs_display_flush_cb_t flush;
    pocketjs_display_flush_wait_cb_t flush_wait;
    pocketjs_display_end_frame_cb_t end_frame;
    pocketjs_display_abort_frame_cb_t abort_frame;
} pocketjs_display_callbacks_t;

esp_err_t pocketjs_display_create(
    uint32_t width,
    uint32_t height,
    pocketjs_display_t **out_display
);

esp_err_t pocketjs_display_set_buffers(
    pocketjs_display_t *display,
    void *buffer1,
    void *buffer2,
    size_t buffer_size_bytes,
    pocketjs_display_render_mode_t mode
);

esp_err_t pocketjs_display_set_callbacks(
    pocketjs_display_t *display,
    const pocketjs_display_callbacks_t *callbacks,
    void *user_data,
    uint32_t flush_timeout_ms
);

/**
 * Complete the most recently submitted asynchronous map.
 *
 * A synchronous flush callback calls this before returning. Exactly one
 * completion is required for each successful flush submission when
 * flush_wait_cb is not installed.
 *
 * Call this from the display owner task. Interrupt handlers must use
 * pocketjs_display_flush_ready_from_isr().
 */
esp_err_t pocketjs_display_flush_ready(
    pocketjs_display_t *display,
    esp_err_t status
);

esp_err_t pocketjs_display_flush_ready_from_isr(
    pocketjs_display_t *display,
    esp_err_t status,
    BaseType_t *higher_priority_task_woken
);

uint32_t pocketjs_display_get_width(const pocketjs_display_t *display);
uint32_t pocketjs_display_get_height(const pocketjs_display_t *display);
pocketjs_display_render_mode_t pocketjs_display_get_render_mode(
    const pocketjs_display_t *display
);

esp_err_t pocketjs_display_delete(pocketjs_display_t *display);

#ifdef __cplusplus
}
#endif
