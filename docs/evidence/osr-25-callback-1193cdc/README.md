# OSR-25 final evidence after the callback fix (1193cdc)

Measured source: `1193cdc847f1dba06e6b345a1c8775f534ce89f8` (version 0.3.0). It is merged `main` `7232bcc` plus one product commit: a single negative, short, zero or over-count `write_bytes` return now fails `ki_convert` and `ki_convert_cfg` with `KI_CAUSE_IO` instead of returning success with an invalid `startxref`. Every check below ran on that one revision, sequentially, in a fresh clone with no tracked changes, on the host described in the [OSR-23 baseline](../osr-23-baseline-075455c/README.md). The commit that adds this directory changes only documentation.

The earlier records stay as they were and are historical: [48bdef2](../osr-25-integration-48bdef2/README.md) (integrated 0.3.0 before the fix) and [d95a5c1](../osr-25-corrective-d95a5c1/README.md) (test-only harness correction). Those revisions contain the callback defect. Their measurements are not reused here as evidence for 1193cdc; the nine resource cases, the ABI checks and all gates were run again.

Paths are normalized: `<final-root>` is this run's external directory (clone in `<final-root>/checkout`, disk-backed scratch), `<osr25-root>` the 48bdef2 run's directory, `<fix-root>` the directory where 1193cdc was written, `<evidence-root>` the OSR-23 evidence root (old 0.2 prefix and canaries), `<mission-root>` the validation evidence of the failed 7232bcc check, `<repo>` a harness checkout. No other byte was changed. `SHA256SUMS-originals` lists the hashes of every file here before that substitution; `resource/SHA256SUMS` was written by the runner at capture time.

## Gates

`gates/run_gates.sh` ran the 20 commands in `gates/summary.tsv`; every command exited 0. Each log starts with the command and `HEAD 1193cdc…`.

| Gate | Result | Log |
| --- | --- | --- |
| `make test`, `make check` (includes `cli_pipeline`, `catalog_verify`, `integration_check`, the RSS gate) | 60 groups ok, including `callback_one_shot_write`, `callback_accepted_bytes`, `integration_contexts`, `big_stream_fixed_memory` | `01-make-test.txt`, `02-make-check.txt` |
| `make CC=gcc test-san`, `make CC=clang test-san` (ASan/UBSan) | 60 groups ok each, no sanitizer report | `03-san-gcc.txt`, `04-san-clang.txt` |
| Clang and GCC clean rebuilds with functional tests | ok | `05-…`, `06-…` |
| `tests/fuzz_smoke.py` | seed 20261007, 24 iterations, 11 XML corpus files, 6 fonts, ok | `07-fuzz.txt` |
| `tests/font_fixtures.py --check` (184-byte truncated cmap) | ok | `08-font-fixtures.txt` |
| CLI (24 scenarios), catalog (success=12 rejected=72 usage=5), integration (3 cases × 4 routes, 8 failure runs), `--help`, `--version` | `xml2pdf 0.3.0` | `09-cli-catalog.txt` |
| `tests/build_integration.sh` with GCC and with Clang | 16 installed C/C++ static/shared Make, CMake-target and pkg-config rows each, CTest and Poppler oracle | `10-…`, `11-…` |
| `tests/docs_smoke.sh`, header dependency, `tests/prefix_switch.sh` | ok | `12-…`, `13-…`, `16-…` |
| `make distcheck` and five negative controls (`CC=false`, `CXX=false`, failing reader, wrong oracle, missing license) | archive ok, each control failed as required | `14-distcheck.txt`, `15-distcheck-negative.txt` |
| CMake build and CTest | 9/9 tests passed | `17-cmake-ctest.txt` |
| Resource runner unit tests, `large_proof.py` 1 GiB selection | 19,522,650 records, 41 pages, 432,803 bytes, RSS 2756 KiB | `18-…`, `19-large-proof.txt` |
| Install, pkg-config, CMake version file, ELF metadata | `--modversion` 0.3.0, `--static --libs` `-lki_xml_pdf -lz`, SONAME `libki_xml_pdf.so.0`, NEEDED `libz.so.1` | `20-pkgconfig-static.txt` |

The distcheck archive built from 1193cdc has SHA-256 `bc12c9e5…3729d` (`14-distcheck.txt`). The archive of the merged commit is recorded in the delivery notes, because it is built from a different commit.

## Callback lifecycle on the installed library

`lifecycle/` holds the installed-client reproducer that found the defect at `7232bcc`. `lifecycle_client.c` and `lifecycle_matrix.py` are byte-identical to the validator's copies (SHA-256 `a0ab2f82…` and `8c2ee16b…`). They were built through pkg-config against a fresh `make install` of 1193cdc (`install.txt`) and run unchanged:

- `lifecycle-matrix.txt`, `lifecycle-results.json`: 29 cases from 17 failure specifications over all six public calls, with an existing and an absent destination where a file is written. All 29 statuses and causes match. A failed write on callback call 2 now gives status 3, cause 3 `pdf write failed`, 2 write calls, and `output_bytes` equal to the accepted bytes (8192 for `-1`, 12288 for a half-short write). At `7232bcc` the same matrix had 27 of 29 matches, because these two cases reported success.
- `lifecycle_client_xref.c`, `lifecycle_xref_matrix.py`, `lifecycle-xref-matrix.txt`, `lifecycle-xref-results.json`: the same 29 cases with a structural check added. The successful reuse after each failure is saved as its own PDF. Every one of the 29 PDFs passes the independent classic xref check in `tests/pdf_offsets.py` (`startxref` points at `xref`, every entry points at its object, trailer `/Size` matches), and `pdftotext -layout` gives exactly the 160 input lines in order. The bytes captured from the failed callback calls are not a complete PDF (no `%%EOF`). For the negative and short write they are a strict prefix of the reference PDF and as long as the accepted bytes.
- `historical-7232bcc-xref-negative-control.txt`: the same xref check rejects the two PDFs that 7232bcc reported as successful (`startxref 416213 does not point at xref`). Poppler still reads those PDFs, which is why the xref check is needed.

## Version and old ABI

Header, CLI, pkg-config, CMake package version and archive name say 0.3.0, SONAME `libki_xml_pdf.so.0`. `abi/exports-old.txt` and `abi/exports-new.txt` are the global defined dynamic symbols of the preserved 0.2.0 library and the 1193cdc library: all `KI_0_2` entries are unchanged, and only `KI_0_3` with `ki_config_init`, `ki_convert_cfg`, `ki_convert_file_cfg` and `ki_convert_to_file_cfg` is added. This list is identical to the one at 48bdef2; the fix adds no symbol and changes no structure.

- `abi/run_canary.txt`: `tests/abi_canary/run_canary.sh` ran the C and C++ canaries compiled against 0.2.0 in OSR-23, without a rebuild, against the installed 1193cdc library. Each resolved `libki_xml_pdf.so.0` from that prefix; the script checks the canary hashes first and the PDFs with Poppler afterwards.
- `abi/early_probe_run.txt`: the early-USAGE and guard-byte probe compiled against the 0.2.0 header for the 48bdef2 run (hashes checked in `abi/early_probe_hashcheck.txt`) gives the same results with the 0.2.0 and the 1193cdc library.

## Resource series

`resource/` holds the nine runs of `tests/resource_baseline.py matrix` with the same runner, fixtures, options and font as the baseline. `resource-vs-baseline.json` is `compare` against `../osr-23-baseline-075455c/baseline`: settings and fixture hashes match for every case, no problems. `resource-vs-48bdef2.json` is `compare --expect-identical-output` against the 48bdef2 run: every PDF is byte-identical, which is expected because the fix changes only error handling.

| Case | Input bytes | Records checked | Pages | PDF bytes (baseline → final) | Converter RSS KiB (baseline / 48bdef2 / final) | Converter s (baseline / 48bdef2 / final) |
| --- | ---: | ---: | ---: | ---: | --- | --- |
| full-records-1MiB | 1,048,593 | 33,462 / 33,462 | 2,008 | 12,216,261 → 2,332,511 | 2396 / 2952 / 2820 | 0.12 / 0.22 / 0.22 |
| full-records-16MiB | 16,777,233 | 499,983 / 499,983 | 29,999 | 180,555,585 → 29,422,443 | 2420 / 2752 / 2892 | 1.07 / 2.91 / 2.92 |
| full-records-64MiB | 67,108,881 | 1,925,863 / 1,925,863 | 115,552 | 713,436,448 → 112,864,984 | 2396 / 2880 / 2804 | 4.27 / 11.54 / 11.52 |
| full-report-1MiB | 1,048,578 | 15,981 / 15,981 | 640 | 6,052,147 → 1,192,452 | 2340 / 2952 / 2892 | 0.07 / 0.14 / 0.14 |
| full-report-16MiB | 16,777,245 | 246,368 / 246,368 | 9,855 | 86,884,343 → 12,545,307 (14.44%) | 2524 / 2880 / 2976 | 0.73 / 1.63 / 1.63 |
| full-report-64MiB | 67,108,881 | 975,812 / 975,812 | 39,033 | 347,124,499 → 48,620,561 | 2480 / 2880 / 2816 | 2.93 / 6.38 / 6.47 |
| select-report-16MiB-limit1000 | 16,777,245 | 1,000 of 246,368 | 41 | 1,095,465 → 460,952 | 2536 / 2824 / 2816 | 0.15 / 0.17 / 0.16 |
| select-report-256MiB-limit1000 | 268,435,464 | 1,000 of 3,812,079 | 41 | 1,095,481 → 460,949 | 2340 / 2764 / 2892 | 1.78 / 1.93 / 1.88 |
| select-report-1024MiB-limit1000 | 1,073,741,895 | 1,000 of 15,013,206 | 41 | 1,095,497 → 460,955 | 2456 / 2848 / 2764 | 6.96 / 7.43 / 7.39 |

The oracle checked 3,700,469 records in total. Generator, reader and oracle RSS, disk blocks and times of every process are in `resource/runs.csv`; they grow with the rendered content and are not constants. Selection rows render only 1000 records and do not stand in for full output. Full output of a 1 GiB input was not measured. The 16 MiB `report-v1` size target (at most 50% of the baseline, first met in [OSR-9](../osr-9-flate-3acf0cb/README.md)) holds at 14.44%.

| Gate | Limit | Final result | Evidence |
| --- | --- | --- | --- |
| Parser 9,586,989-byte stream and 4 MiB CDATA stream, RSS delta | < 2048 KiB | pass | `gates/01-make-test.txt` (`big_stream_fixed_memory`, `cdata_stream_fixed_memory`) |
| Full records-v1 16 MiB, RSS below half the input | < 8,388,616 B | 2892 KiB = 2,961,408 B | `resource/full-records-16MiB/run.json` |
| Full records-v1 16 MiB, RSS ≤ small reference + 4 MiB | ≤ 2860 + 4096 = 6956 KiB | 2892 KiB | `resource/ref-small-fixture/run.json` |
| `tests/verify.sh` 16 MiB gate | < half input and ≤ small + 4 MiB | 2728 KiB, small 2936 KiB, cap 7,200,768 B; pass | `verify.txt` |
| Converter RSS, all nine runs including full 64 MiB | ≤ 65536 KiB | maximum 2976 KiB | `resource/runs.csv` |
| Selection pages and PDF size | ≤ 10,000 pages, ≤ 256 MiB | 41 pages, ≤ 460,955 B | `resource/runs.csv` |
| Font file size | ≤ 8 MiB | 8,388,608 B accepted; 8,388,609 B fails with exit 1 `font: cannot load` | `controls/controls.json` |
| Metadata spool (logical offset-table bytes) | ≤ 64 MiB | maximum 2,773,320 B; `--max-spool-bytes 16` fails with exit 4 | `resource/runs.csv`, `controls/controls.json` |
| Page and output quotas | reduced quota fails as RESOURCE | `--max-pages 1` and `--max-output-bytes 4096` exit 4, destination kept | `controls/controls.json` |

`controls/` is `tests/resource_controls.py` on the same binary; all 13 controls passed. The oracle accepts the correct output and rejects a missing record, a changed digit, swapped values and a fixture lacking a record; malformed XML and a profile error after 15,981 records keep the destination. The 8,000,000-object cap is unchanged in the code and was not exercised by allocating that many objects.

## Deferred recommendations from OSR-10

Unchanged from the [48bdef2 record](../osr-25-integration-48bdef2/README.md#deferred-recommendations-from-osr-10): a NUL byte after each `BDC` operator (legal PDF whitespace) and custom fonts embedded without reading OS/2 `fsType` (a licensing policy decision). Neither causes a failure of approved behavior.
