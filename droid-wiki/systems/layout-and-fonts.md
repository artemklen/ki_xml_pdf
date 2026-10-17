# Layout and fonts
Active contributors: Artem

`src/layout.c` turns incremental UTF-8 text into word-wrapped lines and fixed-size pages. `src/font.c` supplies glyph IDs, advances and ink bounds from a TrueType file; it retains an open file and fixed metrics, not a full-font heap copy.

## State and integration

| Abstraction | Storage / responsibility |
| --- | --- |
| `Layout` | One 1024-byte line/pending-word buffer, up to 50 fixed page lines, lengths and a four-byte UTF-8 hold |
| `FontFace` | Open `FILE *`, source path, size/FNV hash and fixed repertoire metrics/ink arrays |
| `PageFrame` / `InkRun` | Shared geometry, baseline/line count and measured horizontal extents |
| `layout_text`, `layout_break`, `layout_finish` | Incremental text, explicit nonempty break and final flush |

Definitions are in `src/layout.h`, `src/font.h` and `src/geometry.h`. This subsystem has two C implementation units and two focused C test files, `tests/test_layout_wrap.c` and `tests/test_font_metrics.c` (1:1). Independent Poppler checks are `tests/wrap_check.py` and `tests/font_metrics_check.py`.

Mappers call `ki_doc_text`/`ki_doc_break` in `src/convert.c`. The default diagnostic mapper uses layout directly. Layout and the writer use the same [geometry](geometry.md) and font metrics.

## Word wrapping

`Layout.line[0,len)` is placed content; the following `wlen` bytes are the pending word. There is no separate token-sized allocation (`src/layout.h`).

- Only U+0020 space and U+0009 tab are word-break opportunities. Tab is measured/drawn as a space; ActualText preserves its original value. NBSP, hyphens and punctuation stay in their word.
- A pending word that does not fit after the current content starts the next line. Exact-width words fit, with no empty first line.
- A word wider than a full line is cut at a complete UTF-8 code point before the next positive-advance glyph, retaining zero-advance marks with their base.
- The same fallback handles the 1023-byte line limit. A single base/mark cluster exceeding that buffer or the usable width fails with `KI_CAUSE_RESOURCE`; it is not split or silently dropped.
- Spaces/tabs stay on the line they follow, even beyond its right edge. The writer omits out-of-margin trailing space glyphs but retains them in ActualText.
- LF/CR in emitted text commit a line, even empty. `layout_break` on an empty line adds nothing. Finishing places the pending word once.

`word_append`, `place_word`, `commit_word` and `space_append` in `src/layout.c` implement this policy. A four-byte hold permits `ki_doc_text` calls to split UTF-8 sequences. Read sizes, TEXT/CDATA boundaries and separate emission calls do not change the wrapping of the same emitted text.

`tests/test_layout_wrap.c` checks exact widths, NBSP, tabs, clusters, fragments, page boundaries and complete 64 KiB / 4 MiB unbroken tokens. Large words do not need large buffers.

## Page lifecycle

`commit_line` copies a finished line into fixed `Layout.page` storage, using `PageFrame.max_lines` capped at 50. `flush_page` calls `pdf_page_begin`, then `pdf_page_line` for each stored line, then `pdf_page_end`. It does not join the page into a second heap string.

The [PDF writer](pdf-writer.md) has one lazily allocated content-stream buffer reused for the whole conversion. Layout clears its line count after flushing; it does not retain prior pages.

## TrueType loading and embedding

`font_load` accepts files from 12 bytes through 8 MiB, reads the table directory and required table spans, validates offsets/lengths, builds fixed metrics and releases those temporary table allocations. Loading can temporarily allocate large individual tables such as `glyf`; bounded document memory does not mean allocation-free font initialization.

Required tables are `head`, `cmap`, `hhea` and `hmtx`. The mapper uses Windows platform 3 / encoding 1 / format 4 cmap; `maxp`, when present, bounds glyph IDs. `glyf`/`loca` must be present together and supply checked glyph boxes; without outlines the advance box stands in. Space and `A` must exist. A present glyph with zero advance is valid (`src/font.c`).

| Fixed repertoire in `src/font.h` | Meaning |
| --- | --- |
| U+0000–U+04FF | Lookup slots for Latin, Greek, Cyrillic and combining marks, subject to XML legality and actual font coverage |
| U+2010–U+2027 | 24 punctuation slots |

Missing/out-of-range glyphs produce `KI_CAUSE_FONT` with `U+XXXX`, not an XML syntax error. There is no shaping engine, font fallback, subsetting or general Unicode font support.

The font name comes from usable printable-ASCII nameID 6 (1–63 characters), otherwise `KiFont-XXXXXXXX` from a file hash. The writer escapes PDF-name delimiters. Before embedding, `font_stream_matches` rechecks the open file's size and FNV-1a checksum; `src/pdf_write.c` then rereads that file in 16 KiB chunks into Flate. This detects tested pre-embedding changes, not arbitrary concurrent mutation or cryptographic tampering.

## Font lookup

`PdfConfig.font_path` uses only that explicit nonempty path. Otherwise `font_open_default` tries:

1. Nonempty `KI_XML_PDF_FONT`, exclusively; a failure does not fall back.
2. The compiled install-prefix font path.
3. Font paths relative to `/proc/self/exe`: `fonts/DejaVuSans.ttf`, `../fonts/DejaVuSans.ttf`, `../share/ki_xml_pdf/DejaVuSans.ttf`.
4. `fonts/DejaVuSans.ttf` relative to the working directory.

The bundled font has a separate license. Font discovery and cookie streams are part of the Linux/glibc-only platform contract; see [Packaging](../features/packaging.md) and [Dependencies](../reference/dependencies.md).

## Change entry points

- Wrapping: `word_append`/`place_word` in `src/layout.c`; preserve cluster and fragment invariants.
- Font parsing/repertoire: `src/font.c`, `src/font.h`; update writer `/W` and ToUnicode enumeration together.
- Ink/placement: `src/geometry.c`; writer and layout must agree on rounded values and PDF advance rounding.
- Font search/install path: `font_open_default`, `Makefile`, `CMakeLists.txt`; test installed consumers outside the source tree.

## Key source files

| File | Role |
| --- | --- |
| `src/layout.c`, `src/layout.h` | Shared line/word buffer and fixed page emission |
| `src/font.c`, `src/font.h` | Checked TTF tables, metrics, file lifecycle and lookup |
| `src/geometry.c`, `src/geometry.h` | Ink fitting and font-aware page frame |
| `src/pdf_write.c` | Reused content buffer, ActualText and streamed font embedding |
| `tests/test_layout_wrap.c`, `tests/test_font_metrics.c` | Wrap/mark/name/mutation regressions |
| `fonts/DejaVuSans.ttf`, `fonts/DejaVuSans.copyright` | Bundled font and license |

Related: [Page geometry](../features/page-geometry.md), [Custom mappers](../features/custom-mappers.md), [Resource limits](../features/resource-limits.md).
