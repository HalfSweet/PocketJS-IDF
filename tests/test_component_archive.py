from __future__ import annotations

import importlib.util
import io
import sys
import tarfile
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TOOL_PATH = ROOT / "tools" / "audit_component_archive.py"

SPEC = importlib.util.spec_from_file_location("audit_component_archive", TOOL_PATH)
assert SPEC is not None and SPEC.loader is not None
audit_component_archive = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = audit_component_archive
SPEC.loader.exec_module(audit_component_archive)


def _write_archive(path: Path, members: set[str]) -> None:
    with tarfile.open(path, mode="w:gz") as archive:
        for name in sorted(members):
            payload = name.encode("utf-8")
            entry = tarfile.TarInfo(f"./{name}")
            entry.size = len(payload)
            archive.addfile(entry, io.BytesIO(payload))


class ComponentArchiveAuditTests(unittest.TestCase):
    def test_accepts_required_sources_without_local_artifacts(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            archive = Path(temporary) / "component.tgz"
            _write_archive(
                archive,
                set(audit_component_archive.REQUIRED_MEMBERS)
                | {"README.md", "src/pocketjs.c"},
            )

            report = audit_component_archive.audit_archive(archive)

            self.assertEqual(report.errors, ())

    def test_reports_every_missing_required_source(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            archive = Path(temporary) / "component.tgz"
            _write_archive(archive, {"README.md"})

            report = audit_component_archive.audit_archive(archive)

            self.assertEqual(
                len(report.errors),
                len(audit_component_archive.REQUIRED_MEMBERS),
            )
            self.assertTrue(
                all(
                    error.startswith("missing required PocketJS source:")
                    for error in report.errors
                )
            )

    def test_reports_forbidden_and_duplicate_rust_artifacts_together(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            archive = Path(temporary) / "component.tgz"
            _write_archive(
                archive,
                set(audit_component_archive.REQUIRED_MEMBERS)
                | {
                    ".git/config",
                    "rust/pocketjs-core/src/lib.rs",
                    "vendor/pocketjs/node_modules/module.js",
                },
            )

            report = audit_component_archive.audit_archive(archive)

            self.assertIn(
                "forbidden directory: .git/config",
                report.errors,
            )
            self.assertIn(
                "duplicate component-local Rust crate: "
                "rust/pocketjs-core/src/lib.rs",
                report.errors,
            )
            self.assertIn(
                "forbidden directory: "
                "vendor/pocketjs/node_modules/module.js",
                report.errors,
            )


if __name__ == "__main__":
    unittest.main()
