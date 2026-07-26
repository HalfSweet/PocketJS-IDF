# PocketJS source provenance

- Repository: https://github.com/pocket-stack/pocketjs
- Downstream repository: https://github.com/HalfSweet/pocketjs
- Commit: `a4e154789655cafa9dc0f57a8c83fc2114d74776`
- Based on official commit:
  `e8a8e807da7d74b98dde9cb604b6a67e6735a87b`
- Upstream review: https://github.com/pocket-stack/pocketjs/pull/190
- Submodule path: `vendor/pocketjs`
- License: MIT; see `licenses/POCKETJS-MIT.txt`

The component consumes these source trees directly from that exact checkout:

- `contracts`, `framework`, and `tools` for deterministic `.pocket`
  compilation;
- `engine/core` for retained UI, damage tracking, and software rasterization;
- `engine/backends/esp32p4-ppa` for transactional target history, compact
  RGB565 strip rendering, PPA operation routing, and ordered fallback.

The Registry archive contains the required files from the checkout without
submodule or other Git metadata. It deliberately contains no copied
`rust/pocketjs-core` or `rust/pocketjs-esp32p4-ppa` tree.

The component-owned `rust/src/lib.rs` is a narrow C ABI and lifecycle adapter.
The allocator, ESP-IDF PPA driver bridge, display scheduler, and QuickJS host
remain outside the upstream submodule because they implement the ESP-IDF
component contract rather than platform-independent PocketJS behavior.
