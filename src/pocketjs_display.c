#include "pocketjs_display_internal.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include "esp_timer.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define POCKETJS_NATIVE_TARGET_COUNT 2U

struct pocketjs_display {
    uint32_t width;
    uint32_t height;
    void *buffers[2];
    size_t buffer_size_bytes;
    uint32_t buffer_count;
    uint32_t next_buffer;
    pocketjs_display_render_mode_t mode;
    pocketjs_display_callbacks_t callbacks;
    void *callback_user_data;
    uint32_t flush_timeout_ms;
    SemaphoreHandle_t completion;
    atomic_bool awaiting_ready;
    atomic_int completion_status;
    TaskHandle_t owner_task;
    bool buffers_configured;
    bool callbacks_configured;
    bool attached;
    bool busy;
    bool flush_inflight;
};

typedef struct {
    pocketjs_display_area_t regions[POCKETJS_MAX_DAMAGE_REGIONS];
    uint32_t count;
} aligned_damage_t;

static bool display_is_owner(const pocketjs_display_t *display)
{
    return display != NULL &&
        xTaskGetCurrentTaskHandle() == display->owner_task;
}

static bool is_power_of_two(uint32_t value)
{
    return value != 0 && (value & (value - 1U)) == 0;
}

static uint32_t align_down(uint32_t value, uint32_t alignment)
{
    return value & ~(alignment - 1U);
}

static uint32_t align_up_clamped(
    uint32_t value,
    uint32_t alignment,
    uint32_t limit
)
{
    const uint64_t aligned =
        ((uint64_t)value + alignment - 1U) & ~((uint64_t)alignment - 1U);
    return aligned > limit ? limit : (uint32_t)aligned;
}

static void render_stats_add_operations(
    pocketjs_render_stats_t *total,
    const pocketjs_render_stats_t *part
)
{
    total->ppa_fills += part->ppa_fills;
    total->ppa_blends += part->ppa_blends;
    total->ppa_srm += part->ppa_srm;
    total->software_ops += part->software_ops;
    total->software_words += part->software_words;
}

static void drain_completion(pocketjs_display_t *display)
{
    while (xSemaphoreTake(display->completion, 0) == pdTRUE) {
    }
}

static esp_err_t wait_for_flush(pocketjs_display_t *display, uint32_t *wait_us)
{
    if (!display->flush_inflight) {
        return ESP_OK;
    }

    const int64_t started = esp_timer_get_time();
    esp_err_t result = ESP_OK;
    if (display->callbacks.flush_wait != NULL) {
        result = display->callbacks.flush_wait(
            display,
            display->flush_timeout_ms,
            display->callback_user_data
        );
        atomic_store_explicit(
            &display->awaiting_ready,
            false,
            memory_order_release
        );
    } else {
        const TickType_t timeout = pdMS_TO_TICKS(display->flush_timeout_ms);
        if (xSemaphoreTake(display->completion, timeout) != pdTRUE) {
            result = ESP_ERR_TIMEOUT;
            atomic_store_explicit(
                &display->awaiting_ready,
                false,
                memory_order_release
            );
        } else {
            result = (esp_err_t)atomic_load_explicit(
                &display->completion_status,
                memory_order_acquire
            );
        }
    }
    display->flush_inflight = false;

    if (wait_us != NULL) {
        const int64_t elapsed = esp_timer_get_time() - started;
        *wait_us += elapsed > UINT32_MAX ? UINT32_MAX : (uint32_t)elapsed;
    }
    return result;
}

static esp_err_t submit_flush(
    pocketjs_display_t *display,
    const pocketjs_display_flush_t *flush
)
{
    if (display->flush_inflight) {
        return ESP_ERR_INVALID_STATE;
    }

    drain_completion(display);
    atomic_store_explicit(
        &display->completion_status,
        ESP_ERR_INVALID_STATE,
        memory_order_relaxed
    );
    atomic_store_explicit(&display->awaiting_ready, true, memory_order_release);
    display->flush_inflight = true;

    const esp_err_t result =
        display->callbacks.flush(display, flush, display->callback_user_data);
    if (result != ESP_OK) {
        atomic_store_explicit(
            &display->awaiting_ready,
            false,
            memory_order_release
        );
        display->flush_inflight = false;
        return result;
    }
    return ESP_OK;
}

static pocketjs_display_area_t aligned_damage_area(
    const pocketjs_display_t *display,
    const pocketjs_damage_rect_t *damage,
    const pocketjs_display_frame_info_t *frame
)
{
    const uint32_t x1 = damage->x > 0 ? damage->x - 1U : 0;
    const uint32_t y1 = damage->y > 0 ? damage->y - 1U : 0;
    const uint32_t x2 = damage->x + damage->width < display->width
        ? damage->x + damage->width + 1U
        : display->width;
    const uint32_t y2 = damage->y + damage->height < display->height
        ? damage->y + damage->height + 1U
        : display->height;

    return (pocketjs_display_area_t){
        .x1 = align_down(x1, frame->x_alignment),
        .y1 = align_down(y1, frame->y_alignment),
        .x2 = align_up_clamped(x2, frame->x_alignment, display->width),
        .y2 = align_up_clamped(y2, frame->y_alignment, display->height),
    };
}

static bool damage_areas_touch(
    const pocketjs_display_area_t *left,
    const pocketjs_display_area_t *right
)
{
    return left->x1 <= right->x2 && right->x1 <= left->x2 &&
        left->y1 <= right->y2 && right->y1 <= left->y2;
}

static pocketjs_display_area_t damage_area_union(
    const pocketjs_display_area_t *left,
    const pocketjs_display_area_t *right
)
{
    return (pocketjs_display_area_t){
        .x1 = left->x1 < right->x1 ? left->x1 : right->x1,
        .y1 = left->y1 < right->y1 ? left->y1 : right->y1,
        .x2 = left->x2 > right->x2 ? left->x2 : right->x2,
        .y2 = left->y2 > right->y2 ? left->y2 : right->y2,
    };
}

static bool aligned_damage_add(
    aligned_damage_t *damage,
    pocketjs_display_area_t area
)
{
    uint32_t index = 0;
    while (index < damage->count) {
        if (!damage_areas_touch(&area, &damage->regions[index])) {
            ++index;
            continue;
        }
        area = damage_area_union(&area, &damage->regions[index]);
        --damage->count;
        damage->regions[index] = damage->regions[damage->count];
        index = 0;
    }
    if (damage->count >= POCKETJS_MAX_DAMAGE_REGIONS) {
        return false;
    }
    damage->regions[damage->count++] = area;
    return true;
}

static void aligned_damage_sort(aligned_damage_t *damage)
{
    for (uint32_t index = 1; index < damage->count; ++index) {
        const pocketjs_display_area_t value = damage->regions[index];
        uint32_t insert = index;
        while (insert > 0) {
            const pocketjs_display_area_t *previous =
                &damage->regions[insert - 1U];
            if (previous->y1 < value.y1 ||
                (previous->y1 == value.y1 && previous->x1 <= value.x1)) {
                break;
            }
            damage->regions[insert] = *previous;
            --insert;
        }
        damage->regions[insert] = value;
    }
}

static bool build_aligned_damage(
    const pocketjs_display_t *display,
    const pocketjs_damage_plan_t *plan,
    const pocketjs_display_frame_info_t *frame,
    aligned_damage_t *out_damage
)
{
    if (plan->region_count > POCKETJS_MAX_DAMAGE_REGIONS ||
        out_damage == NULL) {
        return false;
    }

    *out_damage = (aligned_damage_t){ 0 };
    for (uint32_t index = 0; index < plan->region_count; ++index) {
        const pocketjs_damage_rect_t *source = &plan->regions[index];
        if (source->width == 0 || source->height == 0 ||
            source->x >= display->width || source->y >= display->height ||
            source->width > display->width - source->x ||
            source->height > display->height - source->y) {
            return false;
        }
        const pocketjs_display_area_t area =
            aligned_damage_area(display, source, frame);
        if (area.x1 >= area.x2 || area.y1 >= area.y2 ||
            !aligned_damage_add(out_damage, area)) {
            return false;
        }
    }
    aligned_damage_sort(out_damage);
    return true;
}

static esp_err_t render_transaction(
    pocketjs_display_t *display,
    pocketjs_core_t *core,
    const pocketjs_display_frame_info_t *frame,
    const pocketjs_damage_plan_t *plan,
    const pocketjs_render_stats_t *plan_stats,
    uint32_t *flush_wait_us,
    pocketjs_render_stats_t *out_stats
)
{
    *out_stats = *plan_stats;
    aligned_damage_t damage = { 0 };
    if (!build_aligned_damage(display, plan, frame, &damage)) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    const size_t row_bytes = (size_t)display->width * sizeof(uint16_t);
    uint32_t max_rows = (uint32_t)(display->buffer_size_bytes / row_bytes);
    if (display->mode != POCKETJS_DISPLAY_RENDER_MODE_PARTIAL) {
        max_rows = display->height;
    }
    if (max_rows == 0 || max_rows < frame->y_alignment) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (max_rows < display->height) {
        max_rows = align_down(max_rows, frame->y_alignment);
    }

    uint32_t buffer_index = display->next_buffer;
    if (display->mode == POCKETJS_DISPLAY_RENDER_MODE_DIRECT) {
        buffer_index = display->buffer_count == 1U ? 0 : frame->back_target_id;
        if (buffer_index >= display->buffer_count) {
            return ESP_ERR_INVALID_RESPONSE;
        }
    }
    for (uint32_t region_index = 0; region_index < damage.count;
         ++region_index) {
        const pocketjs_display_area_t area = damage.regions[region_index];

        for (uint32_t y = area.y1; y < area.y2;) {
            uint32_t chunk_height = area.y2 - y;
            if (chunk_height > max_rows) {
                chunk_height = max_rows;
            }
            if (y + chunk_height < area.y2 && frame->y_alignment > 1U) {
                chunk_height = align_down(chunk_height, frame->y_alignment);
            }
            if (chunk_height == 0) {
                return ESP_ERR_INVALID_SIZE;
            }

            if (display->buffer_count == 1U ||
                display->mode == POCKETJS_DISPLAY_RENDER_MODE_DIRECT) {
                const esp_err_t wait_result =
                    wait_for_flush(display, flush_wait_us);
                if (wait_result != ESP_OK) {
                    return wait_result;
                }
            }

            uint16_t *strip = display->buffers[buffer_index];
            uint16_t *render_base = strip;
            size_t render_size = row_bytes * chunk_height;
            if (display->mode != POCKETJS_DISPLAY_RENDER_MODE_PARTIAL) {
                render_base += (size_t)y * display->width;
            }

            pocketjs_render_stats_t strip_stats = { 0 };
            if (!pocketjs_core_render_rgb565_strip(
                    core,
                    render_base,
                    render_size,
                    area.x1,
                    y,
                    area.x2 - area.x1,
                    chunk_height,
                    &strip_stats
                )) {
                return ESP_FAIL;
            }
            render_stats_add_operations(out_stats, &strip_stats);

            const esp_err_t wait_result =
                wait_for_flush(display, flush_wait_us);
            if (wait_result != ESP_OK) {
                return wait_result;
            }

            const bool last = region_index + 1U == damage.count &&
                y + chunk_height == area.y2;
            const uint16_t *map_pixels = render_base + area.x1;
            const pocketjs_display_flush_t flush = {
                .area = {
                    .x1 = area.x1,
                    .y1 = y,
                    .x2 = area.x2,
                    .y2 = y + chunk_height,
                },
                .pixels = map_pixels,
                .size_bytes = (chunk_height - 1U) * row_bytes +
                    (area.x2 - area.x1) * sizeof(uint16_t),
                .stride_bytes = row_bytes,
                .format = POCKETJS_PIXEL_FORMAT_RGB565,
                .target_id = frame->back_target_id,
                .is_last = last,
            };
            const esp_err_t submit_result = submit_flush(display, &flush);
            if (submit_result != ESP_OK) {
                return submit_result;
            }

            if (display->mode == POCKETJS_DISPLAY_RENDER_MODE_PARTIAL) {
                buffer_index = (buffer_index + 1U) % display->buffer_count;
            }
            y += chunk_height;
        }
    }

    const esp_err_t result = wait_for_flush(display, flush_wait_us);
    if (result == ESP_OK) {
        display->next_buffer =
            display->mode == POCKETJS_DISPLAY_RENDER_MODE_DIRECT
            ? (buffer_index + 1U) % display->buffer_count
            : buffer_index;
    }
    return result;
}

static esp_err_t render_damage_frame(
    pocketjs_display_t *display,
    pocketjs_core_t *core,
    const pocketjs_display_frame_info_t *frame,
    uint32_t *flush_wait_us,
    pocketjs_render_stats_t *out_stats,
    bool *out_present
)
{
    *out_present = false;
    pocketjs_damage_plan_t front_plan = { 0 };
    pocketjs_render_stats_t front_stats = { 0 };
    if (!pocketjs_core_prepare_rgb565_frame(
            core,
            frame->front_target_id,
            &front_plan,
            &front_stats
        )) {
        return ESP_FAIL;
    }

    if (front_plan.region_count == 0) {
        if (!pocketjs_core_commit_rgb565_frame(core, frame->front_target_id)) {
            return ESP_FAIL;
        }
        *out_stats = front_stats;
        *out_present = false;
        return ESP_OK;
    }

    if (!pocketjs_core_cancel_rgb565_frame(core, frame->front_target_id)) {
        return ESP_FAIL;
    }

    pocketjs_damage_plan_t back_plan = { 0 };
    pocketjs_render_stats_t back_stats = { 0 };
    if (!pocketjs_core_prepare_rgb565_frame(
            core,
            frame->back_target_id,
            &back_plan,
            &back_stats
        )) {
        return ESP_FAIL;
    }

    esp_err_t result = ESP_OK;
    if (back_plan.region_count != 0) {
        result = render_transaction(
            display,
            core,
            frame,
            &back_plan,
            &back_stats,
            flush_wait_us,
            out_stats
        );
        if (result != ESP_OK) {
            pocketjs_core_abort_rgb565_frame(core, frame->back_target_id);
            return result;
        }
    } else {
        *out_stats = back_stats;
    }

    if (!pocketjs_core_commit_rgb565_frame(core, frame->back_target_id)) {
        pocketjs_core_abort_rgb565_frame(core, frame->back_target_id);
        return ESP_FAIL;
    }
    *out_present = true;
    return ESP_OK;
}

static esp_err_t render_full_frame(
    pocketjs_display_t *display,
    pocketjs_core_t *core,
    const pocketjs_display_frame_info_t *frame,
    uint32_t *flush_wait_us,
    pocketjs_render_stats_t *out_stats
)
{
    const uint32_t buffer_index = display->next_buffer;
    uint16_t *buffer = display->buffers[buffer_index];
    const size_t frame_bytes =
        (size_t)display->width * display->height * sizeof(uint16_t);

    if (!pocketjs_core_render_rgb565(core, buffer, frame_bytes, out_stats)) {
        return ESP_FAIL;
    }

    const pocketjs_display_flush_t flush = {
        .area = {
            .x1 = 0,
            .y1 = 0,
            .x2 = display->width,
            .y2 = display->height,
        },
        .pixels = buffer,
        .size_bytes = frame_bytes,
        .stride_bytes = (size_t)display->width * sizeof(uint16_t),
        .format = POCKETJS_PIXEL_FORMAT_RGB565,
        .target_id = frame->back_target_id,
        .is_last = true,
    };
    esp_err_t result = submit_flush(display, &flush);
    if (result == ESP_OK) {
        result = wait_for_flush(display, flush_wait_us);
    }
    if (result == ESP_OK) {
        display->next_buffer = (buffer_index + 1U) % display->buffer_count;
    }
    return result;
}

esp_err_t pocketjs_display_create(
    uint32_t width,
    uint32_t height,
    pocketjs_display_t **out_display
)
{
    if (width == 0 || height == 0 || out_display == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_display = NULL;

    pocketjs_display_t *display = calloc(1, sizeof(*display));
    if (display == NULL) {
        return ESP_ERR_NO_MEM;
    }
    display->completion = xSemaphoreCreateBinary();
    if (display->completion == NULL) {
        free(display);
        return ESP_ERR_NO_MEM;
    }
    display->width = width;
    display->height = height;
    display->owner_task = xTaskGetCurrentTaskHandle();
    display->flush_timeout_ms = POCKETJS_DISPLAY_DEFAULT_FLUSH_TIMEOUT_MS;
    atomic_init(&display->awaiting_ready, false);
    atomic_init(&display->completion_status, ESP_ERR_INVALID_STATE);
    *out_display = display;
    return ESP_OK;
}

esp_err_t pocketjs_display_set_buffers(
    pocketjs_display_t *display,
    void *buffer1,
    void *buffer2,
    size_t buffer_size_bytes,
    pocketjs_display_render_mode_t mode
)
{
    if (display == NULL || buffer1 == NULL ||
        mode > POCKETJS_DISPLAY_RENDER_MODE_FULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!display_is_owner(display) || display->attached || display->busy) {
        return ESP_ERR_INVALID_STATE;
    }
    if (((uintptr_t)buffer1 % POCKETJS_DRAW_BUFFER_ALIGNMENT) != 0 ||
        (buffer2 != NULL &&
         ((uintptr_t)buffer2 % POCKETJS_DRAW_BUFFER_ALIGNMENT) != 0)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (display->width > SIZE_MAX / sizeof(uint16_t) ||
        display->height >
            SIZE_MAX / ((size_t)display->width * sizeof(uint16_t))) {
        return ESP_ERR_INVALID_SIZE;
    }
    const size_t row_bytes = (size_t)display->width * sizeof(uint16_t);
    const size_t frame_bytes = row_bytes * display->height;
    const size_t required =
        mode == POCKETJS_DISPLAY_RENDER_MODE_PARTIAL ? row_bytes : frame_bytes;
    if (buffer_size_bytes < required || buffer_size_bytes % sizeof(uint16_t)) {
        return ESP_ERR_INVALID_SIZE;
    }

    display->buffers[0] = buffer1;
    display->buffers[1] = buffer2;
    display->buffer_size_bytes = buffer_size_bytes;
    display->buffer_count = buffer2 != NULL ? 2U : 1U;
    display->mode = mode;
    display->next_buffer = 0;
    display->buffers_configured = true;
    return ESP_OK;
}

esp_err_t pocketjs_display_set_callbacks(
    pocketjs_display_t *display,
    const pocketjs_display_callbacks_t *callbacks,
    void *user_data,
    uint32_t flush_timeout_ms
)
{
    if (display == NULL || callbacks == NULL || callbacks->flush == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!display_is_owner(display) || display->attached || display->busy) {
        return ESP_ERR_INVALID_STATE;
    }
    const bool has_begin = callbacks->begin_frame != NULL;
    if (has_begin != (callbacks->end_frame != NULL) ||
        has_begin != (callbacks->abort_frame != NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    display->callbacks = *callbacks;
    display->callback_user_data = user_data;
    display->flush_timeout_ms = flush_timeout_ms != 0
        ? flush_timeout_ms
        : POCKETJS_DISPLAY_DEFAULT_FLUSH_TIMEOUT_MS;
    display->callbacks_configured = true;
    return ESP_OK;
}

esp_err_t pocketjs_display_flush_ready(
    pocketjs_display_t *display,
    esp_err_t status
)
{
    if (display == NULL || display->callbacks.flush_wait != NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!display_is_owner(display)) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!atomic_exchange_explicit(
            &display->awaiting_ready,
            false,
            memory_order_acq_rel
        )) {
        return ESP_ERR_INVALID_STATE;
    }
    atomic_store_explicit(
        &display->completion_status,
        status,
        memory_order_release
    );
    return xSemaphoreGive(display->completion) == pdTRUE ? ESP_OK : ESP_FAIL;
}

esp_err_t pocketjs_display_flush_ready_from_isr(
    pocketjs_display_t *display,
    esp_err_t status,
    BaseType_t *higher_priority_task_woken
)
{
    if (display == NULL || display->callbacks.flush_wait != NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!atomic_exchange_explicit(
            &display->awaiting_ready,
            false,
            memory_order_acq_rel
        )) {
        return ESP_ERR_INVALID_STATE;
    }
    atomic_store_explicit(
        &display->completion_status,
        status,
        memory_order_release
    );
    BaseType_t local_woken = pdFALSE;
    BaseType_t *woken = higher_priority_task_woken != NULL
        ? higher_priority_task_woken
        : &local_woken;
    return xSemaphoreGiveFromISR(display->completion, woken) == pdTRUE
        ? ESP_OK
        : ESP_FAIL;
}

uint32_t pocketjs_display_get_width(const pocketjs_display_t *display)
{
    return display != NULL ? display->width : 0;
}

uint32_t pocketjs_display_get_height(const pocketjs_display_t *display)
{
    return display != NULL ? display->height : 0;
}

pocketjs_display_render_mode_t pocketjs_display_get_render_mode(
    const pocketjs_display_t *display
)
{
    return display != NULL ? display->mode
                           : POCKETJS_DISPLAY_RENDER_MODE_PARTIAL;
}

esp_err_t pocketjs_display_attach_internal(pocketjs_display_t *display)
{
    if (display == NULL || !display_is_owner(display) || display->attached ||
        !display->buffers_configured || !display->callbacks_configured) {
        return ESP_ERR_INVALID_STATE;
    }
    display->attached = true;
    return ESP_OK;
}

void pocketjs_display_detach_internal(pocketjs_display_t *display)
{
    if (display_is_owner(display)) {
        display->attached = false;
    }
}

esp_err_t pocketjs_display_render_internal(
    pocketjs_display_t *display,
    pocketjs_core_t *core,
    pocketjs_render_stats_t *out_stats,
    uint32_t *out_flush_wait_us
)
{
    if (display == NULL || core == NULL || out_stats == NULL ||
        !display->attached || display->busy) {
        return ESP_ERR_INVALID_STATE;
    }

    display->busy = true;
    *out_stats = (pocketjs_render_stats_t){ 0 };
    if (out_flush_wait_us != NULL) {
        *out_flush_wait_us = 0;
    }

    pocketjs_display_frame_info_t frame = {
        .front_target_id = 0,
        .back_target_id = display->mode == POCKETJS_DISPLAY_RENDER_MODE_DIRECT
            ? display->next_buffer
            : 0,
        .x_alignment = 1,
        .y_alignment = 1,
    };
    if (display->callbacks.begin_frame == NULL &&
        display->mode == POCKETJS_DISPLAY_RENDER_MODE_DIRECT &&
        display->buffer_count == 2U) {
        frame.front_target_id = frame.back_target_id ^ 1U;
    }
    bool present = display->mode == POCKETJS_DISPLAY_RENDER_MODE_FULL;
    bool frame_started = display->callbacks.begin_frame == NULL;
    esp_err_t result = ESP_OK;
    if (display->callbacks.begin_frame != NULL) {
        result = display->callbacks
                     .begin_frame(display, &frame, display->callback_user_data);
        frame_started = result == ESP_OK;
    }
    if (result == ESP_OK &&
        (frame.front_target_id >= POCKETJS_NATIVE_TARGET_COUNT ||
         frame.back_target_id >= POCKETJS_NATIVE_TARGET_COUNT ||
         !is_power_of_two(frame.x_alignment) ||
         !is_power_of_two(frame.y_alignment))) {
        result = ESP_ERR_INVALID_ARG;
    }

    if (result == ESP_OK) {
        result = display->mode == POCKETJS_DISPLAY_RENDER_MODE_FULL
            ? render_full_frame(
                  display,
                  core,
                  &frame,
                  out_flush_wait_us,
                  out_stats
              )
            : render_damage_frame(
                  display,
                  core,
                  &frame,
                  out_flush_wait_us,
                  out_stats,
                  &present
              );
    }

    if (result == ESP_OK && present && display->callbacks.end_frame != NULL) {
        result = display->callbacks
                     .end_frame(display, &frame, display->callback_user_data);
        if (result != ESP_OK) {
            pocketjs_core_abort_rgb565_frame(core, frame.back_target_id);
        }
    }
    if (frame_started && (!present || result != ESP_OK) &&
        display->callbacks.abort_frame != NULL) {
        display->callbacks
            .abort_frame(display, &frame, display->callback_user_data);
    }

    display->busy = false;
    return result;
}

esp_err_t pocketjs_display_delete(pocketjs_display_t *display)
{
    if (display == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!display_is_owner(display) || display->attached || display->busy ||
        display->flush_inflight) {
        return ESP_ERR_INVALID_STATE;
    }
    vSemaphoreDelete(display->completion);
    free(display);
    return ESP_OK;
}
