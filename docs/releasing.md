# Release process

`v0.1.1` changes only the PocketJS submodule source URL and Registry
authentication. It keeps the exact PocketJS revision and component runtime
code released in `v0.1.0`. The existing software and Tab5 acceptance records
remain applicable; this release does not claim a new device run.

## Registry prerequisite

Before creating the tag, a maintainer must sign in to the ESP Component
Registry and confirm all of the following:

- namespace: `halfsweet`;
- component: `pocketjs-idf`;
- trusted uploader repository: `HalfSweet/PocketJS-IDF`;
- trusted uploader branch and environment: unset, so annotated tag workflows
  can publish.

The publish job grants `id-token: write` only to the Registry upload job.
`espressif/upload-components-ci-action@v2` exchanges the GitHub OIDC identity
for short-lived Registry authorization; no long-lived Registry token or
Actions secret is used.

## Software gates

- [x] component adapter `cargo fmt --check`;
- [x] component adapter `cargo test --locked`;
- [x] upstream Core 100-test suite;
- [x] upstream ESP32-P4 PPA 20-test suite and `esp-idf` feature check;
- [x] pinned Bun frozen install and encoder tests;
- [x] upstream PocketJS submodule can be cloned from
  `pocket-stack/pocketjs` at
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

The `v0.1.0` source migration rebuilt all three images but was not flashed
again because the device was unavailable. Since `v0.1.1` changes neither the
pinned PocketJS tree nor component runtime code, it retains that acceptance
record without claiming a separate device run.

## Tag and publish

After the PR is merged to `main` and every gate is checked:

```sh
git switch main
git pull --ff-only
git tag -a v0.1.1 -m "PocketJS-IDF v0.1.1"
git push origin v0.1.1
```

An existing tag can be revalidated and published without moving it:

```sh
gh workflow run publish.yml \
  --ref main \
  -f tag=v0.1.1
```

`publish.yml` rejects non-semver tags, lightweight tags, and commits not
contained in `origin/main`. It repeats source/package tests, packs the tagged
component, builds an unpacked archive consumer with ESP-IDF 6.0.2, and only
then invokes `espressif/upload-components-ci-action@v2` with GitHub OIDC.

## Registry verification

After publishing, create a new project outside both repositories with:

```yaml
dependencies:
  halfsweet/pocketjs-idf:
    version: "0.1.1"
```

Build and confirm the link and application `.bin` without an
`override_path`, Git dependency, or workspace files. Only then remove the
Tab5 development override and pin the same exact version.
