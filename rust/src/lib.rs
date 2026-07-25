#![no_std]
#![allow(static_mut_refs)]

extern crate alloc;
#[cfg(test)]
extern crate std;

use alloc::vec::Vec;
#[cfg(not(test))]
use core::alloc::{GlobalAlloc, Layout};
use core::slice;

use pocketjs_core::{DrawList, Ui};
use pocketjs_esp32p4_ppa::{
    PpaOps, QuarterTurn, Rect, RenderDamagePlan, RenderStats, RenderTargetState, Renderer,
    RendererConfig, SrmTransform,
};

extern "C" {
    #[cfg(not(test))]
    fn pocketjs_rust_alloc(size: usize, alignment: usize) -> *mut u8;
    #[cfg(not(test))]
    fn pocketjs_rust_dealloc(ptr: *mut u8, size: usize, alignment: usize);
    #[cfg(not(test))]
    fn pocketjs_rust_panic();
    #[cfg(not(test))]
    fn pocketjs_ppa_fill_rgb565_bridge(
        destination: *mut u16,
        destination_pixels: usize,
        width: u32,
        height: u32,
        x: u32,
        y: u32,
        rect_width: u32,
        rect_height: u32,
        color: u16,
    ) -> i32;
    #[cfg(not(test))]
    fn pocketjs_ppa_blend_a8_rgb565_bridge(
        destination: *mut u16,
        destination_pixels: usize,
        width: u32,
        height: u32,
        mask: *const u8,
        mask_len: usize,
        x: u32,
        y: u32,
        rect_width: u32,
        rect_height: u32,
        red: u8,
        green: u8,
        blue: u8,
        global_alpha: u8,
    ) -> i32;
    #[cfg(not(test))]
    fn pocketjs_ppa_srm_psm5650_rgb565_bridge(
        destination: *mut u16,
        destination_pixels: usize,
        width: u32,
        height: u32,
        source: *const u8,
        source_len: usize,
        source_width: u32,
        source_height: u32,
        source_x: u32,
        source_y: u32,
        source_rect_width: u32,
        source_rect_height: u32,
        destination_x: u32,
        destination_y: u32,
        destination_rect_width: u32,
        destination_rect_height: u32,
        quarter_turn: u32,
        mirror_x: i32,
        mirror_y: i32,
    ) -> i32;
}

// Unit tests exercise the transactional strip FFI without ESP-IDF. Rejecting
// the hardware operations makes the renderer take its ordered software paths.
#[cfg(test)]
unsafe fn pocketjs_ppa_fill_rgb565_bridge(
    _destination: *mut u16,
    _destination_pixels: usize,
    _width: u32,
    _height: u32,
    _x: u32,
    _y: u32,
    _rect_width: u32,
    _rect_height: u32,
    _color: u16,
) -> i32 {
    0
}

#[cfg(test)]
unsafe fn pocketjs_ppa_blend_a8_rgb565_bridge(
    _destination: *mut u16,
    _destination_pixels: usize,
    _width: u32,
    _height: u32,
    _mask: *const u8,
    _mask_len: usize,
    _x: u32,
    _y: u32,
    _rect_width: u32,
    _rect_height: u32,
    _red: u8,
    _green: u8,
    _blue: u8,
    _global_alpha: u8,
) -> i32 {
    0
}

#[cfg(test)]
unsafe fn pocketjs_ppa_srm_psm5650_rgb565_bridge(
    _destination: *mut u16,
    _destination_pixels: usize,
    _width: u32,
    _height: u32,
    _source: *const u8,
    _source_len: usize,
    _source_width: u32,
    _source_height: u32,
    _source_x: u32,
    _source_y: u32,
    _source_rect_width: u32,
    _source_rect_height: u32,
    _destination_x: u32,
    _destination_y: u32,
    _destination_rect_width: u32,
    _destination_rect_height: u32,
    _quarter_turn: u32,
    _mirror_x: i32,
    _mirror_y: i32,
) -> i32 {
    0
}

#[cfg(not(test))]
struct EspAllocator;

#[cfg(not(test))]
unsafe impl GlobalAlloc for EspAllocator {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        pocketjs_rust_alloc(layout.size(), layout.align())
    }

    unsafe fn dealloc(&self, ptr: *mut u8, layout: Layout) {
        pocketjs_rust_dealloc(ptr, layout.size(), layout.align())
    }
}

#[cfg(not(test))]
#[global_allocator]
static ALLOCATOR: EspAllocator = EspAllocator;

#[cfg(not(test))]
#[panic_handler]
fn panic(_info: &core::panic::PanicInfo<'_>) -> ! {
    unsafe {
        pocketjs_rust_panic();
    }
    loop {
        core::hint::spin_loop();
    }
}

static mut UI: Option<Ui> = None;
static mut RENDERER: Option<Renderer> = None;
static mut RENDER_TARGETS: Option<Vec<TrackedRenderTarget>> = None;
static mut FRAME_TARGETS: Option<Vec<TrackedFrameTarget>> = None;
static mut PENDING_FRAME: Option<PendingFrame> = None;
static mut REUSABLE_FRAME: Option<FrameFingerprint> = None;
static mut UI_MUTATION_EPOCH: u64 = 1;

#[cfg(test)]
static mut DRAW_REBUILD_COUNT: usize = 0;

const MAX_RENDER_TARGETS: usize = 2;
const MAX_FRAME_TARGETS: usize = 3;
const POCKETJS_MAX_DAMAGE_REGIONS: usize = 8;

struct TrackedRenderTarget {
    address: usize,
    state: RenderTargetState,
}

struct TrackedFrameTarget {
    id: u32,
    state: RenderTargetState,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
struct FrameFingerprint {
    word_count: usize,
    draw_hash: u64,
    raster_revision: u64,
    mutation_epoch: u64,
}

impl FrameFingerprint {
    fn capture(ui: &Ui, words: &[u32], mutation_epoch: u64) -> Self {
        Self {
            word_count: words.len(),
            draw_hash: fnv1a_words(words),
            raster_revision: ui.raster_revision(),
            mutation_epoch,
        }
    }

    fn matches_cached(self, ui: &Ui, words: &[u32], mutation_epoch: u64) -> bool {
        self.mutation_epoch == mutation_epoch
            && self.raster_revision == ui.raster_revision()
            && self.word_count == words.len()
            && self.draw_hash == fnv1a_words(words)
    }
}

#[derive(Clone, Copy)]
struct PendingFrame {
    target_id: u32,
    fingerprint: FrameFingerprint,
    /// Only a prepare immediately followed by cancel may arm REUSABLE_FRAME.
    /// Every other exported operation clears this bit.
    cancel_reusable: bool,
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct PocketjsRenderStats {
    ppa_fills: u32,
    ppa_blends: u32,
    ppa_srm: u32,
    software_ops: u32,
    software_words: u32,
    damage_regions: u32,
    damage_pixels: u32,
    damage_x: u32,
    damage_y: u32,
    damage_width: u32,
    damage_height: u32,
    full_redraw: u32,
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct PocketjsDamageRect {
    x: u32,
    y: u32,
    width: u32,
    height: u32,
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct PocketjsDamagePlan {
    region_count: u32,
    full_redraw: u32,
    regions: [PocketjsDamageRect; POCKETJS_MAX_DAMAGE_REGIONS],
}

impl From<RenderStats> for PocketjsRenderStats {
    fn from(value: RenderStats) -> Self {
        Self {
            ppa_fills: value.ppa_fills,
            ppa_blends: value.ppa_blends,
            ppa_srm: value.ppa_srm,
            software_ops: value.software_ops,
            software_words: value.software_words,
            damage_regions: value.damage_regions,
            damage_pixels: value.damage_pixels,
            damage_x: value.damage_bounds.x,
            damage_y: value.damage_bounds.y,
            damage_width: value.damage_bounds.w,
            damage_height: value.damage_bounds.h,
            full_redraw: value.full_redraw as u32,
        }
    }
}

impl From<&RenderDamagePlan> for PocketjsDamagePlan {
    fn from(value: &RenderDamagePlan) -> Self {
        let mut output = Self {
            region_count: value.region_count() as u32,
            full_redraw: value.is_full_redraw() as u32,
            ..Self::default()
        };
        for (destination, source) in output.regions.iter_mut().zip(value.regions()) {
            *destination = PocketjsDamageRect {
                x: source.x0 as u32,
                y: source.y0 as u32,
                width: (source.x1 - source.x0) as u32,
                height: (source.y1 - source.y0) as u32,
            };
        }
        output
    }
}

fn damage_plan_stats(plan: &RenderDamagePlan, scale: u32) -> RenderStats {
    let bounds = plan.bounds();
    let damage_bounds = if bounds.is_empty() {
        Rect::default()
    } else {
        Rect {
            x: bounds.x0 as u32 * scale,
            y: bounds.y0 as u32 * scale,
            w: (bounds.x1 - bounds.x0) as u32 * scale,
            h: (bounds.y1 - bounds.y0) as u32 * scale,
        }
    };
    RenderStats {
        damage_regions: plan.region_count() as u32,
        damage_pixels: plan
            .area()
            .saturating_mul(scale as u64)
            .saturating_mul(scale as u64)
            .min(u32::MAX as u64) as u32,
        damage_bounds,
        full_redraw: plan.is_full_redraw(),
        ..RenderStats::default()
    }
}

struct EspPpaOps;

impl PpaOps for EspPpaOps {
    fn fill_rgb565(
        &mut self,
        destination: &mut [u16],
        width: u32,
        height: u32,
        rect: Rect,
        color: u16,
    ) -> bool {
        unsafe {
            pocketjs_ppa_fill_rgb565_bridge(
                destination.as_mut_ptr(),
                destination.len(),
                width,
                height,
                rect.x,
                rect.y,
                rect.w,
                rect.h,
                color,
            ) != 0
        }
    }

    fn blend_a8_rgb565(
        &mut self,
        destination: &mut [u16],
        width: u32,
        height: u32,
        mask: &[u8],
        rect: Rect,
        color: [u8; 3],
        global_alpha: u8,
    ) -> bool {
        unsafe {
            pocketjs_ppa_blend_a8_rgb565_bridge(
                destination.as_mut_ptr(),
                destination.len(),
                width,
                height,
                mask.as_ptr(),
                mask.len(),
                rect.x,
                rect.y,
                rect.w,
                rect.h,
                color[0],
                color[1],
                color[2],
                global_alpha,
            ) != 0
        }
    }

    fn srm_psm5650_to_rgb565(
        &mut self,
        destination: &mut [u16],
        width: u32,
        height: u32,
        source: &[u8],
        source_width: u32,
        source_height: u32,
        source_rect: Rect,
        destination_rect: Rect,
        transform: SrmTransform,
    ) -> bool {
        let quarter_turn = match transform.rotation {
            QuarterTurn::None => 0,
            QuarterTurn::Ccw90 => 1,
            QuarterTurn::Ccw180 => 2,
            QuarterTurn::Ccw270 => 3,
        };
        unsafe {
            pocketjs_ppa_srm_psm5650_rgb565_bridge(
                destination.as_mut_ptr(),
                destination.len(),
                width,
                height,
                source.as_ptr(),
                source.len(),
                source_width,
                source_height,
                source_rect.x,
                source_rect.y,
                source_rect.w,
                source_rect.h,
                destination_rect.x,
                destination_rect.y,
                destination_rect.w,
                destination_rect.h,
                quarter_turn,
                transform.mirror_x as i32,
                transform.mirror_y as i32,
            ) != 0
        }
    }
}

#[inline]
fn ui() -> Option<&'static mut Ui> {
    unsafe { UI.as_mut() }
}

fn renderer() -> Option<&'static mut Renderer> {
    unsafe {
        if RENDERER.is_none() {
            RENDERER = Renderer::new(RendererConfig::default());
        }
        RENDERER.as_mut()
    }
}

/// Invalidate the one-shot front-probe reuse path.
///
/// The token is deliberately stricter than ordinary DrawList validity: it is
/// valid only when the next exported call after `cancel` is `prepare`. While a
/// frame is pending, any intervening exported operation also prevents that
/// frame's later cancel from arming a token.
fn invalidate_frame_reuse() {
    unsafe {
        REUSABLE_FRAME = None;
        if let Some(pending) = PENDING_FRAME.as_mut() {
            pending.cancel_reusable = false;
        }
    }
}

/// Record a host-visible UI mutation and invalidate cached-frame reuse.
fn mark_ui_mutated() {
    unsafe {
        UI_MUTATION_EPOCH = UI_MUTATION_EPOCH.wrapping_add(1);
    }
    invalidate_frame_reuse();
}

fn rebuild_draw_list(ui: &mut Ui) -> *const DrawList {
    #[cfg(test)]
    unsafe {
        DRAW_REBUILD_COUNT += 1;
    }
    ui.draw()
}

unsafe fn render_target_state(framebuffer: *mut u16) -> Option<*mut RenderTargetState> {
    if RENDER_TARGETS.is_none() {
        RENDER_TARGETS = Some(Vec::with_capacity(MAX_RENDER_TARGETS));
    }
    let targets = RENDER_TARGETS.as_mut()?;
    let address = framebuffer as usize;
    if let Some(target) = targets.iter_mut().find(|target| target.address == address) {
        return Some(&mut target.state);
    }
    if targets.len() == MAX_RENDER_TARGETS {
        targets.remove(0);
    }
    targets.push(TrackedRenderTarget {
        address,
        state: RenderTargetState::new(),
    });
    targets.last_mut().map(|target| &mut target.state as *mut _)
}

unsafe fn frame_target_state(target_id: u32) -> Option<*mut RenderTargetState> {
    if target_id >= MAX_FRAME_TARGETS as u32 {
        return None;
    }
    if FRAME_TARGETS.is_none() {
        FRAME_TARGETS = Some(Vec::with_capacity(MAX_FRAME_TARGETS));
    }
    let targets = FRAME_TARGETS.as_mut()?;
    if let Some(target) = targets.iter_mut().find(|target| target.id == target_id) {
        return Some(&mut target.state);
    }
    targets.push(TrackedFrameTarget {
        id: target_id,
        state: RenderTargetState::new(),
    });
    targets.last_mut().map(|target| &mut target.state as *mut _)
}

unsafe fn invalidate_render_target(framebuffer: *mut u16) {
    let Some(targets) = RENDER_TARGETS.as_mut() else {
        return;
    };
    let address = framebuffer as usize;
    if let Some(target) = targets.iter_mut().find(|target| target.address == address) {
        target.state.invalidate();
    }
}

unsafe fn invalidate_render_resources() {
    if let Some(renderer) = RENDERER.as_mut() {
        renderer.invalidate_resources();
    }
    if let Some(targets) = RENDER_TARGETS.as_mut() {
        for target in targets {
            target.state.invalidate();
        }
    }
    if let Some(targets) = FRAME_TARGETS.as_mut() {
        for target in targets {
            target.state.invalidate();
        }
    }
    PENDING_FRAME = None;
    REUSABLE_FRAME = None;
}

#[no_mangle]
pub extern "C" fn pocketjs_core_init(width: u32, height: u32, raster_density: u32) -> i32 {
    invalidate_frame_reuse();
    if width == 0 || height == 0 || raster_density == 0 {
        return 0;
    }

    let mut instance = Ui::new_with_raster_density(raster_density);
    instance.set_viewport(width as f32, height as f32);
    unsafe {
        RENDERER = None;
        RENDER_TARGETS = None;
        FRAME_TARGETS = None;
        PENDING_FRAME = None;
        REUSABLE_FRAME = None;
        UI_MUTATION_EPOCH = UI_MUTATION_EPOCH.wrapping_add(1);
        UI = Some(instance);
    }
    1
}

#[no_mangle]
pub extern "C" fn pocketjs_core_reset() {
    unsafe {
        RENDERER = None;
        RENDER_TARGETS = None;
        FRAME_TARGETS = None;
        PENDING_FRAME = None;
        REUSABLE_FRAME = None;
        UI_MUTATION_EPOCH = UI_MUTATION_EPOCH.wrapping_add(1);
        UI = None;
    }
}

#[no_mangle]
pub extern "C" fn pocketjs_core_create_node(node_type: u32) -> i32 {
    mark_ui_mutated();
    ui().map_or(0, |u| u.create_node(node_type as u8))
}

#[no_mangle]
pub extern "C" fn pocketjs_core_destroy_node(id: i32) {
    mark_ui_mutated();
    if let Some(u) = ui() {
        u.destroy_node(id);
    }
}

#[no_mangle]
pub extern "C" fn pocketjs_core_insert_before(parent: i32, child: i32, anchor: i32) {
    mark_ui_mutated();
    if let Some(u) = ui() {
        u.insert_before(parent, child, anchor);
    }
}

#[no_mangle]
pub extern "C" fn pocketjs_core_remove_child(parent: i32, child: i32) {
    mark_ui_mutated();
    if let Some(u) = ui() {
        u.remove_child(parent, child);
    }
}

#[no_mangle]
pub extern "C" fn pocketjs_core_set_style(id: i32, style_id: i32) {
    mark_ui_mutated();
    if let Some(u) = ui() {
        u.set_style(id, style_id);
    }
}

#[no_mangle]
pub extern "C" fn pocketjs_core_set_prop(id: i32, prop: u32, value: f64) {
    mark_ui_mutated();
    if prop > u8::MAX as u32 {
        return;
    }
    if let Some(u) = ui() {
        u.set_prop(id, prop as u8, value);
    }
}

unsafe fn input_text<'a>(ptr: *const u8, len: usize) -> Option<&'a str> {
    if ptr.is_null() {
        return if len == 0 { Some("") } else { None };
    }
    core::str::from_utf8(slice::from_raw_parts(ptr, len)).ok()
}

#[no_mangle]
pub unsafe extern "C" fn pocketjs_core_set_text(id: i32, ptr: *const u8, len: usize) -> i32 {
    mark_ui_mutated();
    let Some(text) = input_text(ptr, len) else {
        return 0;
    };
    let Some(u) = ui() else {
        return 0;
    };
    u.set_text(id, text);
    1
}

#[no_mangle]
pub unsafe extern "C" fn pocketjs_core_replace_text(id: i32, ptr: *const u8, len: usize) -> i32 {
    mark_ui_mutated();
    let Some(text) = input_text(ptr, len) else {
        return 0;
    };
    let Some(u) = ui() else {
        return 0;
    };
    u.replace_text(id, text);
    1
}

#[no_mangle]
pub extern "C" fn pocketjs_core_animate(
    id: i32,
    prop: u32,
    to: f64,
    duration_ms: u32,
    easing: u32,
    delay_ms: u32,
) -> i32 {
    mark_ui_mutated();
    if prop > u8::MAX as u32 || easing > u8::MAX as u32 {
        return 0;
    }
    ui().map_or(0, |u| {
        u.animate(id, prop as u8, to, duration_ms, easing as u8, delay_ms)
    })
}

#[no_mangle]
pub extern "C" fn pocketjs_core_cancel_animation(animation_id: i32) {
    mark_ui_mutated();
    if let Some(u) = ui() {
        u.cancel_anim(animation_id);
    }
}

#[no_mangle]
pub extern "C" fn pocketjs_core_set_focus(id: i32) {
    mark_ui_mutated();
    if let Some(u) = ui() {
        u.set_focus(id);
    }
}

#[no_mangle]
pub extern "C" fn pocketjs_core_set_active(id: i32, active: i32) {
    mark_ui_mutated();
    if let Some(u) = ui() {
        u.set_active(id, active != 0);
    }
}

#[no_mangle]
pub extern "C" fn pocketjs_core_hit_test(x: f32, y: f32) -> i32 {
    invalidate_frame_reuse();
    ui().map_or(0, |u| u.hit_test(x, y))
}

#[no_mangle]
pub unsafe extern "C" fn pocketjs_core_load_styles(ptr: *const u8, len: usize) -> i32 {
    mark_ui_mutated();
    if ptr.is_null() || len == 0 {
        return 0;
    }
    let result = ui().map_or(0, |u| u.load_styles(slice::from_raw_parts(ptr, len)) as i32);
    if result != 0 {
        invalidate_render_resources();
    }
    result
}

#[no_mangle]
pub unsafe extern "C" fn pocketjs_core_load_font_atlas(ptr: *const u8, len: usize) -> i32 {
    mark_ui_mutated();
    if ptr.is_null() || len == 0 {
        return 0;
    }
    let result = ui().map_or(0, |u| {
        u.load_font_atlas(slice::from_raw_parts(ptr, len)) as i32
    });
    if result != 0 {
        invalidate_render_resources();
    }
    result
}

#[no_mangle]
pub unsafe extern "C" fn pocketjs_core_upload_texture(
    ptr: *const u8,
    len: usize,
    width: u32,
    height: u32,
    psm: u32,
) -> i32 {
    mark_ui_mutated();
    if ptr.is_null() || len == 0 {
        return -1;
    }
    let result = ui().map_or(-1, |u| {
        u.upload_texture(slice::from_raw_parts(ptr, len), width, height, psm)
    });
    if result >= 0 {
        invalidate_render_resources();
    }
    result
}

#[no_mangle]
pub extern "C" fn pocketjs_core_set_image(id: i32, texture: i32) {
    mark_ui_mutated();
    if let Some(u) = ui() {
        u.set_image(id, texture);
    }
}

#[no_mangle]
pub extern "C" fn pocketjs_core_set_sprite(
    id: i32,
    atlas: i32,
    frames: u32,
    columns: u32,
    step: u32,
) {
    mark_ui_mutated();
    if let Some(u) = ui() {
        u.set_sprite(id, atlas, frames, columns, step);
    }
}

#[no_mangle]
pub unsafe extern "C" fn pocketjs_core_measure_text(
    ptr: *const u8,
    len: usize,
    font_slot: u32,
) -> f32 {
    invalidate_frame_reuse();
    if font_slot > u8::MAX as u32 {
        return 0.0;
    }
    let Some(text) = input_text(ptr, len) else {
        return 0.0;
    };
    ui().map_or(0.0, |u| u.measure_text(text, font_slot as u8))
}

#[no_mangle]
pub extern "C" fn pocketjs_core_tick() {
    mark_ui_mutated();
    if let Some(u) = ui() {
        u.tick();
    }
}

fn fnv1a_words(words: &[u32]) -> u64 {
    let mut hash = 0xcbf2_9ce4_8422_2325u64;
    for word in words {
        for byte in word.to_le_bytes() {
            hash ^= byte as u64;
            hash = hash.wrapping_mul(0x0000_0100_0000_01b3);
        }
    }
    hash
}

#[no_mangle]
pub extern "C" fn pocketjs_core_draw_hash() -> u64 {
    invalidate_frame_reuse();
    ui().map_or(0, |u| {
        let draw_list = rebuild_draw_list(u);
        unsafe { fnv1a_words(&(*draw_list).words) }
    })
}

#[no_mangle]
pub extern "C" fn pocketjs_core_draw_word_count() -> usize {
    invalidate_frame_reuse();
    ui().map_or(0, |u| {
        let draw_list = rebuild_draw_list(u);
        unsafe { (*draw_list).words.len() }
    })
}

#[no_mangle]
pub extern "C" fn pocketjs_core_framebuffer_bytes() -> usize {
    invalidate_frame_reuse();
    ui().map_or(0, |u| {
        let (width, height) = u.viewport();
        (width as usize)
            .checked_mul(height as usize)
            .and_then(|pixels| pixels.checked_mul(core::mem::size_of::<u16>()))
            .unwrap_or(0)
    })
}

unsafe fn render_rgb565(
    framebuffer: *mut u16,
    len: usize,
    out_stats: *mut PocketjsRenderStats,
    incremental: bool,
) -> i32 {
    invalidate_frame_reuse();
    if framebuffer.is_null() {
        return 0;
    }
    let Some(u) = ui() else {
        return 0;
    };
    let (width, height) = u.viewport();
    let Some(expected_pixels) = (width as usize).checked_mul(height as usize) else {
        return 0;
    };
    let Some(expected_bytes) = expected_pixels.checked_mul(core::mem::size_of::<u16>()) else {
        return 0;
    };
    if len != expected_bytes {
        return 0;
    }

    let draw_list = rebuild_draw_list(u);
    let ui_ref: &Ui = &*(u as *const Ui);
    if RENDERER.is_none() {
        let Some(renderer) = Renderer::new(RendererConfig::default()) else {
            return 0;
        };
        RENDERER = Some(renderer);
    }
    let Some(renderer) = RENDERER.as_mut() else {
        return 0;
    };
    let output = slice::from_raw_parts_mut(framebuffer, expected_pixels);
    let mut ppa = EspPpaOps;
    let stats = if incremental {
        let Some(target) = render_target_state(framebuffer) else {
            return 0;
        };
        renderer.render_incremental(
            &mut *target,
            ui_ref,
            &(*draw_list).words,
            output,
            width as u32,
            height as u32,
            &mut ppa,
        )
    } else {
        renderer.render(
            ui_ref,
            &(*draw_list).words,
            output,
            width as u32,
            height as u32,
            &mut ppa,
        )
    };
    let Some(stats) = stats else {
        return 0;
    };
    if !incremental {
        invalidate_render_target(framebuffer);
    }
    if !out_stats.is_null() {
        *out_stats = stats.into();
    }
    1
}

/// Render a complete RGB565 frame through the C ABI.
///
/// # Safety
///
/// `framebuffer` must reference `len` writable bytes, and `out_stats` must be
/// null or point to writable `PocketjsRenderStats` storage.
#[no_mangle]
pub unsafe extern "C" fn pocketjs_core_render_rgb565(
    framebuffer: *mut u16,
    len: usize,
    out_stats: *mut PocketjsRenderStats,
) -> i32 {
    render_rgb565(framebuffer, len, out_stats, false)
}

/// Incrementally update a persistent RGB565 frame through the C ABI.
///
/// # Safety
///
/// `framebuffer` must reference `len` writable bytes that remain unchanged
/// between calls, and `out_stats` must be null or point to writable
/// `PocketjsRenderStats` storage.
#[no_mangle]
pub unsafe extern "C" fn pocketjs_core_render_rgb565_incremental(
    framebuffer: *mut u16,
    len: usize,
    out_stats: *mut PocketjsRenderStats,
) -> i32 {
    render_rgb565(framebuffer, len, out_stats, true)
}

/// Prepare one DrawList damage transaction for native framebuffer target
/// `0`/`1` or the headless target `2`.
///
/// The returned plan uses global logical viewport coordinates. Preparation
/// does not advance the target snapshot; call `commit`, `cancel`, or `abort`
/// before preparing another target.
#[no_mangle]
pub unsafe extern "C" fn pocketjs_core_prepare_rgb565_frame(
    target_id: u32,
    out_plan: *mut PocketjsDamagePlan,
    out_stats: *mut PocketjsRenderStats,
) -> i32 {
    // REUSABLE_FRAME is a one-shot token: even a failed prepare attempt
    // consumes it. If another transaction is still pending, this attempted
    // interleave also makes that transaction ineligible to arm a new token.
    let reusable = REUSABLE_FRAME.take();
    if target_id >= MAX_FRAME_TARGETS as u32 || out_plan.is_null() || PENDING_FRAME.is_some() {
        if let Some(pending) = PENDING_FRAME.as_mut() {
            pending.cancel_reusable = false;
        }
        return 0;
    }
    let Some(u) = ui() else {
        return 0;
    };
    let ui_ptr = u as *mut Ui;
    let ui_ref: &Ui = &*ui_ptr;
    let cached_draw_list = ui_ref.current_draw_list() as *const DrawList;
    let mutation_epoch = UI_MUTATION_EPOCH;
    let (draw_list, fingerprint) = match reusable {
        Some(fingerprint)
            if fingerprint.matches_cached(ui_ref, &(*cached_draw_list).words, mutation_epoch) =>
        {
            (cached_draw_list, fingerprint)
        }
        _ => {
            let draw_list = rebuild_draw_list(&mut *ui_ptr);
            let ui_ref = &*ui_ptr;
            let fingerprint =
                FrameFingerprint::capture(ui_ref, &(*draw_list).words, mutation_epoch);
            (draw_list, fingerprint)
        }
    };
    let ui_ref: &Ui = &*ui_ptr;
    let words = &(*draw_list).words;

    let Some(renderer) = renderer() else {
        return 0;
    };
    let Some(target) = frame_target_state(target_id) else {
        return 0;
    };
    let Some(plan) = renderer.prepare_damage(&*target, ui_ref, words) else {
        return 0;
    };
    debug_assert!(plan.region_count() <= POCKETJS_MAX_DAMAGE_REGIONS);
    *out_plan = PocketjsDamagePlan::from(&plan);
    if !out_stats.is_null() {
        *out_stats = damage_plan_stats(&plan, renderer.config().scale).into();
    }
    PENDING_FRAME = Some(PendingFrame {
        target_id,
        fingerprint,
        cancel_reusable: true,
    });
    1
}

/// Render one dirty logical rectangle into a full-width compact RGB565 strip.
///
/// `len` is bytes and must equal `viewport_width * height * sizeof(uint16_t)`.
/// The strip represents global rows `y..y+height`; only columns
/// `x..x+width` are modified.
#[no_mangle]
pub unsafe extern "C" fn pocketjs_core_render_rgb565_strip(
    framebuffer: *mut u16,
    len: usize,
    x: u32,
    y: u32,
    width: u32,
    height: u32,
    out_stats: *mut PocketjsRenderStats,
) -> i32 {
    invalidate_frame_reuse();
    if framebuffer.is_null() || width == 0 || height == 0 {
        return 0;
    }
    let Some(pending) = PENDING_FRAME else {
        return 0;
    };
    let Some(u) = ui() else {
        return 0;
    };
    let ui_ref: &Ui = &*(u as *const Ui);
    let words = &ui_ref.current_draw_list().words;
    if !pending
        .fingerprint
        .matches_cached(ui_ref, words, UI_MUTATION_EPOCH)
    {
        return 0;
    }
    let (viewport_w, _) = ui_ref.viewport();
    let Some(expected_pixels) = (viewport_w as usize).checked_mul(height as usize) else {
        return 0;
    };
    let Some(expected_bytes) = expected_pixels.checked_mul(core::mem::size_of::<u16>()) else {
        return 0;
    };
    if len != expected_bytes {
        return 0;
    }

    let Some(renderer) = renderer() else {
        return 0;
    };
    if renderer.config().scale != 1 {
        return 0;
    }
    let output = slice::from_raw_parts_mut(framebuffer, expected_pixels);
    let mut ppa = EspPpaOps;
    let Some(stats) = renderer.render_strip(
        ui_ref,
        words,
        output,
        Rect {
            x,
            y,
            w: width,
            h: height,
        },
        &mut ppa,
    ) else {
        return 0;
    };
    if !out_stats.is_null() {
        *out_stats = stats.into();
    }
    1
}

/// Commit the pending DrawList only when both its target and cached DrawList
/// still match the prepared transaction.
///
/// Preparation is the sole DrawList build for one logical frame. Every
/// exported UI mutator advances `UI_MUTATION_EPOCH`, resource replacement
/// clears the pending transaction, and the current cached words are rehashed
/// against the prepared fingerprint below. Rebuilding again here would
/// therefore duplicate `Ui::draw()` without strengthening validation.
#[no_mangle]
pub extern "C" fn pocketjs_core_commit_rgb565_frame(target_id: u32) -> i32 {
    invalidate_frame_reuse();
    unsafe {
        let Some(pending) = PENDING_FRAME else {
            return 0;
        };
        if pending.target_id != target_id {
            if let Some(target) = frame_target_state(pending.target_id) {
                (*target).invalidate();
            }
            PENDING_FRAME = None;
            return 0;
        }
        let Some(u) = ui() else {
            if let Some(target) = frame_target_state(pending.target_id) {
                (*target).invalidate();
            }
            PENDING_FRAME = None;
            return 0;
        };
        let ui_ref: &Ui = &*(u as *const Ui);
        let words = &ui_ref.current_draw_list().words;
        let Some(target) = frame_target_state(target_id) else {
            PENDING_FRAME = None;
            return 0;
        };
        let current = FrameFingerprint::capture(ui_ref, words, UI_MUTATION_EPOCH);
        if current != pending.fingerprint {
            (*target).invalidate();
            PENDING_FRAME = None;
            return 0;
        }
        let Some(renderer) = renderer() else {
            (*target).invalidate();
            PENDING_FRAME = None;
            return 0;
        };
        let committed = renderer.commit_damage(&mut *target, ui_ref, words);
        if !committed {
            (*target).invalidate();
        }
        PENDING_FRAME = None;
        committed as i32
    }
}

/// Discard a prepared front-target probe without changing its retained damage
/// history.
#[no_mangle]
pub extern "C" fn pocketjs_core_cancel_rgb565_frame(target_id: u32) -> i32 {
    unsafe {
        // A successful direct prepare -> cancel transition may arm exactly
        // one cached DrawList reuse. A wrong-target cancel is an interleaved
        // transaction attempt and permanently disqualifies the pending frame.
        REUSABLE_FRAME = None;
        let Some(pending) = PENDING_FRAME else {
            return 0;
        };
        if pending.target_id != target_id {
            if let Some(pending) = PENDING_FRAME.as_mut() {
                pending.cancel_reusable = false;
            }
            return 0;
        }
        PENDING_FRAME = None;
        if pending.cancel_reusable
            && UI.as_ref().is_some_and(|ui| {
                pending.fingerprint.matches_cached(
                    ui,
                    &ui.current_draw_list().words,
                    UI_MUTATION_EPOCH,
                )
            })
        {
            REUSABLE_FRAME = Some(pending.fingerprint);
        }
        1
    }
}

/// Discard a failed/partial transaction and force the target's next prepare to
/// return a full redraw.
#[no_mangle]
pub extern "C" fn pocketjs_core_abort_rgb565_frame(target_id: u32) {
    invalidate_frame_reuse();
    unsafe {
        if let Some(target) = frame_target_state(target_id) {
            (*target).invalidate();
        }
        if PENDING_FRAME
            .as_ref()
            .is_some_and(|pending| pending.target_id == target_id)
        {
            PENDING_FRAME = None;
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use core::ptr;

    unsafe fn prepare(target_id: u32) -> PocketjsDamagePlan {
        let mut plan = PocketjsDamagePlan::default();
        assert_eq!(
            pocketjs_core_prepare_rgb565_frame(target_id, &mut plan, ptr::null_mut()),
            1
        );
        plan
    }

    #[test]
    fn frame_transactions_build_once_and_adjacent_cancel_reuses_cached_draw_list() {
        unsafe {
            DRAW_REBUILD_COUNT = 0;
        }
        assert_eq!(pocketjs_core_init(32, 16, 1), 1);

        let _front_plan = unsafe { prepare(0) };
        let (first_fingerprint, cached_words) = unsafe {
            let pending = PENDING_FRAME.expect("front probe must be pending");
            (
                pending.fingerprint,
                UI.as_ref().unwrap().current_draw_list().words.as_ptr(),
            )
        };
        assert_eq!(unsafe { DRAW_REBUILD_COUNT }, 1);
        assert_eq!(pocketjs_core_cancel_rgb565_frame(0), 1);
        assert_eq!(unsafe { REUSABLE_FRAME }, Some(first_fingerprint));

        // This is the intended front-buffer probe -> back-buffer prepare
        // transition. It consumes the token and keeps the exact cached list.
        let back_plan = unsafe { prepare(1) };
        assert_eq!(unsafe { DRAW_REBUILD_COUNT }, 1);
        assert_eq!(back_plan.region_count, 1);
        assert_eq!(back_plan.full_redraw, 1);
        unsafe {
            assert_eq!(REUSABLE_FRAME, None);
            assert_eq!(
                UI.as_ref().unwrap().current_draw_list().words.as_ptr(),
                cached_words
            );
            assert_eq!(PENDING_FRAME.unwrap().fingerprint, first_fingerprint);
        }

        let mut strip = [0u16; 32 * 16];
        assert_eq!(
            unsafe {
                pocketjs_core_render_rgb565_strip(
                    strip.as_mut_ptr(),
                    strip.len() * core::mem::size_of::<u16>(),
                    0,
                    0,
                    32,
                    16,
                    ptr::null_mut(),
                )
            },
            1
        );
        assert_eq!(pocketjs_core_commit_rgb565_frame(1), 1);
        assert_eq!(
            unsafe { DRAW_REBUILD_COUNT },
            1,
            "changed frame must build only during its front prepare"
        );

        // An unchanged frame also builds once to discover that its current
        // front target is already up to date; commit validates the cache.
        let unchanged = unsafe { prepare(1) };
        assert_eq!(unchanged.region_count, 0);
        assert_eq!(pocketjs_core_commit_rgb565_frame(1), 1);
        assert_eq!(
            unsafe { DRAW_REBUILD_COUNT },
            2,
            "unchanged commit must not rebuild the DrawList"
        );

        // The token is strict adjacency state, not a general DrawList cache:
        // even a read-only exported call between cancel and prepare consumes
        // the opportunity and forces one rebuild.
        let _front_plan = unsafe { prepare(0) };
        assert_eq!(unsafe { DRAW_REBUILD_COUNT }, 3);
        assert_eq!(pocketjs_core_cancel_rgb565_frame(0), 1);
        assert_eq!(unsafe { REUSABLE_FRAME }, Some(first_fingerprint));
        assert_eq!(pocketjs_core_framebuffer_bytes(), 32 * 16 * 2);
        assert_eq!(unsafe { REUSABLE_FRAME }, None);
        let _headless_plan = unsafe { prepare(2) };
        assert_eq!(unsafe { DRAW_REBUILD_COUNT }, 4);

        // UI mutations independently invalidate a freshly armed token.
        assert_eq!(pocketjs_core_cancel_rgb565_frame(2), 1);
        pocketjs_core_tick();
        assert_eq!(unsafe { REUSABLE_FRAME }, None);
        let _next_plan = unsafe { prepare(0) };
        assert_eq!(unsafe { DRAW_REBUILD_COUNT }, 5);

        pocketjs_core_abort_rgb565_frame(0);
        pocketjs_core_reset();
    }
}
