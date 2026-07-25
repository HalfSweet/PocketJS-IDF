#!/usr/bin/env bun

import {
  copyFileSync,
  existsSync,
  lstatSync,
  mkdirSync,
  readdirSync,
  readlinkSync,
  renameSync,
  symlinkSync,
  unlinkSync,
} from "node:fs";
import {
  basename,
  dirname,
  isAbsolute,
  join,
  relative,
  resolve,
  sep,
} from "node:path";
import {
  encodeIdentity,
  encodePocketPackage,
  POCKET_SECTION,
} from "../vendor/pocketjs/contracts/spec/pocket-package.ts";
import type {
  PocketManifestV2,
} from "../vendor/pocketjs/contracts/spec/pocket-manifest.ts";
import {
  POCKET_CAPABILITIES,
  type PresentationMode,
  type PlatformContractRegistry,
  type PocketCapabilityId,
  type TargetProfile,
  type Viewport,
} from "../vendor/pocketjs/contracts/spec/platforms.ts";
import {
  resolveBuildPlan,
} from "../vendor/pocketjs/framework/src/manifest/resolve.ts";
import {
  validatePocketManifest,
} from "../vendor/pocketjs/framework/src/manifest/validate.ts";

const TARGET_ID = "esp32p4-idf";
const HOST_ABI = 1;
const RASTER_DENSITY = 1;
const TOOLCHAIN_REVISION = "49726ab31cf1f55f1439eb19b3b6e1ad0260ae88";
const SUPPORTED_CAPABILITIES: readonly PocketCapabilityId[] = [
  "input.analog.left",
  "input.buttons",
  "input.cursor",
  "text.glyphs.baked",
];

interface Options {
  manifest: string;
  output: string;
  workDir: string;
  depfile?: string;
}

function fail(message: string): never {
  console.error(`PocketJS-IDF app build: ${message}`);
  process.exit(1);
}

function parseOptions(argv: string[]): Options {
  const values = new Map<string, string>();
  for (const argument of argv) {
    const equals = argument.indexOf("=");
    if (!argument.startsWith("--") || equals < 3) {
      fail(
        "usage: build_pocket.ts --manifest=<pocket.json> " +
          "--output=<app.pocket> --work-dir=<build-dir> [--depfile=<path>]",
      );
    }
    values.set(argument.slice(2, equals), argument.slice(equals + 1));
  }
  for (const required of ["manifest", "output", "work-dir"]) {
    if (!values.get(required)) fail(`missing --${required}`);
  }
  return {
    manifest: resolve(values.get("manifest")!),
    output: resolve(values.get("output")!),
    workDir: resolve(values.get("work-dir")!),
    depfile: values.get("depfile") ? resolve(values.get("depfile")!) : undefined,
  };
}

function fixedViewport(
  manifest: PocketManifestV2,
): { logical: Viewport; presentation: PresentationMode } {
  const viewport = manifest.app.viewport;
  const fixed = "logical" in viewport ? viewport : viewport.fixed;
  if (!fixed) {
    fail("esp32p4-idf requires app.viewport.fixed");
  }
  return {
    logical: [fixed.logical[0], fixed.logical[1]],
    presentation: fixed.presentation,
  };
}

function resolvePlan(input: unknown) {
  const validated = validatePocketManifest(input);
  if (!validated.ok) {
    const diagnostics = validated.diagnostics
      .map((item) => `${item.path}: ${item.message}`)
      .join("\n  ");
    fail(`invalid pocket.json:\n  ${diagnostics}`);
  }

  const viewport = fixedViewport(validated.value);
  const profile: TargetProfile<PocketCapabilityId> = {
    hostAbi: HOST_ABI,
    platform: "esp-idf",
    form: "embedded",
    display: {
      // The component owns a logical surface. Board callbacks own any
      // rotation, scaling, centering, and physical framebuffer dimensions.
      physicalViewport: viewport.logical,
      logicalViewports: [viewport.logical],
      presentations: [viewport.presentation],
      rasterDensity: RASTER_DENSITY,
    },
    capabilities: SUPPORTED_CAPABILITIES,
  };
  const registry: PlatformContractRegistry = {
    capabilities: POCKET_CAPABILITIES,
    targets: { [TARGET_ID]: profile },
  };
  const result = resolveBuildPlan(
    validated.value,
    { target: TARGET_ID },
    registry,
  );
  if (!result.ok) {
    const diagnostics = result.diagnostics
      .map((item) => `${item.path}: ${item.message}`)
      .join("\n  ");
    fail(`manifest is not compatible with ${TARGET_ID}:\n  ${diagnostics}`);
  }
  return { manifest: validated.value, plan: result.plan };
}

function syncToolchain(source: string, destination: string): void {
  mkdirSync(destination, { recursive: true });
  for (const entry of readdirSync(source, { withFileTypes: true })) {
    if ([".cache", "dist", "node_modules", "target"].includes(entry.name)) {
      continue;
    }
    const from = join(source, entry.name);
    const to = join(destination, entry.name);
    if (entry.isDirectory()) {
      syncToolchain(from, to);
    } else if (entry.isFile()) {
      copyFileSync(from, to);
    } else if (entry.isSymbolicLink()) {
      const expected = readlinkSync(from);
      if (existsSync(to) || lstatExists(to)) unlinkSync(to);
      symlinkSync(expected, to);
    }
  }
}

function lstatExists(path: string): boolean {
  try {
    lstatSync(path);
    return true;
  } catch {
    return false;
  }
}

function linkDependencies(sourceModules: string, workModules: string): void {
  if (!existsSync(sourceModules)) {
    fail(
      `Bun dependencies are missing. Run:\n` +
        `  bun install --cwd ${JSON.stringify(dirname(sourceModules))} --frozen-lockfile`,
    );
  }
  if (lstatExists(workModules)) {
    const stat = lstatSync(workModules);
    if (
      stat.isSymbolicLink() &&
      resolve(dirname(workModules), readlinkSync(workModules)) ===
        resolve(sourceModules)
    ) {
      return;
    }
    fail(`${workModules} exists but is not the expected node_modules link`);
  }
  symlinkSync(sourceModules, workModules, process.platform === "win32" ? "junction" : "dir");
}

function depEscape(path: string): string {
  return path
    .replaceAll("$", "$$")
    .replaceAll("#", "\\#")
    .replaceAll(" ", "\\ ");
}

function resolveImport(fromFile: string, specifier: string): string | null {
  if (
    !specifier.startsWith("./") &&
    !specifier.startsWith("../") &&
    !isAbsolute(specifier)
  ) {
    return null;
  }
  try {
    return Bun.resolveSync(specifier, dirname(fromFile));
  } catch {
    const base = resolve(dirname(fromFile), specifier);
    for (const candidate of [
      base,
      `${base}.ts`,
      `${base}.tsx`,
      `${base}.vue`,
      join(base, "index.ts"),
      join(base, "index.tsx"),
    ]) {
      if (existsSync(candidate)) return candidate;
    }
    return null;
  }
}

async function collectSourceDependencies(entry: string): Promise<Set<string>> {
  const dependencies = new Set<string>();
  const queue = [entry];
  const importPattern =
    /(?:^|\n)\s*(?:import|export)\b[^;'"]*?from\s*(["'])([^"']+)\1|(?:^|\n)\s*import\s*(["'])([^"']+)\3/g;

  while (queue.length > 0) {
    const file = resolve(queue.pop()!);
    if (dependencies.has(file) || !existsSync(file)) continue;
    dependencies.add(file);
    if (!/\.(?:ts|tsx|vue|js|jsx|json)$/.test(file)) continue;
    const source = await Bun.file(file).text();
    let match: RegExpExecArray | null;
    while ((match = importPattern.exec(source))) {
      const imported = resolveImport(file, match[2] ?? match[4]);
      if (imported) queue.push(imported);
    }
  }
  return dependencies;
}

function collectAppDirectory(path: string, output: Set<string>): void {
  if (!existsSync(path)) return;
  output.add(path);
  for (const entry of readdirSync(path, { withFileTypes: true })) {
    if ([".git", ".cache", "build", "node_modules", "target"].includes(entry.name)) {
      continue;
    }
    const child = join(path, entry.name);
    if (entry.isDirectory()) {
      collectAppDirectory(child, output);
    } else if (entry.isFile()) {
      output.add(child);
    }
  }
}

async function writeDepfile(
  depfile: string | undefined,
  output: string,
  manifest: string,
  entry: string,
): Promise<void> {
  if (!depfile) return;
  const dependencies = await collectSourceDependencies(entry);
  collectAppDirectory(dirname(entry), dependencies);
  dependencies.add(manifest);
  const config = join(dirname(manifest), "pocket.config.ts");
  if (existsSync(config)) dependencies.add(config);
  mkdirSync(dirname(depfile), { recursive: true });
  const sorted = [...dependencies].sort();
  await Bun.write(
    depfile,
    `${depEscape(output)}: ${sorted.map(depEscape).join(" ")}\n`,
  );
}

function normalizeBundlePaths(bundle: Uint8Array): Uint8Array {
  const source = new TextDecoder().decode(bundle);
  const normalized = source
    .split("\n")
    .map((line) => {
      const unixLine = line.replaceAll("\\", "/");
      for (const marker of [
        "/pocketjs-toolchain/",
        "/vendor/pocketjs/node_modules/",
      ]) {
        const markerIndex = unixLine.indexOf(marker);
        if (markerIndex >= 0 && /^\s*\/\//.test(unixLine)) {
          const prefix = unixLine.slice(0, unixLine.indexOf("//") + 3);
          const suffix = unixLine.slice(markerIndex + marker.length);
          return `${prefix}@pocketjs/${suffix}`;
        }
      }
      return line;
    })
    .join("\n");
  return new TextEncoder().encode(normalized);
}

async function main(): Promise<void> {
  const options = parseOptions(process.argv.slice(2));
  if (!existsSync(options.manifest)) {
    fail(`manifest does not exist: ${options.manifest}`);
  }

  const componentRoot = resolve(import.meta.dir, "..");
  const vendorRoot = join(componentRoot, "vendor", "pocketjs");
  const sourceModules = join(vendorRoot, "node_modules");
  const workRoot = join(options.workDir, "pocketjs-toolchain");
  const relativeWork = relative(options.workDir, workRoot);
  if (relativeWork.startsWith("..") || isAbsolute(relativeWork)) {
    fail("toolchain work directory must remain inside the requested build directory");
  }

  syncToolchain(vendorRoot, workRoot);
  linkDependencies(sourceModules, join(workRoot, "node_modules"));
  /*
   * Upstream pass 1 walks framework imports before it generates this module.
   * Bun on case-sensitive Linux caches that first failed relative resolution,
   * so a completely fresh build can still miss the file during pass 2. Seed
   * an empty table in the build-local toolchain; build.ts overwrites it with
   * the compiled style IDs before bundling. This never modifies component
   * source and also removes stale generated state between rebuilds.
   */
  await Bun.write(
    join(workRoot, "framework", "src", "styles.generated.ts"),
    "export const STYLE_IDS: Record<string, number> = {};\n",
  );

  const manifestBytes = new Uint8Array(
    await Bun.file(options.manifest).arrayBuffer(),
  );
  let manifestJson: unknown;
  try {
    manifestJson = JSON.parse(new TextDecoder().decode(manifestBytes));
  } catch (error) {
    fail(`cannot parse ${options.manifest}: ${String(error)}`);
  }
  const { manifest, plan } = resolvePlan(manifestJson);
  const projectRoot = dirname(options.manifest);
  const entry = resolve(projectRoot, plan.app.entry);
  if (!existsSync(entry)) {
    fail(`manifest entry does not exist: ${entry}`);
  }

  mkdirSync(options.workDir, { recursive: true });
  mkdirSync(dirname(options.output), { recursive: true });
  const planPath = join(options.workDir, `${plan.app.output}.plan.json`);
  const planBytes = new TextEncoder().encode(JSON.stringify(plan));
  await Bun.write(planPath, planBytes);

  const build = Bun.spawn(
    [
      process.execPath,
      join(workRoot, "tools", "build.ts"),
      `--plan=${planPath}`,
      `--project-root=${projectRoot}`,
      `--outdir=${options.workDir}`,
    ],
    {
      cwd: projectRoot,
      env: {
        ...process.env,
        NODE_PATH: sourceModules,
        POCKETJS_IDF_TOOLCHAIN_REVISION: TOOLCHAIN_REVISION,
      },
      stdout: "inherit",
      stderr: "inherit",
    },
  );
  const exitCode = await build.exited;
  if (exitCode !== 0) {
    fail(`PocketJS compiler exited with status ${exitCode}`);
  }

  const javascriptPath = join(options.workDir, `${plan.app.output}.js`);
  const pakPath = join(options.workDir, `${plan.app.output}.pak`);
  if (!existsSync(javascriptPath) || !existsSync(pakPath)) {
    fail("PocketJS compiler did not produce the planned JavaScript and PAK outputs");
  }
  const javascript = normalizeBundlePaths(
    new Uint8Array(await Bun.file(javascriptPath).arrayBuffer()),
  );
  const terminatedJavascript = new Uint8Array(javascript.length + 1);
  terminatedJavascript.set(javascript);
  const pak = new Uint8Array(await Bun.file(pakPath).arrayBuffer());
  const packageBytes = encodePocketPackage({
    manifest: manifestBytes,
    variants: [
      {
        target: TARGET_ID,
        hostAbi: HOST_ABI,
        sections: [
          {
            kind: POCKET_SECTION.identity,
            bytes: encodeIdentity({
              output: plan.app.output,
              id: manifest.id,
              title: manifest.title,
            }),
          },
          { kind: POCKET_SECTION.plan, bytes: planBytes },
          { kind: POCKET_SECTION.js, bytes: terminatedJavascript },
          { kind: POCKET_SECTION.pak, bytes: pak },
        ],
      },
    ],
  });

  const temporary = `${options.output}.tmp-${process.pid}`;
  await Bun.write(temporary, packageBytes);
  renameSync(temporary, options.output);
  await writeDepfile(
    options.depfile,
    options.output,
    options.manifest,
    entry,
  );
  console.log(
    `PocketJS-IDF app build: ${basename(options.output)} ` +
      `(${plan.viewport.logical[0]}x${plan.viewport.logical[1]}, ` +
      `${packageBytes.length} bytes, ${TARGET_ID} ABI ${HOST_ABI})`,
  );
}

await main();
