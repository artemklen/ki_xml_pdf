# Release and distribution

A release is a source commit plus its `git archive`, accepted through the gates in `docs/release-checklist.md` and preserved by CI as an artifact with checksum and commit provenance. Nothing in the pipeline creates a version tag or GitHub Release automatically; publishing is a separate manual action, and rollback is a revert.

## Building the distribution

`make dist` archives Git HEAD, not the working tree:

- The version comes from `git show HEAD:include/ki_xml_pdf.h` (`DIST_VERSION` in `Makefile`), so the archive name `ki_xml_pdf-<version>.tar.gz` and its single root directory always describe HEAD. An uncommitted version bump never reaches the archive; commit the `include/ki_xml_pdf.h` change first.
- The archive is a `git archive --prefix=ki_xml_pdf-<version>/`, and `sha256sum` of it prints to the terminal.
- `make print-version` and `make print-dist-name` report the working-tree version and the archive name, which differ only when the tree is dirty.

## Accepting it: `make distcheck`

`make distcheck` builds the archive and hands it to `tests/distcheck.sh`, which:

1. Verifies one archive root named after the header version, and that the header defines exactly one version.
2. Checks a required-file list: sources, headers, build files, docs, examples, tests, corpus, font and licenses.
3. Rejects any `.git` metadata or private workspace path inside the archive.
4. Installs with Make into a disposable prefix, builds the C and C++ consumers against the installed static and shared libraries, and runs them from `/`.
5. Checks the report PDF against the oracle through `pdftotext`, then runs the installed CLI pipelines.
6. Runs `tests/build_integration.sh`, requiring exactly 16 installed consumer rows and 4 catalog variants.

The script is fail-fast and cleans its unpack, build and install directories on exit. `DISTCHECK_ORACLE` points at a replacement oracle and `DISTCHECK_BREAK=license` removes the license, both to prove the suite rejects bad input; the `gcc` CI job asserts that all five negative controls fail.

## CI source artifact and provenance

The `source` job in `.github/workflows/ci.yml` runs only after `gcc`, `clang`, `asan` and `resources` all pass. It runs `make dist`, writes `SHA256SUMS` (the archive checksum) and `SOURCE_COMMIT` (the HEAD commit), checks that `git get-tar-commit-id` on the archive matches `SOURCE_COMMIT`, and uploads all three files as an artifact named `ki_xml_pdf-<version>-source-<commit>`.

To accept an artifact:

1. Download it (GitHub login is required for artifact downloads).
2. `sha256sum -c SHA256SUMS`.
3. Match `SOURCE_COMMIT` against the accepted revision; the tar metadata carries the same commit.
4. Optionally re-run `make distcheck` from a clean checkout of that commit.

The workflow runs on the prepared PR head and again on the merged `main` commit, so a merge that only resolves textually still gets full gates. Historical measurements stay tied to the revision that produced them; acceptance always uses current CI metrics.

## Release checklist gates

`docs/release-checklist.md` defines acceptance for one source commit and its archive. The gates:

| Gate | Evidence |
| --- | --- |
| License and assets | MIT `LICENSE` and the separate DejaVu notice and font present in archive and install |
| API compatibility | Current-call `KiResult`, unchanged `KiOptions` and exports, additive `ki_convert_to_file`, `make test` coverage |
| Built-in report and pipelines | `tests/cli_pipeline.py`: four stream modes, oracle, usage paths, atomic failure and cancellation |
| Public custom mapper | `tests/catalog_verify.py`: fixed oracle, long field across short reads, byte offsets, distinct causes |
| Installed C/C++ consumers | `tests/build_integration.sh`: staging, discovery matrix, ELF linkage, font, foreign cwd |
| Documented commands | `tests/docs_smoke.sh`: README and mapper-guide blocks executed and oracle-checked |
| Memory safety and fuzz | Functional tests on both toolchains, ASan/UBSan with fatal diagnostics, seeded fuzz with reader checks |
| Ordinary converter memory | `make check` 16 MiB gate, `tests/large_proof.py 1073741824` with RSS at or under 64 MiB, PDF and spool budgets |
| Clean archive | `make distcheck` on exact HEAD without a Git checkout |
| Version and provenance | Header as the single version source, archive name from HEAD, CI retains archive, `SHA256SUMS` and `SOURCE_COMMIT` |
| Common delivery | One PR with all children, full CI on the PR head, independent review, full post-merge CI on the merge commit |

Run everything from a clean source revision. `make dist` excludes uncommitted changes by design.

## Tags and releases

A green CI run, a merged PR and a published artifact do not create a version tag, a GitHub Release or a package-registry publication. Those are deliberate follow-up actions outside the workflow, recorded in `CHANGELOG.md` when they happen. `README.md` states the same rule for users.

## Rollback

Rollback is a revert of the merge commit on `main`. No release tag or registry action is part of the delivery, so there is nothing else to undo; the source artifact of the previous accepted revision remains the distribution to fall back to.

## Related pages

- [Tooling](how-to-contribute/tooling.md) for the CI job graph and Make/CMake targets.
- [Testing](how-to-contribute/testing.md) for the suites behind each gate.
- [Packaging](features/packaging.md) for installed layout and consumers.
- [How to contribute](how-to-contribute/index.md) for the delivery process around a release.
