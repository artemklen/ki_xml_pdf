# Distribution and releases
Active contributors: Artem

The project distributes a C library, CLI, headers, font/notices and source archives—not a deployed service. Source acceptance, installation and GitHub tag publication have distinct workflows.

## Build and install

Only 64-bit Linux/glibc is supported.
Both `Makefile` and `CMakeLists.txt` install static/shared libraries and the CLI.
The installed header is `include/ki_xml_pdf.h`; zlib is a required build/link dependency.
Installed package metadata supports CMake and pkg-config consumers.

Choose the runtime prefix at build time.
`DESTDIR` stages files without changing the compiled font path or logical package prefix.
Keep `LICENSE` and `fonts/DejaVuSans.copyright` with the installed font.
See [Packaging](features/packaging.md) for exact layout, relocation and consumer commands.

## Exact-HEAD source archives

`make dist` uses `git archive HEAD`.
Its version, archive name and root directory derive from the header committed at HEAD.
Uncommitted source/version edits are not included.

`make distcheck` extracts that archive into a disposable root without Git metadata, checks required sources/assets/notices, builds/installs, runs external C/C++ clients, verifies report text and CLI behavior, and runs the installed integration matrix.
Failure in a required stage fails the command.

CI explicitly checks negative controls:
bad C compiler, bad C++ compiler, failing PDF reader, incorrect oracle and missing license.
Do not treat a successful archive creation alone as acceptance.

## CI source artifacts

`.github/workflows/ci.yml` runs on PRs, `main` pushes and manual dispatch.
PR checkout uses the exact head SHA.
The `source` job waits for `gcc`, `clang`, `asan` and `resources`.

It then creates:

| Artifact member | Meaning |
| --- | --- |
| Versioned source archive | Git HEAD content, one versioned root |
| `SHA256SUMS` | Archive SHA-256 |
| `SOURCE_COMMIT` | Accepted source commit |

The embedded Git tar commit must equal `SOURCE_COMMIT`.
The upload name is `ki_xml_pdf-<version>-source-<commit>`.
Artifact downloads require GitHub login.

Verify `sha256sum -c SHA256SUMS`, then match `SOURCE_COMMIT` and tar metadata to the accepted revision.
A checksum proves consistency with the supplied checksum file, not an independent signature or security review.

## Acceptance before tagging

`docs/release-checklist.md` requires full CI on the prepared PR head, independent review, merge and full post-merge CI.
It includes geometry/Info integration, old-header ABI canaries, installed consumers, documented commands, sanitizer/fuzz/resource and clean-archive evidence.

For a release, the maintainer must first ensure the target commit:
1. Is already on `main`.
2. Has full CI green on that exact commit.
3. Has consistent header/version/changelog and accepted source evidence.

These are **maintainer preconditions**.
The release job neither queries/enforces main-green status nor reruns the CI test jobs.

## Tag-triggered GitHub Releases

`.github/workflows/release.yml` triggers only on pushed `v*` tags.
Merging to `main` does not create a tag or release.
Pushing a tag is a separate maintainer action.

| Tag | Workflow behavior |
| --- | --- |
| Exactly `v$(make -s print-version)` | Publishes the matching-version GitHub Release; currently that name is `v0.3.0` |
| Exactly `v0.0.0-test` | Equality exception for fork proof; creates a draft and explicitly does not claim the header's version |
| Any other `v*` name | Fails the version check |

After checking the tag, the workflow runs `make dist`, creates checksum/commit files, verifies tar provenance, extracts the matching `CHANGELOG.md` section and attaches those three artifacts to the GitHub Release.
If no changelog section exists, it generates a note saying so; the workflow does not turn that into a test gate.
The proof tag gets explanatory draft notes.

Only this workflow grants `contents: write`; ordinary CI has `contents: read`.
There is no automatic publication to vcpkg, Conan, Homebrew, Debian or another package registry.

## Rollback and evidence scope

Source acceptance rollback is a revert of the common merge commit.
A separately published tag/release is an additional distribution action; a source revert does not retract downloaded copies.
Retain previous accepted artifacts and their provenance.

Historical evidence remains tied to its measured commit.
The `de8ba0e` OSR-82 heap profile is not a measurement of current streamed-font, two-pass-CMap and reused-page-buffer behavior.
Current acceptance must use current revision evidence.

## Key source files

| File | Role |
| --- | --- |
| `.github/workflows/ci.yml` | Test gates and retained source artifact |
| `.github/workflows/release.yml` | Exact tag check, proof exception and release creation |
| `docs/release-checklist.md` | Acceptance and maintainer tagging preconditions |
| `Makefile`, `tests/distcheck.sh` | HEAD archive and clean-source checks |
| `CHANGELOG.md` | Release notes source |

Related: [Tooling](how-to-contribute/tooling.md), [Testing](how-to-contribute/testing.md).
