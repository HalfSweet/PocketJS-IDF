#pragma once

#include "pocketjs.h"
#include "quickjs.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pocketjs_extension_context pocketjs_extension_context_t;

typedef esp_err_t (*pocketjs_extension_install_cb_t)(
    pocketjs_extension_context_t *context
);
typedef esp_err_t (*pocketjs_extension_before_frame_cb_t)(
    pocketjs_extension_context_t *context,
    const pocketjs_input_t *input
);
typedef void (*pocketjs_extension_destroy_cb_t)(
    pocketjs_extension_context_t *context
);

typedef struct pocketjs_extension {
    pocketjs_extension_install_cb_t install;
    pocketjs_extension_before_frame_cb_t before_frame;
    pocketjs_extension_destroy_cb_t destroy;
    void *user_data;
} pocketjs_extension_t;

/**
 * Advanced, version-pinned escape hatch. The returned context is owned by
 * PocketJS-IDF and is available only while an extension callback is running.
 * Extension code that leaves a QuickJS exception pending must return an
 * error; PocketJS-IDF logs and clears it so later frames remain runnable.
 */
JSContext *pocketjs_extension_get_js_context(
    pocketjs_extension_context_t *context
);

pocketjs_t *pocketjs_extension_get_runtime(
    pocketjs_extension_context_t *context
);

void *pocketjs_extension_get_user_data(pocketjs_extension_context_t *context);

#ifdef __cplusplus
}
#endif
