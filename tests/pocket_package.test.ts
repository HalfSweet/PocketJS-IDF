import { expect, test } from "bun:test";
import {
  decodePocketPackage,
  encodePocketPackage,
  findSection,
  findVariant,
  POCKET_SECTION,
} from "../vendor/pocketjs/contracts/spec/pocket-package.ts";

const fixture = new URL(
  "../examples/prebuilt/main/app/hello.pocket",
  import.meta.url,
);

test("the official encoder round-trips the ESP32-P4 fixture", async () => {
  const bytes = new Uint8Array(await Bun.file(fixture).arrayBuffer());
  const decoded = decodePocketPackage(bytes);
  const variant = findVariant(decoded, "esp32p4-idf");

  expect(variant).not.toBeNull();
  expect(variant?.hostAbi).toBe(1);
  expect(findSection(variant!, POCKET_SECTION.plan)).not.toBeNull();
  expect(findSection(variant!, POCKET_SECTION.pak)).not.toBeNull();
  expect(findSection(variant!, POCKET_SECTION.js)?.at(-1)).toBe(0);
  expect(encodePocketPackage(decoded)).toEqual(bytes);
});

test("the official decoder rejects a stale package footer", async () => {
  const bytes = new Uint8Array(await Bun.file(fixture).arrayBuffer());
  bytes[bytes.length - 16] ^= 0x80;
  expect(() => decodePocketPackage(bytes)).toThrow("hash mismatch");
});
