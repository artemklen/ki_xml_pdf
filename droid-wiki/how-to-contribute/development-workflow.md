# Development workflow
Active contributors: Artem

Work from a reproducible Linux/glibc build, add a regression, and submit the change with revision-specific evidence. Branch submission, acceptance and tag publication are separate steps.

## Local iteration

From the repository root:

```sh
make
make test
./xml2pdf --profile report-v1 examples/report-v1.xml /tmp/report.pdf
```

GNU Make uses incremental dependency files; a header change rebuilds its dependants.
`make check` deliberately cleans before rebuilding, so it is a broader acceptance run, not the shortest edit loop.
For an out-of-tree alternative, follow [Getting started](../overview/getting-started.md).

## Find the change entry point

| Behavior | Source | Regression starting point |
| --- | --- | --- |
| Public validation, routing, publication | `src/convert.c` | `tests/test_stream.c`, `tests/test_callback_io.c` |
| XML subset and consumed positions | `src/xml_parse.c` | `tests/test_xml_subset.c`, `tests/test_xml_coords.c` |
| Font loading, naming, metrics and ink | `src/font.c` | `tests/test_font_metrics.c` |
| Word wrapping and page lines | `src/layout.c` | `tests/test_layout_wrap.c` |
| Point geometry and font fitting | `src/geometry.c` | `tests/test_geometry.c` |
| Optional document information | `src/info.c` | `tests/test_info.c` |
| PDF objects, compression and quotas | `src/pdf_write.c` | `tests/test_pdf_bounds.c`, `tests/test_flate.c` |
| report-v1 grammar/selection | `src/report.c` | `tests/test_stream.c` |
| CLI parsing, streams and signals | `src/main.c` | `tests/cli_pipeline.py`, feature check scripts |
| Installation/discovery | `Makefile`, `CMakeLists.txt`, `cmake/` | `tests/build_integration.sh` |

## Add evidence before asking for review

1. Make the smallest synthetic reproduction.
2. Add the regression to the owning suite; make it fail on the pre-fix code.
3. Check public status/cause, output text or geometry, resource ownership and cleanup as relevant.
4. Exercise failures as well as successful rendering.
5. Run the checks appropriate to the change, recording revision and command output.

C extension suites are included by `tests/test_stream.c`, not built as independent programs.
Register new groups in its test table.
`tests/verify.sh` derives the expected group count from that table; it does not require an obsolete fixed count of 16.
See [Testing](testing.md) for the full matrix.

## Submit and accept

Use a focused branch and pull request against `main`.
Recent task commits use tracker-prefixed imperative summaries such as `OSR-89 State that only Linux with glibc is supported.`
That is repository history, not a requirement to invent a tracker identifier.

The PR should explain the observable change, regression and check evidence.
Follow `CONTRIBUTING.md` and `.github/pull_request_template.md`.
Release acceptance also requires independent review and full CI on the exact prepared head.

`.github/workflows/ci.yml` runs for pull requests, pushes to `main` and manual dispatch.
A push to a child branch alone is not a CI trigger.
PR jobs check out the PR head SHA rather than GitHub's synthetic merge ref.
After merge, verify CI on the actual `main` merge commit.

## Versions and archives

`include/ki_xml_pdf.h` defines `KI_XML_PDF_VERSION`, currently 0.3.0.
Make, CMake, pkg-config and CLI versions derive from it.
`make dist` instead reads that header **at Git HEAD** and archives committed content only.
An uncommitted version or source edit is absent from the archive.

Preserve the 0.2 struct layouts and older ELF nodes when working on the additive 0.3 API.
CMake's pre-1.0 same-minor package policy is distinct from that concrete ABI continuity.
See [C API](../api/c-api.md) and [Packaging](../features/packaging.md).

## Key source files

| File | Role |
| --- | --- |
| `CONTRIBUTING.md` | Contribution rules |
| `.github/pull_request_template.md` | Required review evidence |
| `.github/workflows/ci.yml` | Checks on PR heads and `main` |
| `docs/release-checklist.md` | Exact-revision acceptance and separate release procedure |
| `include/ki_xml_pdf.h` | Version and public contracts |

Related: [Contributing](index.md), [Tooling](tooling.md), [Distribution and releases](../deployment.md).
