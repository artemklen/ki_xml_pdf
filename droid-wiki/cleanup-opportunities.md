# Cleanup opportunities

Verified candidates for removal or simplification in the current tree. None of these is a defect; each item is dead weight, duplication or a stale record the project could shed, most of them at the next 0.y bump. Counts and line spans below were measured, not estimated.

## Dead ends

- **`src/page_sink.c` and `src/page_sink.h`** were removed. They were a fixed byte-grid page buffer from the 0.1 design, compiled into the library but called only from `test_page_sink_flushes`. The conversion path uses `src/layout.c`. What the module did is still described on [Page sink](systems/page-sink.md).
- **`XML2PDF_PAGE_COLS`, `XML2PDF_PAGE_LINES`, `XML2PDF_PAGE_CAP`** in `src/convert.h`: the old 90x48 grid, referenced only by `tests/test_stream.c`. The comment above them still says "Helvetica 10pt", although the pipeline has used DejaVu metrics since `src/font.c` was added.
- **`xml2pdf_convert` and `xml2pdf_convert_cap`** (`src/convert.c`): 0.1-era wrappers around `ki_convert_file` with a hardwired stderr logger. `xml2pdf_convert_cap` maps its `page_cap` argument into `max_output_bytes`, so the parameter is a byte cap, not a page cap, which is a trap for anyone reading the signature. Both are exported in the `KI_0_1` node of `libki_xml_pdf.map`, and the wrapper is also the only code that resets the debug hooks.
- **`pdf_debug_fail_at_page`** (`src/pdf_write.c:583`): a no-op stub declared in `src/pdf_write.h` with an empty body. The working mechanism is `PdfConfig.debug_fail_page`, fed by the convert-level hook. A search for callers returns nothing; declaration and definition can go together.

## Markers

There are no open code comments begging for attention: `rg '\b(TODO|FIXME)\b'` across the repository returns zero matches. Work items live in the owner's tracker and arrive as `OSR-n`/`KW-n` identifiers in commit messages and docs, so there is no marker debt to clean.

## Complexity hotspots

Measured spans, from each function's first line to the next function:

| Function | Location | Lines |
| --- | --- | --- |
| `convert_streams` | `src/convert.c:452-768` | 317 |
| `pdf_add_page` | `src/pdf_write.c:587-697` | 111 |
| `write_tounicode` | `src/pdf_write.c:91-189` | 99 |
| `parse_start` | `src/xml_parse.c:792-880` | 89 |
| `pdf_begin_cfg` | `src/pdf_write.c:493-578` | 86 |

The largest source files are `src/xml_parse.c` (1,163 lines), `src/convert.c` (861) and `src/pdf_write.c` (774); the largest file in the repository is `tests/test_stream.c` (3,259 lines).

`convert_streams` is the obvious first candidate for decomposition. One function holds the event loop, three mapper dispatch arms, error mapping for four failure families, the publish sequence, and result bookkeeping, joined by a single `done` cleanup label. Splitting mapper dispatch, publication and result reporting into helpers would cut its span substantially without changing behavior.

## Duplicated cancellation checks

The four-line pattern checking `opt != NULL && opt->cancelled != NULL` followed by setting status, cause, message and the failed flag appears nine times: six in `convert_streams` and three in `conv_read` (`src/convert.c`). The PDF writer already shows the shape a convert-side helper would take: `stop_pdf` in `src/pdf_write.c` wraps the same test in one function and is used at every xref checkpoint. A shared `check_cancel(opt, &failed, ...)` helper would remove roughly 50 lines and one recurring copy-paste hazard.

## Russian-only historical records

`docs/contract.md`, `docs/profile-decision.md` and `docs/license-decision.md` are decision records written in Russian, and the `0.1.0` section of `CHANGELOG.md` is Russian as well (the `0.2.0` section is English). `README.ru.md` is a deliberate parallel onboarding document, not a stale record. The decision documents have no English counterpart in the tree; this wiki summarizes them on [Design decisions](background/design-decisions.md). Translating the three records, or marking them as historical with an English abstract at the top, would close the gap without losing the originals.

## `baseline-manifest.json` staleness

The manifest is a snapshot of a 07.10.2026 review against revision `c73176ec...`: 13 files hashed, of which 7 are already recorded as `matches_review: false` (`Makefile`, `src/convert.c`, `src/convert.h`, `src/pdf_write.c`, `src/pdf_write.h`, `tests/test_stream.c`, `tests/verify.sh`). Nothing in the Makefile, CMake or CI regenerates or verifies it, yet it ships in the source archive (`tests/distcheck.sh` includes it in the file list). Its note says the working tree is compared against the review, so every post-review edit deepens the mismatch. Two clean exits: regenerate it as a release-step and check it in CI, or retire it in favor of the commit-bound provenance CI already records (`SOURCE_COMMIT`, `SHA256SUMS`).

## Constraints on any cleanup

- The ABI is unstable until 1.0, but exports are versioned (`KI_0_1`, `KI_0_2` nodes in `libki_xml_pdf.map`), so removing `xml2pdf_convert`/`xml2pdf_convert_cap` is a deliberate ABI decision for the next 0.y release, not a casual deletion.
- The debug hooks are how tests inject page, write and read failures (`tests/test_stream.c` sets them around lines 1617 to 1628). They need a replacement, for example per-conversion injection through `KiOptions`, or those tests lose their failure coverage.
- Removing `page_sink` touches both build lists and its unit tests; [Page sink](systems/page-sink.md) documents what would be lost.
- `tests/test_stream.c` is the largest file in the repository (3,259 lines) and the only place that exercises the debug hooks and `page_sink`. Any cleanup that touches it should be verified with `make test-san`, since CI runs the sanitizer suite over this file in both its gcc and clang jobs, not just `make test`.

## Related pages

- [Page sink](systems/page-sink.md) for what the dead module does.
- [Design decisions](background/design-decisions.md) for why the current structure exists.
- [Patterns and conventions](how-to-contribute/patterns-and-conventions.md) for the rules a cleanup must preserve.
