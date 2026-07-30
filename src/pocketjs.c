#include "pocketjs.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pocketjs_core.h"
#include "pocketjs_display_internal.h"
#include "pocketjs_package_internal.h"
#include "pocketjs_ppa.h"
#include "pocketjs_runtime_internal.h"

static const char *TAG = "pocketjs";
static atomic_bool s_runtime_claimed = false;

struct pocketjs {
    pocketjs_app_t app;
    pocketjs_package_view_t package;
    pocketjs_core_t *core;
    pocketjs_js_runtime_t *javascript;
    pocketjs_display_t *display;
    TaskHandle_t owner_task;
    bool ppa_initialized;
    bool busy;
};

static bool runtime_is_owner(const pocketjs_t *runtime)
{
    return runtime != NULL &&
        xTaskGetCurrentTaskHandle() == runtime->owner_task;
}

static uint32_t elapsed_us(int64_t started)
{
    const int64_t elapsed = esp_timer_get_time() - started;
    return elapsed > UINT32_MAX ? UINT32_MAX : (uint32_t)elapsed;
}

static void copy_render_stats(
    pocketjs_frame_stats_t *destination,
    const pocketjs_render_stats_t *source
)
{
    destination->ppa_fills = source->ppa_fills;
    destination->ppa_blends = source->ppa_blends;
    destination->ppa_srm = source->ppa_srm;
    destination->software_ops = source->software_ops;
    destination->software_words = source->software_words;
    destination->damage_regions = source->damage_regions;
    destination->damage_pixels = source->damage_pixels;
    destination->full_redraw = source->full_redraw;
}

esp_err_t pocketjs_create(
    const pocketjs_app_t *app,
    const pocketjs_config_t *config,
    pocketjs_t **out_runtime
)
{
    if (app == NULL || config == NULL || out_runtime == NULL ||
        app->target_id == NULL ||
        strcmp(app->target_id, POCKETJS_TARGET_ID) != 0 ||
        app->host_abi != POCKETJS_HOST_ABI || app->logical_width == 0 ||
        app->logical_height == 0 || app->raster_density == 0 ||
        app->pixel_format != POCKETJS_PIXEL_FORMAT_RGB565) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_runtime = NULL;

    bool expected = false;
    if (!atomic_compare_exchange_strong(&s_runtime_claimed, &expected, true)) {
        return ESP_ERR_INVALID_STATE;
    }

    pocketjs_t *runtime = calloc(1, sizeof(*runtime));
    if (runtime == NULL) {
        atomic_store(&s_runtime_claimed, false);
        return ESP_ERR_NO_MEM;
    }
    runtime->app = *app;
    runtime->owner_task = xTaskGetCurrentTaskHandle();
    runtime->busy = true;

    esp_err_t result = pocketjs_package_open(app, &runtime->package);
    if (result != ESP_OK) {
        goto fail;
    }

    runtime->core = pocketjs_core_create(
        app->logical_width,
        app->logical_height,
        app->raster_density
    );
    if (runtime->core == NULL) {
        result = ESP_ERR_NO_MEM;
        goto fail;
    }

    const esp_err_t ppa_result = pocketjs_ppa_init();
    runtime->ppa_initialized = ppa_result == ESP_OK;
    if (ppa_result != ESP_OK) {
        ESP_LOGW(
            TAG,
            "PPA unavailable (%s); ordered software fallback remains active",
            esp_err_to_name(ppa_result)
        );
    }

    result = pocketjs_js_runtime_create(
        runtime,
        runtime->core,
        app,
        config,
        &runtime->package,
        &runtime->javascript
    );
    if (result != ESP_OK) {
        goto fail;
    }

    runtime->busy = false;
    *out_runtime = runtime;
    return ESP_OK;

fail:
    if (runtime->javascript != NULL) {
        pocketjs_js_runtime_destroy(runtime->javascript);
    }
    if (runtime->ppa_initialized) {
        pocketjs_ppa_deinit();
    }
    if (runtime->core != NULL) {
        pocketjs_core_destroy(runtime->core);
    }
    free(runtime);
    atomic_store(&s_runtime_claimed, false);
    return result;
}

esp_err_t pocketjs_attach_display(
    pocketjs_t *runtime,
    pocketjs_display_t *display
)
{
    if (runtime == NULL || display == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!runtime_is_owner(runtime) || runtime->busy ||
        runtime->display != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (pocketjs_display_get_width(display) != runtime->app.logical_width ||
        pocketjs_display_get_height(display) != runtime->app.logical_height) {
        return ESP_ERR_INVALID_SIZE;
    }

    const esp_err_t result = pocketjs_display_attach_internal(display);
    if (result == ESP_OK) {
        runtime->display = display;
    }
    return result;
}

esp_err_t pocketjs_detach_display(pocketjs_t *runtime)
{
    if (runtime == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!runtime_is_owner(runtime) || runtime->busy ||
        runtime->display == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    pocketjs_display_detach_internal(runtime->display);
    runtime->display = NULL;
    return ESP_OK;
}

esp_err_t pocketjs_run_frame(
    pocketjs_t *runtime,
    const pocketjs_input_t *input,
    pocketjs_frame_stats_t *out_stats
)
{
    if (runtime == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!runtime_is_owner(runtime) || runtime->busy) {
        return ESP_ERR_INVALID_STATE;
    }

    runtime->busy = true;
    pocketjs_frame_stats_t stats = { 0 };
    const int64_t total_started = esp_timer_get_time();

    const int64_t javascript_started = esp_timer_get_time();
    esp_err_t result =
        pocketjs_js_runtime_run_frame(runtime->javascript, input);
    stats.javascript_us = elapsed_us(javascript_started);
    if (result != ESP_OK) {
        goto finish;
    }

    const int64_t tick_started = esp_timer_get_time();
    pocketjs_core_tick(runtime->core);
    stats.core_tick_us = elapsed_us(tick_started);

    const int64_t render_started = esp_timer_get_time();
    pocketjs_render_stats_t render_stats = { 0 };
    if (runtime->display != NULL) {
        result = pocketjs_display_render_internal(
            runtime->display,
            runtime->core,
            &render_stats,
            &stats.flush_wait_us
        );
    } else {
        pocketjs_damage_plan_t plan = { 0 };
        if (!pocketjs_core_prepare_rgb565_frame(
                runtime->core,
                POCKETJS_HEADLESS_TARGET_ID,
                &plan,
                &render_stats
            )) {
            result = ESP_FAIL;
        } else if (!pocketjs_core_commit_rgb565_frame(
                       runtime->core,
                       POCKETJS_HEADLESS_TARGET_ID
                   )) {
            pocketjs_core_abort_rgb565_frame(
                runtime->core,
                POCKETJS_HEADLESS_TARGET_ID
            );
            result = ESP_FAIL;
        }
    }
    const uint32_t render_and_wait = elapsed_us(render_started);
    stats.render_us = render_and_wait > stats.flush_wait_us
        ? render_and_wait - stats.flush_wait_us
        : 0;
    copy_render_stats(&stats, &render_stats);

finish:
    stats.total_us = elapsed_us(total_started);
    runtime->busy = false;
    if (out_stats != NULL) {
        *out_stats = stats;
    }
    return result;
}

void pocketjs_destroy(pocketjs_t *runtime)
{
    if (runtime == NULL) {
        return;
    }
    if (!runtime_is_owner(runtime) || runtime->busy) {
        ESP_LOGE(TAG, "destroy rejected outside the idle owner-task context");
        return;
    }
    runtime->busy = true;
    if (runtime->display != NULL) {
        pocketjs_display_detach_internal(runtime->display);
        runtime->display = NULL;
    }
    pocketjs_js_runtime_destroy(runtime->javascript);
    if (runtime->ppa_initialized) {
        pocketjs_ppa_deinit();
    }
    pocketjs_core_destroy(runtime->core);
    free(runtime);
    atomic_store(&s_runtime_claimed, false);
}
