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

The official JavaScript framework/compiler source is checked out as the
`vendor/pocketjs` submodule at the exact base commit. Component packing copies
the required checked-out files into the Registry archive and excludes all Git
metadata. The platform-independent Rust Core and ESP32-P4 PPA backend port are
stored under `rust/`. Port-specific C wrappers are kept in `src/`.

The two Core fixtures omitted by the original partial vendor copy are
included byte-for-byte from upstream commit
`e8b7cd83071e4f592bc919ccf4246feb80d68f9e`, an ancestor of the pinned base.
Their exact paths and SHA-256 values are recorded in
`tests/fixtures/README.md`.

## QuickJS-NG

QuickJS-NG is not vendored. ESP-IDF Component Manager resolves
`espressif/quickjs-ng` version `0.14.0` from the ESP Component Registry.
Its component declares `MIT AND Apache-2.0`.
