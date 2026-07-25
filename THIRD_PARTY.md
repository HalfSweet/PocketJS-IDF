# Third-party software

## PocketJS

- Repository: <https://github.com/pocket-stack/pocketjs>
- Framework, compiler, package contract, and base Core commit:
  `49726ab31cf1f55f1439eb19b3b6e1ad0260ae88`
- ESP32-P4 Core/PPA fork commit:
  `5a5ffc78355aeee906c978884d6d5b83d6386b74`
- Local dirty-region extensions were checkpointed in PocketJS-esp32p4 commits
  `29be939` and `ec6665f`.
- License: MIT; see `licenses/POCKETJS-MIT.txt`.

The JavaScript build toolchain is stored under `vendor/pocketjs`. The
platform-independent Rust Core and ESP32-P4 PPA backend are stored under
`rust/`. Port-specific C wrappers are kept in `src/`.

## QuickJS-NG

QuickJS-NG is not vendored. ESP-IDF Component Manager resolves
`espressif/quickjs-ng` version `0.14.0` from the ESP Component Registry.
Its component declares `MIT AND Apache-2.0`.

