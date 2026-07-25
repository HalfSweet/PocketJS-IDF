from __future__ import annotations

import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TOOL_PATH = ROOT / "tools" / "pocket_embed.py"
PACKAGE_PATH = ROOT / "examples" / "prebuilt" / "main" / "app" / "hello.pocket"

SPEC = importlib.util.spec_from_file_location("pocket_embed", TOOL_PATH)
assert SPEC is not None and SPEC.loader is not None
pocket_embed = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = pocket_embed
SPEC.loader.exec_module(pocket_embed)


class PocketEmbedTests(unittest.TestCase):
    def test_parses_committed_esp32p4_fixture(self) -> None:
        package = PACKAGE_PATH.read_bytes()
        metadata = pocket_embed.parse_package(package)
        self.assertEqual((metadata.width, metadata.height), (320, 180))
        self.assertEqual(metadata.raster_density, 1)
        self.assertEqual(metadata.package_size, len(package))

    def test_footer_hash_rejects_tampering(self) -> None:
        package = bytearray(PACKAGE_PATH.read_bytes())
        package[-16] ^= 0x80
        with self.assertRaisesRegex(
            pocket_embed.PackageError,
            "footer hash mismatch",
        ):
            pocket_embed.parse_package(bytes(package))

    def test_generates_fixed_symbols_and_rgb565_descriptor(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            header, source, assembly = pocket_embed.generate(
                PACKAGE_PATH,
                "hello",
                output,
            )
            self.assertIn(
                "extern const pocketjs_app_t pocketjs_app_hello",
                header.read_text(),
            )
            source_text = source.read_text()
            self.assertIn(".logical_width = 320U", source_text)
            self.assertIn(
                ".pixel_format = POCKETJS_PIXEL_FORMAT_RGB565",
                source_text,
            )
            assembly_text = assembly.read_text()
            self.assertIn("pocketjs_app_hello_package_start", assembly_text)
            self.assertIn('.incbin "', assembly_text)

    def test_rejects_unstable_c_symbol_names(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            with self.assertRaisesRegex(
                pocket_embed.PackageError,
                "NAME must match",
            ):
                pocket_embed.generate(
                    PACKAGE_PATH,
                    "hello-world",
                    Path(temporary),
                )


if __name__ == "__main__":
    unittest.main()
