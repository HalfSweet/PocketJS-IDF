# AGENTS.md

## Component boundaries

- The repository root is one ESP-IDF component.
- ESP32-P4 and RGB565 are the only v1 target and pixel format.
- Keep board, panel, touch, sensor, rotation, scaling, and physical
  framebuffer implementations out of the component.
- The component owns PocketJS, QuickJS, logical framebuffer damage, draw
  buffer scheduling, and flush lifecycle.
- Public API is limited to `pocketjs.h`, `pocketjs_display.h`, and
  `pocketjs_quickjs.h`.
- v1 supports one runtime, one application, and one attached display.
- Draw buffers are caller-owned and must never be allocated or freed by the
  component.
- Only upstream `.pocket` packages are accepted by the public runtime.

## Upstream and target constraints

- PocketJS framework/tooling, Core, and ESP32-P4 PPA backend are sourced
  directly from the `HalfSweet/pocketjs` downstream submodule pinned to
  `a4e154789655cafa9dc0f57a8c83fc2114d74776`.
- Keep the component-owned `rust/` crate limited to the C ABI, allocator, and
  ESP-IDF driver bridges. Do not copy upstream Rust crates into it.
- Minimum ESP-IDF is 5.4 and QuickJS-NG is pinned to component version 0.14.0.
- Build Rust for `riscv32imafc-unknown-none-elf` with the checked-in lockfile.
- ESP-IDF PPA RGB565 fixed fills must use expanded ARGB components through
  `fill_argb_color`, never a packed RGB565 word through `fill_color_val`.

## Verification

- Run the component Rust tests, the upstream Core and PPA backend tests,
  package/tooling tests, component-manager lint and pack checks, and full
  ESP-IDF link tests.
- Do not claim on-device verification without flashing and observing hardware.
- Do not tag a release until all CI and hardware release gates pass.
