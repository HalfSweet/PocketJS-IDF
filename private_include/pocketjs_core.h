#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define POCKETJS_MAX_DAMAGE_REGIONS 8U

typedef struct pocketjs_core pocketjs_core_t;

enum {
    POCKETJS_ROOT_ID = 1,
};

enum pocketjs_node_type {
    POCKETJS_NODE_VIEW = 0,
    POCKETJS_NODE_TEXT = 1,
    POCKETJS_NODE_IMAGE = 2,
};

enum pocketjs_prop {
    POCKETJS_PROP_WIDTH = 1,
    POCKETJS_PROP_HEIGHT = 2,
    POCKETJS_PROP_PADDING_TOP = 8,
    POCKETJS_PROP_PADDING_RIGHT = 9,
    POCKETJS_PROP_PADDING_BOTTOM = 10,
    POCKETJS_PROP_PADDING_LEFT = 11,
    POCKETJS_PROP_GAP = 16,
    POCKETJS_PROP_FLEX_DIRECTION = 17,
    POCKETJS_PROP_JUSTIFY = 18,
    POCKETJS_PROP_ALIGN = 19,
    POCKETJS_PROP_GROW = 20,
    POCKETJS_PROP_POSITION_TYPE = 24,
    POCKETJS_PROP_INSET_TOP = 25,
    POCKETJS_PROP_INSET_RIGHT = 26,
    POCKETJS_PROP_INSET_BOTTOM = 27,
    POCKETJS_PROP_INSET_LEFT = 28,
    POCKETJS_PROP_BACKGROUND_COLOR = 64,
    POCKETJS_PROP_GRADIENT_FROM = 65,
    POCKETJS_PROP_GRADIENT_TO = 66,
    POCKETJS_PROP_GRADIENT_DIRECTION = 67,
    POCKETJS_PROP_RADIUS = 68,
    POCKETJS_PROP_OPACITY = 69,
    POCKETJS_PROP_BORDER_COLOR = 70,
    POCKETJS_PROP_BORDER_WIDTH = 71,
    POCKETJS_PROP_TEXT_COLOR = 96,
    POCKETJS_PROP_FONT_SLOT = 97,
    POCKETJS_PROP_TEXT_ALIGN = 98,
    POCKETJS_PROP_TRANSLATE_X = 128,
    POCKETJS_PROP_TRANSLATE_Y = 129,
    POCKETJS_PROP_SCALE = 130,
    POCKETJS_PROP_ROTATE = 131,
};

pocketjs_core_t *pocketjs_core_create(
    uint32_t width,
    uint32_t height,
    uint32_t raster_density
);
void pocketjs_core_destroy(pocketjs_core_t *core);

int32_t pocketjs_core_create_node(pocketjs_core_t *core, uint32_t node_type);
void pocketjs_core_destroy_node(pocketjs_core_t *core, int32_t id);
void pocketjs_core_insert_before(
    pocketjs_core_t *core,
    int32_t parent,
    int32_t child,
    int32_t anchor
);
void pocketjs_core_remove_child(
    pocketjs_core_t *core,
    int32_t parent,
    int32_t child
);
void pocketjs_core_set_style(
    pocketjs_core_t *core,
    int32_t id,
    int32_t style_id
);
void pocketjs_core_set_prop(
    pocketjs_core_t *core,
    int32_t id,
    uint32_t prop,
    double value
);
int pocketjs_core_set_text(
    pocketjs_core_t *core,
    int32_t id,
    const uint8_t *text,
    size_t len
);
int pocketjs_core_replace_text(
    pocketjs_core_t *core,
    int32_t id,
    const uint8_t *text,
    size_t len
);
int32_t pocketjs_core_animate(
    pocketjs_core_t *core,
    int32_t id,
    uint32_t prop,
    double to,
    uint32_t duration_ms,
    uint32_t easing,
    uint32_t delay_ms
);
void pocketjs_core_cancel_animation(
    pocketjs_core_t *core,
    int32_t animation_id
);
void pocketjs_core_set_focus(pocketjs_core_t *core, int32_t id);
void pocketjs_core_set_active(pocketjs_core_t *core, int32_t id, int active);
int32_t pocketjs_core_hit_test(pocketjs_core_t *core, float x, float y);

int pocketjs_core_load_styles(
    pocketjs_core_t *core,
    const uint8_t *data,
    size_t len
);
int pocketjs_core_load_font_atlas(
    pocketjs_core_t *core,
    const uint8_t *data,
    size_t len
);
int32_t pocketjs_core_upload_texture(
    pocketjs_core_t *core,
    const uint8_t *data,
    size_t len,
    uint32_t width,
    uint32_t height,
    uint32_t psm
);
void pocketjs_core_set_image(
    pocketjs_core_t *core,
    int32_t id,
    int32_t texture
);
void pocketjs_core_set_sprite(
    pocketjs_core_t *core,
    int32_t id,
    int32_t atlas,
    uint32_t frames,
    uint32_t columns,
    uint32_t step
);
float pocketjs_core_measure_text(
    pocketjs_core_t *core,
    const uint8_t *text,
    size_t len,
    uint32_t font_slot
);

void pocketjs_core_tick(pocketjs_core_t *core);
uint64_t pocketjs_core_draw_hash(pocketjs_core_t *core);
size_t pocketjs_core_draw_word_count(pocketjs_core_t *core);
size_t pocketjs_core_framebuffer_bytes(pocketjs_core_t *core);

typedef struct {
    uint32_t ppa_fills;
    uint32_t ppa_blends;
    uint32_t ppa_srm;
    uint32_t software_ops;
    uint32_t software_words;
    uint32_t damage_regions;
    uint32_t damage_pixels;
    uint32_t damage_x;
    uint32_t damage_y;
    uint32_t damage_width;
    uint32_t damage_height;
    uint32_t full_redraw;
} pocketjs_render_stats_t;

/**
 * One global logical dirty rectangle with half-open pixel bounds
 * [x, x + width) x [y, y + height).
 */
typedef struct {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
} pocketjs_damage_rect_t;

typedef struct {
    uint32_t region_count;
    uint32_t full_redraw;
    pocketjs_damage_rect_t regions[POCKETJS_MAX_DAMAGE_REGIONS];
} pocketjs_damage_plan_t;

/**
 * Render into a tightly packed native RGB565 framebuffer.
 *
 * PPA-compatible fills, A8 coverage blends, and texture transforms are
 * accelerated. Unsupported DrawList operations fall back in order to the
 * RGB565 software rasterizer. The caller-owned buffer must be cache-line
 * aligned when PPA output is enabled.
 */
int pocketjs_core_render_rgb565(
    pocketjs_core_t *core,
    uint16_t *framebuffer,
    size_t len,
    pocketjs_render_stats_t *out_stats
);

/**
 * Incrementally update a persistent RGB565 framebuffer.
 *
 * The first call for each framebuffer address performs a full render.
 * Subsequent calls compare the current DrawList with the history associated
 * with that exact buffer and repaint only changed regions. The caller must
 * preserve buffer contents between calls. Up to two alternating framebuffer
 * addresses are tracked for the Tab5 render pipeline.
 */
int pocketjs_core_render_rgb565_incremental(
    pocketjs_core_t *core,
    uint16_t *framebuffer,
    size_t len,
    pocketjs_render_stats_t *out_stats
);

/**
 * Prepare a transactional dirty-region plan for native framebuffer target 0
 * or 1, or headless target 2. Only one prepared transaction may exist.
 *
 * Preparation does not advance the target history. Finish it with exactly one
 * of commit, cancel, or abort before preparing another target.
 */
int pocketjs_core_prepare_rgb565_frame(
    pocketjs_core_t *core,
    uint32_t target_id,
    pocketjs_damage_plan_t *out_plan,
    pocketjs_render_stats_t *out_stats
);

/**
 * Render one dirty rect into a tightly packed full-viewport-width strip.
 *
 * The strip represents global logical rows [y, y + height), contains exactly
 * viewport_width * height RGB565 pixels, and only columns [x, x + width) are
 * modified. `len` is the strip size in bytes.
 */
int pocketjs_core_render_rgb565_strip(
    pocketjs_core_t *core,
    uint16_t *framebuffer,
    size_t len,
    uint32_t x,
    uint32_t y,
    uint32_t width,
    uint32_t height,
    pocketjs_render_stats_t *out_stats
);

/**
 * Commit a successfully presented pending frame. This fails and invalidates
 * the target if either the target id or current DrawList no longer matches the
 * prepared transaction.
 */
int pocketjs_core_commit_rgb565_frame(
    pocketjs_core_t *core,
    uint32_t target_id
);

/**
 * Drop a prepared target probe without changing its retained damage history.
 */
int pocketjs_core_cancel_rgb565_frame(
    pocketjs_core_t *core,
    uint32_t target_id
);

/**
 * Drop a failed/partial update and invalidate the target for a full redraw on
 * its next prepare.
 */
void pocketjs_core_abort_rgb565_frame(
    pocketjs_core_t *core,
    uint32_t target_id
);

#ifdef __cplusplus
}
#endif
