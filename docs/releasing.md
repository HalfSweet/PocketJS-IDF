# Release process

An annotated semantic-version tag is the only source of a released component
version. GitHub Releases is the canonical changelog; release notes are generated
from Git history by git-cliff and are not maintained by hand.

## Registry prerequisite

Before creating a tag, a maintainer must sign in to the ESP Component Registry
and confirm all of the following:

- namespace: `halfsweet`;
- component: `pocketjs-idf`;
- trusted uploader repository: `HalfSweet/PocketJS-IDF`;
- trusted uploader branch and environment: unset, so annotated tag workflows
  can publish.

The Registry upload job alone receives `id-token: write`.
`espressif/upload-components-ci-action` exchanges the GitHub OIDC identity for
short-lived Registry authorization; no long-lived Registry token or Actions
secret is used.

## Required gates

Every release must pass the same workflow used by pull requests:

- clang-format, Rustfmt, Clippy, ShellCheck, and actionlint;
- Component Manager manifest lint, warning-as-error pack, and archive audit;
- component Rust tests and the pinned upstream Core and PPA backend tests;
- Pocket encoder, embed validator, and deterministic package tests;
- ESP-IDF 5.4.4 and 6.0.2 prebuilt builds;
- ESP-IDF 6.0.2 source and unpacked-archive builds.

The archive audit confirms that required flattened PocketJS sources are present
and that VCS metadata, duplicate Rust crates, caches, build outputs, and
`node_modules` are absent.

## Hardware release gate

When a release changes runtime, rendering, PPA, display scheduling, QuickJS, or
board integration behavior, build, flash, and observe every affected firmware
on the target ESP32-P4 hardware before tagging. Record the device, firmware
configuration, image sizes, runtime duration, logs, and visual acceptance in
the release PR or its linked validation record.

Documentation, formatting, or release-infrastructure-only changes do not create
a new hardware claim. Do not reuse an older hardware record if the changed code
could affect device behavior.

## Tag and publish

After the release commit is on `main` and all applicable software and hardware
gates pass:

```sh
git switch main
git pull --ff-only
tag=vX.Y.Z
git tag -a "$tag" -m "PocketJS-IDF $tag"
git push origin "$tag"
```

`publish.yml` rejects tags that are not semantic versions, lightweight tags,
or tags whose commits are not contained in `origin/main`. It reruns the complete
quality and ESP-IDF matrix against the tagged tree, creates a draft GitHub
Release from git-cliff output, publishes the component through Registry OIDC,
and only then makes the GitHub Release public. Tags containing a prerelease
suffix are marked as prereleases.

If a workflow or external service fails, never move or recreate the tag. Fix
the workflow on `main`, then revalidate the same tag:

```sh
gh workflow run publish.yml \
  --ref main \
  -f tag=vX.Y.Z
```

The workflow is idempotent: it updates an existing draft, the Registry uploader
skips an already published component version, and an already public GitHub
Release remains public.

## Registry verification

After publishing, create a clean project outside this repository and consume
the exact released version:

```yaml
dependencies:
  halfsweet/pocketjs-idf:
    version: "X.Y.Z"
```

Build and confirm the link and application `.bin` without an `override_path`,
Git dependency, or workspace files. Only then update downstream lockfiles or
remove development overrides.
