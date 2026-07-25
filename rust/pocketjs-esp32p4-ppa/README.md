# PocketJS ESP32-P4 PPA backend

This `no_std` crate interprets PocketJS DrawLists directly into an RGB565
surface. It accelerates operations that map exactly to the ESP32-P4 Pixel
Processing Accelerator and preserves DrawList order with an RGB565 software
fallback for everything else.

The crate deliberately does not depend on ESP-IDF or a board support package.
An ESP-IDF host implements `PpaOps` with `ppa_do_fill`, `ppa_do_blend`, and
`ppa_do_scale_rotate_mirror`. Board-specific display presentation remains in
the BSP.

Accelerated paths:

- opaque and translucent flat rectangles (`FILL` or A8 `BLEND`);
- antialiased font runs (`A8` coverage composed once, then `BLEND`);
- single-color alpha textures such as PocketJS rounded-corner masks (`A8`
  `BLEND`);
- opaque PSM 5650 texture quads (`SRM`) when scaling semantics are compatible.

Gradients, arbitrary triangles, textured triangles, and unsupported texture
formats fall back to `pocketjs_core::raster::render_scaled_rgb565_over`.
No full-frame RGB888 or ARGB8888 surface is allocated.

## Incremental rendering

`Renderer::render_incremental` uses the backend-independent
`pocketjs_core::damage::DamageTracker` to compare the current DrawList with
the DrawList that produced a persistent RGB565 target. Changed operation
bounds are collected into up to 8 disjoint logical damage rectangles. Each
rectangle is cleared and the current DrawList is replayed through that
rectangle's scissor, preserving normal painter order and translucent
composition without touching unchanged pixels.

Keep one `RenderTargetState` per framebuffer. This is required for
double-buffered hosts because each target contains a different older frame.
The first render and structural DrawList changes use a conservative full
redraw. This backend additionally promotes damage covering at least 75
percent of the viewport; that transaction-cost policy is deliberately kept
outside the common damage planner.

Core-managed texture, font, and style mutations bump `Ui::raster_revision()`,
so every `RenderTargetState` automatically forces a complete repaint and the
renderer drops texture classifications even when DrawList handles stay
unchanged. Call `Renderer::invalidate_resources()` and explicitly invalidate
target states only for output-affecting changes performed outside `Ui`.

## Transactional strip rendering

`prepare_damage`, `render_strip`, and `commit_damage` split damage planning,
pixel generation, and target-history advancement into one transaction. A host
can therefore render only a full-width row strip that intersects a dirty
rectangle, submit that compact buffer to a board-specific presenter, and
commit the persistent target only after every regional update succeeds.
Canceling a read-only front-buffer probe preserves its history; aborting a
partially updated target invalidates that history so its next transaction is a
full redraw.

Strip rendering retains global logical coordinates for clipping and software
fallback, while translating destination rectangles and A8 mask accesses to
strip-local coordinates. The reusable mask grows only to the largest strip
seen rather than to the full viewport. Persistent target identity belongs to
the host's final framebuffer (for Tab5, native MIPI framebuffer 0 or 1), not
to the transient strip address.
