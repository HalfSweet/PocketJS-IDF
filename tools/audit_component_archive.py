#!/usr/bin/env python3

"""Audit the contents of one packed PocketJS-IDF component archive."""

from __future__ import annotations

import argparse
import tarfile
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from typing import Iterable

REQUIRED_MEMBERS = frozenset(
    {
        "vendor/pocketjs/contracts/spec/pocket-package.ts",
        "vendor/pocketjs/framework/src/index.ts",
        "vendor/pocketjs/engine/core/src/damage.rs",
        "vendor/pocketjs/engine/backends/esp32p4-ppa/src/lib.rs",
    }
)

FORBIDDEN_PARTS = frozenset(
    {
        ".cache",
        ".ci",
        ".git",
        ".github",
        ".hg",
        ".mypy_cache",
        ".nox",
        ".pytest_cache",
        ".ruff_cache",
        ".svn",
        ".tox",
        "__pycache__",
        "dist",
        "managed_components",
        "node_modules",
        "target",
    }
)
FORBIDDEN_FILES = frozenset(
    {
        ".clang-format",
        ".gitmodules",
        "AGENTS.md",
        "cliff.toml",
        "dependencies.lock",
        "sdkconfig",
        "sdkconfig.old",
    }
)
FORBIDDEN_EXACT_MEMBERS = frozenset(
    {
        "tests/test_component_archive.py",
        "tools/audit_component_archive.py",
    }
)
DUPLICATE_RUST_PREFIXES = (
    "rust/pocketjs-core/",
    "rust/pocketjs-esp32p4-ppa/",
)


@dataclass(frozen=True)
class AuditReport:
    member_count: int
    errors: tuple[str, ...]


def _normalize_member(name: str) -> str | None:
    path = PurePosixPath(name)
    if path.is_absolute() or ".." in path.parts:
        return None
    parts = tuple(part for part in path.parts if part not in {"", "."})
    return "/".join(parts)


def _forbidden_reason(member: str) -> str | None:
    path = PurePosixPath(member)
    parts = path.parts
    if member in FORBIDDEN_EXACT_MEMBERS:
        return "CI-only archive audit file"
    if any(part in FORBIDDEN_PARTS for part in parts):
        return "forbidden directory"
    if any(part.startswith("build") for part in parts[:-1]):
        return "build output"
    if path.name in FORBIDDEN_FILES:
        return "forbidden repository artifact"
    if member.startswith(DUPLICATE_RUST_PREFIXES):
        return "duplicate component-local Rust crate"
    return None


def audit_member_names(names: Iterable[str]) -> AuditReport:
    normalized: set[str] = set()
    unsafe: list[str] = []
    for name in names:
        member = _normalize_member(name)
        if member is None:
            unsafe.append(name)
        elif member:
            normalized.add(member)

    errors = [f"unsafe archive member path: {name}" for name in sorted(unsafe)]
    errors.extend(
        f"missing required PocketJS source: {member}"
        for member in sorted(REQUIRED_MEMBERS - normalized)
    )
    errors.extend(
        f"{reason}: {member}"
        for member in sorted(normalized)
        if (reason := _forbidden_reason(member)) is not None
    )
    return AuditReport(len(normalized), tuple(errors))


def audit_archive(archive: Path) -> AuditReport:
    try:
        with tarfile.open(archive, mode="r:gz") as component:
            return audit_member_names(member.name for member in component.getmembers())
    except (OSError, tarfile.TarError) as error:
        return AuditReport(0, (f"cannot read component archive: {error}",))


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Verify required and forbidden component archive members."
    )
    parser.add_argument("archive", type=Path, help="component .tgz archive")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    report = audit_archive(args.archive)
    if report.errors:
        print(f"component archive audit failed: {args.archive}")
        for error in report.errors:
            print(f"- {error}")
        return 1
    print(
        "component archive audit passed: "
        f"{args.archive} ({report.member_count} members)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
