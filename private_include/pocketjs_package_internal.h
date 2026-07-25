#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "pocketjs.h"
#include "pocketjs_core.h"
#include "quickjs.h"

typedef struct {
    const uint8_t *manifest;
    size_t manifest_size;
    const uint8_t *identity;
    size_t identity_size;
    const uint8_t *plan;
    size_t plan_size;
    const uint8_t *javascript;
    size_t javascript_size;
    const uint8_t *pak;
    size_t pak_size;
} pocketjs_package_view_t;

esp_err_t pocketjs_package_open(
    const pocketjs_app_t *app,
    pocketjs_package_view_t *out_view
);

esp_err_t pocketjs_package_validate_plan(
    JSContext *context,
    const pocketjs_app_t *app,
    const pocketjs_package_view_t *view
);

esp_err_t pocketjs_package_install_assets(
    pocketjs_core_t *core,
    JSContext *context,
    JSValue ui,
    const pocketjs_package_view_t *view
);
