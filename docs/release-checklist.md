# Source distribution acceptance: 0.2.0

Acceptance applies to one source commit and its archive, rather than to every future revision. A source snapshot does not automatically create a version tag, GitHub Release or package-registry publication.

| Gate | Evidence |
| --- | --- |
| License and assets | MIT `LICENSE` and the separate DejaVu notice/font are present in the archive and installed data directory |
| API compatibility | Current-call `KiResult`, unchanged `KiOptions` fields and existing exports; additive `ki_convert_to_file`; `make test` covers errors, quotas, cancellation, caller resources and concurrent contexts |
| Built-in report and pipelines | `tests/cli_pipeline.py`: four file/stdin → file/stdout modes, independent report oracle, usage/help/dash paths, atomic failure/cancellation and broken output |
| Public custom mapper | `tests/catalog_verify.py`: fixed English oracle, full long field across short reads/TEXT/CDATA/references, grammar byte offsets, distinct causes and previous-file safety |
| Installed C/C++ consumers | `tests/build_integration.sh`: clean Make/CMake staging, CMake/pkg-config × C/C++ × static/shared matrix, actual ELF linkage, installed font/notices, foreign cwd and installed catalog |
| Documented commands | `tests/docs_smoke.sh`: actual English README/mapper-guide blocks in private prefixes, C/C++ clients and independent PDF oracles; English/Russian links and anchors |
| Memory safety and fuzz | GCC/Clang functional tests, both toolchains with ASan/UBSan and fatal sanitizer diagnostics; bounded seeded fuzz, golden/mutant PDF reader checks and invalid-input/font controls |
| Ordinary converter memory | `make check` runs the existing 16 MiB synthetic workload; `python3 tests/large_proof.py 1073741824` selects 1000 records while validating the full 1 GiB XML. Converter RSS ≤64 MiB; PDF ≤10,000 pages/256 MiB; spool ≤64 MiB. Poppler runs separately under RLIMIT_AS 1 GiB |
| Clean archive | `make distcheck`: unpack exact HEAD without Git checkout, verify required sources/assets/docs/tests, install, run external clients, CLI, report and catalog oracles; compiler/reader/oracle/license failures must fail |
| Version and provenance | Public header is the version source for CLI/CMake/pkg-config; archive name/root derive from the header at Git HEAD. CI retains the archive, `SHA256SUMS` and `SOURCE_COMMIT`, and verifies the tar commit |
| Common delivery | One PR for OSR-1 and all children, full GitHub CI on the exact source head, independent Grok review, merge and full post-merge CI on the merge commit |

Run the checks from a clean source revision. `make dist` archives Git HEAD; uncommitted changes are excluded, including an uncommitted version change. `make distcheck` uses a disposable extraction/install root and fails on any required stage. Installed consumers need only their own copied sources and installed package metadata, without the library checkout.

The [CI workflow](../.github/workflows/ci.yml) runs once for the prepared PR head and again for the merged `main` commit. Child branch pushes do not start test pipelines. Its source artifact is produced only after GCC, Clang, sanitizer and resource jobs succeed. Artifact downloads require GitHub login. Verify `sha256sum -c SHA256SUMS` and match `SOURCE_COMMIT` to the accepted revision; the Git tar metadata carries the same commit.

Historical measurements remain attached to revision [`7c62fe4`](https://github.com/artemklen/ki_xml_pdf/commit/7c62fe4af72b097885d76a7f9f897492f5c4c2d5) in [the report contract](report-v1.md) and [core contract](contract.md). Current acceptance uses current CI metrics; earlier numbers are not substituted for them.

Rollback is a revert of the common merge commit. No release tag or registry action is part of OSR-1.
