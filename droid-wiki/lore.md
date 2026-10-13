# Lore

The repository is nine commits old, and nearly all of its story fits into two evenings. The first public drop arrived finished, with a full test suite, CI and a bundled font; a week later a single 36-minute sprint took it from 0.1.0 to 0.2.0. Between those two points, most production code never changed at all. This page tells that history from the commit record, with speculation marked as such.

## Eras

### Before the public record

The history starts at `7c62fe4` ("first commit", 49 files, 8,824 lines), but the tree carries an older trace. `baseline-manifest.json` hashes the working tree against a "review source revision" `c73176e...` dated 2026-10-07, and that revision is not an object anywhere in the public history (`git cat-file` rejects it). The manifest records what the pre-public version looked like:

- Six core files match the review byte-for-byte: `src/main.c`, `src/page_sink.c`, `src/page_sink.h`, `src/xml_parse.c`, `src/xml_parse.h` and `tests/fixture.xml`.
- Seven files changed after the review: `Makefile`, `src/convert.c`, `src/convert.h`, `src/pdf_write.c`, `src/pdf_write.h`, `tests/test_stream.c` and `tests/verify.sh`.
- Ten entries were added after the review: the README, the contract doc, the license files, `src/font.c`/`src/font.h` and `src/layout.c`/`src/layout.h`.

The decision records in `docs/` predate the drop as well: KW-1004 (build its own streaming pipeline, in `docs/contract.md`), KW-1021 (adopt MIT plus the report-v1 profile, in `docs/profile-decision.md`) and KW-1024 (MIT license, in `docs/license-decision.md`) are all dated 07.10.2026. A reasonable reading, though the repository does not state it outright, is that an internal review revision was hardened with real glyph-width layout and a real font loader before anything was published. See [design decisions](background/design-decisions.md).

### The public drop (7 October 2026)

`7c62fe4` landed at 21:10 local time with everything at once: parser (`src/xml_parse.c`), driver (`src/convert.c`), PDF writer (`src/pdf_write.c`), font reader (`src/font.c`), layout (`src/layout.c`), both profiles, the CLI, the 2,693-line test binary, a 551-line verification script, CI, the MIT license and the DejaVu Sans font. The commit message is literally "first commit"; every later commit follows the "OSR-N imperative summary" pattern.

Twenty-four minutes later, `cedcaab` ("Fix CI dependencies and prepare the source distribution") touched 9 files (+123/-32). The 0.1.0 changelog explains what that was about: CI had to install Poppler and Python explicitly for the sanitizer jobs, and the verified source archive with its SHA-256 had to be saved as an artifact. A release that needed a fix within half an hour of existing, and got one, is apparently what a delivery gate looks like here.

### The quiet week (8 to 12 October 2026)

No commits. The repository says nothing about what happened in those five days, and anything else would be invention.

### The OSR sprint (13 October 2026)

Six commits, `f1d1a53` (OSR-2) through `37255b9` (OSR-7), landed between 02:32 and 03:08 UTC, one every 7.3 minutes on average, on the `osr-1` branch. PR #1 merged them at 06:39 local time as `037e2bf` (34 files, +3,118/-306). The series does not expand what "OSR" stands for; the Linear links in `docs/` suggest a numbered task list in the owner's tracker.

The sprint delivered, in order: result-reset before option validation (OSR-2, a 3-line fix plus 71 test lines), CMake and pkg-config install integration (OSR-3, +389), CLI streams and `ki_convert_to_file` (OSR-4, +928), the public catalog mapper example (OSR-5, +819), English onboarding with the Russian one preserved (OSR-6, +544), and version 0.2.0 with the delivery gates (OSR-7, +366). Each commit carries its own tests: the sprint added two Python harnesses, two shell harnesses and 566 lines to `tests/test_stream.c`.

## Longest-standing features

These have not changed since the first public commit and, for the ones in `baseline-manifest.json`, not since the pre-public review either:

- `src/xml_parse.c` (1,163 lines): the whole streaming parser, including the 4096-byte read window and the 64-element name stack. It appears in exactly one commit, the drop.
- `src/pdf_write.c` (774 lines), `src/font.c` (340), `src/report.c` (263), `src/layout.c` (225) and `src/page_sink.c` (206): one commit each.
- Atomic file publication (temporary file beside the destination, `fsync`, `rename`), in `src/convert.c` since the drop and carried through the OSR-4 rewrite with its contract intact.
- The 64 MiB RSS ceiling and the measurement protocol in `docs/contract.md`.
- The parser limits, unchanged in `src/xml_parse.h`: 64 open elements, 63-byte names, 8 attributes, 255-byte attribute values, 1023-byte text fragments.

Across all of history only three production files ever changed: `src/convert.c`, `src/main.c` and `include/ki_xml_pdf.h`.

## Superseded and vestigial

- **The byte-grid page sink.** `src/page_sink.c` and `src/page_sink.h` implement pages as a fixed grid of 90 columns by 48 lines (`XML2PDF_PAGE_COLS`/`XML2PDF_PAGE_LINES` in `src/convert.h`), capped at 4,368 bytes per page (48 rows of 90 columns plus newlines). The production pipeline does not use it: `src/convert.c` includes `src/layout.h`, and nothing in `src/` includes `page_sink.h` at all. Its only caller anywhere is the test `test_page_sink_flushes` in `tests/test_stream.c`. The library still compiles it in. Given that the review revision contained `page_sink.c` but not `font.c` or `layout.c`, the glyph-width layout almost certainly replaced the grid before the first public drop. See [the page sink page](systems/page-sink.md).
- **The Helvetica comment.** The comment above the grid constants in `src/convert.h` still describes "a letter page in Helvetica 10pt". The shipped font has been DejaVu Sans 10 pt all along (`/BaseFont /DejaVuSans` in `src/pdf_write.c`), and wrapping has counted glyph advance units rather than columns since `src/layout.c` arrived. The comment is a fossil of the byte-grid era. See [layout and fonts](systems/layout-and-fonts.md).
- **The legacy exports.** `xml2pdf_convert` and `xml2pdf_convert_cap` sit in the KI_0_1 node of `libki_xml_pdf.map` next to the `ki_*` API, plus three `xml2pdf_debug_*` hooks in `src/convert.h` that the map keeps private. The 0.2.0 changelog explicitly promises to preserve existing exports, so these stay until a 0.y ABI break is declared.
- **`baseline-manifest.json`.** A snapshot comparing the tree to the unreachable review revision, listing six matching files, seven changed and ten added. It reads like a museum plaque for the pre-public hardening, and nothing in the tree regenerates or retires it. It is collected with other candidates on [cleanup opportunities](cleanup-opportunities.md).
- **The Russian-first documents.** `README.md` was rewritten in English in OSR-6 (+145/-45) and the previous Russian onboarding was preserved as `README.ru.md` (122 lines). `docs/contract.md`, `docs/profile-decision.md` and `docs/license-decision.md` remain in Russian, and `CHANGELOG.md` keeps its 0.1.0 entry in Russian while 0.2.0 is in English.

## Major rewrites

- **OSR-4 rebuilt the I/O layer.** `src/main.c` grew by 88 lines with 32 removed to accept `-` for stdin and stdout; `src/convert.c` moved 55/45 lines for stream plumbing and the new `ki_convert_to_file`. The CLI went from file-only to pipeline-capable, and the public header gained the function that made callback input safe for atomic file output. Tests followed: 495 new lines in `tests/test_stream.c` and the new 240-line `tests/cli_pipeline.py`.
- **`tests/fuzz_smoke.py` was rewritten in OSR-7** (+145/-89, more than half the file). The fuzz harness that shipped with 0.1.0 was a 129-line smoke test; 0.2.0 carries a 185-line bounded fuzzer with an independent reader.
- **`README.md` doubled twice.** 49 lines at the drop, +68 in the CI fix commit, then +145/-45 in OSR-6 to reach 211 lines.
- **The largest rewrite happened before the public history began**: adding `src/font.c` and `src/layout.c` after the review, which turned a byte-grid renderer into a glyph-width typesetter.

## Growth

| Point | Tracked files | Lines | Test harnesses |
| --- | --- | --- | --- |
| Drop, 2026-10-07 | 49 | 8,824 | 1 C file (2,693) + `tests/verify.sh` (551) |
| After PR #1, 2026-10-13 | 67 | 11,726 | 2 C programs (3,318) + 4 Python (899) + 6 shell (1,161) |

CI grew from a 95-line workflow to 137 lines across five jobs (`gcc`, `clang`, `asan`, `resources`, `source`). The `gcc` job even asserts that broken tooling is rejected: a run with `CC=false`, a bogus PDF reader or a missing license file must fail for the gate to pass. The project stayed small on purpose; what grew was the ring of checks around it.

## Related pages

- [By the numbers](by-the-numbers.md) for the measurements behind this history
- [Design decisions](background/design-decisions.md) for KW-1004, KW-1021 and KW-1024
- [Testing](how-to-contribute/testing.md) for what the harnesses do
- [Overview](overview/index.md) and [Architecture](overview/architecture.md)
