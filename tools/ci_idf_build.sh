#!/usr/bin/env bash
set -euo pipefail

mode="${1:-}"
package_version="${2:-0.1.0-rc.1}"
case "$mode" in
  prebuilt|source|archive) ;;
  *)
    echo "usage: $0 <prebuilt|source|archive>" >&2
    exit 2
    ;;
esac

component_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ci_cache_dir="$component_dir/.ci"
export CARGO_HOME="$ci_cache_dir/cargo"
export RUSTUP_HOME="$ci_cache_dir/rustup"
export BUN_INSTALL="$ci_cache_dir/bun"
export PATH="$CARGO_HOME/bin:$BUN_INSTALL/bin:$PATH"

mkdir -p "$CARGO_HOME" "$RUSTUP_HOME" "$BUN_INSTALL"

if ! command -v rustup >/dev/null 2>&1; then
  curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs |
    sh -s -- -y --profile minimal --default-toolchain 1.89.0 \
      --no-modify-path
fi
rustup toolchain install 1.89.0 --profile minimal --no-self-update
rustup default 1.89.0
rustup target add \
  --toolchain 1.89.0 \
  riscv32imafc-unknown-none-elf

case "$mode" in
  prebuilt)
    project_dir="$component_dir/examples/prebuilt"
    ;;
  source)
    if ! command -v bun >/dev/null 2>&1; then
      curl --proto '=https' --tlsv1.2 -fsSL https://bun.sh/install |
        bash -s -- bun-v1.3.14
    fi
    bun install \
      --cwd "$component_dir/vendor/pocketjs" \
      --frozen-lockfile
    project_dir="$component_dir/examples/source"
    ;;
  archive)
    python -m pip install \
      --disable-pip-version-check \
      "idf-component-manager==3.0.3"
    archive_dir="$(mktemp -d)"
    trap 'rm -rf "$archive_dir"' EXIT
    compote -W component pack \
      --project-dir "$component_dir" \
      --name pocketjs-idf \
      --version "$package_version" \
      --dest-dir "$archive_dir"
    archive="$archive_dir/pocketjs-idf_${package_version}.tgz"
    tar -tzf "$archive" >"$archive_dir/archive.list"
    if grep -E \
      '^\./(\.git/|\.github/|\.ci/|AGENTS\.md$|.*node_modules/|.*target/)' \
      "$archive_dir/archive.list"; then
      echo "component archive contains a forbidden local artifact" >&2
      exit 1
    fi
    mkdir -p "$archive_dir/extracted/pocketjs-idf"
    tar -xzf "$archive" -C "$archive_dir/extracted/pocketjs-idf"
    project_dir="$archive_dir/extracted/pocketjs-idf/examples/prebuilt"
    ;;
esac

cd "$project_dir"
idf.py -DCCACHE_ENABLE=1 -B build-ci set-target esp32p4 build

firmware="$(find build-ci -maxdepth 1 -type f -name '*.bin' -print -quit)"
if [[ -z "$firmware" || ! -s "$firmware" ]]; then
  echo "ESP-IDF build did not produce a firmware .bin" >&2
  exit 1
fi
echo "Built $firmware ($(wc -c <"$firmware") bytes)"
