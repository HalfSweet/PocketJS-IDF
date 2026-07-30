#![no_std]
// Every public unsafe Rust item below is an exported C ABI entry point whose
// pointer contract is documented in private_include/pocketjs_core.h.
#![allow(clippy::missing_safety_doc)]
extern crate alloc;
#[cfg(test)]
extern crate std;

use alloc::{boxed::Box, vec::Vec};
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
// The test stub must keep the argument-for-argument ESP-IDF C bridge ABI.
#[allow(clippy::too_many_arguments)]
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
// The test stub must keep the argument-for-argument ESP-IDF C bridge ABI.
#[allow(clippy::too_many_arguments)]
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
// The test stub must keep the argument-for-argument ESP-IDF C bridge ABI.
#[allow(clippy::too_many_arguments)]
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

const MAX_RENDER_TARGETS: usize = 2;
const MAX_FRAME_TARGETS: usize = 3;
const POCKETJS_MAX_DAMAGE_REGIONS: usize = 8;

#[repr(C)]
pub struct PocketjsCore {
    ui: Ui,
    renderer: Option<Renderer>,
    render_targets: Vec<TrackedRenderTarget>,
    frame_targets: Vec<TrackedFrameTarget>,
    pending_frame: Option<PendingFrame>,
    reusable_frame: Option<FrameFingerprint>,
    mutation_epoch: u64,
    #[cfg(test)]
    draw_rebuild_count: usize,
}

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

impl PocketjsCore {
    fn new(width: u32, height: u32, raster_density: u32) -> Option<Self> {
        if width == 0 || height == 0 || raster_density == 0 {
            return None;
        }
        let mut ui = Ui::new_with_raster_density(raster_density);
        ui.set_viewport(width as f32, height as f32);
        Some(Self {
            ui,
            renderer: None,
            render_targets: Vec::with_capacity(MAX_RENDER_TARGETS),
            frame_targets: Vec::with_capacity(MAX_FRAME_TARGETS),
            pending_frame: None,
            reusable_frame: None,
            mutation_epoch: 1,
            #[cfg(test)]
            draw_rebuild_count: 0,
        })
    }

    fn renderer(&mut self) -> Option<*mut Renderer> {
        if self.renderer.is_none() {
            self.renderer = Renderer::new(RendererConfig::default());
        }
        self.renderer.as_mut().map(|renderer| renderer as *mut _)
    }

    fn invalidate_frame_reuse(&mut self) {
        self.reusable_frame = None;
        if let Some(pending) = self.pending_frame.as_mut() {
            pending.cancel_reusable = false;
        }
    }

    fn mark_ui_mutated(&mut self) {
        self.mutation_epoch = self.mutation_epoch.wrapping_add(1);
        self.invalidate_frame_reuse();
    }

    fn rebuild_draw_list(&mut self) -> *const DrawList {
        #[cfg(test)]
        {
            self.draw_rebuild_count += 1;
        }
        self.ui.draw()
    }

    fn render_target_state(&mut self, framebuffer: *mut u16) -> Option<*mut RenderTargetState> {
        let targets = &mut self.render_targets;
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

    fn frame_target_state(&mut self, target_id: u32) -> Option<*mut RenderTargetState> {
        if target_id >= MAX_FRAME_TARGETS as u32 {
            return None;
        }
        let targets = &mut self.frame_targets;
        if let Some(target) = targets.iter_mut().find(|target| target.id == target_id) {
            return Some(&mut target.state);
        }
        targets.push(TrackedFrameTarget {
            id: target_id,
            state: RenderTargetState::new(),
        });
        targets.last_mut().map(|target| &mut target.state as *mut _)
    }

    fn invalidate_render_target(&mut self, framebuffer: *mut u16) {
        let address = framebuffer as usize;
        if let Some(target) = self
            .render_targets
            .iter_mut()
            .find(|target| target.address == address)
        {
            target.state.invalidate();
        }
    }

    fn invalidate_render_resources(&mut self) {
        if let Some(renderer) = self.renderer.as_mut() {
            renderer.invalidate_resources();
        }
        for target in &mut self.render_targets {
            target.state.invalidate();
        }
        for target in &mut self.frame_targets {
            target.state.invalidate();
        }
        self.pending_frame = None;
        self.reusable_frame = None;
    }
}

unsafe fn core_mut<'a>(core: *mut PocketjsCore) -> Option<&'a mut PocketjsCore> {
    core.as_mut()
}

#[no_mangle]
pub extern "C" fn pocketjs_core_create(
    width: u32,
    height: u32,
    raster_density: u32,
) -> *mut PocketjsCore {
    PocketjsCore::new(width, height, raster_density)
        .map(|core| Box::into_raw(Box::new(core)))
        .unwrap_or(core::ptr::null_mut())
}

#[no_mangle]
pub unsafe extern "C" fn pocketjs_core_destroy(core: *mut PocketjsCore) {
    if !core.is_null() {
        drop(Box::from_raw(core));
    }
}

#[no_mangle]
pub unsafe extern "C" fn pocketjs_core_create_node(core: *mut PocketjsCore, node_type: u32) -> i32 {
    let Some(core) = core_mut(core) else {
        return 0;
    };
    core.mark_ui_mutated();
    core.ui.create_node(node_type as u8)
}

#[no_mangle]
pub unsafe extern "C" fn pocketjs_core_destroy_node(core: *mut PocketjsCore, id: i32) {
    let Some(core) = core_mut(core) else {
        return;
    };
    core.mark_ui_mutated();
    core.ui.destroy_node(id);
}

#[no_mangle]
pub unsafe extern "C" fn pocketjs_core_insert_before(
    core: *mut PocketjsCore,
    parent: i32,
    child: i32,
    anchor: i32,
) {
    let Some(core) = core_mut(core) else {
        return;
    };
    core.mark_ui_mutated();
    core.ui.insert_before(parent, child, anchor);
}

#[no_mangle]
pub unsafe extern "C" fn pocketjs_core_remove_child(
    core: *mut PocketjsCore,
    parent: i32,
    child: i32,
) {
    let Some(core) = core_mut(core) else {
        return;
    };
    core.mark_ui_mutated();
    core.ui.remove_child(parent, child);
}

#[no_mangle]
pub unsafe extern "C" fn pocketjs_core_set_style(core: *mut PocketjsCore, id: i32, style_id: i32) {
    let Some(core) = core_mut(core) else {
        return;
    };
    core.mark_ui_mutated();
    core.ui.set_style(id, style_id);
}

#[no_mangle]
pub unsafe extern "C" fn pocketjs_core_set_prop(
    core: *mut PocketjsCore,
    id: i32,
    prop: u32,
    value: f64,
) {
    let Some(core) = core_mut(core) else {
        return;
    };
    core.mark_ui_mutated();
    if prop > u8::MAX as u32 {
        return;
    }
    core.ui.set_prop(id, prop as u8, value);
}

unsafe fn input_text<'a>(ptr: *const u8, len: usize) -> Option<&'a str> {
    if ptr.is_null() {
        return if len == 0 { Some("") } else { None };
    }
    core::str::from_utf8(slice::from_raw_parts(ptr, len)).ok()
}

#[no_mangle]
pub unsafe extern "C" fn pocketjs_core_set_text(
    core: *mut PocketjsCore,
    id: i32,
    ptr: *const u8,
    len: usize,
) -> i32 {
    let Some(core) = core_mut(core) else {
        return 0;
    };
    core.mark_ui_mutated();
    let Some(text) = input_text(ptr, len) else {
        return 0;
    };
    core.ui.set_text(id, text);
    1
}

#[no_mangle]
pub unsafe extern "C" fn pocketjs_core_replace_text(
    core: *mut PocketjsCore,
    id: i32,
    ptr: *const u8,
    len: usize,
) -> i32 {
    let Some(core) = core_mut(core) else {
        return 0;
    };
    core.mark_ui_mutated();
    let Some(text) = input_text(ptr, len) else {
        return 0;
    };
    core.ui.replace_text(id, text);
    1
}

#[no_mangle]
pub unsafe extern "C" fn pocketjs_core_animate(
    core: *mut PocketjsCore,
    id: i32,
    prop: u32,
    to: f64,
    duration_ms: u32,
    easing: u32,
    delay_ms: u32,
) -> i32 {
    let Some(core) = core_mut(core) else {
        return 0;
    };
    core.mark_ui_mutated();
    if prop > u8::MAX as u32 || easing > u8::MAX as u32 {
        return 0;
    }
    core.ui
        .animate(id, prop as u8, to, duration_ms, easing as u8, delay_ms)
}

#[no_mangle]
pub unsafe extern "C" fn pocketjs_core_cancel_animation(
    core: *mut PocketjsCore,
    animation_id: i32,
) {
    let Some(core) = core_mut(core) else {
        return;
    };
    core.mark_ui_mutated();
    core.ui.cancel_anim(animation_id);
}

#[no_mangle]
pub unsafe extern "C" fn pocketjs_core_set_focus(core: *mut PocketjsCore, id: i32) {
    let Some(core) = core_mut(core) else {
        return;
    };
    core.mark_ui_mutated();
    core.ui.set_focus(id);
}

#[no_mangle]
pub unsafe extern "C" fn pocketjs_core_set_active(core: *mut PocketjsCore, id: i32, active: i32) {
    let Some(core) = core_mut(core) else {
        return;
    };
    core.mark_ui_mutated();
    core.ui.set_active(id, active != 0);
}

#[no_mangle]
pub unsafe extern "C" fn pocketjs_core_hit_test(core: *mut PocketjsCore, x: f32, y: f32) -> i32 {
    let Some(core) = core_mut(core) else {
        return 0;
    };
    core.invalidate_frame_reuse();
    core.ui.hit_test(x, y)
}

#[no_mangle]
pub unsafe extern "C" fn pocketjs_core_load_styles(
    core: *mut PocketjsCore,
    ptr: *const u8,
    len: usize,
) -> i32 {
    let Some(core) = core_mut(core) else {
        return 0;
    };
    core.mark_ui_mutated();
    if ptr.is_null() || len == 0 {
        return 0;
    }
    let result = core.ui.load_styles(slice::from_raw_parts(ptr, len)) as i32;
    if result != 0 {
        core.invalidate_render_resources();
    }
    result
}

#[no_mangle]
pub unsafe extern "C" fn pocketjs_core_load_font_atlas(
    core: *mut PocketjsCore,
    ptr: *const u8,
    len: usize,
) -> i32 {
    let Some(core) = core_mut(core) else {
        return 0;
    };
    core.mark_ui_mutated();
    if ptr.is_null() || len == 0 {
        return 0;
    }
    let result = core.ui.load_font_atlas(slice::from_raw_parts(ptr, len)) as i32;
    if result != 0 {
        core.invalidate_render_resources();
    }
    result
}

#[no_mangle]
pub unsafe extern "C" fn pocketjs_core_upload_texture(
    core: *mut PocketjsCore,
    ptr: *const u8,
    len: usize,
    width: u32,
    height: u32,
    psm: u32,
) -> i32 {
    let Some(core) = core_mut(core) else {
        return -1;
    };
    core.mark_ui_mutated();
    if ptr.is_null() || len == 0 {
        return -1;
    }
    let result = core
        .ui
        .upload_texture(slice::from_raw_parts(ptr, len), width, height, psm);
    if result >= 0 {
        core.invalidate_render_resources();
    }
    result
}

#[no_mangle]
pub unsafe extern "C" fn pocketjs_core_set_image(core: *mut PocketjsCore, id: i32, texture: i32) {
    let Some(core) = core_mut(core) else {
        return;
    };
    core.mark_ui_mutated();
    core.ui.set_image(id, texture);
}

#[no_mangle]
pub unsafe extern "C" fn pocketjs_core_set_sprite(
    core: *mut PocketjsCore,
    id: i32,
    atlas: i32,
    frames: u32,
    columns: u32,
    step: u32,
) {
    let Some(core) = core_mut(core) else {
        return;
    };
    core.mark_ui_mutated();
    core.ui.set_sprite(id, atlas, frames, columns, step);
}

#[no_mangle]
pub unsafe extern "C" fn pocketjs_core_measure_text(
    core: *mut PocketjsCore,
    ptr: *const u8,
    len: usize,
    font_slot: u32,
) -> f32 {
    let Some(core) = core_mut(core) else {
        return 0.0;
    };
    core.invalidate_frame_reuse();
    if font_slot > u8::MAX as u32 {
        return 0.0;
    }
    let Some(text) = input_text(ptr, len) else {
        return 0.0;
    };
    core.ui.measure_text(text, font_slot as u8)
}

#[no_mangle]
pub unsafe extern "C" fn pocketjs_core_tick(core: *mut PocketjsCore) {
    let Some(core) = core_mut(core) else {
        return;
    };
    core.mark_ui_mutated();
    core.ui.tick();
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
pub unsafe extern "C" fn pocketjs_core_draw_hash(core: *mut PocketjsCore) -> u64 {
    let Some(core) = core_mut(core) else {
        return 0;
    };
    core.invalidate_frame_reuse();
    let draw_list = core.rebuild_draw_list();
    fnv1a_words(&(*draw_list).words)
}

#[no_mangle]
pub unsafe extern "C" fn pocketjs_core_draw_word_count(core: *mut PocketjsCore) -> usize {
    let Some(core) = core_mut(core) else {
        return 0;
    };
    core.invalidate_frame_reuse();
    let draw_list = core.rebuild_draw_list();
    (*draw_list).words.len()
}

#[no_mangle]
pub unsafe extern "C" fn pocketjs_core_framebuffer_bytes(core: *mut PocketjsCore) -> usize {
    let Some(core) = core_mut(core) else {
        return 0;
    };
    core.invalidate_frame_reuse();
    let (width, height) = core.ui.viewport();
    (width as usize)
        .checked_mul(height as usize)
        .and_then(|pixels| pixels.checked_mul(core::mem::size_of::<u16>()))
        .unwrap_or(0)
}

unsafe fn render_rgb565(
    core: *mut PocketjsCore,
    framebuffer: *mut u16,
    len: usize,
    out_stats: *mut PocketjsRenderStats,
    incremental: bool,
) -> i32 {
    let Some(core) = core_mut(core) else {
        return 0;
    };
    core.invalidate_frame_reuse();
    if framebuffer.is_null() {
        return 0;
    }
    let (width, height) = core.ui.viewport();
    let Some(expected_pixels) = (width as usize).checked_mul(height as usize) else {
        return 0;
    };
    let Some(expected_bytes) = expected_pixels.checked_mul(core::mem::size_of::<u16>()) else {
        return 0;
    };
    if len != expected_bytes {
        return 0;
    }

    let draw_list = core.rebuild_draw_list();
    let ui_ptr: *const Ui = &core.ui;
    let Some(renderer) = core.renderer() else {
        return 0;
    };
    let output = slice::from_raw_parts_mut(framebuffer, expected_pixels);
    let mut ppa = EspPpaOps;
    let stats = if incremental {
        let Some(target) = core.render_target_state(framebuffer) else {
            return 0;
        };
        (*renderer).render_incremental(
            &mut *target,
            &*ui_ptr,
            &(*draw_list).words,
            output,
            width as u32,
            height as u32,
            &mut ppa,
        )
    } else {
        (*renderer).render(
            &*ui_ptr,
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
        core.invalidate_render_target(framebuffer);
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
    core: *mut PocketjsCore,
    framebuffer: *mut u16,
    len: usize,
    out_stats: *mut PocketjsRenderStats,
) -> i32 {
    render_rgb565(core, framebuffer, len, out_stats, false)
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
    core: *mut PocketjsCore,
    framebuffer: *mut u16,
    len: usize,
    out_stats: *mut PocketjsRenderStats,
) -> i32 {
    render_rgb565(core, framebuffer, len, out_stats, true)
}

/// Prepare one DrawList damage transaction for native framebuffer target
/// `0`/`1` or the headless target `2`.
///
/// The returned plan uses global logical viewport coordinates. Preparation
/// does not advance the target snapshot; call `commit`, `cancel`, or `abort`
/// before preparing another target.
#[no_mangle]
pub unsafe extern "C" fn pocketjs_core_prepare_rgb565_frame(
    core: *mut PocketjsCore,
    target_id: u32,
    out_plan: *mut PocketjsDamagePlan,
    out_stats: *mut PocketjsRenderStats,
) -> i32 {
    let Some(core) = core_mut(core) else {
        return 0;
    };
    // REUSABLE_FRAME is a one-shot token: even a failed prepare attempt
    // consumes it. If another transaction is still pending, this attempted
    // interleave also makes that transaction ineligible to arm a new token.
    let reusable = core.reusable_frame.take();
    if target_id >= MAX_FRAME_TARGETS as u32 || out_plan.is_null() || core.pending_frame.is_some() {
        if let Some(pending) = core.pending_frame.as_mut() {
            pending.cancel_reusable = false;
        }
        return 0;
    }
    let ui_ptr = &mut core.ui as *mut Ui;
    let ui_ref: &Ui = &*ui_ptr;
    let cached_draw_list = ui_ref.current_draw_list() as *const DrawList;
    let mutation_epoch = core.mutation_epoch;
    let (draw_list, fingerprint) = match reusable {
        Some(fingerprint)
            if fingerprint.matches_cached(ui_ref, &(*cached_draw_list).words, mutation_epoch) =>
        {
            (cached_draw_list, fingerprint)
        }
        _ => {
            let draw_list = core.rebuild_draw_list();
            let ui_ref = &*ui_ptr;
            let fingerprint =
                FrameFingerprint::capture(ui_ref, &(*draw_list).words, mutation_epoch);
            (draw_list, fingerprint)
        }
    };
    let ui_ref: &Ui = &*ui_ptr;
    let words = &(*draw_list).words;

    let Some(renderer) = core.renderer() else {
        return 0;
    };
    let Some(target) = core.frame_target_state(target_id) else {
        return 0;
    };
    let Some(plan) = (*renderer).prepare_damage(&*target, ui_ref, words) else {
        return 0;
    };
    debug_assert!(plan.region_count() <= POCKETJS_MAX_DAMAGE_REGIONS);
    *out_plan = PocketjsDamagePlan::from(&plan);
    if !out_stats.is_null() {
        *out_stats = damage_plan_stats(&plan, (*renderer).config().scale).into();
    }
    core.pending_frame = Some(PendingFrame {
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
    core: *mut PocketjsCore,
    framebuffer: *mut u16,
    len: usize,
    x: u32,
    y: u32,
    width: u32,
    height: u32,
    out_stats: *mut PocketjsRenderStats,
) -> i32 {
    let Some(core) = core_mut(core) else {
        return 0;
    };
    core.invalidate_frame_reuse();
    if framebuffer.is_null() || width == 0 || height == 0 {
        return 0;
    }
    let Some(pending) = core.pending_frame else {
        return 0;
    };
    let ui_ref: &Ui = &*(&core.ui as *const Ui);
    let words = &ui_ref.current_draw_list().words;
    if !pending
        .fingerprint
        .matches_cached(ui_ref, words, core.mutation_epoch)
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

    let Some(renderer) = core.renderer() else {
        return 0;
    };
    if (*renderer).config().scale != 1 {
        return 0;
    }
    let output = slice::from_raw_parts_mut(framebuffer, expected_pixels);
    let mut ppa = EspPpaOps;
    let Some(stats) = (*renderer).render_strip(
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
pub unsafe extern "C" fn pocketjs_core_commit_rgb565_frame(
    core: *mut PocketjsCore,
    target_id: u32,
) -> i32 {
    let Some(core) = core_mut(core) else {
        return 0;
    };
    core.invalidate_frame_reuse();
    let Some(pending) = core.pending_frame else {
        return 0;
    };
    if pending.target_id != target_id {
        if let Some(target) = core.frame_target_state(pending.target_id) {
            (*target).invalidate();
        }
        core.pending_frame = None;
        return 0;
    }
    let ui_ref: &Ui = &*(&core.ui as *const Ui);
    let words = &ui_ref.current_draw_list().words;
    let Some(target) = core.frame_target_state(target_id) else {
        core.pending_frame = None;
        return 0;
    };
    let current = FrameFingerprint::capture(ui_ref, words, core.mutation_epoch);
    if current != pending.fingerprint {
        (*target).invalidate();
        core.pending_frame = None;
        return 0;
    }
    let Some(renderer) = core.renderer() else {
        (*target).invalidate();
        core.pending_frame = None;
        return 0;
    };
    let committed = (*renderer).commit_damage(&mut *target, ui_ref, words);
    if !committed {
        (*target).invalidate();
    }
    core.pending_frame = None;
    committed as i32
}

/// Discard a prepared front-target probe without changing its retained damage
/// history.
#[no_mangle]
pub unsafe extern "C" fn pocketjs_core_cancel_rgb565_frame(
    core: *mut PocketjsCore,
    target_id: u32,
) -> i32 {
    let Some(core) = core_mut(core) else {
        return 0;
    };
    // A successful direct prepare -> cancel transition may arm exactly one
    // cached DrawList reuse. A wrong-target cancel permanently disqualifies
    // the pending frame.
    core.reusable_frame = None;
    let Some(pending) = core.pending_frame else {
        return 0;
    };
    if pending.target_id != target_id {
        if let Some(pending) = core.pending_frame.as_mut() {
            pending.cancel_reusable = false;
        }
        return 0;
    }
    core.pending_frame = None;
    if pending.cancel_reusable
        && pending.fingerprint.matches_cached(
            &core.ui,
            &core.ui.current_draw_list().words,
            core.mutation_epoch,
        )
    {
        core.reusable_frame = Some(pending.fingerprint);
    }
    1
}

/// Discard a failed/partial transaction and force the target's next prepare to
/// return a full redraw.
#[no_mangle]
pub unsafe extern "C" fn pocketjs_core_abort_rgb565_frame(core: *mut PocketjsCore, target_id: u32) {
    let Some(core) = core_mut(core) else {
        return;
    };
    core.invalidate_frame_reuse();
    if let Some(target) = core.frame_target_state(target_id) {
        (*target).invalidate();
    }
    if core
        .pending_frame
        .as_ref()
        .is_some_and(|pending| pending.target_id == target_id)
    {
        core.pending_frame = None;
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use core::ptr;

    unsafe fn prepare(core: *mut PocketjsCore, target_id: u32) -> PocketjsDamagePlan {
        let mut plan = PocketjsDamagePlan::default();
        assert_eq!(
            pocketjs_core_prepare_rgb565_frame(core, target_id, &mut plan, ptr::null_mut()),
            1
        );
        plan
    }

    #[test]
    fn frame_transactions_build_once_and_adjacent_cancel_reuses_cached_draw_list() {
        let core = pocketjs_core_create(32, 16, 1);
        assert!(!core.is_null());

        let _front_plan = unsafe { prepare(core, 0) };
        let (first_fingerprint, cached_words) = unsafe {
            let pending = (*core).pending_frame.expect("front probe must be pending");
            (
                pending.fingerprint,
                (*core).ui.current_draw_list().words.as_ptr(),
            )
        };
        assert_eq!(unsafe { (*core).draw_rebuild_count }, 1);
        assert_eq!(unsafe { pocketjs_core_cancel_rgb565_frame(core, 0) }, 1);
        assert_eq!(unsafe { (*core).reusable_frame }, Some(first_fingerprint));

        // This is the intended front-buffer probe -> back-buffer prepare
        // transition. It consumes the token and keeps the exact cached list.
        let back_plan = unsafe { prepare(core, 1) };
        assert_eq!(unsafe { (*core).draw_rebuild_count }, 1);
        assert_eq!(back_plan.region_count, 1);
        assert_eq!(back_plan.full_redraw, 1);
        unsafe {
            assert_eq!((*core).reusable_frame, None);
            assert_eq!((*core).ui.current_draw_list().words.as_ptr(), cached_words);
            assert_eq!(
                (*core).pending_frame.unwrap().fingerprint,
                first_fingerprint
            );
        }

        let mut strip = [0u16; 32 * 16];
        assert_eq!(
            unsafe {
                pocketjs_core_render_rgb565_strip(
                    core,
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
        assert_eq!(unsafe { pocketjs_core_commit_rgb565_frame(core, 1) }, 1);
        assert_eq!(
            unsafe { (*core).draw_rebuild_count },
            1,
            "changed frame must build only during its front prepare"
        );

        // An unchanged frame also builds once to discover that its current
        // front target is already up to date; commit validates the cache.
        let unchanged = unsafe { prepare(core, 1) };
        assert_eq!(unchanged.region_count, 0);
        assert_eq!(unsafe { pocketjs_core_commit_rgb565_frame(core, 1) }, 1);
        assert_eq!(
            unsafe { (*core).draw_rebuild_count },
            2,
            "unchanged commit must not rebuild the DrawList"
        );

        // The token is strict adjacency state, not a general DrawList cache:
        // even a read-only exported call between cancel and prepare consumes
        // the opportunity and forces one rebuild.
        let _front_plan = unsafe { prepare(core, 0) };
        assert_eq!(unsafe { (*core).draw_rebuild_count }, 3);
        assert_eq!(unsafe { pocketjs_core_cancel_rgb565_frame(core, 0) }, 1);
        assert_eq!(unsafe { (*core).reusable_frame }, Some(first_fingerprint));
        assert_eq!(
            unsafe { pocketjs_core_framebuffer_bytes(core) },
            32 * 16 * 2
        );
        assert_eq!(unsafe { (*core).reusable_frame }, None);
        let _headless_plan = unsafe { prepare(core, 2) };
        assert_eq!(unsafe { (*core).draw_rebuild_count }, 4);

        // UI mutations independently invalidate a freshly armed token.
        assert_eq!(unsafe { pocketjs_core_cancel_rgb565_frame(core, 2) }, 1);
        unsafe { pocketjs_core_tick(core) };
        assert_eq!(unsafe { (*core).reusable_frame }, None);
        let _next_plan = unsafe { prepare(core, 0) };
        assert_eq!(unsafe { (*core).draw_rebuild_count }, 5);

        unsafe {
            pocketjs_core_abort_rgb565_frame(core, 0);
            pocketjs_core_destroy(core);
        }
    }
}
