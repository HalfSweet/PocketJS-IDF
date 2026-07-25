# Prebuilt Pocket example

This consumer validates and embeds `main/app/hello.pocket`, then executes one
headless frame. It requires Cargo and the ESP32-P4 Rust target, but does not
require Bun.

During component development, `main/idf_component.yml` uses `override_path`
to the repository root. When the example is downloaded independently, remove
that line to resolve the published Registry version.

```sh
idf.py set-target esp32p4
idf.py build
```
