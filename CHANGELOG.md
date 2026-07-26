# Changelog

## 0.1.0-rc.2 — 2026-07-26

- Consume PocketJS Core and the ESP32-P4 PPA backend directly from a pinned
  `HalfSweet/pocketjs` downstream submodule while upstream review continues.
- Upstream transactional damage prepare/commit/abort, compact RGB565 strip
  rendering, and strip-local PPA coordinate translation.
- Remove the duplicate component-local Core/backend crates and test fixtures.
- Remove the obsolete Solid compiler overlay now provided by PocketJS itself.
- Test the upstream Core and PPA crates in CI and verify that Registry archives
  contain those sources without duplicate Rust trees.

## 0.1.0-rc.1 — 2026-07-26

- Package PocketJS Core, QuickJS runtime, RGB565 renderer, logical display,
  and application tooling as one ESP-IDF component.
- Add instance-owned Core state and transactional target-specific damage
  history.
- Add caller-owned-buffer `PARTIAL`, `DIRECT`, and `FULL` display modes with
  asynchronous flush completion.
- Accept only verified `esp32p4-idf` host ABI 1 `.pocket` applications.
- Add version-pinned QuickJS extensions for board-provided capabilities.
- Add deterministic source compilation and prebuilt package embedding.
- Pin the official PocketJS framework/compiler checkout as a Git submodule;
  Registry archives contain the required flattened sources without VCS
  metadata.
- Add ESP-IDF 5.4.4/6.0.2, archive-consumer, and Registry-token release
  workflows.

Tab5 hardware acceptance completed on 2026-07-26.
