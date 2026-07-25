#!/usr/bin/env bash
set -euo pipefail

component_dir="$(cd "$(dirname "$0")/.." && pwd)"
temporary_dir="$(mktemp -d)"
trap 'rm -rf "$temporary_dir"' EXIT

for run in first second; do
  bun "$component_dir/tools/build_pocket.ts" \
    --manifest="$component_dir/examples/source/main/app/pocket.json" \
    --output="$temporary_dir/$run/hello.pocket" \
    --work-dir="$temporary_dir/$run/work" \
    --depfile="$temporary_dir/$run/hello.d" \
    >/dev/null
done

cmp \
  "$temporary_dir/first/hello.pocket" \
  "$temporary_dir/second/hello.pocket"
python3 "$component_dir/tools/pocket_embed.py" \
  --package="$temporary_dir/first/hello.pocket" \
  --name=hello \
  --output-dir="$temporary_dir/generated" \
  >/dev/null

echo "PocketJS-IDF deterministic package test passed"
