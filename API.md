# PocketJS-IDF API

The public API consists of exactly three headers:

- `pocketjs.h`: runtime, application, input, frame execution, and statistics;
- `pocketjs_display.h`: logical display and flush transactions;
- `pocketjs_quickjs.h`: advanced, QuickJS-version-pinned extensions.

## Runtime lifecycle

`pocketjs_create()` validates the generated descriptor and the complete
`.pocket` package: footer and variant hashes, target `esp32p4-idf`, host ABI
1, build-plan checksum, viewport, raster density, RGB565 contract, JavaScript
termination, and the PAK container. Package storage remains caller-owned.

Only one display may be attached. A second attachment returns
`ESP_ERR_INVALID_STATE`. Detaching keeps the runtime and application alive
and returns to headless execution. Destroying a runtime never frees
application package bytes, display buffers, or board resources.

`pocketjs_run_frame()` invokes extensions, JavaScript, the Core tick,
rendering, and display submission once. The optional input contains:

- `buttons`: application-defined bit map;
- `analog_x`, `analog_y`: signed analog direction values.

The returned statistics contain JavaScript, Core, render, flush-wait, and
total microseconds; damage region/pixel counts; full-redraw status; and
PPA/software operation counts.

If a display transaction fails, the component aborts it, invalidates the
affected target history, and returns the error. The next successful
transaction forces a complete redraw.

## Display configuration

Configuration order:

1. `pocketjs_display_create(width, height, &display)`;
2. `pocketjs_display_set_buffers(...)`;
3. `pocketjs_display_set_callbacks(...)`;
4. `pocketjs_attach_display(runtime, display)`.

Configuration locks after attachment. Buffers are always caller-owned and
must be 128-byte aligned.

| Mode | Buffer requirement | Rendering contract |
| --- | --- | --- |
| `PARTIAL` | 1–2 buffers, each at least `width * 2` bytes | Compact, contiguous logical strips split by capacity |
| `DIRECT` | 1–2 buffers, each exactly large enough for a logical framebuffer | Damage rendered in place; stable target IDs select history/buffer |
| `FULL` | 1–2 complete logical framebuffers | Complete redraw and submission every frame |

Two partial buffers let CPU rendering overlap an asynchronous flush. Submitted
pixels remain immutable until their matching completion.

## Flush lifecycle

`flush` is mandatory. Each map contains:

- a half-open logical area `[x1, x2) × [y1, y2)`;
- RGB565 pixels, byte size, and stride;
- the stable destination `target_id`;
- `is_last`, identifying the final map in the frame.

When `flush_wait` is absent, every successful submission requires exactly one
`pocketjs_display_flush_ready()` or
`pocketjs_display_flush_ready_from_isr()` call. The component waits using its
internal synchronization and the configured timeout.

When `flush_wait` is present, it owns the wait strategy and returns the final
map status. The same configured timeout is passed to it.

If `begin_frame` is installed, `end_frame` and `abort_frame` are mandatory.
Every successful begin has exactly one matching:

- `end_frame` after all maps were submitted successfully; or
- `abort_frame` after a failure or when the frame had no changed pixels.

`begin_frame` may return front/back target IDs and power-of-two X/Y alignment.
The component expands damage by one logical pixel, clips it, aligns internal
boundaries, chunks it, and manages history for the returned back target.

## Extension lifecycle

Extensions are passed in `pocketjs_config_t` and retain caller-owned
`user_data`.

- `install`: called once while the QuickJS context is available;
- `before_frame`: called once immediately before each JavaScript frame;
- `destroy`: called during runtime teardown.

`pocketjs_extension_get_js_context()` is valid only while one of those
callbacks is active. Extension code must not retain it. A callback that
leaves a QuickJS exception pending must return an error. The runtime logs and
clears the exception so later frames can continue.

## Threading contract

The creator task/core becomes the owner. Runtime and display calls are
non-reentrant and owner-only, except
`pocketjs_display_flush_ready_from_isr()`. Drivers that finish work on another
task must marshal the normal ready call back to the owner task or signal from
their ISR with the ISR-safe function.
