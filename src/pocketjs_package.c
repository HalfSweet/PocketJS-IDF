#include "pocketjs_package_internal.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"

#define POCKET_MAGIC 0x544b4350U
#define POCKET_VERSION 1U
#define POCKET_HEADER_SIZE 16U
#define POCKET_VARIANT_SIZE 40U
#define POCKET_SECTION_SIZE 16U
#define POCKET_TARGET_BYTES 16U
#define POCKET_ALIGN 16U

#define POCKET_SECTION_IDENTITY 1U
#define POCKET_SECTION_PLAN 2U
#define POCKET_SECTION_JAVASCRIPT 3U
#define POCKET_SECTION_PAK 4U

#define PAK_MAGIC 0x4b504344U
#define PAK_VERSION 1U
#define PAK_HEADER_SIZE 32U
#define PAK_ENTRY_SIZE 24U

static const char *TAG = "pocketjs_package";

static bool range_valid(size_t offset, size_t length, size_t total)
{
    return offset <= total && length <= total - offset;
}

static bool read_u16(
    const uint8_t *bytes,
    size_t size,
    size_t offset,
    uint16_t *value
)
{
    if (!range_valid(offset, sizeof(uint16_t), size)) {
        return false;
    }
    *value = (uint16_t)bytes[offset] |
        ((uint16_t)bytes[offset + 1U] << 8U);
    return true;
}

static bool read_u32(
    const uint8_t *bytes,
    size_t size,
    size_t offset,
    uint32_t *value
)
{
    if (!range_valid(offset, sizeof(uint32_t), size)) {
        return false;
    }
    *value = (uint32_t)bytes[offset] |
        ((uint32_t)bytes[offset + 1U] << 8U) |
        ((uint32_t)bytes[offset + 2U] << 16U) |
        ((uint32_t)bytes[offset + 3U] << 24U);
    return true;
}

static bool read_u64(
    const uint8_t *bytes,
    size_t size,
    size_t offset,
    uint64_t *value
)
{
    uint32_t low = 0;
    uint32_t high = 0;
    if (
        !read_u32(bytes, size, offset, &low) ||
        !read_u32(bytes, size, offset + 4U, &high)
    ) {
        return false;
    }
    *value = (uint64_t)low | ((uint64_t)high << 32U);
    return true;
}

static uint64_t fnv1a64_update(
    uint64_t hash,
    const uint8_t *bytes,
    size_t size
)
{
    for (size_t index = 0; index < size; ++index) {
        hash ^= bytes[index];
        hash *= UINT64_C(0x100000001b3);
    }
    return hash;
}

static uint64_t fnv1a64(const uint8_t *bytes, size_t size)
{
    return fnv1a64_update(UINT64_C(0xcbf29ce484222325), bytes, size);
}

static size_t align16(size_t value)
{
    if (value > SIZE_MAX - (POCKET_ALIGN - 1U)) {
        return SIZE_MAX;
    }
    return (value + POCKET_ALIGN - 1U) & ~(size_t)(POCKET_ALIGN - 1U);
}

esp_err_t pocketjs_package_open(
    const pocketjs_app_t *app,
    pocketjs_package_view_t *out_view
)
{
    if (
        app == NULL ||
        out_view == NULL ||
        app->package_data == NULL ||
        app->package_size < POCKET_HEADER_SIZE + sizeof(uint64_t) ||
        app->target_id == NULL
    ) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_view = (pocketjs_package_view_t) {0};

    const uint8_t *bytes = app->package_data;
    const size_t size = app->package_size;
    uint32_t magic = 0;
    uint32_t version = 0;
    uint32_t manifest_size = 0;
    uint32_t variant_count = 0;
    if (
        !read_u32(bytes, size, 0, &magic) ||
        !read_u32(bytes, size, 4, &version) ||
        !read_u32(bytes, size, 8, &manifest_size) ||
        !read_u32(bytes, size, 12, &variant_count) ||
        magic != POCKET_MAGIC ||
        version != POCKET_VERSION ||
        variant_count == 0
    ) {
        return ESP_ERR_INVALID_VERSION;
    }

    uint64_t stored_hash = 0;
    if (
        !read_u64(bytes, size, size - sizeof(uint64_t), &stored_hash) ||
        stored_hash != fnv1a64(bytes, size - sizeof(uint64_t))
    ) {
        ESP_LOGE(TAG, ".pocket footer hash mismatch");
        return ESP_ERR_INVALID_CRC;
    }

    if (!range_valid(POCKET_HEADER_SIZE, manifest_size, size)) {
        return ESP_ERR_INVALID_SIZE;
    }
    const size_t variant_table =
        align16(POCKET_HEADER_SIZE + (size_t)manifest_size);
    if (
        variant_table == SIZE_MAX ||
        variant_count > SIZE_MAX / POCKET_VARIANT_SIZE ||
        !range_valid(
            variant_table,
            (size_t)variant_count * POCKET_VARIANT_SIZE,
            size
        )
    ) {
        return ESP_ERR_INVALID_SIZE;
    }

    out_view->manifest = bytes + POCKET_HEADER_SIZE;
    out_view->manifest_size = manifest_size;

    bool found = false;
    for (uint32_t index = 0; index < variant_count; ++index) {
        const size_t entry =
            variant_table + (size_t)index * POCKET_VARIANT_SIZE;
        size_t target_length = 0;
        while (
            target_length < POCKET_TARGET_BYTES &&
            bytes[entry + target_length] != 0
        ) {
            ++target_length;
        }
        if (
            target_length == POCKET_TARGET_BYTES ||
            strlen(app->target_id) != target_length ||
            memcmp(bytes + entry, app->target_id, target_length) != 0
        ) {
            continue;
        }

        uint32_t host_abi = 0;
        uint32_t section_count = 0;
        uint32_t sections_offset = 0;
        uint64_t variant_hash = 0;
        if (
            !read_u32(bytes, size, entry + 16U, &host_abi) ||
            !read_u32(bytes, size, entry + 20U, &section_count) ||
            !read_u32(bytes, size, entry + 24U, &sections_offset) ||
            !read_u64(bytes, size, entry + 32U, &variant_hash) ||
            host_abi != app->host_abi ||
            section_count == 0 ||
            section_count > SIZE_MAX / POCKET_SECTION_SIZE ||
            !range_valid(
                sections_offset,
                (size_t)section_count * POCKET_SECTION_SIZE,
                size
            )
        ) {
            return ESP_ERR_INVALID_RESPONSE;
        }

        uint64_t computed_variant_hash =
            UINT64_C(0xcbf29ce484222325);
        uint32_t previous_kind = 0;
        for (uint32_t section = 0; section < section_count; ++section) {
            const size_t section_entry =
                sections_offset + (size_t)section * POCKET_SECTION_SIZE;
            uint32_t kind = 0;
            uint32_t offset = 0;
            uint32_t length = 0;
            if (
                !read_u32(bytes, size, section_entry, &kind) ||
                !read_u32(bytes, size, section_entry + 8U, &offset) ||
                !read_u32(bytes, size, section_entry + 12U, &length) ||
                kind == 0 ||
                (section != 0 && kind <= previous_kind) ||
                !range_valid(offset, length, size - sizeof(uint64_t))
            ) {
                return ESP_ERR_INVALID_RESPONSE;
            }
            previous_kind = kind;

            const uint8_t *payload = bytes + offset;
            computed_variant_hash = fnv1a64_update(
                computed_variant_hash,
                payload,
                length
            );
            switch (kind) {
            case POCKET_SECTION_IDENTITY:
                out_view->identity = payload;
                out_view->identity_size = length;
                break;
            case POCKET_SECTION_PLAN:
                out_view->plan = payload;
                out_view->plan_size = length;
                break;
            case POCKET_SECTION_JAVASCRIPT:
                out_view->javascript = payload;
                out_view->javascript_size = length;
                break;
            case POCKET_SECTION_PAK:
                out_view->pak = payload;
                out_view->pak_size = length;
                break;
            default:
                break;
            }
        }
        if (computed_variant_hash != variant_hash) {
            ESP_LOGE(TAG, ".pocket variant hash mismatch");
            return ESP_ERR_INVALID_CRC;
        }
        found = true;
        break;
    }

    if (!found) {
        return ESP_ERR_NOT_FOUND;
    }
    if (
        out_view->plan == NULL ||
        out_view->plan_size == 0 ||
        out_view->javascript == NULL ||
        out_view->javascript_size < 2 ||
        out_view->javascript[out_view->javascript_size - 1U] != 0 ||
        out_view->pak == NULL ||
        out_view->pak_size == 0
    ) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}

static bool js_read_u32(
    JSContext *context,
    JSValueConst object,
    const char *name,
    uint32_t *out
)
{
    JSValue value = JS_GetPropertyStr(context, object, name);
    const bool ok =
        !JS_IsException(value) &&
        JS_ToUint32(context, out, value) == 0;
    JS_FreeValue(context, value);
    return ok;
}

esp_err_t pocketjs_package_validate_plan(
    JSContext *context,
    const pocketjs_app_t *app,
    const pocketjs_package_view_t *view
)
{
    if (context == NULL || app == NULL || view == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (view->plan_size == SIZE_MAX) {
        return ESP_ERR_INVALID_SIZE;
    }
    char *json = malloc(view->plan_size + 1U);
    if (json == NULL) {
        return ESP_ERR_NO_MEM;
    }
    memcpy(json, view->plan, view->plan_size);
    json[view->plan_size] = '\0';
    JSValue plan = JS_ParseJSON(
        context,
        json,
        view->plan_size,
        "<pocket-plan>"
    );
    free(json);
    if (JS_IsException(plan)) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    esp_err_t result = ESP_ERR_INVALID_RESPONSE;
    JSValue target = JS_GetPropertyStr(context, plan, "target");
    JSValue target_id = JS_GetPropertyStr(context, target, "id");
    const char *target_text = JS_ToCString(context, target_id);
    uint32_t host_abi = 0;
    if (
        target_text == NULL ||
        strcmp(target_text, app->target_id) != 0 ||
        !js_read_u32(context, target, "hostAbi", &host_abi) ||
        host_abi != app->host_abi
    ) {
        goto cleanup_target;
    }

    JSValue viewport = JS_GetPropertyStr(context, plan, "viewport");
    JSValue logical = JS_GetPropertyStr(context, viewport, "logical");
    JSValue logical_width = JS_GetPropertyUint32(context, logical, 0);
    JSValue logical_height = JS_GetPropertyUint32(context, logical, 1);
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t density = 0;
    if (
        JS_ToUint32(context, &width, logical_width) == 0 &&
        JS_ToUint32(context, &height, logical_height) == 0 &&
        js_read_u32(context, viewport, "rasterDensity", &density) &&
        width == app->logical_width &&
        height == app->logical_height &&
        density == app->raster_density
    ) {
        result = ESP_OK;
    }
    JS_FreeValue(context, logical_height);
    JS_FreeValue(context, logical_width);
    JS_FreeValue(context, logical);
    JS_FreeValue(context, viewport);

cleanup_target:
    if (target_text != NULL) {
        JS_FreeCString(context, target_text);
    }
    JS_FreeValue(context, target_id);
    JS_FreeValue(context, target);
    JS_FreeValue(context, plan);
    return result;
}

static esp_err_t set_property_bytes(
    JSContext *context,
    JSValue object,
    const uint8_t *name,
    size_t name_size,
    JSValue value
)
{
    JSAtom atom = JS_NewAtomLen(context, (const char *)name, name_size);
    if (atom == JS_ATOM_NULL) {
        JS_FreeValue(context, value);
        return ESP_ERR_NO_MEM;
    }
    const int result = JS_SetProperty(context, object, atom, value);
    JS_FreeAtom(context, atom);
    return result < 0 ? ESP_FAIL : ESP_OK;
}

esp_err_t pocketjs_package_install_assets(
    pocketjs_core_t *core,
    JSContext *context,
    JSValue ui,
    const pocketjs_package_view_t *view
)
{
    if (core == NULL || context == NULL || view == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    const uint8_t *pak = view->pak;
    const size_t pak_size = view->pak_size;
    uint32_t magic = 0;
    uint16_t version = 0;
    uint32_t count = 0;
    uint32_t directory = 0;
    uint32_t names = 0;
    uint32_t declared_size = 0;
    if (
        pak_size < PAK_HEADER_SIZE ||
        !read_u32(pak, pak_size, 0, &magic) ||
        !read_u16(pak, pak_size, 4, &version) ||
        !read_u32(pak, pak_size, 8, &count) ||
        !read_u32(pak, pak_size, 12, &directory) ||
        !read_u32(pak, pak_size, 16, &names) ||
        !read_u32(pak, pak_size, 24, &declared_size) ||
        magic != PAK_MAGIC ||
        version != PAK_VERSION ||
        declared_size != pak_size ||
        count > SIZE_MAX / PAK_ENTRY_SIZE ||
        !range_valid(directory, (size_t)count * PAK_ENTRY_SIZE, pak_size)
    ) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    JSValue textures = JS_NewObject(context);
    JSValue sprites = JS_NewObject(context);
    if (JS_IsException(textures) || JS_IsException(sprites)) {
        JS_FreeValue(context, sprites);
        JS_FreeValue(context, textures);
        return ESP_ERR_NO_MEM;
    }

    esp_err_t result = ESP_OK;
    for (uint32_t index = 0; index < count && result == ESP_OK; ++index) {
        const size_t entry = directory + (size_t)index * PAK_ENTRY_SIZE;
        uint32_t blob_offset = 0;
        uint32_t blob_size = 0;
        uint32_t name_offset = 0;
        uint16_t name_size = 0;
        if (
            !read_u32(pak, pak_size, entry + 4U, &blob_offset) ||
            !read_u32(pak, pak_size, entry + 8U, &blob_size) ||
            !read_u32(pak, pak_size, entry + 12U, &name_offset) ||
            !read_u16(pak, pak_size, entry + 16U, &name_size) ||
            !range_valid(blob_offset, blob_size, pak_size) ||
            !range_valid((size_t)names + name_offset, name_size, pak_size)
        ) {
            result = ESP_ERR_INVALID_RESPONSE;
            break;
        }
        const uint8_t *name = pak + names + name_offset;
        const uint8_t *blob = pak + blob_offset;

        if (name_size == 9U && memcmp(name, "ui:styles", 9U) == 0) {
            if (!pocketjs_core_load_styles(core, blob, blob_size)) {
                result = ESP_ERR_INVALID_RESPONSE;
            }
        } else if (
            name_size > 8U &&
            memcmp(name, "ui:font.", 8U) == 0
        ) {
            if (!pocketjs_core_load_font_atlas(core, blob, blob_size)) {
                result = ESP_ERR_INVALID_RESPONSE;
            }
        } else if (
            name_size > 7U &&
            memcmp(name, "ui:img.", 7U) == 0
        ) {
            uint16_t width = 0;
            uint16_t height = 0;
            if (
                blob_size < 8U ||
                !read_u16(blob, blob_size, 0, &width) ||
                !read_u16(blob, blob_size, 2, &height)
            ) {
                result = ESP_ERR_INVALID_RESPONSE;
                break;
            }
            const int32_t handle = pocketjs_core_upload_texture(
                core,
                blob + 8U,
                blob_size - 8U,
                width,
                height,
                blob[4]
            );
            if (handle < 0) {
                result = ESP_ERR_INVALID_RESPONSE;
                break;
            }
            result = set_property_bytes(
                context,
                textures,
                name + 7U,
                name_size - 7U,
                JS_NewInt32(context, handle)
            );
        } else if (
            name_size > 10U &&
            memcmp(name, "ui:sprite.", 10U) == 0
        ) {
            uint16_t width = 0;
            uint16_t height = 0;
            uint16_t frames = 0;
            uint16_t columns = 0;
            uint16_t step = 0;
            if (
                blob_size < 16U ||
                !read_u16(blob, blob_size, 0, &width) ||
                !read_u16(blob, blob_size, 2, &height) ||
                !read_u16(blob, blob_size, 6, &frames) ||
                !read_u16(blob, blob_size, 8, &columns) ||
                !read_u16(blob, blob_size, 10, &step)
            ) {
                result = ESP_ERR_INVALID_RESPONSE;
                break;
            }
            const int32_t handle = pocketjs_core_upload_texture(
                core,
                blob + 16U,
                blob_size - 16U,
                width,
                height,
                blob[4]
            );
            if (handle < 0) {
                result = ESP_ERR_INVALID_RESPONSE;
                break;
            }
            JSValue metadata = JS_NewObject(context);
            if (JS_IsException(metadata)) {
                result = ESP_ERR_NO_MEM;
                break;
            }
            const int handle_result = JS_SetPropertyStr(
                context,
                metadata,
                "handle",
                JS_NewInt32(context, handle)
            );
            const int frames_result = JS_SetPropertyStr(
                context,
                metadata,
                "frames",
                JS_NewUint32(context, frames)
            );
            const int columns_result = JS_SetPropertyStr(
                context,
                metadata,
                "cols",
                JS_NewUint32(context, columns)
            );
            const int step_result = JS_SetPropertyStr(
                context,
                metadata,
                "step",
                JS_NewUint32(context, step)
            );
            if (
                handle_result < 0 ||
                frames_result < 0 ||
                columns_result < 0 ||
                step_result < 0
            ) {
                JS_FreeValue(context, metadata);
                result = ESP_FAIL;
            } else {
                result = set_property_bytes(
                    context,
                    sprites,
                    name + 10U,
                    name_size - 10U,
                    metadata
                );
            }
        }
    }

    if (result == ESP_OK) {
        const int textures_result = JS_SetPropertyStr(
            context,
            ui,
            "__textures",
            textures
        );
        const int sprites_result = JS_SetPropertyStr(
            context,
            ui,
            "__sprites",
            sprites
        );
        if (textures_result < 0 || sprites_result < 0) {
            result = ESP_FAIL;
        }
    } else {
        JS_FreeValue(context, sprites);
        JS_FreeValue(context, textures);
    }
    return result;
}
