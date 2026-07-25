# Source-built Pocket example

This consumer compiles the Solid application under `main/app` into a
deterministic `esp32p4-idf` ABI 1 `.pocket`, embeds it, and executes one
headless frame.

Install the component's pinned Bun dependencies first:

```sh
bun install --cwd ../../vendor/pocketjs --frozen-lockfile
idf.py set-target esp32p4
idf.py build
```

During component development, `main/idf_component.yml` uses `override_path`
to the repository root. When the example is downloaded independently, remove
that line to resolve the published Registry version.
