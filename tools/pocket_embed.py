#!/usr/bin/env python3

"""Validate one ESP32-P4 .pocket and generate an embedded app descriptor."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
from dataclasses import dataclass
from pathlib import Path

POCKET_MAGIC = 0x544B4350
POCKET_VERSION = 1
POCKET_HEADER_SIZE = 16
POCKET_VARIANT_SIZE = 40
POCKET_SECTION_SIZE = 16
POCKET_TARGET_BYTES = 16
POCKET_ALIGN = 16
POCKET_FOOTER_SIZE = 8

SECTION_PLAN = 2
SECTION_JAVASCRIPT = 3
SECTION_PAK = 4

PAK_MAGIC = 0x4B504344
TARGET_ID = "esp32p4-idf"
HOST_ABI = 1
NAME_PATTERN = re.compile(r"^[a-z][a-z0-9_]*$")


class PackageError(ValueError):
    """The package does not satisfy the PocketJS-IDF contract."""


@dataclass(frozen=True)
class AppMetadata:
    width: int
    height: int
    raster_density: int
    package_size: int


def fnv1a64(data: bytes) -> int:
    value = 0xCBF29CE484222325
    for byte in data:
        value ^= byte
        value = (value * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return value


def _u32(data: bytes, offset: int) -> int:
    if offset < 0 or offset + 4 > len(data):
        raise PackageError("truncated u32")
    return struct.unpack_from("<I", data, offset)[0]


def _u64(data: bytes, offset: int) -> int:
    if offset < 0 or offset + 8 > len(data):
        raise PackageError("truncated u64")
    return struct.unpack_from("<Q", data, offset)[0]


def _range(data: bytes, offset: int, size: int, limit: int | None = None) -> bytes:
    end_limit = len(data) if limit is None else limit
    if offset < 0 or size < 0 or offset > end_limit or size > end_limit - offset:
        raise PackageError("section range is outside the package")
    return data[offset : offset + size]


def _align16(value: int) -> int:
    return (value + POCKET_ALIGN - 1) & ~(POCKET_ALIGN - 1)


def _canonical_json(value: object) -> str:
    return json.dumps(
        value,
        ensure_ascii=False,
        separators=(",", ":"),
        sort_keys=True,
    )


def parse_package(data: bytes) -> AppMetadata:
    if len(data) < POCKET_HEADER_SIZE + POCKET_FOOTER_SIZE:
        raise PackageError("package is truncated")
    if _u32(data, 0) != POCKET_MAGIC:
        raise PackageError("bad .pocket magic")
    if _u32(data, 4) != POCKET_VERSION:
        raise PackageError("unsupported .pocket version")
    stored_hash = _u64(data, len(data) - POCKET_FOOTER_SIZE)
    if fnv1a64(data[:-POCKET_FOOTER_SIZE]) != stored_hash:
        raise PackageError(".pocket footer hash mismatch")

    manifest_size = _u32(data, 8)
    variant_count = _u32(data, 12)
    if variant_count == 0:
        raise PackageError("package has no variants")
    _range(data, POCKET_HEADER_SIZE, manifest_size)
    variant_table = _align16(POCKET_HEADER_SIZE + manifest_size)
    _range(data, variant_table, variant_count * POCKET_VARIANT_SIZE)

    sections: dict[int, bytes] | None = None
    for variant_index in range(variant_count):
        entry = variant_table + variant_index * POCKET_VARIANT_SIZE
        raw_target = _range(data, entry, POCKET_TARGET_BYTES)
        terminator = raw_target.find(b"\0")
        if terminator < 0:
            raise PackageError("variant target is not NUL terminated")
        try:
            target = raw_target[:terminator].decode("utf-8")
        except UnicodeDecodeError as error:
            raise PackageError("variant target is not UTF-8") from error
        if target != TARGET_ID:
            continue
        if _u32(data, entry + 16) != HOST_ABI:
            raise PackageError(f"{TARGET_ID} host ABI is not {HOST_ABI}")

        section_count = _u32(data, entry + 20)
        section_table = _u32(data, entry + 24)
        expected_hash = _u64(data, entry + 32)
        if section_count == 0:
            raise PackageError("target variant has no sections")
        _range(data, section_table, section_count * POCKET_SECTION_SIZE)
        sections = {}
        previous_kind = 0
        payloads: list[bytes] = []
        for section_index in range(section_count):
            section_entry = section_table + section_index * POCKET_SECTION_SIZE
            kind = _u32(data, section_entry)
            offset = _u32(data, section_entry + 8)
            size = _u32(data, section_entry + 12)
            if kind == 0 or kind <= previous_kind:
                raise PackageError("section kinds must be unique and increasing")
            previous_kind = kind
            payload = _range(
                data,
                offset,
                size,
                len(data) - POCKET_FOOTER_SIZE,
            )
            sections[kind] = payload
            payloads.append(payload)
        if fnv1a64(b"".join(payloads)) != expected_hash:
            raise PackageError("target variant hash mismatch")
        break

    if sections is None:
        raise PackageError(f"package has no {TARGET_ID} variant")
    for kind, label in [
        (SECTION_PLAN, "build plan"),
        (SECTION_JAVASCRIPT, "JavaScript"),
        (SECTION_PAK, "PAK"),
    ]:
        if not sections.get(kind):
            raise PackageError(f"target variant has no {label} section")
    if not sections[SECTION_JAVASCRIPT].endswith(b"\0"):
        raise PackageError("JavaScript section is not NUL terminated")
    if len(sections[SECTION_PAK]) < 4 or _u32(sections[SECTION_PAK], 0) != PAK_MAGIC:
        raise PackageError("asset section is not a PocketJS PAK")

    try:
        plan = json.loads(sections[SECTION_PLAN].decode("utf-8"))
        target = plan["target"]
        viewport = plan["viewport"]
        logical = viewport["logical"]
        width = int(logical[0])
        height = int(logical[1])
        raster_density = int(viewport["rasterDensity"])
    except (KeyError, IndexError, TypeError, ValueError, UnicodeDecodeError, json.JSONDecodeError) as error:
        raise PackageError("build plan is malformed") from error
    if target.get("id") != TARGET_ID or target.get("hostAbi") != HOST_ABI:
        raise PackageError("build plan target or ABI does not match the variant")
    plan_content = dict(plan)
    plan_hash = plan_content.pop("planHash", None)
    expected_plan_hash = "sha256:" + hashlib.sha256(
        _canonical_json(plan_content).encode("utf-8")
    ).hexdigest()
    if plan_hash != expected_plan_hash:
        raise PackageError("build plan checksum mismatch")
    if (
        not isinstance(logical, list)
        or len(logical) != 2
        or any(type(value) is not int for value in logical)
        or type(viewport.get("rasterDensity")) is not int
        or width <= 0
        or height <= 0
        or raster_density != 1
    ):
        raise PackageError("build plan viewport is unsupported")
    return AppMetadata(width, height, raster_density, len(data))


def _assembly_path(path: Path) -> str:
    return str(path.resolve()).replace("\\", "\\\\").replace('"', '\\"')


def generate(package: Path, name: str, output_dir: Path) -> tuple[Path, Path, Path]:
    if not NAME_PATTERN.fullmatch(name):
        raise PackageError(
            "NAME must match [a-z][a-z0-9_]* so generated C symbols are stable"
        )
    package = package.resolve()
    metadata = parse_package(package.read_bytes())
    output_dir.mkdir(parents=True, exist_ok=True)
    symbol = f"pocketjs_app_{name}"
    header = output_dir / f"{symbol}.h"
    source = output_dir / f"{symbol}.c"
    assembly = output_dir / f"{symbol}.S"

    header.write_text(
        "#pragma once\n\n"
        '#include "pocketjs.h"\n\n'
        "#ifdef __cplusplus\n"
        'extern "C" {\n'
        "#endif\n\n"
        f"extern const pocketjs_app_t {symbol};\n\n"
        "#ifdef __cplusplus\n"
        "}\n"
        "#endif\n",
        encoding="utf-8",
    )
    source.write_text(
        f'#include "{symbol}.h"\n\n'
        "#include <stdint.h>\n\n"
        f"extern const uint8_t {symbol}_package_start[];\n\n"
        f"const pocketjs_app_t {symbol} = {{\n"
        f"    .package_data = {symbol}_package_start,\n"
        f"    .package_size = {metadata.package_size}U,\n"
        "    .target_id = POCKETJS_TARGET_ID,\n"
        "    .host_abi = POCKETJS_HOST_ABI,\n"
        f"    .logical_width = {metadata.width}U,\n"
        f"    .logical_height = {metadata.height}U,\n"
        f"    .raster_density = {metadata.raster_density}U,\n"
        "    .pixel_format = POCKETJS_PIXEL_FORMAT_RGB565,\n"
        "};\n",
        encoding="utf-8",
    )
    assembly.write_text(
        f'.section .rodata.{symbol},"a",@progbits\n'
        ".balign 16\n"
        f".global {symbol}_package_start\n"
        f".type {symbol}_package_start, @object\n"
        f"{symbol}_package_start:\n"
        f'.incbin "{_assembly_path(package)}"\n'
        f".global {symbol}_package_end\n"
        f"{symbol}_package_end:\n"
        f".size {symbol}_package_start, "
        f"{symbol}_package_end - {symbol}_package_start\n",
        encoding="utf-8",
    )
    return header, source, assembly


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--package", type=Path, required=True)
    parser.add_argument("--name", required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    arguments = parser.parse_args()
    try:
        outputs = generate(arguments.package, arguments.name, arguments.output_dir)
    except (OSError, PackageError) as error:
        parser.error(str(error))
    print("PocketJS-IDF embed: " + ", ".join(str(path) for path in outputs))


if __name__ == "__main__":
    main()
