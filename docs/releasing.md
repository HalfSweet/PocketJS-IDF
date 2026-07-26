# Release process

No release is published merely because the software checks are green. The
current candidate is `v0.1.0-rc.2`; create its annotated tag only after every
gate below passes.

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
- [ ] prebuilt example builds with ESP-IDF 5.4.4 and 6.0.2 in CI;
- [ ] source-build example builds with ESP-IDF 6.0.2 in CI;
- [x] the current Tab5 consumer fully links with local ESP-IDF 6.1.

Rustfmt applies to the component-owned FFI crate. Upstream Core and PPA source
is tested directly from the pinned submodule checkout.

## Tab5 hardware gates

Build, flash, and observe Vue Vapor, Solid, and Motion Lab separately on an
ESP32-P4 rev 1.3 M5Stack Tab5.

- [x] each firmware keeps the rev-less-than-v3 target selection;
- [x] CPU remains 360 MHz;
- [x] main task stack remains 128 KiB;
- [x] partition table offset remains `0x10000`;
- [x] all three firmware images link and generate `.bin` files;
- [ ] all three run for at least 960 frames;
- [ ] Vue and Solid receive live BMI270 values through a QuickJS extension;
- [ ] orientation and RGB565 colors are visually correct;
- [ ] rounded corners and 1.875x fractional-scale boundaries have no seams;
- [ ] dirty regions leave no stale pixels;
- [ ] no visible tearing or animation discontinuity;
- [ ] serial output has no PPA, display transaction, memory, QuickJS, IMU, or
  task-watchdog error;
- [ ] profiler values are recorded in the Tab5 repository.
- [x] firmware sizes are recorded: Vue 1,793,632 bytes, Solid 1,647,120 bytes,
  and Motion Lab 1,806,944 bytes.

The rc.1 observations and maintainer visual approval were recorded on
2026-07-26. They do not replace the pending rc.2 device run.

## Tag and publish

After the PR is merged to `main` and every gate is checked:

```sh
git switch main
git pull --ff-only
git tag -a v0.1.0-rc.2 -m "PocketJS-IDF v0.1.0-rc.2"
git push origin v0.1.0-rc.2
```

An existing tag can be revalidated and published without moving it:

```sh
gh workflow run publish.yml \
  --ref main \
  -f tag=v0.1.0-rc.2
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
    version: "0.1.0-rc.2"
    pre_release: true
```

Build and confirm the link and application `.bin` without an
`override_path`, Git dependency, or workspace files. Only then remove the
Tab5 development override and pin the same exact version.
