# Release process

No release is published merely because the software CI is green. The first
allowed tag is the annotated tag `v0.1.0-rc.1`, after all gates below pass.

## Registry prerequisite

Before creating the tag, a maintainer must sign in to the ESP Component
Registry and confirm all of the following:

- namespace: `halfsweet`;
- component: `pocketjs-idf`;
- trusted uploader repository: `HalfSweet/PocketJS-IDF`;
- trusted uploader workflow: `publish.yml`;
- branch restriction: unset.

The publish job uses GitHub OIDC and `id-token: write`; no long-lived registry
token is stored. Stop before tagging if the trusted uploader is not visible.

## Software gates

- [ ] adapter `cargo fmt --check`;
- [ ] complete Rust workspace `cargo test --locked`;
- [ ] pinned Bun frozen install and encoder tests;
- [ ] official PocketJS submodule is initialized at
  `49726ab31cf1f55f1439eb19b3b6e1ad0260ae88`;
- [ ] deterministic `.pocket` generation;
- [ ] Component Manager warning-as-error pack;
- [ ] archive contains the required flattened PocketJS sources and no VCS
  metadata, cache, build, Cargo target, or `node_modules`;
- [ ] unpacked archive consumer links and produces a firmware `.bin`;
- [ ] prebuilt example builds with ESP-IDF 5.4.4 and 6.0.2;
- [ ] source-build example builds with ESP-IDF 6.0.2;
- [ ] the current Tab5 consumer fully links with local ESP-IDF 6.1.

The vendored upstream Core is kept in its pinned formatting. Rustfmt applies
to the component-owned FFI crate and PPA adapter; workspace tests cover all
three crates.

## Tab5 hardware gates

Build, flash, and observe Vue Vapor, Solid, and Motion Lab separately on an
ESP32-P4 rev 1.3 M5Stack Tab5.

- [ ] each firmware keeps the rev-less-than-v3 target selection;
- [ ] CPU remains 360 MHz;
- [ ] main task stack remains 128 KiB;
- [ ] partition table offset remains `0x10000`;
- [ ] all three firmware images link and generate `.bin` files;
- [ ] all three run for at least 960 frames;
- [ ] Vue and Solid receive live BMI270 values through a QuickJS extension;
- [ ] orientation and RGB565 colors are visually correct;
- [ ] rounded corners and 1.875x fractional-scale boundaries have no seams;
- [ ] dirty regions leave no stale pixels;
- [ ] no visible tearing or animation discontinuity;
- [ ] serial output has no PPA, display transaction, memory, QuickJS, IMU, or
  task-watchdog error;
- [ ] profiler values and firmware sizes are recorded in the Tab5 repository.

Do not claim device verification until these observations are recorded.

## Tag and publish

After the PR is merged to `main` and every gate is checked:

```sh
git switch main
git pull --ff-only
git tag -a v0.1.0-rc.1 -m "PocketJS-IDF v0.1.0-rc.1"
git push origin v0.1.0-rc.1
```

`publish.yml` rejects non-semver tags, lightweight tags, and commits not
contained in `origin/main`. It repeats source/package tests, packs the tagged
component, builds an unpacked archive consumer with ESP-IDF 6.0.2, and only
then invokes `espressif/upload-components-ci-action@v2`.

## Registry verification

After publishing, create a new project outside both repositories with:

```yaml
dependencies:
  halfsweet/pocketjs-idf:
    version: "0.1.0-rc.1"
    pre_release: true
```

Build and confirm the link and application `.bin` without an
`override_path`, Git dependency, or workspace files. Only then remove the
Tab5 development override and pin the same exact version.
