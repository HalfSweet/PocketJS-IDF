# Third-party software

## PocketJS

- Repository: <https://github.com/pocket-stack/pocketjs>
- Framework, compiler, package contract, Core, and ESP32-P4 PPA backend
  commit: `a4e154789655cafa9dc0f57a8c83fc2114d74776`
- Merged review:
  <https://github.com/pocket-stack/pocketjs/pull/190>
- License: MIT; see `licenses/POCKETJS-MIT.txt`.

The complete upstream checkout is the `vendor/pocketjs` submodule. The
component consumes `engine/core` and `engine/backends/esp32p4-ppa` directly;
there is no second copied Core or renderer crate. Component packing flattens
the required checked-out files into the Registry archive and excludes Git
metadata, upstream tests, and build artifacts. Component-specific C and Rust
ABI wrappers remain in `src/` and `rust/src/`.

## QuickJS-NG

QuickJS-NG is not vendored. ESP-IDF Component Manager resolves
`espressif/quickjs-ng` version `0.14.0` from the ESP Component Registry.
Its component declares `MIT AND Apache-2.0`.
