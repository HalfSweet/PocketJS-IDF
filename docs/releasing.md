# Release process

`v0.1.0` is the first stable release. The maintainer explicitly approved
publishing it on 2026-07-26 after the source migration, local release checks,
and all three Tab5 link builds completed.

## Registry prerequisite

Before creating the tag, a maintainer must sign in to the ESP Component
Registry and confirm all of the following:

- namespace: `halfsweet`;
- component: `pocketjs-idf`;
- repository Actions secret: `IDF_COMPONENTS_TOKEN`.

The publish job passes this secret only to
`espressif/upload-components-ci-action@v2`. GitHub OIDC publication is tracked
upstream in
[espressif/upload-components-ci-action#35](https://github.com/espressif/upload-components-ci-action/issues/35)
because repositories created with immutable OIDC subjects are currently
rejected by the Registry trusted-uploader path.

## Software gates

- [x] component adapter `cargo fmt --check`;
- [x] component adapter `cargo test --locked`;
- [x] upstream Core 100-test suite;
- [x] upstream ESP32-P4 PPA 20-test suite and `esp-idf` feature check;
- [x] pinned Bun frozen install and encoder tests;
- [x] downstream PocketJS submodule can be cloned from
  `HalfSweet/pocketjs` at
  `a4e154789655cafa9dc0f57a8c83fc2114d74776`;
- [x] deterministic `.pocket` generation;
- [x] Component Manager warning-as-error pack;
- [x] archive contains the required flattened PocketJS sources and no VCS
  metadata, duplicate Rust crates, cache, build, Cargo target, or
  `node_modules`;
- [x] unpacked archive consumer links and produces a firmware `.bin`;
- [x] prebuilt and source-build examples link with local ESP-IDF 6.1;
- [x] CI definitions cover the prebuilt example on ESP-IDF 5.4.4 and 6.0.2;
- [x] CI definitions cover the source-build example on ESP-IDF 6.0.2;
- [x] the current Tab5 consumer fully links with local ESP-IDF 6.1.

Rustfmt applies to the component-owned FFI crate. Upstream Core and PPA source
is tested directly from the pinned submodule checkout.

## Tab5 hardware record

Build, flash, and observe Vue Vapor, Solid, and Motion Lab separately on an
ESP32-P4 rev 1.3 M5Stack Tab5.

- [x] each firmware keeps the rev-less-than-v3 target selection;
- [x] CPU remains 360 MHz;
- [x] main task stack remains 128 KiB;
- [x] partition table offset remains `0x10000`;
- [x] all three firmware images link and generate `.bin` files;
- [x] firmware sizes are recorded: Vue 1,793,632 bytes, Solid 1,647,120 bytes,
  and Motion Lab 1,806,944 bytes.

The rc.1 firmware ran all three applications for at least 960 frames. Vue and
Solid received live BMI270 data; serial logs contained no PPA, display
transaction, memory, QuickJS, IMU, or task-watchdog errors. Orientation,
RGB565 color, rounded corners, fractional scaling, dirty regions, tearing,
and animation continuity received maintainer visual approval on 2026-07-26.

The stable source migration rebuilt all three images but was not flashed
again because the device was unavailable. The maintainer explicitly approved
publishing `0.1.0` with the existing device record. This document therefore
does not claim a separate stable-tag device run.

## Tag and publish

After the PR is merged to `main` and every gate is checked:

```sh
git switch main
git pull --ff-only
git tag -a v0.1.0 -m "PocketJS-IDF v0.1.0"
git push origin v0.1.0
```

An existing tag can be revalidated and published without moving it:

```sh
gh workflow run publish.yml \
  --ref main \
  -f tag=v0.1.0
```

`publish.yml` rejects non-semver tags, lightweight tags, and commits not
contained in `origin/main`. It repeats source/package tests, packs the tagged
component, builds an unpacked archive consumer with ESP-IDF 6.0.2, and only
then invokes `espressif/upload-components-ci-action@v2` with the Registry
token.

## Registry verification

After publishing, create a new project outside both repositories with:

```yaml
dependencies:
  halfsweet/pocketjs-idf:
    version: "0.1.0"
```

Build and confirm the link and application `.bin` without an
`override_path`, Git dependency, or workspace files. Only then remove the
Tab5 development override and pin the same exact version.
