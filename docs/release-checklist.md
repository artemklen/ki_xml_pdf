# Source distribution acceptance: 0.3.0

Acceptance applies to one source commit and its archive, rather than to every future revision. A source snapshot does not automatically create a version tag, GitHub Release or package-registry publication.

| Gate | Evidence |
| --- | --- |
| License and assets | MIT `LICENSE` and the separate DejaVu notice/font are present in the archive and installed data directory |
| API compatibility | Unchanged `KiOptions`/`KiResult`/`KiEvent` layout and `KI_0_2` exports; additive `KiConfig`, `ki_config_init` and `_cfg` calls in `KI_0_3`; soname `libki_xml_pdf.so.0`. C/C++ clients compiled against the installed 0.2.0 header run unchanged against the new shared library (`tests/abi_canary/run_canary.sh`). `make test` covers errors, quotas, cancellation, caller resources and concurrent contexts across all three `_cfg` calls |
| Combined features | `tests/integration_check.py`: geometry, wrapping, a non-default font with zero-advance marks, Flate and `/Info` in both profiles over the four CLI routes, with independent word, MediaBox, ink, stream and metadata checks and combined failure cases |
| Built-in report and pipelines | `tests/cli_pipeline.py`: four file/stdin → file/stdout modes, independent report oracle, usage/help/dash paths, atomic failure/cancellation and broken output |
| Public custom mapper | `tests/catalog_verify.py`: fixed English oracle, full long field across short reads/TEXT/CDATA/references, grammar byte offsets, distinct causes and previous-file safety |
| Installed C/C++ consumers | `tests/build_integration.sh`: clean Make/CMake staging, CMake/pkg-config × C/C++ × static/shared matrix, actual ELF linkage, installed font/notices, foreign cwd and installed catalog |
| Documented commands | `tests/docs_smoke.sh`: actual English README/mapper-guide blocks in private prefixes, C/C++ clients and independent PDF oracles; English/Russian links and anchors |
| Memory safety and fuzz | GCC/Clang functional tests, both toolchains with ASan/UBSan and fatal sanitizer diagnostics; bounded seeded fuzz, golden/mutant PDF reader checks and invalid-input/font controls |
| Ordinary converter memory | `make check` runs the existing 16 MiB synthetic workload; `tests/resource_baseline.py matrix` repeats the six full-output and three fixed-1000 selection cases and compares them with the 075455c baseline; `python3 tests/large_proof.py 1073741824` selects 1000 records while validating the full 1 GiB XML. Converter RSS ≤64 MiB; PDF ≤10,000 pages/256 MiB; spool ≤64 MiB. Poppler runs separately under RLIMIT_AS 1 GiB |
| Clean archive | `make distcheck`: unpack exact HEAD without Git checkout, verify required sources/assets/docs/tests, install, run external clients, CLI, report and catalog oracles; compiler/reader/oracle/license failures must fail |
| Version and provenance | Public header is the version source for CLI/CMake/pkg-config; archive name/root derive from the header at Git HEAD. CI retains the archive, `SHA256SUMS` and `SOURCE_COMMIT`, and verifies the tar commit |
| Common delivery | One PR for the accepted tasks, full GitHub CI on the exact source head, an independent review of that head, merge and full post-merge CI on the merge commit |

Run the checks from a clean source revision. `make dist` archives Git HEAD; uncommitted changes are excluded, including an uncommitted version change. `make distcheck` uses a disposable extraction/install root and fails on any required stage. Installed consumers need only their own copied sources and installed package metadata, without the library checkout.

The [CI workflow](../.github/workflows/ci.yml) runs once for the prepared PR head and again for the merged `main` commit. Child branch pushes do not start test pipelines. Its source artifact is produced only after GCC, Clang, sanitizer and resource jobs succeed. Artifact downloads require GitHub login. Verify `sha256sum -c SHA256SUMS` and match `SOURCE_COMMIT` to the accepted revision; the Git tar metadata carries the same commit.

Historical measurements remain attached to revision [`7c62fe4`](https://github.com/artemklen/ki_xml_pdf/commit/7c62fe4af72b097885d76a7f9f897492f5c4c2d5) in [the report contract](report-v1.md) and [core contract](contract.md). Current acceptance uses current CI metrics; earlier numbers are not substituted for them.

Rollback is a revert of the common merge commit. No release tag or registry action is part of this acceptance.
