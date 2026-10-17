# Lore
By Artem.

The dated story visible at **2026-10-17**, HEAD `d573069`: a finished public import, a 0.2 integration sprint, a much larger 0.3 capability/evidence cycle, then a memory and maintenance pass. The reachable record contains 68 commits, not a claim about work done before publication. See [the measured history](by-the-numbers.md).

## 7 October: the public starting point

`7c62fe4` (“first commit”) introduced the parser, glyph-width layout, TrueType reader, append-only writer, CLI, both built-in profiles, tests, CI and bundled DejaVu Sans together. The later `cedcaab` fixed CI dependencies and prepared source distribution on the same day.

The 0.1.0 section of `CHANGELOG.md` records streaming records-v1, report-v1 selection, callback mapping, quotas/cancellation and atomic file output. These are not inventions of the later 0.3 cycle. `docs/contract.md`, `docs/profile-decision.md` and `docs/license-decision.md` preserve the original Russian-language design/license decisions, dated 2026-10-07.

`baseline-manifest.json` is a separate review artifact dated that day. Its `review_source_revision` is a 64-character identifier, **not a reachable Git revision established by this history**. It records 13 file hashes and ten added entries; it should not be treated as a checksum manifest for today's tree. Do not infer an undocumented pre-public architecture from that identifier.

## 8–12 October: no visible commits

The reachable history has no commits on these dates. It does not tell us whether development or review continued elsewhere.

## 13 October: the 0.2 integration sprint

Six OSR commits merged in PR #1 (`037e2bf`):

| Commit | Milestone |
| --- | --- |
| `f1d1a53` (OSR-2) | Reset callback results before option validation |
| `28fcb34` (OSR-3) | Installed CMake targets and pkg-config integration |
| `94e0e5e` (OSR-4) | CLI stdin/stdout and atomic callback-to-file conversion |
| `833b4d5` (OSR-5) | Bounded public catalog mapper example |
| `803cc11` (OSR-6) | English onboarding while retaining `README.ru.md` |
| `37255b9` (OSR-7) | Version 0.2.0 and delivery gates |

`075455c` added the generated wiki and ignored `.factory`. That revision later became the explicit **unmodified baseline** for OSR-23 resource and ABI comparisons; it is not the final 0.3 measurement revision.

## 16 October: the 0.3 capability and evidence era

The 32 commits dated October 16 include merges as well as implementation and evidence-only changes. The implementation did not stop at a version-string bump:

| Area | Implementation milestone | Current evidence/entry point |
| --- | --- | --- |
| Repeatable resources and old-binary checks | `67ce264` (OSR-23) | `tests/resource_baseline.py`, `tests/abi_canary/`, baseline `075455c` |
| Font span and XML validation | `3fdfb68` (OSR-24), `cd41ac1` (OSR-11) | `src/font.c`, `src/xml_parse.c`, focused negative fixtures |
| Font names and zero-advance marks | `ecfe429` (OSR-13) | `src/font.c`, `tests/test_font_metrics.c` |
| Counted PDF bytes and callback accounting | `9ea09c2`, `d24723f` (OSR-19) | `src/pdf_write.c`, `src/convert.c`, `tests/test_pdf_bounds.c` |
| Flate page/font streams | `3acf0cb` (OSR-9) | `src/pdf_write.c`, `tests/test_flate.c`; zlib becomes a dependency |
| Consumed line/column/byte diagnostics | `4e38307` (OSR-12) | `src/xml_parse.c`, `tests/test_xml_coords.c` |
| Word-aware bounded wrapping | `2510d65` (OSR-14) | `src/layout.c`, `tests/test_layout_wrap.c` |
| Letter/A4/custom geometry | `7637800` (OSR-15) | `src/geometry.c`, separate size-checked `KiConfig` |
| Optional safe PDF `/Info` | `016f32c` (OSR-17) | `src/info.c`, explicit title/producer/date, no implicit clock |
| Integrated version 0.3.0 | `48bdef2` (OSR-25) | `include/ki_xml_pdf.h`, `tests/integration_check.py` |

The additive `KI_0_3` node in `libki_xml_pdf.map` adds four functions without extending `KiOptions`, `KiResult` or `KiEvent`. Old C/C++ ABI canaries exercise that distinction; pre-1.0 CMake package selection still matches its own minor version.

### Corrections after integration

The evidence trail deliberately retains earlier attempts rather than silently replacing them:

- `d95a5c1` serialized the deflate test counter and cleaned integration fixtures; `docs/evidence/osr-25-corrective-d95a5c1/README.md` records corrective gates and controls.
- `1193cdc` fixed a one-shot callback write failure that buffering could otherwise turn into false success. `src/convert.c` now latches the first failed write and counts only accepted callback bytes.
- `64338ab` recorded final gates, installed lifecycle checks, ABI canaries and resource evidence for **1193cdc**, in `docs/evidence/osr-25-callback-1193cdc/README.md`.
- PR #4 merged that series as `de8ba0e`. Neither the earlier integration evidence nor these final 0.3 measurements describe subsequent October 17 memory changes.

### Research is not shipped support

Three October 16 research outcomes are explicitly deferred/no-go:

- **Internal TTF subsetting:** `36ad651` (OSR-10), `docs/evidence/osr-10-font-spike/README.md`. The best measured subset would remove 2.92% of the target 16 MiB report PDF after Flate; no internal subsetter shipped.
- **UTF-16 input:** `b018964` (OSR-16), `docs/evidence/osr-16-utf16-spike/README.md`. The external adapter is a synthetic research prototype, not a library/CLI encoding feature. UTF-8 remains the input contract.
- **Tables:** `3fc9a3f` (OSR-18), `docs/evidence/osr-18-table-spike/README.md`. Synthetic bounded-table/pre-layout experiments do not implement a table API or new profile.

`tests/research_font_subset.py`, `tests/research_utf16.py`, `tests/research_utf16_adapter.c` and `tests/research_table.py` are research tools. Their presence is not a capability advertisement.

## 17 October: measure, remove duplication, tighten the contract

The 26 commits on this date include the following distinct workstreams:

| Work | Commits | Result at this HEAD |
| --- | --- | --- |
| Heap/RSS baseline | `28076d6`, `1a59729` (OSR-82) | Profile of **de8ba0e**, corrected peak-tree comparison; historical, not current allocation facts |
| Remove obsolete byte-grid sink | `66fcf13` (OSR-90) | Obsolete byte-grid implementation, header and dedicated test removed ([removal diff](https://github.com/artemklen/ki_xml_pdf/commit/66fcf13)) |
| Community maintenance | `980cbf8` (OSR-88) | `SECURITY.md`, `CODE_OF_CONDUCT.md`, issue/PR templates |
| Release automation | `28eb107`, `379664c` (OSR-87) | Tag-triggered `.github/workflows/release.yml`, archive commit proof and gzip/SIGPIPE handling |
| RSS acceptance | `688c29d`, `5f30171` (OSR-86) | 4,096 KiB gate consistently described |
| Stream the font | `9595c0e` (OSR-83) | Open file plus fixed metrics; size/hash recheck before 16 KiB-chunk embedding, no retained whole TTF |
| Stream ToUnicode | `b1f3e99` (OSR-84) | Count then write the uncompressed CMap with fixed arrays, no heap assembly buffer |
| Reuse page content storage | `42930f3` (OSR-85) | One lazily allocated reusable writer buffer; layout emits fixed page lines without a joined-page copy |
| Narrow platform promise | `f824f3f` (OSR-89) | Only 64-bit Linux with glibc is supported |
| Addon design | `68422ec`, `1b69a39` (OSR-91) | Accepted ADR, **not implemented** |

The removed `page_sink` was the old fixed byte-grid implementation, not the production glyph-width pipeline. It is historical context, not an open cleanup task. Some test-only grid constants and their old Helvetica comment remain in `src/convert.h`; see [verified cleanup candidates](cleanup-opportunities.md).

The addon ADR, `docs/adr/0001-font-script-addons.md`, explicitly describes a tree after OSR-83 and before the CMap/page-buffer changes. Its retained 256 KiB CMap discussion is a labeled historical assumption. Its proposed dynamic page IDs and extra font resources are future design: today's writer still derives page IDs from a fixed three-object-per-page formula.

**A workflow is not a release.** Version 0.3.0 in the header/changelog and a `v*` release trigger do not establish that a `v0.3.0` tag or GitHub Release exists. The `v0.0.0-test` path is an explicit draft proof exception, not evidence of a production release.

## Threads that survived the changes

- The parser still consumes a fixed 4,096-byte window with bounded element/name/attribute/text storage (`src/xml_parse.h`), despite substantially stronger validation and diagnostics.
- File publication still happens only after successful conversion, flushing, synchronization and close (`src/convert.c`); callback/stdout destinations can already contain partial output on failure.
- `src/report.c` retains its Russian report labels; English onboarding did not silently translate the profile's output contract.
- Full-font embedding remains the supported path. Streaming its bytes is not subsetting it, and safe UTF-16BE `/Info` strings are not UTF-16 XML input support.

## Key source files and related pages

| Sources | Why they matter to the history |
| --- | --- |
| `CHANGELOG.md`, `include/ki_xml_pdf.h`, `libki_xml_pdf.map` | Version/capability and additive ABI record |
| `baseline-manifest.json`, `docs/contract.md`, `docs/profile-decision.md`, `docs/license-decision.md` | Original review/design artifacts |
| `docs/evidence/osr-23-baseline-075455c/README.md`, `docs/evidence/osr-25-callback-1193cdc/README.md`, `docs/evidence/osr-82-heap-profile/README.md` | Three different measured revisions |
| `docs/adr/0001-font-script-addons.md`, `.github/workflows/release.yml`, `src/platform.h` | Deferred addon design, release mechanism and platform boundary |

[Overview](overview/index.md) · [Architecture](overview/architecture.md) · [PDF writer](systems/pdf-writer.md) · [Page geometry](features/page-geometry.md) · [Testing](how-to-contribute/testing.md) · [Design decisions](background/design-decisions.md) · [Fun facts](fun-facts.md)
