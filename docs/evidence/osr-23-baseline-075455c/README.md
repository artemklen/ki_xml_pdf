# OSR-23 resource baseline on 075455c

This directory holds the raw measurements of the **unmodified** 0.2.0 implementation at [`075455c`](https://github.com/artemklen/ki_xml_pdf/commit/075455cc930a1f5e79a9ae4811e82b5c95af06c4), captured on 16 October 2026 before any writer or layout change. Later revisions are compared against these files; they are not replaced.

## Provenance

| Item | Value |
| --- | --- |
| Measured source | `075455cc930a1f5e79a9ae4811e82b5c95af06c4`, a clean clone with no tracked changes (`baseline/environment.json`: `measured_source_tracked_changes` and `measured_product_diff_vs_base` are empty) |
| Build | `make -j2 PREFIX=<old-prefix> xml2pdf test_stream libki_xml_pdf.so` then `make install`, GCC 14.2.0 default `CFLAGS` (`-O2 -g`); log in `abi-0.2/build-075455c.txt` |
| Converter binary SHA-256 | `875cd641607798d5aef53611b3968e9af2635a663f8b0547e01d67d4e527beaa` |
| Font | `fonts/DejaVuSans.ttf`, 759,720 bytes, SHA-256 `57f73e11f51999432bf7ab22ce55b6f945d5eca1bf824404cfa9ec2e3718c84e`, passed explicitly with `--font` |
| Harness | commit `67ce264` on branch `osr-8`; it adds files under `tests/` only (`harness_product_diff_vs_base` is empty) |
| Host | Debian 13, Linux 6.12.111 x86_64, glibc 2.41, Intel i9-12900K, 8 CPUs, 19.5 GiB RAM (`MemTotal` 20,486,964 kB), no swap; Python 3.13.5; Poppler 25.03.0; GNU time |
| Scratch | project disk, reported as `ext2/ext3` by `stat -f` in each `preflight.json`, with about 247 GB free; not `/tmp` tmpfs |

## Commands

```sh
python3 tests/resource_baseline_test.py
python3 tests/resource_baseline.py matrix --binary <src>/xml2pdf --font <src>/fonts/DejaVuSans.ttf \
  --source-dir <src> --evidence-dir <new-dir> --scratch <disk-backed-dir>
python3 tests/resource_controls.py --binary <src>/xml2pdf --font <src>/fonts/DejaVuSans.ttf \
  --evidence-dir <new-dir> --scratch <disk-backed-dir>
python3 tests/resource_baseline.py compare docs/evidence/osr-23-baseline-075455c/baseline <new-dir>
```

`matrix` runs the nine cases one after another. The fixture generator (`tests/resource_recipe.py`), the converter, the reader (`pdftotext -layout`) and the oracle (`tests/resource_oracle.py`) are separate processes, each with its own `/usr/bin/time -v` record (`generator.time`, `converter.time`, `reader.time`, `oracle.time`). The oracle derives every expected line from the fixture recipe, never from a PDF, and checks every rendered record in order while streaming. Large XML and PDF files are deleted after measurement; their sizes and SHA-256 remain in `run.json`. Before each case the runner checks free disk against input target + output quota + 64 MiB spool + 4 GiB reserve.

Full-output cases use `--max-pages 1000000 --max-output-bytes 2147483648 --max-spool-bytes 67108864`. Selection cases use `--record-limit 1000 --max-pages 10000 --max-output-bytes 268435456 --max-spool-bytes 67108864`. The exact command of every row is in `runs.csv` (`converter_command`).

## Full output: every record rendered

RSS is the converter process only. Generator and reader RSS are separate processes. Times are wall-clock seconds on this host and are not a guarantee.

| Profile | XML bytes | Records | Records checked | Pages | PDF bytes | Spool bytes | Converter RSS KiB | Converter s | Generator RSS KiB | Reader RSS KiB | Reader s |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| records-v1 | 1,048,593 | 33,462 | 33,462 | 2,008 | 12,216,261 | 32,192 | 2396 | 0.12 | 23192 | 21560 | 1.41 |
| records-v1 | 16,777,233 | 499,983 | 499,983 | 29,999 | 180,555,585 | 480,048 | 2420 | 1.07 | 23388 | 69200 | 20.85 |
| records-v1 | 67,108,881 | 1,925,863 | 1,925,863 | 115,552 | 713,436,448 | 1,848,896 | 2396 | 4.27 | 23364 | 218808 | 81.10 |
| report-v1 | 1,048,578 | 15,981 | 15,981 | 640 | 6,052,147 | 10,304 | 2340 | 0.07 | 20940 | 19384 | 0.48 |
| report-v1 | 16,777,245 | 246,368 | 246,368 | 9,855 | 86,884,343 | 157,744 | 2524 | 0.73 | 21188 | 34588 | 7.25 |
| report-v1 | 67,108,881 | 975,812 | 975,812 | 39,033 | 347,124,499 | 624,592 | 2480 | 2.93 | 21156 | 84992 | 28.65 |

PDF size, spool size, page count and time grow with the rendered content: the 64 MiB records-v1 input produced a 713 MB PDF. Full output of a 1 GiB input was **not** measured.

## Selection: first 1000 report-v1 records of a larger input

The whole input is read and validated; only 1000 records are rendered. These rows do not describe a full-output render.

| XML bytes | Records total | Selected and checked | Skipped (rendered counter) | Pages | PDF bytes | Spool bytes | Converter RSS KiB | Converter s | Generator RSS KiB | Reader RSS KiB |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 16,777,245 | 246,368 | 1,000 | 245,368 | 41 | 1,095,465 | 720 | 2536 | 0.15 | 21176 | 18176 |
| 268,435,464 | 3,812,079 | 1,000 | 3,811,079 | 41 | 1,095,481 | 720 | 2340 | 1.78 | 21188 | 18228 |
| 1,073,741,895 | 15,013,206 | 1,000 | 15,012,206 | 41 | 1,095,497 | 720 | 2456 | 6.96 | 21136 | 18172 |

The oracle checked the title, all 1000 selected records and the three counter lines `Всего записей`, `Выбрано записей` and `Пропущено записей` against the recipe totals.

## Resource gates

| Gate | Limit | Baseline result | Evidence |
| --- | --- | --- | --- |
| Parser, 9,586,989-byte stream, RSS delta | < 2048 KiB | 0 KiB (`big_stream_fixed_memory`); 4 MiB CDATA stream also 0 KiB | `existing-gates/make-test.txt` |
| Full records-v1 16 MiB, RSS below half the input | < 8,388,616 B | 2420 KiB = 2,478,080 B | `baseline/full-records-16MiB/run.json` |
| Full records-v1 16 MiB, RSS ≤ small reference + 4 MiB | ≤ 2536 + 4096 = 6632 KiB (reference: `tests/fixture.xml`, same binary and font) | 2420 KiB | `baseline/ref-small-fixture/run.json` |
| `tests/verify.sh` 16 MiB gate (its own generator, default font) | < half input and ≤ small + 4 MiB | 16,777,313 B input: 2352 KiB, small 2360 KiB, cap 6,610,944 B; pass | `existing-gates/verify-mem.txt` |
| Converter RSS, all nine runs including full 64 MiB | ≤ 65536 KiB | maximum 2536 KiB | `baseline/runs.csv` |
| Font file size | ≤ 8 MiB (`src/font.c`) | 8,388,608-byte font accepted; 8,388,609-byte font fails with exit 1, `font: cannot load` | `controls/controls.json` |
| Metadata spool | ≤ 64 MiB | maximum 1,848,896 B (full records-v1 64 MiB); `--max-spool-bytes 16` fails with exit 4 | `baseline/runs.csv`, `controls/controls.json` |
| PDF objects | < 8,000,000 (`MAX_OBJECTS`, `src/pdf_write.c`) | maximum 231,111; the cap was reviewed in code, not reached | `baseline/runs.csv` |
| Selection pages | ≤ 10,000 | 41 | `baseline/runs.csv` |
| Selection PDF | ≤ 256 MiB | 1,095,497 B | `baseline/runs.csv` |
| Page and output quotas | reduced quota fails as resource error | `--max-pages 1` and `--max-output-bytes 4096` fail with exit 4, `resource limit`; destination kept | `controls/controls.json` |

## Negative controls

`controls/controls.json` records twelve controls; all passed on 075455c:

- the unmodified reader stream of a 300 KB report passes with all 4649 records checked;
- the same stream without interior record 2324 fails (`expected 'Запись 2324', got 'Запись 2325'`);
- the same stream with one changed digit in record 2325 fails;
- a records-v1 fixture without record 1234 converts, and the oracle, which still expects that record, fails;
- with `--record-limit 1000`, a valid 1 MiB tail succeeds with 15,981 total and 14,981 skipped records; a malformed XML record after the selection fails with exit 1 `xml: end tag mismatch`, and a well-formed but profile-invalid record fails with exit 1 `profile: unknown element at byte 1048592`. Both keep the previous destination file.
- the page, output, spool and font controls in the gate table.

## Reproducibility

`rerun-small/` repeats three configurations (full records-v1 1 MiB, full report-v1 1 MiB, selection 16 MiB) into a separate directory with the same binary and recipe. `comparison/baseline-vs-rerun-small.json` (`compare --expect-identical-output`) shows identical fixture hashes, PDF hashes, pages, spool and object counts, no environment differences, and RSS values that vary by a few hundred KiB. `comparison/baseline-vs-docs-reproduction.json` compares the baseline with the full nine-case matrix run by the README command from the harness checkout (`67ce264`, product sources identical to 075455c but built with the default `PREFIX`): it reports the different source SHA and binary hash as environment differences, and all nine PDFs are byte-identical. `comparison/negative-tampered-rerun.json` shows that the comparison fails when a copied PDF hash and fixture hash are altered.

## Old 0.2 ABI consumers

The old installed prefix and precompiled consumers are kept outside the repository and outside any `make clean` target, read-only:

- `/home/artem/projects/ki_xml_pdf-evidence/osr-23/old-0.2-prefix/` — header, `libki_xml_pdf.so` (soname `libki_xml_pdf.so.0`), static library, CMake and pkg-config metadata from 075455c;
- `/home/artem/projects/ki_xml_pdf-evidence/osr-23/abi-canary-0.2/` — `abi_canary_c` and `abi_canary_cpp`, compiled once against that header by `tests/abi_canary/build_canary.sh` and linked dynamically.

`abi-0.2/` holds their hashes, the exported symbol and version inventory, and the `KiOptions`/`KiResult`/`KiEvent` sizes and offsets as compiled: 144, 224 and 184 bytes. To check a newer library without recompiling the consumers:

```sh
bash tests/abi_canary/run_canary.sh /home/artem/projects/ki_xml_pdf-evidence/osr-23/abi-canary-0.2 \
  <new-prefix>/lib <font> <empty-output-dir>
```

## Not claimed

These numbers describe this revision, synthetic recipe, font and host. They do not promise constant RSS for any input or font, do not show that output needs no disk (PDF and spool grow with rendered content), and make no comparison with Apache FOP, WeasyPrint or any other converter. The [historical `7c62fe4` measurements](../../contract.md#числа) remain separate.
