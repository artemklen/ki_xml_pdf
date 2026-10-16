# OSR-9 Flate evidence (3acf0cb)

Measured source: `3acf0cb86833bfd44462543afefe77d3bf971f86`, built in an isolated clone on the same machine as the OSR-23 baseline (`075455c`, `docs/evidence/osr-23-baseline-075455c/baseline`). Same runner (`tests/resource_baseline.py matrix --cases full-report-16MiB,full-records-16MiB`), same fixture recipe, profile, quotas and font. Paths use `<repo>`, `<evidence-root>` (OSR-23 evidence root) and `<osr9-root>` (this run's scratch root); `full-16MiB/SHA256SUMS` hashes the files before that substitution.

## 16 MiB full output against the uncompressed baseline

| Case | Fixture SHA-256 | Baseline PDF bytes | Flate PDF bytes | Ratio | Pages (both) | Records checked (both) | Converter RSS KiB base → Flate |
|---|---|---:|---:|---:|---:|---:|---|
| full-report-16MiB (agreed target) | `2cc851a4f8f15142…` | 86,884,343 | 12,501,989 | 14.39% | 9,855 | 246,368 / 246,368 | 2,524 → 2,804 |
| full-records-16MiB | `76eb12d9bdc6bf36…` | 180,555,585 | 29,296,558 | 16.23% | 29,999 | 499,983 / 499,983 | 2,420 → 2,804 |

Font SHA-256 `57f73e11f51999432bf7ab22ce55b6f945d5eca1bf824404cfa9ec2e3718c84e` (DejaVu Sans) in both. The target of at most 50% is met. Every record of both outputs passed the independent streaming oracle in order through `pdftotext`, which decodes the Flate streams. `full-16MiB-vs-baseline.json` is the runner comparison: settings and fixture hashes match, no problems; PDF bytes, hashes and object counts differ as expected (a page now has a contents-length object). The RSS increase of about 280 KiB is the zlib deflate state and its 16 KiB output window; all OSR-23 gates still pass.

## Stream checks and controls

- `pdf-streams.txt`: `tests/pdf_streams.py --binary` through a stdout pipe. Inventory lists object, filter, declared and decoded length: every page contents stream and the font are FlateDecode, the decoded font hashes to the font file and `/Length1` equals 759,720. The independent reader rejects a damaged page body, a lost deflate tail, a body cut in half, an output truncated inside a page stream, a wrong length-object value and a wrong `/Length1`.
- `controls.json`: OSR-23 oracle, tail and quota controls rerun on the Flate binary, plus a new `oracle-reordered-values` control. A missing record, a changed digit and two swapped record values in the reader stream, and a fixture lacking a record, all fail the oracle; page, output and spool quotas keep the old destination; the 8 MiB font cap is unchanged.
- C tests (`tests/test_flate.c`): minimal and multipage inventories, a low-compressibility font whose deflate output spans many 16 KiB windows, deflate faults injected with `--wrap` (`Z_MEM_ERROR` from init gives RESOURCE; `Z_STREAM_ERROR`, `Z_DATA_ERROR` and no-progress `Z_BUF_ERROR` on the first and last deflate call give IO "pdf compression failed"; file destinations kept and no temp left), and reader controls.
- `sanitizers/`: GCC and Clang ASan/UBSan `make test-san` in the isolated clone, all groups pass with no reports.
- `make-check-verify.txt`: `make check` RSS gate lines (16 MiB input, 2,800 KiB RSS).
- `build-integration.txt`, `distcheck.txt`, `abi-canary.txt`: installed Make/CMake static and shared C/C++ consumers for GCC and Clang (CTest includes `pdf_streams`), clean `make distcheck`, and the precompiled 0.2 C/C++ ABI canaries run against the new shared library without a rebuild. The new `.so` lists `libz.so.1` as NEEDED and exports the same symbols as 0.2.
