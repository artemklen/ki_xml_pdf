# How to contribute

Changes move from an issue to a branch, then a single pull request, then CI and an independent review, then `main`. This page is the map of that process and what "done" means; the subpages cover the workflow, tests, debugging and tooling.

## Picking up work

- Ordinary bugs start in [GitHub Issues](https://github.com/artemklen/ki_xml_pdf/issues). A useful report includes the version from `xml2pdf --version`, a commit ID or archive SHA-256, the OS, compiler and linkage, the exact command or public API call, its return code, the stderr or `KiResult` diagnostic, and a small reproducible XML input you may share. `CONTRIBUTING.md` asks for a synthetic reproducer when the original document is private.
- Security reports do not go through issues. Send them privately to Artem Klen at artem.klen@icloud.com with the affected version, impact and the smallest shareable input. See [Security](../security.md).
- Before changing code, read [Getting started](../overview/getting-started.md) and [Architecture](../overview/architecture.md), then the system page that owns the code you will touch: [Conversion pipeline](../systems/conversion-pipeline.md), [XML parser](../systems/xml-parser.md), [Layout and fonts](../systems/layout-and-fonts.md) or [PDF writer](../systems/pdf-writer.md).
- Good first changes are small and observable: an error path, a fixture, a mapper example, documentation. Every behavior change needs a regression that fails before the fix, as `CONTRIBUTING.md` requires.

## Pull request process

1. Branch from `main` with a short name derived from your tracker task, for example `osr-N-topic`.
2. Write the change following [Patterns and conventions](patterns-and-conventions.md): fixed-size state, plain return codes, `goto` cleanup, no global side effects.
3. Add a regression that fails before the fix and checks an observable result. Prefer an independent PDF reader (`pdftotext`) over byte-matching the writer's own output. See [Testing](testing.md).
4. Run the checks that match the change and record the commands, results and source revision in the PR. Say which checks you could not run and why.
5. Commit with the tracker prefix and an imperative summary. Real commits on `main` read `OSR-4 Support CLI streams and atomic callback-to-file output` and `OSR-6 Add English onboarding and contribution guides`. The prefix comes from the owner's tracker; plain topics may use the issue number instead.
6. Push the branch and open one PR against `main`. CI runs on the PR head; a branch push with no open PR starts nothing (see [Tooling](tooling.md)).
7. Address review, wait for green CI on the exact head, merge, then confirm the full post-merge CI run on the merge commit.

Version 0.2.0 followed this shape: one PR (`osr-1`) carried the parent task and all children, with full CI on the prepared PR head, an independent review, merge, and full CI on the merge commit. `docs/release-checklist.md` records that as the delivery gate.

## Review expectations

Review checks the public contract, not style preferences. A reviewer will look for:

- Bounded parser and mapper state. Nothing may grow with input size; every limit stays a `#define` and an error, never a reallocation.
- Atomic file output preserved: late XML, profile, I/O, quota and cancellation failures keep the previous destination and remove temporary files. Stream output keeps its documented partial-output behavior.
- Cancellation checkpoints in any new long-running step, polled like the existing ones in `src/convert.c`.
- A regression that fails before the fix, and evidence in the PR: commands, results, revision, and reasons for skipped checks.
- Stable `KiOptions` layout within a 0.y series; new exports go through a new version node in `libki_xml_pdf.map`.
- Intentional schema or API changes documented in the relevant reference and `CHANGELOG.md`, with header, CLI, CMake, pkg-config and archive versions consistent.

An independent review is part of release acceptance (`docs/release-checklist.md` lists the "independent Grok review" gate), so expect at least one reviewer outside the author.

## Definition of done

A change is done when all of these hold:

- `make test` passes, and `make check` passes when streaming, CLI or report behavior changed.
- `make test-san` is clean when memory handling changed; sanitizer runs are separate from RSS measurements.
- `tests/build_integration.sh` passes when installed packaging changed; `make distcheck` passes when the source distribution changed.
- The new regression fails on the pre-fix revision and passes now.
- No regression in the 4096 KiB (4 MiB) converter RSS gate; `python3 tests/large_proof.py --self-test` passes when budgets changed.
- `CHANGELOG.md` and the affected docs are updated; versions remain derived from `include/ki_xml_pdf.h`.
- CI is green on the PR head and again on the merged `main` commit.

A green CI run does not create a version tag or GitHub Release; releasing is a separate step described in [Release and distribution](../deployment.md).

## Related pages

- [Development workflow](development-workflow.md) for the day-to-day loop.
- [Testing](testing.md) for every check and how to run it.
- [Debugging](debugging.md) for reading failures and known traps.
- [Tooling](tooling.md) for build targets, CI jobs and version derivation.
- [Patterns and conventions](patterns-and-conventions.md) for the code style rules.
