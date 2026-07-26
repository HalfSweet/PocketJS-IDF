# Changelog

## 0.1.0-rc.1 — unreleased

- Package PocketJS Core, QuickJS runtime, RGB565 renderer, logical display,
  and application tooling as one ESP-IDF component.
- Add instance-owned Core state and transactional target-specific damage
  history.
- Add LVGL-style `PARTIAL`, `DIRECT`, and `FULL` display modes with
  asynchronous flush completion.
- Accept only verified `esp32p4-idf` host ABI 1 `.pocket` applications.
- Add version-pinned QuickJS extensions for board-provided capabilities.
- Add deterministic source compilation and prebuilt package embedding.
- Pin the official PocketJS framework/compiler checkout as a Git submodule;
  Registry archives contain the required flattened sources without VCS
  metadata.
- Add ESP-IDF 5.4.4/6.0.2, archive-consumer, and OIDC release workflows.

Tab5 hardware acceptance completed on 2026-07-26. Registry publication setup
remains pending.
