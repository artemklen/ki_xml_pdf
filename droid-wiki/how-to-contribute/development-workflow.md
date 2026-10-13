# Development workflow

The working loop is: build, change, add a regression, run the matching checks, commit with the tracker prefix, then one PR into `main`. This page walks through one iteration on a Linux machine with the tool list from [Getting started](../overview/getting-started.md).

## Set up

```sh
git clone https://github.com/artemklen/ki_xml_pdf.git
cd ki_xml_pdf
make
./xml2pdf --profile report-v1 examples/report-v1.xml /tmp/report.pdf
make test
```

The tested platform is 64-bit Linux with glibc. `CMakeLists.txt` refuses to configure elsewhere, and `ki_convert` needs glibc's `fopencookie`, so there is no point trying to iterate on another system; edit on any machine, but run the checks on Linux.

## Find the owner of the behavior

Pick the file that owns the behavior before editing:

| Behavior | Owner | Page |
| --- | --- | --- |
| Options validation, event loop, atomic publication, error mapping | `src/convert.c` | [Conversion pipeline](../systems/conversion-pipeline.md) |
| XML syntax, bounds, entity decoding | `src/xml_parse.c` | [XML parser](../systems/xml-parser.md) |
| Wrapping, page assembly, UTF-8 | `src/layout.c` | [Layout and fonts](../systems/layout-and-fonts.md) |
| TrueType metrics, repertoire | `src/font.c` | [Layout and fonts](../systems/layout-and-fonts.md) |
| PDF structure, offsets spool, quotas | `src/pdf_write.c` | [PDF writer](../systems/pdf-writer.md) |
| report-v1 grammar and selection | `src/report.c` | [report-v1](../features/report-v1.md) |
| CLI flags, streams, signals | `src/main.c` | [CLI](../features/cli.md) |
| Public contract | `include/ki_xml_pdf.h` | [C API](../api/c-api.md) |

## Write the change

Follow [Patterns and conventions](patterns-and-conventions.md). The short version: fixed-size state only, 0/-1 return codes with the reason recorded on the owning struct, `goto done` cleanup, and no new global state or signal handlers in the library.

Build incrementally with `make` and try the CLI directly. For a faster inner loop on unit tests, `make test` rebuilds only what `tests/test_stream.c` needs.

## Add the regression

Put the regression where the behavior lives:

- Parser, writer, layout, library contract: add a case to an existing group in `tests/test_stream.c`, or a new group registered in `main()` there. `tests/verify.sh` counts exactly 16 `ok` group lines, so a new group means updating that count.
- CLI behavior, streams, publication: extend `tests/cli_pipeline.py`.
- report-v1 mapping: extend `tests/test_stream.c` (`report_v1` group) and keep `docs/report-v1.md` aligned.
- Custom mapper example: extend `tests/catalog_verify.py` and the oracle `examples/catalog.expected.txt` when output changes intentionally.
- Build, install, packaging: extend `tests/build_integration.sh`, `tests/docs_smoke.sh` or `tests/distcheck.sh`.

The regression must fail before your fix and pass after. Check text through `pdftotext` rather than by matching the writer's own bytes. Details and commands are on [Testing](testing.md).

## Run the matching checks

| Change | Checks |
| --- | --- |
| Parser, layout, font, writer internals | `make test`, `make test-san` |
| Conversion contract, CLI, streams | `make check` |
| report-v1 rules or labels | `make check`, update `examples/report-v1.expected.txt` if intended |
| Packaging, install, discovery | `tests/build_integration.sh`, `make distcheck`, `tests/docs_smoke.sh` |
| Memory or quotas | `make check`, `python3 tests/large_proof.py --self-test`, then the sized run |
| Docs with commands | `tests/docs_smoke.sh` (it executes the documented blocks) |

## Commit

Stage the code, the regression and the doc update in one commit per coherent step. The message is the tracker prefix plus an imperative summary, matching `git log` on `main`:

```
OSR-4 Support CLI streams and atomic callback-to-file output
```

Small fixes can share a prefix when they belong to one task. Do not mix unrelated changes into one commit; reviewers read commits, not just the aggregate diff.

## Open the pull request

One PR against `main`, with the evidence in the description: what changed, the regression, the check commands and their results, the source revision, and any check that could not run. CI runs on the PR head once the PR is open; a branch push without an open PR triggers nothing. After review and green CI, merge and watch the post-merge CI run on the merge commit, which `docs/release-checklist.md` requires.

## Version and changelog

The version lives in one place: the `KI_XML_PDF_VERSION` define in `include/ki_xml_pdf.h`. The Makefile, CMake, the CLI, pkg-config and the archive name all derive from it, so a version bump is a one-line change plus a `CHANGELOG.md` entry, committed before `make dist` (the archive records Git HEAD, not the working tree). A merged PR does not tag or publish anything; see [Release and distribution](../deployment.md).

## Related pages

- [How to contribute](index.md) for the process view and definition of done.
- [Testing](testing.md) and [Debugging](debugging.md) for the checks and failure triage.
- [Patterns and conventions](patterns-and-conventions.md) for code rules.
