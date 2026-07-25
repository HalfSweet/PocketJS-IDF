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

- PocketJS framework/tooling is pinned to
  `49726ab31cf1f55f1439eb19b3b6e1ad0260ae88`.
- The vendored Core and ESP32-P4 PPA port is based on
  `5a5ffc78355aeee906c978884d6d5b83d6386b74`.
- Minimum ESP-IDF is 5.4 and QuickJS-NG is pinned to component version 0.14.0.
- Build Rust for `riscv32imafc-unknown-none-elf` with the checked-in lockfile.
- ESP-IDF PPA RGB565 fixed fills must use expanded ARGB components through
  `fill_argb_color`, never a packed RGB565 word through `fill_color_val`.

## Verification

- Run Rust workspace tests, package/tooling tests, component-manager lint and
  pack checks, and full ESP-IDF link tests.
- Do not claim on-device verification without flashing and observing hardware.
- Do not tag a release until all CI and hardware release gates pass.

