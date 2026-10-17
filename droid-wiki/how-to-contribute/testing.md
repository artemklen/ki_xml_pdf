# Testing
Active contributors: Artem

The repository combines C regressions, independent PDF readers, fault injection, installed consumers and ordinary-process resource measurements. A passing writer self-check alone is not sufficient.

## Main commands

Run these from the repository root on 64-bit Linux/glibc with the [test dependencies](../reference/dependencies.md).

| Command | Actual scope |
| --- | --- |
| `make test` | C regression binary, `tests/pdf_offsets.py`, `tests/pdf_streams.py` |
| `make check` | Clean build, `make test`, `tests/verify.sh`, examples, CLI/font/wrap/geometry/Info/integration/catalog Python checks |
| `make test-san` | C regression binary with ASan/UBSan and fatal diagnostics |
| `bash tests/build_integration.sh` | Installed Make/CMake × C/C++ × static/shared × CMake/pkg-config matrix |
| `bash tests/docs_smoke.sh` | Documented shell syntax, links/anchors, executed English onboarding and mapper examples, installed clients and PDF oracles |
| `make distcheck` | Exact-HEAD archive, clean extraction/install, clients, CLI/report/catalog and integration |
| `python3 tests/fuzz_smoke.py` | Bounded seeded XML/font mutations and independent reading |
| `python3 tests/large_proof.py --self-test` | Negative controls for the resource judge |
| `python3 tests/large_proof.py 1073741824` | Full 1 GiB validation with fixed-1000 report selection |

CTest registers `stream`, `catalog`, `cli_pipeline`, `pdf_offsets`, `pdf_streams`, `wrap_check`, `geometry_check`, `info_check` and `integration_check`.
It does not replace `make check`, sanitizer, archive or installed-consumer acceptance.

## C regression map

`tests/test_stream.c` includes ten focused C extension files and registers 60 groups at this revision.
`tests/verify.sh` derives its expected count from that table, requires at least 16, and checks the final success marker.

| Source | Coverage |
| --- | --- |
| `tests/test_stream.c` | Fragmented reads, entities, CDATA, atomic output, quotas, result reuse, report selection, cancellation and concurrent contexts |
| `tests/test_xml_subset.c` | XML Char/UTF-8 validation in rendered and skipped contexts, declaration/DTD/PI rules and corpus |
| `tests/test_xml_coords.c` | Consumed line/column/byte positions, overflow controls, profile and custom failures |
| `tests/test_font_metrics.c` | Font table bounds, names, zero advances, mark runs, replacement before embedding |
| `tests/test_pdf_bounds.c` | Classic-xref ceiling and exact output quotas |
| `tests/test_flate.c` | Stream inflation, expansion, compressor failures and damaged-reader controls |
| `tests/test_layout_wrap.c` | Space/tab opportunities, long tokens, mark clusters, fragment/page invariance and bounded token memory |
| `tests/test_geometry.c` | Config sizes, invalid values, geometry matrix, old-call behavior and outline/ink bounds |
| `tests/test_info.c` | Metadata validation, copying, size compatibility, safe strings and byte quotas |
| `tests/test_integration.c` | Combined `_cfg` calls, independent contexts, error cleanup and caller ownership |
| `tests/test_callback_io.c` | One-shot negative/short/zero/over-count writes, no retry after failure, exact accepted-byte accounting |

`Makefile` links internal tests with `-pthread` and GNU `--wrap` for `fsync`, `deflate` and `deflateInit2_`.
The fsync wrapper makes cancellation after sync deterministic.
zlib wrappers exercise memory/compressor failure branches rather than hoping to exhaust the host.

## Independent PDF checks

| Runner | Oracle |
| --- | --- |
| `tests/cli_pipeline.py` | Fixed report text; all four file/stdin → file/stdout routes, usage, signals and broken outputs |
| `tests/catalog_verify.py` | Fixed catalog text, one-byte reads, long values, schema positions and previous-file safety |
| `tests/font_metrics_check.py` | Non-default font names/metrics, mark extraction and raster checks |
| `tests/wrap_check.py` | Expected wrapped words and marks through Poppler |
| `tests/geometry_check.py` | Expected MediaBoxes and actual raster ink inside margins |
| `tests/info_check.py` | Expected `/Info`, Unicode/date round trips, syntax-injection and quota controls |
| `tests/integration_check.py` | Geometry + wrapping + non-default font + marks + Flate + `/Info`, both profiles and four routes |
| `tests/pdf_offsets.py`, `tests/pdf_streams.py` | xref targets, stream lengths, Flate round trips and deliberate corruption |

Use text extraction for content, structure readers for xref/streams and raster output for ink.
Subsetting research demonstrated that missing outlines can leave extracted text unchanged.
`tests/pdf_inflate.c` provides the C-side stream decoder for regressions.

## Installed clients and ABI

`tests/build_integration.sh` exercises 16 consumer rows plus four installed catalog variants.
It checks actual ELF linkage, version/soname, installed font/notices, foreign working directories and explicit-font failures.
`tests/prefix_switch.sh` covers changing the compiled install prefix without a clean.
`tests/install_smoke.sh` is a smaller installation smoke check.

Prepare old binaries once with `tests/abi_canary/build_canary.sh PREFIX DEST` against an installed 0.2 prefix; do not rebuild them against the new header.
`tests/abi_canary/run_canary.sh BINDIR LIBDIR FONT OUTDIR` runs those separately prepared C/C++ binaries.
It verifies their recorded checksums and loaded library path, then reads their PDFs through Poppler.
Supply an empty output directory; the runner does not compile the old binaries.
This is concrete old-binary evidence, not a promise that every future 0.y ABI is compatible.

## Resource evidence

`tests/verify.sh` measures the ordinary converter on a 16 MiB synthetic document:
RSS ≤4096 KiB, below half the input, and within the small reference plus 4096 KiB.
It also checks rendered markers, page bounds and readable text.
`tests/large_proof.py` separately caps the selected-output proof at 10,000 pages, 256 MiB PDF and 64 MiB spool.
These are workload gates, not a universal RSS ceiling for every accepted font or caller callback.

`tests/resource_baseline.py matrix` runs six full-output cases (both profiles, 1/16/64 MiB) and three fixed-1000 selections (16/256/1024 MiB).
`compare` evaluates revision-specific evidence against the `075455c` baseline.
`tests/resource_recipe.py`, `tests/resource_oracle.py` and `tests/resource_measure.py` separate fixture generation, conversion, Poppler reading and streaming-oracle processes.
`tests/resource_controls.py` supplies real-PDF negative/quota/tail controls; `tests/resource_baseline_test.py` checks the recipe/oracle without the converter.

The extended resource matrix and ABI canaries are acceptance tools, not implicit steps of every CI job.
CI explicitly runs `make check` and the large proof in its resource job.
Historical OSR-82 Massif/RSS evidence measures `de8ba0e`, before later font/CMap/page-buffer reductions; it is not a current heap measurement.

## Safety and provenance

Sanitizer RSS is not ordinary RSS; in-process RSS gates disable themselves under ASan.
`tests/fuzz_smoke.py` is a seeded smoke test, not exhaustive fuzzing.
`make distcheck` uses committed HEAD; it cannot validate uncommitted edits by archiving them.
CI asserts failures for bad C/C++ compilers, a failing reader, a wrong oracle and a missing license.

## Key source files

| File | Role |
| --- | --- |
| `Makefile`, `CMakeLists.txt` | Exact target and CTest membership |
| `.github/workflows/ci.yml` | Executed CI gates |
| `tests/verify.sh` | Synthetic 16 MiB acceptance and group counting |
| `docs/release-checklist.md` | Release acceptance evidence |

Related: [Debugging](debugging.md), [Tooling](tooling.md), [Resource limits](../features/resource-limits.md).
