# OSR-25 integrated 0.3.0 evidence (48bdef2)

Measured source: `48bdef2fd01dfff8b85e0cdd77af55d4c9593531` (version 0.3.0), a clean clone with no tracked changes (`resource/environment.json`: `measured_source_tracked_changes` is empty). Every gate and both resource series ran on that one revision, sequentially, on the host described in the [OSR-23 baseline](../osr-23-baseline-075455c/README.md) (Debian 13, GCC 14.2.0, Clang, Poppler 25.03.0, GNU time). The commit that adds this directory changes only documentation; its own gates are listed in the delivery record.

Paths are normalized like the OSR-23 evidence: `<osr25-root>` is this run's external directory on the measuring host (clone in `<osr25-root>/checkout`, disk-backed scratch), `<evidence-root>` is the OSR-23 evidence root (old 0.2 prefix and canaries), `<repo>` is the harness checkout. No other byte was changed. `resource/SHA256SUMS` was written at capture time and lists the hashes of the files before that substitution.

## Gates

`gates/run_gates.sh` ran the 20 commands in `gates/summary.tsv` in one clone; every command exited 0. Each log starts with the command and `HEAD 48bdef2…`.

| Gate | Result | Log |
| --- | --- | --- |
| `make test`, `make check` (includes `cli_pipeline`, `catalog_verify`, `integration_check`, RSS gate) | all groups ok, including `integration_contexts`, `big_stream_fixed_memory`, `cdata_stream_fixed_memory` | `01-make-test.txt`, `02-make-check.txt` |
| `make CC=gcc test-san`, `make CC=clang test-san` (ASan/UBSan) | all groups ok, no reports | `03-san-gcc.txt`, `04-san-clang.txt` |
| Clang and GCC clean rebuilds with functional tests | ok | `05-clang-functional.txt`, `06-gcc-rebuild.txt` |
| `tests/fuzz_smoke.py` | seed 20261007, 24 iterations, 11 XML corpus files, 6 fonts, ok | `07-fuzz.txt` |
| `tests/font_fixtures.py --check` (184-byte truncated cmap) | ok | `08-font-fixtures.txt` |
| CLI, catalog, integration, `--help`, `--version` | `xml2pdf 0.3.0 records-v1`; catalog success=12 rejected=72 | `09-cli-catalog.txt` |
| `tests/build_integration.sh` with GCC and Clang | 16 installed C/C++ static/shared Make, CMake-target and pkg-config rows each, CTest and Poppler oracle | `10-…`, `11-…` |
| `tests/docs_smoke.sh`, header dependency, `tests/prefix_switch.sh` | ok | `12-…`, `13-…`, `16-…` |
| `make distcheck` and five negative controls (`CC=false`, `CXX=false`, failing reader, wrong oracle, missing license) | archive ok, each control failed as required | `14-distcheck.txt`, `15-distcheck-negative.txt` |
| CMake build and CTest | ok | `17-cmake-ctest.txt` |
| Resource runner unit tests, `large_proof.py` 1 GiB selection | 19,522,650 records, 41 pages, 432,803 bytes, RSS 2748 KiB | `18-…`, `19-large-proof.txt` |
| Install, pkg-config, CMake version file, ELF metadata | `--modversion` 0.3.0, `--static --libs` `-lki_xml_pdf -lz`, SONAME `libki_xml_pdf.so.0`, NEEDED `libz.so.1` | `20-pkgconfig-static.txt` |

## Version and old ABI

The header, CLI, pkg-config file, CMake package version and source archive all say 0.3.0. The SONAME stays `libki_xml_pdf.so.0`. `abi/exports-old.txt` and `abi/exports-new.txt` are the global defined dynamic symbols of the preserved 0.2.0 library and the new library: all 11 old entries (`KI_0_2` and its functions) are still exported unchanged, and the new library adds only `KI_0_3` with `ki_config_init`, `ki_convert_cfg`, `ki_convert_file_cfg` and `ki_convert_to_file_cfg`.

- `abi/run_canary.txt`: console output of `tests/abi_canary/run_canary.sh` (its per-canary logs are copies of it). The C and C++ canaries compiled against 0.2.0 in OSR-23 ran without a rebuild against the installed 0.3.0 library, and each resolved `libki_xml_pdf.so.0` from that prefix. The script runs `sha256sum -c SHA256SUMS` on the canary binaries and stops before running anything if it fails.
- `abi/early_probe.c`, `abi/early_probe_run.txt`: a supplementary probe compiled once against the 0.2.0 header and library, then run against both libraries. With both it reports the same struct sizes (`KiOptions` 144, `KiResult` 224, `KiEvent` 184), that `ki_options_init` writes nothing past `KiOptions`, that size mismatches, negative quotas, NULL options, invalid selections, a missing read callback and an unknown profile return USAGE before any callback or file creation, and that guard bytes around `KiOptions` stay intact. `abi/early_probe.SHA256SUMS` hashes the binary, its source and the old header and library.

## Combined features and failures

- `tests/test_integration.c` (`integration_contexts`): seven jobs through `ki_convert_cfg`, `ki_convert_file_cfg` and `ki_convert_to_file_cfg` (records A4 landscape with `/Info`, report on a custom 400×300 page with offset and limit, custom mapper on Letter, explicit date, late XML failure, page-quota callback failure, an invalid `report-v1` version), first serially for reference outputs, then three rounds on separate threads with separate options, results, I/O and mapper state. Each job's text marker, counters, MediaBox, Flate stream count, `/Info` and error cause must match its reference; failing contexts are retried and must then succeed or fail the same way. It also runs under both sanitizers.
- `tests/integration_check.py`: three combined CLI cases (wrapping, A4 landscape, a custom page and margins, Letter defaults, a non-default font with zero-advance marks, Flate and `/Info` with escaped strings and an explicit date) through all four file/stdin to file/stdout routes. All routes must give identical bytes, and the words, page count, MediaBox, ink margins, streams and metadata are checked against expectations computed from the input. Eight failure runs (a malformed end tag at the end of the input and a 4096-byte output quota, each from a file and from stdin, into an existing and an absent destination) must exit 1 or 4 with diagnostics on stderr only, keep the existing destination byte for byte, create no new one and leave no spool or temporary file.

## Resource series

`resource/` holds the nine runs of `tests/resource_baseline.py matrix` (`runs.csv`, `runs.json`, per-case `run.json`, `fixture.json`, `oracle.json` and `/usr/bin/time` records for the generator, converter, reader and oracle). `resource-console.txt` is its console output. `resource-vs-baseline.json` is `compare` against `../osr-23-baseline-075455c/baseline`: settings and fixture hashes match for every case, no problems; PDF bytes, hashes, object counts and spool sizes differ as expected from Flate and the later writer changes.

| Case | Input bytes | Records checked | Pages | PDF bytes (baseline → final) | Converter RSS KiB (baseline → final) | Converter s (baseline → final) |
| --- | ---: | ---: | ---: | ---: | --- | --- |
| full-records-1MiB | 1,048,593 | 33,462 / 33,462 | 2,008 | 12,216,261 → 2,332,511 | 2396 → 2952 | 0.12 → 0.22 |
| full-records-16MiB | 16,777,233 | 499,983 / 499,983 | 29,999 | 180,555,585 → 29,422,443 | 2420 → 2752 | 1.07 → 2.91 |
| full-records-64MiB | 67,108,881 | 1,925,863 / 1,925,863 | 115,552 | 713,436,448 → 112,864,984 | 2396 → 2880 | 4.27 → 11.54 |
| full-report-1MiB | 1,048,578 | 15,981 / 15,981 | 640 | 6,052,147 → 1,192,452 | 2340 → 2952 | 0.07 → 0.14 |
| full-report-16MiB | 16,777,245 | 246,368 / 246,368 | 9,855 | 86,884,343 → 12,545,307 (14.44%) | 2524 → 2880 | 0.73 → 1.63 |
| full-report-64MiB | 67,108,881 | 975,812 / 975,812 | 39,033 | 347,124,499 → 48,620,561 | 2480 → 2880 | 2.93 → 6.38 |
| select-report-16MiB-limit1000 | 16,777,245 | 1,000 of 246,368 | 41 | 1,095,465 → 460,952 | 2536 → 2824 | 0.15 → 0.17 |
| select-report-256MiB-limit1000 | 268,435,464 | 1,000 of 3,812,079 | 41 | 1,095,481 → 460,949 | 2340 → 2764 | 1.78 → 1.93 |
| select-report-1024MiB-limit1000 | 1,073,741,895 | 1,000 of 15,013,206 | 41 | 1,095,497 → 460,955 | 2456 → 2848 | 6.96 → 7.43 |

Selection rows render only 1000 records and do not stand in for full output. Full output of a 1 GiB input was not measured. The 16 MiB `report-v1` size target (at most 50% of the baseline, first met in [OSR-9](../osr-9-flate-3acf0cb/README.md)) holds at 14.44%.

| Gate | Limit | Final result | Evidence |
| --- | --- | --- | --- |
| Parser 9,586,989-byte stream and 4 MiB CDATA stream, RSS delta | < 2048 KiB | pass | `gates/01-make-test.txt` (`big_stream_fixed_memory`, `cdata_stream_fixed_memory`) |
| Full records-v1 16 MiB, RSS below half the input | < 8,388,616 B | 2752 KiB = 2,818,048 B | `resource/full-records-16MiB/run.json` |
| Full records-v1 16 MiB, RSS ≤ small reference + 4 MiB | ≤ 2916 + 4096 = 7012 KiB | 2752 KiB | `resource/ref-small-fixture/run.json` |
| `tests/verify.sh` 16 MiB gate | < half input and ≤ small + 4 MiB | 2692 KiB, small 2860 KiB, cap 7,122,944 B; pass | `verify.txt` |
| Converter RSS, all nine runs including full 64 MiB | ≤ 65536 KiB | maximum 2952 KiB | `resource/runs.csv` |
| Selection pages and PDF size | ≤ 10,000 pages, ≤ 256 MiB | 41 pages, ≤ 460,955 B | `resource/runs.csv` |
| Font file size | ≤ 8 MiB | 8,388,608 B accepted; 8,388,609 B fails with exit 1 `font: cannot load` | `controls/controls.json` |
| Metadata spool | ≤ 64 MiB | maximum 2,773,320 B; `--max-spool-bytes 16` fails with exit 4 | `resource/runs.csv`, `controls/controls.json` |
| Page and output quotas | reduced quota fails as RESOURCE | `--max-pages 1` and `--max-output-bytes 4096` exit 4, destination kept | `controls/controls.json` |

`controls/` is `tests/resource_controls.py` on the same binary: the oracle accepts the correct output and rejects a missing record, a changed digit, swapped values and a fixture lacking a record; malformed XML and a profile error after 15,981 records keep the destination. The 8,000,000-object cap is unchanged in the code and was not exercised by allocating that many objects.

## Deferred recommendations from OSR-10

Two findings recorded during the OSR-10 font spike were reviewed again. Neither causes a failure of approved behavior, so both are recorded here and not changed:

- `src/pdf_write.c` copies the terminating NUL of `"> >> BDC\n"` into the content stream, so each marked-content block is followed by one NUL byte. NUL is PDF whitespace, Poppler and the independent stream reader accept it, and text and geometry checks pass. Copying nine bytes instead would remove it; it is left for a later change because it alters every page stream byte.
- Custom fonts are embedded without reading the OS/2 `fsType` flags. Enforcing embedding permissions is a licensing policy decision and needs the project owner's approval before any implementation.
