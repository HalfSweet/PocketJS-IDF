# PocketJS source provenance

The PocketJS Core and ESP32-P4 PPA backend are copied from the local fork:

- Fork: https://github.com/HalfSweet/pocketjs
- Local branch: `codex/esp32p4-ppa-rgb565`
- Commit: `5a5ffc78355aeee906c978884d6d5b83d6386b74`
- Based on upstream commit:
  `49726ab31cf1f55f1439eb19b3b6e1ad0260ae88`
- Source directories: `engine/core` and
  `engine/backends/esp32p4-ppa`
- License: MIT; see `licenses/POCKETJS-MIT.txt`

`rust/pocketjs-core` and the PPA backend remain based on the same commit. They
additionally carry the generic Core `DamageTracker`, incremental
RGBA/ARGB/RGB565 software raster APIs, raster-resource revisions, and the PPA
integration ported from local fork commits
`dc00bf00d2637cc0a36682f844b29d924abf7104` and
`65e2e27949338a29de3901a1c4108d8fa93309cd` on
`feat/esp32p4-dirty-regions`. The vendored backend `Cargo.toml` only adjusts
its relative Core dependency for this component layout and omits standalone
development workspace metadata.

This component additionally carries the transactional
damage-plan/strip-render extension originally used by the Tab5 partial
presenter. It adds compact RGB565 window fallback rendering, strip-local PPA
coordinate translation, and separate prepare/commit/abort target-history
operations. These changes are local to this ESP32-P4 integration until they
are reconciled back into the fork.

PocketJS JavaScript/framework build tooling remains pinned to the base
upstream commit so generated Vue Vapor and Solid artifacts stay reproducible.
It is checked out unchanged from
`https://github.com/pocket-stack/pocketjs.git` as the `vendor/pocketjs`
submodule. The component copies only the required toolchain directories into
the ESP-IDF build directory, then applies an exact, fail-on-drift overlay
there: external Solid applications are forced to share the pinned browser and
universal `solid-js` modules with the framework. The submodule worktree itself
remains unmodified.

The ESP32-P4 C FFI, allocator, and ESP-IDF PPA driver bridge live outside the
upstream submodule. This keeps platform integration reviewable and makes a
future upstream update an explicit gitlink change followed by a
checksum/diff review.
