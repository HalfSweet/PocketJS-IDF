# PocketJS-IDF

PocketJS runtime and RGB565 logical framebuffer management as one reusable
ESP-IDF component for ESP32-P4.

> [!IMPORTANT]
> `0.1.0-rc.1` is a release candidate. The component and its clean consumers
> build independently, and all three Tab5 images passed both the 960-frame
> serial gate and human visual inspection. See
> [docs/releasing.md](docs/releasing.md) for the complete release record.

The component owns:

- one PocketJS/QuickJS runtime and one `.pocket` application;
- one RGB565 logical display;
- dirty-region history, one-pixel damage halo, U8.4 phase alignment, chunking,
  caller-provided strip reuse, and flush transactions;
- PPA FILL, A8 BLEND, PSM5650 SRM, and ordered software fallbacks;
- advanced QuickJS extensions for board features such as an IMU.

The board project continues to own the panel, touch, sensors, rotation,
scaling, physical framebuffers, DMA, and swap/retirement policy.

## Compatibility

| Item | v0.1 contract |
| --- | --- |
| SoC | ESP32-P4 only |
| ESP-IDF | `>=5.4`; CI: 5.4.4 and 6.0.2 |
| QuickJS | `espressif/quickjs-ng` 0.14.0 |
| Rust target | `riscv32imafc-unknown-none-elf` |
| Raster format | RGB565 |
| Runtime/display | one application, one runtime, one attached display |
| Input | button bitmap and signed analog X/Y |

## Clone for development

The official PocketJS framework/compiler source is a Git submodule fixed to
commit `49726ab31cf1f55f1439eb19b3b6e1ad0260ae88`:

```sh
git clone --recurse-submodules \
  https://github.com/HalfSweet/PocketJS-IDF.git
cd PocketJS-IDF
git submodule update --init --recursive
```

The submodule URL is
`https://github.com/pocket-stack/pocketjs.git`. Component packing flattens its
required source files into the Registry archive, so Registry consumers do not
need Git or submodule initialization.

## Add the component

After the RC is published, add this to the consumer component's
`idf_component.yml`:

```yaml
dependencies:
  halfsweet/pocketjs-idf:
    version: "0.1.0-rc.1"
    pre_release: true
```

For local development, temporarily add:

```yaml
    override_path: "/absolute/path/to/pocketjs-idf"
```

The final directory must be named `pocketjs-idf` or
`halfsweet__pocketjs-idf`, as required by ESP-IDF dependency resolution.

Install the Rust target once:

```sh
rustup target add riscv32imafc-unknown-none-elf
```

The normal ESP-IDF build requires Cargo. It never installs host tools
automatically.

## Embed an application

Public runtime input is an official `.pocket` package for target
`esp32p4-idf`, host ABI `1`, platform `esp-idf`.

### Prebuilt package (no Bun required)

```cmake
idf_component_register(SRCS "main.c")

pocketjs_embed_package(
    TARGET ${COMPONENT_LIB}
    NAME dashboard
    PACKAGE "app/dashboard.pocket"
)
```

This validates the package and generates
`pocketjs_app_dashboard.h` plus the `pocketjs_app_dashboard` descriptor in
the ESP-IDF build directory.

### Compile application source

First install the pinned compiler dependencies:

```sh
bun install --cwd managed_components/halfsweet__pocketjs-idf/vendor/pocketjs \
  --frozen-lockfile
```

Then use:

```cmake
pocketjs_compile_app(
    TARGET ${COMPONENT_LIB}
    NAME dashboard
    MANIFEST "app/pocket.json"
)
```

The helper compiles a deterministic `esp32p4-idf` ABI 1 `.pocket`, writes a
depfile, and generates the same C descriptor. All outputs stay under the
ESP-IDF build directory. Missing Bun or dependencies produce an actionable
error and are never installed implicitly.

## Run headless

```c
#include "pocketjs.h"
#include "pocketjs_app_dashboard.h"

const pocketjs_config_t config = POCKETJS_DEFAULT_CONFIG();
pocketjs_t *runtime = NULL;
ESP_ERROR_CHECK(pocketjs_create(
    &pocketjs_app_dashboard,
    &config,
    &runtime
));

pocketjs_frame_stats_t stats = {0};
ESP_ERROR_CHECK(pocketjs_run_frame(runtime, NULL, &stats));
pocketjs_destroy(runtime);
```

The package bytes must remain readable until `pocketjs_destroy()` returns.
The caller chooses the task, core affinity, frame rate, and watchdog delay.

## Attach a display

Create a logical RGB565 display object, supply one or two caller-owned
128-byte-aligned draw buffers, and register a mandatory `flush` callback:

```c
pocketjs_display_t *display = NULL;
ESP_ERROR_CHECK(pocketjs_display_create(680, 360, &display));
ESP_ERROR_CHECK(pocketjs_display_set_buffers(
    display,
    strip_a,
    strip_b,
    sizeof(strip_a),
    POCKETJS_DISPLAY_RENDER_MODE_PARTIAL
));
ESP_ERROR_CHECK(pocketjs_display_set_callbacks(
    display,
    &callbacks,
    board,
    1000
));
ESP_ERROR_CHECK(pocketjs_attach_display(runtime, display));
```

`flush` receives a half-open logical area, immutable RGB565 pixels, stride,
stable target ID, and `is_last`. A synchronous driver calls
`pocketjs_display_flush_ready()` before returning. An asynchronous driver
later calls that function from the owner task or
`pocketjs_display_flush_ready_from_isr()` from an ISR.

The modes are:

- `PARTIAL`: one or two buffers of at least one complete logical row;
- `DIRECT`: one or two complete logical framebuffers, updated in place using
  stable target-specific damage history;
- `FULL`: one or two complete framebuffers, redrawn and submitted every frame.

Optional `begin_frame`, `end_frame`, and `abort_frame` callbacks expose stable
front/back target IDs and phase-alignment requirements for physical
double-buffer drivers. Rotation, scaling, physical addresses, and DPI swaps
remain entirely inside those callbacks.

Except for `pocketjs_display_flush_ready_from_isr()`, all APIs are
non-reentrant and must be called from one owner task on one core.

## QuickJS extensions

Include `pocketjs_quickjs.h` only for version-pinned native extensions.
Extensions can install globals, update them in `before_frame`, and release
state in `destroy`. `JSContext *` is valid only during those callbacks.
Pending exceptions and rejected promises make the current frame fail, are
logged and cleared, and do not permanently disable later frames.

The Tab5 BMI270 bridge belongs in this layer; it is intentionally not part of
the generic runtime.

## Examples and documentation

- [`examples/prebuilt`](examples/prebuilt): embeds a committed `.pocket`
  without Bun.
- [`examples/source`](examples/source): compiles a Solid application during
  the ESP-IDF build.
- [API reference](API.md)
- [Architecture](docs/architecture.md)
- [Release and hardware gates](docs/releasing.md)
- [Vendored-source provenance](THIRD_PARTY.md)

Both examples execute one headless frame. Display callbacks remain
board-specific and are exercised by the Tab5 consumer repository.
