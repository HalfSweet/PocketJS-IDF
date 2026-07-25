#pragma once

#include "pocketjs.h"
#include "pocketjs_core.h"
#include "pocketjs_package_internal.h"

typedef struct pocketjs_js_runtime pocketjs_js_runtime_t;

esp_err_t pocketjs_js_runtime_create(
    pocketjs_t *owner,
    pocketjs_core_t *core,
    const pocketjs_app_t *app,
    const pocketjs_config_t *config,
    const pocketjs_package_view_t *package,
    pocketjs_js_runtime_t **out_runtime
);

esp_err_t pocketjs_js_runtime_run_frame(
    pocketjs_js_runtime_t *runtime,
    const pocketjs_input_t *input
);

void pocketjs_js_runtime_destroy(pocketjs_js_runtime_t *runtime);
