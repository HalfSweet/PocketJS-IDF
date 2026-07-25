#include "pocketjs_runtime_internal.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "pocketjs_core.h"
#include "pocketjs_quickjs.h"
#include "quickjs-libc.h"
#include "quickjs.h"

static const char *TAG = "pocketjs_runtime";

typedef union {
    size_t size;
    max_align_t alignment;
} pocketjs_alloc_header_t;

struct pocketjs_js_runtime {
    JSRuntime *rt;
    JSContext *ctx;
    JSValue frame_function;
    pocketjs_t *owner;
    pocketjs_core_t *core;
    pocketjs_extension_context_t *extensions;
    size_t extension_count;
    size_t installed_extension_count;
    int unhandled_rejections;
};

struct pocketjs_extension_context {
    pocketjs_js_runtime_t *runtime;
    pocketjs_extension_t extension;
    bool active;
};

static pocketjs_js_runtime_t *runtime_from_context(JSContext *context)
{
    return JS_GetContextOpaque(context);
}

static pocketjs_core_t *core_from_context(JSContext *context)
{
    pocketjs_js_runtime_t *runtime = runtime_from_context(context);
    return runtime != NULL ? runtime->core : NULL;
}

static void *pocketjs_qjs_malloc(void *opaque, size_t size)
{
    (void)opaque;
    if (size == 0 || size > SIZE_MAX - sizeof(pocketjs_alloc_header_t)) {
        return NULL;
    }

    const size_t total = sizeof(pocketjs_alloc_header_t) + size;
    pocketjs_alloc_header_t *header = heap_caps_malloc(
        total,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );
    if (header == NULL) {
        header = heap_caps_malloc(
            total,
            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT
        );
    }
    if (header == NULL) {
        return NULL;
    }
    header->size = size;
    return header + 1;
}

static void *pocketjs_qjs_calloc(void *opaque, size_t count, size_t size)
{
    if (count != 0 && size > SIZE_MAX / count) {
        return NULL;
    }
    const size_t total = count * size;
    void *ptr = pocketjs_qjs_malloc(opaque, total);
    if (ptr != NULL) {
        memset(ptr, 0, total);
    }
    return ptr;
}

static void pocketjs_qjs_free(void *opaque, void *ptr)
{
    (void)opaque;
    if (ptr != NULL) {
        heap_caps_free(((pocketjs_alloc_header_t *)ptr) - 1);
    }
}

static size_t pocketjs_qjs_usable_size(const void *ptr)
{
    if (ptr == NULL) {
        return 0;
    }
    return (((const pocketjs_alloc_header_t *)ptr) - 1)->size;
}

static void *pocketjs_qjs_realloc(void *opaque, void *ptr, size_t size)
{
    if (ptr == NULL) {
        return pocketjs_qjs_malloc(opaque, size);
    }
    if (size == 0) {
        pocketjs_qjs_free(opaque, ptr);
        return NULL;
    }

    const size_t old_size = pocketjs_qjs_usable_size(ptr);
    void *next = pocketjs_qjs_malloc(opaque, size);
    if (next == NULL) {
        return NULL;
    }
    memcpy(next, ptr, old_size < size ? old_size : size);
    pocketjs_qjs_free(opaque, ptr);
    return next;
}

static const JSMallocFunctions POCKETJS_QJS_ALLOCATOR = {
    .js_calloc = pocketjs_qjs_calloc,
    .js_malloc = pocketjs_qjs_malloc,
    .js_free = pocketjs_qjs_free,
    .js_realloc = pocketjs_qjs_realloc,
    .js_malloc_usable_size = pocketjs_qjs_usable_size,
};

static int32_t js_arg_i32(JSContext *ctx, int argc, JSValueConst *argv, int index)
{
    int32_t value = 0;
    if (index < argc) {
        JS_ToInt32(ctx, &value, argv[index]);
    }
    return value;
}

static uint32_t js_arg_u32(JSContext *ctx, int argc, JSValueConst *argv, int index)
{
    uint32_t value = 0;
    if (index < argc) {
        JS_ToUint32(ctx, &value, argv[index]);
    }
    return value;
}

static double js_arg_f64(JSContext *ctx, int argc, JSValueConst *argv, int index)
{
    double value = 0.0;
    if (index < argc) {
        JS_ToFloat64(ctx, &value, argv[index]);
    }
    return value;
}

static JSValue js_ui_create_node(
    JSContext *ctx,
    JSValueConst this_value,
    int argc,
    JSValueConst *argv
)
{
    (void)this_value;
    return JS_NewInt32(
        ctx,
        pocketjs_core_create_node(
            core_from_context(ctx),
            js_arg_u32(ctx, argc, argv, 0)
        )
    );
}

static JSValue js_ui_destroy_node(
    JSContext *ctx,
    JSValueConst this_value,
    int argc,
    JSValueConst *argv
)
{
    (void)this_value;
    pocketjs_core_destroy_node(
        core_from_context(ctx),
        js_arg_i32(ctx, argc, argv, 0)
    );
    return JS_UNDEFINED;
}

static JSValue js_ui_insert_before(
    JSContext *ctx,
    JSValueConst this_value,
    int argc,
    JSValueConst *argv
)
{
    (void)this_value;
    pocketjs_core_insert_before(
        core_from_context(ctx),
        js_arg_i32(ctx, argc, argv, 0),
        js_arg_i32(ctx, argc, argv, 1),
        js_arg_i32(ctx, argc, argv, 2)
    );
    return JS_UNDEFINED;
}

static JSValue js_ui_remove_child(
    JSContext *ctx,
    JSValueConst this_value,
    int argc,
    JSValueConst *argv
)
{
    (void)this_value;
    pocketjs_core_remove_child(
        core_from_context(ctx),
        js_arg_i32(ctx, argc, argv, 0),
        js_arg_i32(ctx, argc, argv, 1)
    );
    return JS_UNDEFINED;
}

static JSValue js_ui_set_style(
    JSContext *ctx,
    JSValueConst this_value,
    int argc,
    JSValueConst *argv
)
{
    (void)this_value;
    pocketjs_core_set_style(
        core_from_context(ctx),
        js_arg_i32(ctx, argc, argv, 0),
        js_arg_i32(ctx, argc, argv, 1)
    );
    return JS_UNDEFINED;
}

static JSValue js_ui_set_prop(
    JSContext *ctx,
    JSValueConst this_value,
    int argc,
    JSValueConst *argv
)
{
    (void)this_value;
    pocketjs_core_set_prop(
        core_from_context(ctx),
        js_arg_i32(ctx, argc, argv, 0),
        js_arg_u32(ctx, argc, argv, 1),
        js_arg_f64(ctx, argc, argv, 2)
    );
    return JS_UNDEFINED;
}

static JSValue js_set_text_common(
    JSContext *ctx,
    int argc,
    JSValueConst *argv,
    bool replace
)
{
    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "setText requires node id and text");
    }

    size_t len = 0;
    const char *text = JS_ToCStringLen(ctx, &len, argv[1]);
    if (text == NULL) {
        return JS_EXCEPTION;
    }

    int ok = replace
        ? pocketjs_core_replace_text(
              core_from_context(ctx),
              js_arg_i32(ctx, argc, argv, 0),
              (const uint8_t *)text,
              len
          )
        : pocketjs_core_set_text(
              core_from_context(ctx),
              js_arg_i32(ctx, argc, argv, 0),
              (const uint8_t *)text,
              len
          );
    JS_FreeCString(ctx, text);

    if (!ok) {
        return JS_ThrowInternalError(ctx, "PocketJS rejected the UTF-8 text");
    }
    return JS_UNDEFINED;
}

static JSValue js_ui_set_text(
    JSContext *ctx,
    JSValueConst this_value,
    int argc,
    JSValueConst *argv
)
{
    (void)this_value;
    return js_set_text_common(ctx, argc, argv, false);
}

static JSValue js_ui_replace_text(
    JSContext *ctx,
    JSValueConst this_value,
    int argc,
    JSValueConst *argv
)
{
    (void)this_value;
    return js_set_text_common(ctx, argc, argv, true);
}

static JSValue js_ui_animate(
    JSContext *ctx,
    JSValueConst this_value,
    int argc,
    JSValueConst *argv
)
{
    (void)this_value;
    return JS_NewInt32(
        ctx,
        pocketjs_core_animate(
            core_from_context(ctx),
            js_arg_i32(ctx, argc, argv, 0),
            js_arg_u32(ctx, argc, argv, 1),
            js_arg_f64(ctx, argc, argv, 2),
            js_arg_u32(ctx, argc, argv, 3),
            js_arg_u32(ctx, argc, argv, 4),
            js_arg_u32(ctx, argc, argv, 5)
        )
    );
}

static JSValue js_ui_cancel_animation(
    JSContext *ctx,
    JSValueConst this_value,
    int argc,
    JSValueConst *argv
)
{
    (void)this_value;
    pocketjs_core_cancel_animation(
        core_from_context(ctx),
        js_arg_i32(ctx, argc, argv, 0)
    );
    return JS_UNDEFINED;
}

static JSValue js_ui_set_focus(
    JSContext *ctx,
    JSValueConst this_value,
    int argc,
    JSValueConst *argv
)
{
    (void)this_value;
    pocketjs_core_set_focus(
        core_from_context(ctx),
        js_arg_i32(ctx, argc, argv, 0)
    );
    return JS_UNDEFINED;
}

static JSValue js_ui_set_active(
    JSContext *ctx,
    JSValueConst this_value,
    int argc,
    JSValueConst *argv
)
{
    (void)this_value;
    pocketjs_core_set_active(
        core_from_context(ctx),
        js_arg_i32(ctx, argc, argv, 0),
        js_arg_i32(ctx, argc, argv, 1)
    );
    return JS_UNDEFINED;
}

static JSValue js_ui_hit_test(
    JSContext *ctx,
    JSValueConst this_value,
    int argc,
    JSValueConst *argv
)
{
    (void)this_value;
    return JS_NewInt32(
        ctx,
        pocketjs_core_hit_test(
            core_from_context(ctx),
            (float)js_arg_f64(ctx, argc, argv, 0),
            (float)js_arg_f64(ctx, argc, argv, 1)
        )
    );
}

static JSValue js_ui_load_styles(
    JSContext *ctx,
    JSValueConst this_value,
    int argc,
    JSValueConst *argv
)
{
    (void)this_value;
    if (argc < 1) {
        return JS_NewBool(ctx, false);
    }
    size_t len = 0;
    const uint8_t *bytes = JS_GetUint8Array(ctx, &len, argv[0]);
    if (bytes == NULL) {
        return JS_EXCEPTION;
    }
    return JS_NewBool(
        ctx,
        pocketjs_core_load_styles(core_from_context(ctx), bytes, len)
    );
}

static JSValue js_ui_load_font_atlas(
    JSContext *ctx,
    JSValueConst this_value,
    int argc,
    JSValueConst *argv
)
{
    (void)this_value;
    if (argc < 1) {
        return JS_NewBool(ctx, false);
    }
    size_t len = 0;
    const uint8_t *bytes = JS_GetUint8Array(ctx, &len, argv[0]);
    if (bytes == NULL) {
        return JS_EXCEPTION;
    }
    return JS_NewBool(
        ctx,
        pocketjs_core_load_font_atlas(core_from_context(ctx), bytes, len)
    );
}

static JSValue js_ui_upload_texture(
    JSContext *ctx,
    JSValueConst this_value,
    int argc,
    JSValueConst *argv
)
{
    (void)this_value;
    if (argc < 4) {
        return JS_ThrowTypeError(
            ctx,
            "uploadTexture requires pixels, width, height, and psm"
        );
    }
    size_t len = 0;
    const uint8_t *bytes = JS_GetUint8Array(ctx, &len, argv[0]);
    if (bytes == NULL) {
        return JS_EXCEPTION;
    }
    return JS_NewInt32(
        ctx,
        pocketjs_core_upload_texture(
            core_from_context(ctx),
            bytes,
            len,
            js_arg_u32(ctx, argc, argv, 1),
            js_arg_u32(ctx, argc, argv, 2),
            js_arg_u32(ctx, argc, argv, 3)
        )
    );
}

static JSValue js_ui_set_image(
    JSContext *ctx,
    JSValueConst this_value,
    int argc,
    JSValueConst *argv
)
{
    (void)this_value;
    pocketjs_core_set_image(
        core_from_context(ctx),
        js_arg_i32(ctx, argc, argv, 0),
        js_arg_i32(ctx, argc, argv, 1)
    );
    return JS_UNDEFINED;
}

static JSValue js_ui_set_sprite(
    JSContext *ctx,
    JSValueConst this_value,
    int argc,
    JSValueConst *argv
)
{
    (void)this_value;
    pocketjs_core_set_sprite(
        core_from_context(ctx),
        js_arg_i32(ctx, argc, argv, 0),
        js_arg_i32(ctx, argc, argv, 1),
        js_arg_u32(ctx, argc, argv, 2),
        js_arg_u32(ctx, argc, argv, 3),
        js_arg_u32(ctx, argc, argv, 4)
    );
    return JS_UNDEFINED;
}

static JSValue js_ui_measure_text(
    JSContext *ctx,
    JSValueConst this_value,
    int argc,
    JSValueConst *argv
)
{
    (void)this_value;
    if (argc < 1) {
        return JS_NewFloat64(ctx, 0.0);
    }

    size_t len = 0;
    const char *text = JS_ToCStringLen(ctx, &len, argv[0]);
    if (text == NULL) {
        return JS_EXCEPTION;
    }
    const float width = pocketjs_core_measure_text(
        core_from_context(ctx),
        (const uint8_t *)text,
        len,
        js_arg_u32(ctx, argc, argv, 1)
    );
    JS_FreeCString(ctx, text);
    return JS_NewFloat64(ctx, width);
}

static void js_set_function(
    JSContext *ctx,
    JSValue object,
    const char *name,
    JSCFunction *function,
    int arity
)
{
    JS_SetPropertyStr(ctx, object, name, JS_NewCFunction(ctx, function, name, arity));
}

static esp_err_t pocketjs_install_ui(
    pocketjs_js_runtime_t *runtime,
    const pocketjs_app_t *app,
    const pocketjs_package_view_t *package
)
{
    JSContext *ctx = runtime->ctx;
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue ui = JS_NewObject(ctx);
    if (JS_IsException(global) || JS_IsException(ui)) {
        JS_FreeValue(ctx, ui);
        JS_FreeValue(ctx, global);
        return ESP_ERR_NO_MEM;
    }

    js_set_function(ctx, ui, "createNode", js_ui_create_node, 1);
    js_set_function(ctx, ui, "destroyNode", js_ui_destroy_node, 1);
    js_set_function(ctx, ui, "insertBefore", js_ui_insert_before, 3);
    js_set_function(ctx, ui, "removeChild", js_ui_remove_child, 2);
    js_set_function(ctx, ui, "setStyle", js_ui_set_style, 2);
    js_set_function(ctx, ui, "setProp", js_ui_set_prop, 3);
    js_set_function(ctx, ui, "setText", js_ui_set_text, 2);
    js_set_function(ctx, ui, "replaceText", js_ui_replace_text, 2);
    js_set_function(ctx, ui, "animate", js_ui_animate, 6);
    js_set_function(ctx, ui, "cancelAnim", js_ui_cancel_animation, 1);
    js_set_function(ctx, ui, "setFocus", js_ui_set_focus, 1);
    js_set_function(ctx, ui, "setActive", js_ui_set_active, 2);
    js_set_function(ctx, ui, "hitTest", js_ui_hit_test, 2);
    js_set_function(ctx, ui, "loadStyles", js_ui_load_styles, 1);
    js_set_function(ctx, ui, "loadFontAtlas", js_ui_load_font_atlas, 1);
    js_set_function(ctx, ui, "uploadTexture", js_ui_upload_texture, 4);
    js_set_function(ctx, ui, "setImage", js_ui_set_image, 2);
    js_set_function(ctx, ui, "setSprite", js_ui_set_sprite, 5);
    js_set_function(ctx, ui, "measureText", js_ui_measure_text, 2);

    JSValue viewport = JS_NewObject(ctx);
    JS_SetPropertyStr(
        ctx,
        viewport,
        "w",
        JS_NewUint32(ctx, app->logical_width)
    );
    JS_SetPropertyStr(
        ctx,
        viewport,
        "h",
        JS_NewUint32(ctx, app->logical_height)
    );
    JS_SetPropertyStr(
        ctx,
        viewport,
        "width",
        JS_NewUint32(ctx, app->logical_width)
    );
    JS_SetPropertyStr(
        ctx,
        viewport,
        "height",
        JS_NewUint32(ctx, app->logical_height)
    );
    JS_SetPropertyStr(ctx, ui, "__viewport", viewport);
    JS_SetPropertyStr(ctx, ui, "__platform", JS_NewString(ctx, "esp-idf"));
    JS_SetPropertyStr(
        ctx,
        ui,
        "__host",
        JS_NewString(ctx, POCKETJS_TARGET_ID)
    );
    JS_SetPropertyStr(
        ctx,
        ui,
        "__hostAbi",
        JS_NewUint32(ctx, POCKETJS_HOST_ABI)
    );
    if (JS_HasException(ctx)) {
        JS_FreeValue(ctx, ui);
        JS_FreeValue(ctx, global);
        return ESP_FAIL;
    }

    esp_err_t result = pocketjs_package_install_assets(
        runtime->core,
        ctx,
        ui,
        package
    );
    if (result != ESP_OK) {
        JS_FreeValue(ctx, ui);
        JS_FreeValue(ctx, global);
        return result;
    }

    JSValue pak = JS_NewArrayBuffer(
        ctx,
        (uint8_t *)package->pak,
        package->pak_size,
        NULL,
        NULL,
        false
    );
    if (JS_IsException(pak)) {
        JS_FreeValue(ctx, ui);
        JS_FreeValue(ctx, global);
        return ESP_ERR_NO_MEM;
    }
    const int pak_result = JS_SetPropertyStr(
        ctx,
        global,
        "__pak",
        pak
    );
    const int ui_result = JS_SetPropertyStr(
        ctx,
        global,
        "ui",
        ui
    );
    if (pak_result < 0 || ui_result < 0) {
        JS_FreeValue(ctx, global);
        return ESP_FAIL;
    }
    JS_FreeValue(ctx, global);
    return ESP_OK;
}

static void pocketjs_promise_rejection_tracker(
    JSContext *ctx,
    JSValueConst promise,
    JSValueConst reason,
    bool is_handled,
    void *opaque
)
{
    (void)promise;
    pocketjs_js_runtime_t *runtime = opaque;
    if (is_handled) {
        if (runtime->unhandled_rejections > 0) {
            runtime->unhandled_rejections--;
        }
        return;
    }

    runtime->unhandled_rejections++;
    const char *reason_text = JS_ToCString(ctx, reason);
    ESP_LOGE(
        TAG,
        "Unhandled Promise rejection: %s",
        reason_text != NULL ? reason_text : "<unprintable>"
    );
    if (reason_text != NULL) {
        JS_FreeCString(ctx, reason_text);
    }
}

static esp_err_t pocketjs_execute_pending_jobs(pocketjs_js_runtime_t *runtime)
{
    JSContext *job_context = NULL;
    int result = 0;
    while ((result = JS_ExecutePendingJob(runtime->rt, &job_context)) > 0) {
    }
    if (result < 0) {
        ESP_LOGE(TAG, "A pending JavaScript job failed");
        if (job_context != NULL) {
            js_std_dump_error(job_context);
        }
        return ESP_FAIL;
    }
    const bool rejected = runtime->unhandled_rejections != 0;
    runtime->unhandled_rejections = 0;
    return rejected ? ESP_FAIL : ESP_OK;
}

static esp_err_t finish_extension_callback(
    pocketjs_extension_context_t *extension,
    esp_err_t callback_result,
    const char *phase,
    size_t index
)
{
    extension->active = false;
    pocketjs_js_runtime_t *runtime = extension->runtime;
    if (JS_HasException(runtime->ctx)) {
        ESP_LOGE(
            TAG,
            "Extension %u left an exception during %s",
            (unsigned)index,
            phase
        );
        js_std_dump_error(runtime->ctx);
        return ESP_FAIL;
    }
    if (callback_result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Extension %u failed during %s: %s",
            (unsigned)index,
            phase,
            esp_err_to_name(callback_result)
        );
    }
    return callback_result;
}

esp_err_t pocketjs_js_runtime_create(
    pocketjs_t *owner,
    pocketjs_core_t *core,
    const pocketjs_app_t *app,
    const pocketjs_config_t *config,
    const pocketjs_package_view_t *package,
    pocketjs_js_runtime_t **out_runtime
)
{
    if (
        owner == NULL ||
        core == NULL ||
        app == NULL ||
        config == NULL ||
        package == NULL ||
        out_runtime == NULL ||
        config->javascript_heap_limit == 0 ||
        config->javascript_stack_limit == 0 ||
        (config->extension_count != 0 && config->extensions == NULL)
    ) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_runtime = NULL;

    pocketjs_js_runtime_t *runtime = calloc(1, sizeof(*runtime));
    if (runtime == NULL) {
        return ESP_ERR_NO_MEM;
    }
    runtime->frame_function = JS_UNDEFINED;
    runtime->owner = owner;
    runtime->core = core;
    runtime->extension_count = config->extension_count;

    if (runtime->extension_count != 0) {
        runtime->extensions = calloc(
            runtime->extension_count,
            sizeof(*runtime->extensions)
        );
        if (runtime->extensions == NULL) {
            pocketjs_js_runtime_destroy(runtime);
            return ESP_ERR_NO_MEM;
        }
        const pocketjs_extension_t *extensions =
            (const pocketjs_extension_t *)config->extensions;
        for (size_t index = 0; index < runtime->extension_count; ++index) {
            runtime->extensions[index].runtime = runtime;
            runtime->extensions[index].extension = extensions[index];
        }
    }

    runtime->rt = JS_NewRuntime2(&POCKETJS_QJS_ALLOCATOR, NULL);
    if (runtime->rt == NULL) {
        pocketjs_js_runtime_destroy(runtime);
        return ESP_ERR_NO_MEM;
    }
    JS_SetMemoryLimit(runtime->rt, config->javascript_heap_limit);
    JS_SetMaxStackSize(runtime->rt, config->javascript_stack_limit);
    JS_SetRuntimeInfo(runtime->rt, "PocketJS ESP-IDF");
    JS_SetHostPromiseRejectionTracker(
        runtime->rt,
        pocketjs_promise_rejection_tracker,
        runtime
    );
    js_std_init_handlers(runtime->rt);

    runtime->ctx = JS_NewContext(runtime->rt);
    if (runtime->ctx == NULL) {
        pocketjs_js_runtime_destroy(runtime);
        return ESP_ERR_NO_MEM;
    }
    JS_SetContextOpaque(runtime->ctx, runtime);
    js_std_add_helpers(runtime->ctx, 0, NULL);

    esp_err_t result = pocketjs_package_validate_plan(
        runtime->ctx,
        app,
        package
    );
    if (result == ESP_OK) {
        result = pocketjs_install_ui(runtime, app, package);
    }
    if (result != ESP_OK) {
        pocketjs_js_runtime_destroy(runtime);
        return result;
    }

    for (size_t index = 0; index < runtime->extension_count; ++index) {
        pocketjs_extension_context_t *extension =
            &runtime->extensions[index];
        if (extension->extension.install != NULL) {
            extension->active = true;
            result = finish_extension_callback(
                extension,
                extension->extension.install(extension),
                "install",
                index
            );
            if (result != ESP_OK) {
                pocketjs_js_runtime_destroy(runtime);
                return result;
            }
        }
        runtime->installed_extension_count = index + 1U;
    }

    JSValue evaluation = JS_Eval(
        runtime->ctx,
        (const char *)package->javascript,
        package->javascript_size - 1U,
        "<pocket-app>",
        JS_EVAL_TYPE_GLOBAL
    );
    if (JS_IsException(evaluation)) {
        ESP_LOGE(TAG, "JavaScript evaluation failed");
        js_std_dump_error(runtime->ctx);
        JS_FreeValue(runtime->ctx, evaluation);
        pocketjs_js_runtime_destroy(runtime);
        return ESP_FAIL;
    }
    JS_FreeValue(runtime->ctx, evaluation);

    JSValue global = JS_GetGlobalObject(runtime->ctx);
    runtime->frame_function = JS_GetPropertyStr(
        runtime->ctx,
        global,
        "frame"
    );
    JS_FreeValue(runtime->ctx, global);
    if (!JS_IsFunction(runtime->ctx, runtime->frame_function)) {
        ESP_LOGE(TAG, "Application did not install globalThis.frame");
        pocketjs_js_runtime_destroy(runtime);
        return ESP_ERR_NOT_FOUND;
    }

    result = pocketjs_execute_pending_jobs(runtime);
    if (result != ESP_OK) {
        pocketjs_js_runtime_destroy(runtime);
        return result;
    }

    ESP_LOGI(
        TAG,
        "Runtime ready: target=%s ABI=%" PRIu32
        ", viewport=%" PRIu32 "x%" PRIu32
        ", JS heap limit=%u KiB",
        app->target_id,
        app->host_abi,
        app->logical_width,
        app->logical_height,
        (unsigned)(config->javascript_heap_limit / 1024U)
    );
    *out_runtime = runtime;
    return ESP_OK;
}

static uint32_t pack_analog(const pocketjs_input_t *input)
{
    const int32_t x = input != NULL ? input->analog_x : 0;
    const int32_t y = input != NULL ? input->analog_y : 0;
    const uint32_t packed_x = (uint32_t)(x + 32896) / 257U;
    const uint32_t packed_y = (uint32_t)(y + 32896) / 257U;
    return (packed_x << 8U) | packed_y;
}

esp_err_t pocketjs_js_runtime_run_frame(
    pocketjs_js_runtime_t *runtime,
    const pocketjs_input_t *input
)
{
    if (runtime == NULL || runtime->ctx == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!JS_IsFunction(runtime->ctx, runtime->frame_function)) {
        return ESP_ERR_INVALID_STATE;
    }

    const pocketjs_input_t empty = {0};
    const pocketjs_input_t *frame_input = input != NULL ? input : &empty;
    runtime->unhandled_rejections = 0;
    for (size_t index = 0; index < runtime->extension_count; ++index) {
        pocketjs_extension_context_t *extension =
            &runtime->extensions[index];
        if (extension->extension.before_frame != NULL) {
            extension->active = true;
            const esp_err_t result = finish_extension_callback(
                extension,
                extension->extension.before_frame(extension, frame_input),
                "before_frame",
                index
            );
            if (result != ESP_OK) {
                runtime->unhandled_rejections = 0;
                return result;
            }
        }
    }

    JSValue arguments[2] = {
        JS_NewUint32(runtime->ctx, frame_input->buttons),
        JS_NewUint32(runtime->ctx, pack_analog(frame_input)),
    };
    JSValue result = JS_Call(
        runtime->ctx,
        runtime->frame_function,
        JS_UNDEFINED,
        2,
        arguments
    );
    JS_FreeValue(runtime->ctx, arguments[1]);
    JS_FreeValue(runtime->ctx, arguments[0]);
    if (JS_IsException(result)) {
        ESP_LOGE(
            TAG,
            "JavaScript frame failed (buttons=0x%08" PRIx32 ")",
            frame_input->buttons
        );
        js_std_dump_error(runtime->ctx);
        JS_FreeValue(runtime->ctx, result);
        runtime->unhandled_rejections = 0;
        return ESP_FAIL;
    }
    JS_FreeValue(runtime->ctx, result);
    return pocketjs_execute_pending_jobs(runtime);
}

void pocketjs_js_runtime_destroy(pocketjs_js_runtime_t *runtime)
{
    if (runtime == NULL) {
        return;
    }
    if (runtime->ctx != NULL) {
        for (size_t index = runtime->installed_extension_count;
             index > 0;
             --index) {
            pocketjs_extension_context_t *extension =
                &runtime->extensions[index - 1U];
            if (extension->extension.destroy != NULL) {
                extension->active = true;
                extension->extension.destroy(extension);
                (void)finish_extension_callback(
                    extension,
                    ESP_OK,
                    "destroy",
                    index - 1U
                );
            }
        }
        JS_FreeValue(runtime->ctx, runtime->frame_function);
    }
    free(runtime->extensions);
    if (runtime->rt != NULL) {
        js_std_free_handlers(runtime->rt);
    }
    if (runtime->ctx != NULL) {
        JS_FreeContext(runtime->ctx);
    }
    if (runtime->rt != NULL) {
        JS_FreeRuntime(runtime->rt);
    }
    free(runtime);
}

JSContext *pocketjs_extension_get_js_context(
    pocketjs_extension_context_t *context
)
{
    return context != NULL && context->active && context->runtime != NULL
        ? context->runtime->ctx
        : NULL;
}

pocketjs_t *pocketjs_extension_get_runtime(
    pocketjs_extension_context_t *context
)
{
    return context != NULL && context->active && context->runtime != NULL
        ? context->runtime->owner
        : NULL;
}

void *pocketjs_extension_get_user_data(
    pocketjs_extension_context_t *context
)
{
    return context != NULL && context->active
        ? context->extension.user_data
        : NULL;
}
