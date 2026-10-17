# Testing

All checks live in `tests/`: one C unit binary, five Python suites, and shell scripts for integration and distribution. This page lists every suite, how to run it, what it proves, and the measurement rules for memory.

## Quick commands

| Command | Covers |
| --- | --- |
| `make test` | Builds and runs `tests/test_stream.c` (unit and API groups) |
| `make check` | Clean rebuild, `make test`, `tests/verify.sh`, `make example`, `tests/cli_pipeline.py`, `tests/catalog_verify.py` |
| `make test-san` | The same unit binary under ASan and UBSan |
| `make distcheck` | `make dist`, then `tests/distcheck.sh` on the archive |
| `python3 tests/cli_pipeline.py` | CLI pipelines, quotas, signals, publication |
| `python3 tests/catalog_verify.py` | The public catalog mapper example |
| `python3 tests/fuzz_smoke.py` | Bounded seeded fuzz with a PDF reader |
| `python3 tests/large_proof.py --self-test` | Negative controls for the resource judge |
| `python3 tests/large_proof.py 1073741824` | 1 GiB report with RSS/page/spool budgets |
| `tests/build_integration.sh` | Installed package matrix (Make/CMake, C/C++, static/shared) |
| `tests/docs_smoke.sh` | Executes the documented command blocks and checks oracles |
| `tests/install_smoke.sh`, `tests/prefix_switch.sh` | Install into private prefixes |
| `ctest --test-dir build --output-on-failure` | The CTest registration: `stream`, `catalog`, `cli_pipeline` |

## Unit tests: `tests/test_stream.c`

One 3,259-line C file holds 16 test groups. Each group is a `static int test_*(void)` returning 0 on success; `main()` runs them in order, prints `ok <group>` per passing group and `FAILED <group>` on failure, and exits non-zero if anything failed. `tests/verify.sh` re-runs the binary and requires exactly 16 `ok` lines, so adding a group means updating that count.

| Group (`rg '^static int test_' tests/test_stream.c`) | Covers |
| --- | --- |
| `test_big_stream_fixed_memory` | A large input pulled through small reads with the RSS gate on ordinary builds |
| `test_split_mid_tag_and_text` | Tags and text split across read boundaries |
| `test_one_byte_entities_and_nesting` | One-byte reads, predefined entities, nesting |
| `test_skips_and_errors` | Comments, declarations, error paths, repeatable error offsets |
| `test_pdf_appends_and_releases` | Writer append/release behavior, spooled offsets, object limits |
| `test_convert_pipeline` | End-to-end conversions through the public entry points |
| `test_cdata_and_numeric` | CDATA and numeric character references |
| `test_cdata_stream_fixed_memory` | CDATA streaming across reads with fixed memory |
| `test_output_keeps_previous` | Atomic publication preserving a previous destination |
| `test_wellformed_profile` | records-v1 rendering of well-formed documents |
| `test_review_regressions` | Regressions from reviews, including the corpus files below |
| `test_library_contract` | Options validation, result reuse, quotas, cancellation, concurrency |
| `test_callback_usage_result` | `ki_convert` usage errors and result initialization |
| `test_callback_to_file` | `ki_convert_to_file`, callback input into an atomic file |
| `test_report_v1` | report-v1 mapping, selection, counters, byte offsets |

The binary links with `-pthread -Wl,--wrap=fsync` (the `test_stream` target in `Makefile`). `__wrap_fsync` calls `__real_fsync`, then flags that it returned. A test arms `arm_fsync_cancel`, and its `cancelled` callback fires only after that flag is set, which proves deterministically that a cancellation during `fsync` never publishes the PDF and preserves the previous file. `pthread` covers two concurrent conversion contexts, which the header contract allows.

`KI_RSS_GATE` in `tests/test_stream.c` disables the in-process memory checks under ASan (detected via `__SANITIZE_ADDRESS__` or `__has_feature`), because sanitizer shadow memory would break them.

## CLI and memory gate: `tests/verify.sh`

Run by `make check` after `make test`. It requires `python3`, `pdfinfo`, `pdftotext`, `pdftohtml` and `/usr/bin/time`. It uses a scratch directory: set `SCRATCH` to keep it, otherwise it makes one under `TMPDIR`/`/tmp`.

The script, in order:

1. Checks CLI exit codes: `--help` and `--version` return 0, no arguments return 2, and the version line must equal the header version from `include/ki_xml_pdf.h`.
2. Re-runs `./test_stream` and requires exactly 16 `ok` groups.
3. Runs `./xml2pdf tests/fixture.xml` twice. An embedded Python reader decodes the PDF page streams through the embedded ToUnicode map (not Poppler) and compares the two runs for identical recovered text, page count, first/last-page markers, and unescaped entities (`alpha&beta` present, `alpha&amp;beta` absent).
4. Builds a 16 MiB synthetic XML and measures both it and the small fixture with `/usr/bin/time -v`. The gate: large-run RSS must be under half the input size and within small-run RSS plus 4 MiB, the PDF must be larger than the RSS, and first/last markers must appear on the first and later pages. Results land in `mem.txt` in the scratch directory.
5. Reads the run through Poppler: `pdfinfo` reports at least 2 pages, `pdftotext -layout` page 1 and the last page match expected markers, a 90-character line stays inside the 572 pt margin (`pdftotext -bbox`), Cyrillic appears in both `pdftotext` and `pdftohtml`, and an unsupported character (U+1F600) is rejected without publishing a PDF.
6. Removes the large inputs, fails if any file over 8 MiB remains in scratch, and records tool versions plus the Git commit in `versions.txt`.

## Oracles

An oracle is produced independently of the writer. The suites use three kinds:

- **Independent reader oracles**: `examples/report-v1.expected.txt`, `examples/catalog.expected.txt` and the `doc`/`Hello` pair for `examples/hello.xml`, compared against `pdftotext -layout` output in `tests/cli_pipeline.py`, `tests/catalog_verify.py`, `tests/build_integration.sh`, `tests/docs_smoke.sh` and `tests/distcheck.sh`.
- **A second decode path**: the ToUnicode recovery in `tests/verify.sh` decodes page streams with its own code, so a Poppler bug cannot hide a writer bug in the two-CLI-run comparison.
- **Generator oracles**: `tests/large_proof.py` computes the expected selected text and counters from the generator itself, so the writer cannot be both producer and judge.

A wrong oracle must fail the suite: `make distcheck DISTCHECK_ORACLE=/tmp/bad-oracle.txt` is expected to fail, and CI asserts that it does.

## Fixtures and corpus

| File | Used for |
| --- | --- |
| `tests/fixture.xml` | The 80 KB mixed document for the two CLI runs and the small-file RSS baseline in `tests/verify.sh` |
| `examples/hello.xml` | Minimal document for consumers and install smoke |
| `examples/report-v1.xml` + `examples/report-v1.expected.txt` | report-v1 golden input and text oracle |
| `examples/catalog.xml` + `examples/catalog.expected.txt` | Catalog example golden input and oracle |
| `tests/corpus/split_whitespace.xml` | CDATA whitespace split across a fragment boundary |
| `tests/corpus/utf8_512.xml` | Multi-byte UTF-8 text |
| `tests/corpus/truncated-hmtx.ttf` | A deliberately truncated font, rejected by `font_load` in direct, environment-variable and explicit-path tests |
| `tests/consumer/` | A CMake consumer project (`report.c`) built against installed packages by `tests/build_integration.sh` |

## CLI pipelines: `tests/cli_pipeline.py`

`python3 tests/cli_pipeline.py [--binary ...] [--fixture ...] [--oracle ...] [--pdftotext ...]`; the reader also comes from the `PDFTOTEXT` environment variable. Scenarios:

- All four file/stdin to file/stdout combinations: exit 0, stdout carries only PDF bytes (ending `%%EOF` when streamed), diagnostics on stderr, and `pdftotext` text equal to the report oracle.
- `-h`, `--help`, `--version`, and `--` handling for paths that begin with `--`.
- Ten invalid argument shapes, all exiting 2 with empty stdout.
- Late XML error and page, output and spool quota failures (exit 4) preserving an `OLD` destination byte-for-byte, with no temporary PDF or spool file left behind.
- A real `SIGTERM` sent while the CLI blocks reading stdin: exit 4, destination and cleanup preserved.
- `stdout` to `/dev/full` and to a closed pipe: exit 3 with diagnostics, not a silent `SIGPIPE` death.
- A late XML error on stdout producing a partial PDF that starts with `%PDF-` and lacks `%%EOF`.
- The default records-v1 profile rendering `["doc", "Hello"]` from both file and pipeline input.

## Catalog mapper: `tests/catalog_verify.py`

`python3 tests/catalog_verify.py` with the same flag set as `cli_pipeline.py`. It drives the built example (default `examples/catalog`). Success cases run three times with callback chunk sizes `None`, `1` and `4096`, proving short reads and one-byte delivery change nothing. The long-field case pushes a value over 4096 UTF-8 bytes spanning TEXT, CDATA and character references and compares every character. Rejection cases cover the grammar rule, cause, status, byte offset, an unchanged previous file (same bytes and inode) and no leftover temporaries; usage cases cover chunk parsing and a zero page quota. The profile failures check cause 7, the unsupported glyph cause 5 with its `U+` code, and quota cause 4.

## Fuzz smoke: `tests/fuzz_smoke.py`

A bounded, seeded run (seed 20261007, 24 iterations, one to seven single-byte mutations each, payloads capped at 4096 bytes) mutating `examples/report-v1.xml`. Every conversion runs under `ulimit -v` of 256 MiB with a 5 second timeout; a crash, timeout or undocumented exit code writes the input to `fuzz-crash/` and fails the suite. Successful mutants are read back with `pdftotext`; short payload values are cross-checked with Python's ElementTree as a sanity projection, never as an acceptance oracle. Truncation cuts at 0, 1, 4095, 4096, 4097 and one-from-the-end bytes must exit 1. Font controls feed truncated and mutated DejaVu samples plus `tests/corpus/truncated-hmtx.ttf`, expecting exit 1, a preserved old output and no leaked temporaries. The golden input is checked against the fixed full-text oracle first.

## Large proof: `tests/large_proof.py`

`python3 tests/large_proof.py 1073741824` builds a synthetic report of the requested size (default 134217728), converts it selecting 1000 records, and judges four budgets: converter RSS at most 65,536 KiB, at most 10,000 pages, a PDF of at most 256 MiB, and a spool of at most 64 MiB. The selected text must equal the generator's expected lines, including the Russian counters. The reader runs separately under `ulimit -v` of 1 GiB, and first, middle and last pages are spot-checked. `--self-test` runs negative controls against the judge (over-budget RSS, pages, PDF, spool, a missing metric, and a changed value) and fails if any is accepted. Extra cases prove offset/limit selection keeps only the chosen record and that a late invalid record preserves the previous PDF. Inputs are removed on success and failure.

## Installed packages: `tests/build_integration.sh`

Copies the sources into a disposable directory (no Git checkout), installs through both Make and CMake with `DESTDIR` staging, then verifies the staged file list, the `libki_xml_pdf.so.0` soname via `readelf`, the pkg-config version and prefix, and that neither `DESTDIR` nor the source tree leaked into installed metadata or the compiled font path. It then runs 16 consumer rows (Make/CMake install × C/C++ × static/shared × CMake/pkg-config discovery), each from a foreign working directory against the report oracle, plus the installed CLI, four catalog variants through `tests/catalog_verify.py`, and negative controls for a missing installed font and an explicit missing `KI_XML_PDF_FONT`.

## Documented commands: `tests/docs_smoke.sh`

Extracts the fenced `sh` blocks from `README.md`, `README.ru.md`, `CONTRIBUTING.md`, `docs/report-v1.md` and `docs/mapper-guide.md`, syntax-checks them with `bash -n`, verifies every local link and anchor, and executes the documented blocks in private prefixes. Every produced PDF is checked against its oracle through `pdftotext`, and static/shared linkage is verified with `readelf`. Editing a documented command without running this script is how documentation rot starts.

## Distribution: `tests/distcheck.sh`

Run via `make distcheck`. It unpacks the `git archive` produced by `make dist`, requires a single archive root named after the header version, verifies a required-file list, rejects any `.git` metadata or private workspace paths, installs with Make into a disposable prefix, builds the C and C++ consumers against the installed static and shared libraries and runs them from `/`, checks the report oracle, runs the installed CLI pipelines, and finally runs `tests/build_integration.sh` expecting 16 consumer rows and 4 catalog variants. `DISTCHECK_ORACLE` replaces the oracle (a mismatch must fail) and `DISTCHECK_BREAK=license` deletes `LICENSE` (the run must fail). CI asserts all these negative controls fail.

## Sanitizers

`make test-san` compiles all library sources plus `tests/test_stream.c` with `-fsanitize=address,undefined -fno-omit-frame-pointer` and runs with `ASAN_OPTIONS=halt_on_error=1:detect_leaks=1` and `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`. The first error fails the suite. In-process RSS gates are disabled under ASan, so sanitizer runs never replace a memory measurement. CI runs this in the `clang` and `asan` jobs.

## RSS measurement rules

The rules `CONTRIBUTING.md` sets, enforced by the scripts:

- Measure an ordinary, uninstrumented `xml2pdf` with `/usr/bin/time -v` (or `-f rss_kib=%M`). The XML generator, the PDF reader and the sanitizer builds are excluded.
- The verify.sh gate is relative (below half the 16 MiB input, within the small run plus 4 MiB); the release ceiling is the 64 MiB converter RSS budget in `tests/large_proof.py`.
- Historical numbers stay tied to the revision that produced them (2372 KiB on `7c62fe4`); new claims need new measurements.
- Poppler runs under its own address-space limit, separate from the converter measurement.

## Related pages

- [Tooling](tooling.md) for the build targets and CI jobs that run these suites.
- [Debugging](debugging.md) for reading failures the suites surface.
- [Patterns and conventions](patterns-and-conventions.md) for the code rules the unit tests enforce.
- [Resource limits](../features/resource-limits.md) and [Atomic output](../features/atomic-output.md) for the behaviors under test.
