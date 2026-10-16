# OSR-18 bounded table spike (b018964)

**Decision: no-go for a table API in the library now, and defer OSR-18. No table API, profile or layout change was shipped. OSR-18 stays open because no real consumer, target report or permitted example exists.** The user authorized synthetic data only. Synthetic success shows technical feasibility. It does not show that anyone needs tables. Card exit criterion 1 (a permitted real report, its expected result, and why report-v1 blocks or a custom mapper are not enough) cannot be met by this spike.

On generated rows, a streaming fixed-column table is bounded only because of explicit byte caps. With the caps proposed below, the table state is about 27 KiB on top of today's layout buffers. Without the caps, one 8 MiB cell must be held whole. An external pre-layout already produces aligned fixed-column grids with today's unchanged `xml2pdf`, and this was checked with Poppler text and raster output.

`tests/research_table.py` is a research tool. The library, CLI, installed examples, Make/CMake targets and source archive do not use it.

## Timebox

The spike started at 2026-10-16 13:33:23 MSK, after the OSR-18 card was read. `predictions.txt` was written at about 13:36, before any harness code. The final measured run (`run2`) finished at about 13:42, and this record was finished at about 13:45. The total is about 12 minutes, well within the four-hour limit.

## Inputs and commands

- Source: `b018964` (OSR-16 head of the shared `osr-8` branch), `make -j2 xml2pdf`. The hashes of `xml2pdf`, the font, the harness and the fixture are in `SHA256SUMS`.
- Font and geometry: `fonts/DejaVuSans.ttf`, default repertoire, default geometry (Letter, 40 pt margins, 10 pt type, 14 pt leading, 532 pt text width, 50 lines per page).
- Run: `python3 tests/research_table.py --scratch ../osr18-scratch/run2 --json docs/evidence/osr-18-table-spike/results.json`. Result: all six groups passed (`caps`, `firstn`, `fixture`, `stream`, `hugecell`, `late`), exit 0, 28.5 s wall time. A first run (`run1`, before the negative controls were added) produced the same fixture XML (`f6394f97…`), fixture PDF (`c7c011d5…`) and 100,000-row XML (`9ae014b5…`).
- Scratch on the project disk, `/home/artem/projects/osr18-scratch`, outside the repository. The large XML/PDF files were deleted after measurement.
- All rows are generated from fixed strings in the harness. They contain no private data.

## Model, fixed before measuring

**Fixed columns.** Column widths are given up front in points, with an 8 pt gutter. Each cell wraps inside its column with the OSR-14 rule: break at spaces and tabs, cut a word wider than the column before the code point that would overflow, and keep zero-advance marks with their base. Spaces at a break are not drawn. A literal LF inside a cell is a line break in that cell. A row is as tall as its tallest cell. An empty cell is blank, and a row of empty cells is one blank line.

**Pages.** The header repeats at the top of every page. A row that fits the remaining lines is placed. A row that fits an empty page body but not the rest of this page moves to the next page, and the rest of this page stays blank. A row taller than one page body is split at line boundaries, starting on the current page, with the header repeated on every page it covers.

**Streaming.** The table holds the header and one row as UTF-8 bytes. Wrapped lines come from a cursor per column, so the state per column is constant. Everything else goes straight to the page sink.

**External route used for the real PDFs.** The same model runs outside the library as a pre-layout. It writes every physical line into one records-v1 element as CDATA (`<t><![CDATA[…]]></t>`). It pads each cell line with spaces from the absolute pen position to its column start, so the error never accumulates. The element name `t` takes the first line of page 1, so page 1 has 49 table lines. `]]>` inside a cell is split across two CDATA sections.

## Expected output (synthetic fixture)

The fixture has 30 rows and the columns `N` (30 pt), `Наименование товара или услуги` (170 pt), `Описание` (220 pt) and `Сумма, руб.` (70 pt), starting at x = 0, 38, 216 and 444 pt from the left margin. It covers:

- ordinary Cyrillic text with `«»`, `—` and `…`;
- an empty cell (row 3) and a row of four empty cells (row 4);
- a 57-character word without spaces in the 170 pt column, cut at code points;
- `и` + U+0306 (a combining mark) and a tab;
- `]]>` and an LF inside a cell;
- a 4,000-byte cell that is taller than a page (row 10);
- a later, wider value (row 25), which wraps in the fixed width.

The first lines of page 1, as `pdftotext` returns them (blank lines omitted):

```
t
N          Наименование товара или            Описание                                                        Сумма, руб.
            услуги
1          Товар 1                                            Описание позиции 1: Синтетическая          137,50
                                                                    строка отчёта с кириллицей, «кавы
2          Товар 2                                            Описание позиции 2: Синтетическая          274,50
```

The complete expected text for all four pages is `results.json` → `fixture.expected_pages`.

**Results.**

- `xml2pdf` exit 0. The PDF has 4 pages, as the model predicts, with 159 physical lines and a 2-line header.
- The non-blank `pdftotext` lines of every page equal the expected lines exactly, after NFC normalization only.
- All 120 cells are recovered from their wrapped lines, ignoring whitespace. The tall row covers 2 pages.
- **Raster check** (`pdftoppm -r 144 -gray`, pages 1–3). This check does not depend on the PDF writer. It found 0 ink pixels in the gutters (from one space past each column's width to the next column) and ink within one space of the column start for all 238 non-empty cell lines.
- **Negative controls.**
  - If the pre-layout wraps the first three columns 40 pt wider than declared, the raster check finds 1,617 gutter ink pixels and fails.
  - Dropping one character from one expected line makes the text comparison fail.

The space is 3.18 pt, so a column starts up to 3.18 pt to the right of its intended x, and never to the left of it.

## Proposed numerical caps and failure policies

The caps assume the default geometry. "Byte" means UTF-8 bytes, and "cell" means the cell's complete text, however the parser splits it into fragments.

| Bound | Value | Why | Exceeded → |
|---|---:|---|---|
| Columns | 16 | The width allows 24 columns of the 14.22 pt minimum (below). 16 columns of 1,023-byte physical lines leave room for padding. | usage error before input |
| Minimum column width | widest glyph = 1.4219 em (U+01C4, 14.22 pt at 10 pt) | Otherwise the code-point fallback cannot progress | usage error before input |
| Sum of widths and gutters | ≤ text width minus one space | The padded line must not wrap | usage error before input |
| Header cell | 255 bytes | Same as the parser's attribute value limit | resource error |
| Header lines | 12 (and < lines per page) | Leaves 38 of 50 body lines | resource error |
| Cell | 4,096 bytes | 4 parser fragments of 1 KiB | resource error at byte 4,097, with row and column |
| Row | 16,384 bytes | One buffered row | resource error at byte 16,385 |
| Physical line | 1,023 bytes | The existing layout line limit | resource error (for example, one base with 600 marks, 1,212 bytes) |
| Cells per row | exactly the column count | No silent change of shape | profile error for more or fewer cells |
| Glyph outside the repertoire | n/a | Existing font rule | font error with `U+…` |

All 11 policy cases fail with the expected cause, rule, row, column and byte (`results.json` → `caps`). Nothing is truncated or dropped. With fixed widths, later wider data wraps. Widths are never changed after the first row.

**Tall rows inside the caps.** A capped row can still be very tall. Four 4,096-byte cells of `@` (the widest printable ASCII glyph) in 15 pt columns produce **84 pages for one row**. One 4,096-byte cell of `' ` pairs in a 15 pt column produces 14 pages. So the row cap bounds memory, but not pages or output. `max_pages` and `max_output_bytes` remain the only page and output limits, and a row must be split across pages instead of held per page. With 200 one-line rows and a 2-line header, the table has 5 pages, which equals ⌈201 / 48⌉.

## Memory: ordinary and adversarial

| Case | Measured or calculated | Result |
|---|---|---:|
| Fixture, 30 rows, with the 4,000-byte cell | prototype buffer peak (header + row) | 4,115 bytes |
| 100,000 short rows, 4 columns | prototype buffer peak | 125 bytes |
| 8 MiB cell in 1 KiB fragments, caps on | refused at byte 4,097 after 5 of 8,192 fragments; buffer peak | 4,190 bytes |
| Control: same 8 MiB cell, caps lifted | accepted; buffer peak | 8,388,702 bytes |
| First-N widths, N = 32 | measured buffer on the fixture / worst case 32 × 16 KiB | 9,336 bytes / 524,288 bytes |

The control shows that the caps are what bound memory. A "one row in memory" design is not bounded without them. In a fixed-column table, the first line of the row needs text from every column, so all cells before the last must be held. A sequential profile never needs that.

**Converter RSS on real runs** (`/usr/bin/time`, the `xml2pdf` process only):

| Run | Input | Pages | PDF | Peak RSS | Time |
|---|---:|---:|---:|---:|---:|
| tiny records-v1 document | 21 bytes | 1 | — | 2,684 KiB | — |
| 100,000-row table, pre-laid out | 13,096,859 bytes | 2,041 (model 2,041) | 5,799,009 bytes | 2,924 KiB | 1.45 s |
| the same rows as report-v1 blocks | 21,466,752 bytes | 8,001 | 9,808,831 bytes | 2,756 KiB | 1.45 s |
| tiny report-v1 document | — | 1 | — | 2,788 KiB | — |
| one record with an 8,388,608-byte report-v1 field | 8,388,829 bytes | 1,158 | 1,228,061 bytes | 2,852 KiB | 0.53 s |

The last row of the 100,000-row table is on the last page. For the 8 MiB field, `НАЧАЛО` is on the first page and `КОНЕЦ` on the last. report-v1 streams the huge value with +64 KiB RSS, because a field is never buffered. The same value as one uncapped fixed-column cell in a 130 pt column would wrap to 289,263 lines, which is **6,027 pages for one row** with a 2-line header. It would need an 8 MiB row buffer. With the 4,096-byte cap, it fails instead.

**Calculated in-library state** for the caps above, from `sizeof(Layout)` = 52,824 bytes and `sizeof(InkRun)` = 48 bytes measured on this build:

| Part | Bytes |
|---|---:|
| header text, 16 × 255 | 4,080 |
| one row, cap | 16,384 |
| per-column cursor (x, width, start, length, offset, InkRun ≈ 80 bytes) × 16 | 1,280 |
| x positions of segments on the page, 50 lines × 16 × 8 | 6,400 |
| **table total** | **28,144 (about 27.5 KiB)** |
| existing `Layout`, mostly the 50 × 1,024-byte page | 52,824 |

That is constant per conversion and independent of the number of rows, pages and the input size. `pdf_add_page` currently takes left-aligned lines only. An in-library table would also need positioned text segments in the page content.

**First N rows.** Auto widths from the first N rows hold N rows, at most N × 16 KiB = 512 KiB for N = 32. On the fixture, proportional widths came out as 15, 37, 435 and 16 pt, because one long description dominates. The later wider row 33 wrapped to 24 lines. Proportional first-N sizing is a poor policy on this data. If one is ever accepted, it needs an explicit N, the byte cap, per-column minimum and maximum widths, and the rule that later wider data wraps and is never truncated.

## Quotas and late errors

- **Page quota.** A valid 9,000-row table needs 184 pages. `--max-pages 183` gives exit 4 (`resource limit`). The existing destination stayed byte-for-byte the same, and no temporary file was left.
- **Late cap error in the pre-layout.** Row 9,000 has a 4,097-byte cell. The pre-layout stops with a resource error (row 8,999, column 2, byte 4,097), and the pipe ends mid-element. `xml2pdf` then reports `xml: truncated cdata at line 9183, column 102, byte 1196653` (exit 1). With a file destination, the previous file was kept and no temporary file was left. On stdout, 881,072 PDF bytes stayed written, as the contract says for stdout and callbacks.
- **Atomicity.** With the external route, atomicity comes from the existing file lifecycle. The cause the converter reports is misleading (an XML truncation), and the real cause is only in the pre-layout. A pipeline needs `set -o pipefail`, as with `iconv` in OSR-16. An in-library table would report `KI_CAUSE_RESOURCE` with a position.

## Alternatives compared

| | report-v1 blocks or custom mapper (today) | External pre-layout to padded lines (today) | Fixed columns in the library |
|---|---|---|---|
| Visual result | `name: value` blocks, not a grid; 8,001 pages for 100,000 rows | aligned grid within one space (3.18 pt), repeated header; 2,041 pages | grid with exact x placement |
| Memory | streams every field, including 8 MiB | one row in the consumer's code; the converter is unchanged (+240 KiB RSS for 2,041 pages) | about 27.5 KiB with caps; unbounded without them |
| Huge cell | streamed (+64 KiB RSS) | the consumer's cap or its memory | resource error at the cap |
| Page geometry | any OSR-15 geometry | the consumer must copy `max_lines`, text width and font advances from the library | from the shared `PageFrame` |
| Error cause | exact | misleading XML truncation unless `pipefail` is set | `KI_CAUSE_RESOURCE` / profile, with a position |
| Limits | none new | no rules, right alignment only within one space, the `t` line on page 1 | only the caps above |
| Build/licensing | none | none | none new |
| Maintenance | none | each consumer keeps a copy of the wrap rule and font metrics | new profile or API, page content with x positions, a cap and fuzz matrix, docs in EN/RU |

## Seams with OSR-14 and OSR-15

- **OSR-14.** Cells reuse the word-wrap rule unchanged, but per column. The column limit replaces the text width. The 1,023-byte line limit applies to the assembled physical line, not to the cell.
- **OSR-15.** Column widths, the minimum column (1.4219 em × font size), the header limit and the lines per page all depend on the validated `PageFrame`. They must be checked after the font loads, as geometry is today. With a small page, the header may not fit at all. This study does not name any new function or structure.

## Predictions versus results

1. The widest glyph would be under 10 pt. **Wrong:** U+01C4 is 14.22 pt, so the minimum column is 14.22 pt. The width still allows 24 columns, and 16 remains the cap.
2. Columns would start within one space to the right of their intended x. **Confirmed** by the raster check.
3. The state would be about 21 KiB. **Low:** the calculation gives about 27.5 KiB once positioned segments are included.
4. Table RSS would be within 200 KiB of the baseline. **Slightly exceeded:** it was +240 KiB compared with a tiny document and +168 KiB compared with report-v1 blocks of the same rows.
5. The 8 MiB cell would be refused at byte 4,097 with at most 16 KiB held. **Confirmed** (4,190 bytes). The uncapped page estimate was low: 6,027 pages, not 4,000.
6. report-v1 would stream the 8 MiB value with flat RSS. **Confirmed** (+64 KiB).
7. Late errors would keep the destination. **Confirmed**, with a misleading converter cause on the external route.
8. The decision would be no-go and defer. **Confirmed.**

## Decision and conditions for reopening

**No-go for an in-library table API now, and defer OSR-18.** No consumer, target report or permitted example exists. Two routes exist without any library change: sequential report-v1 blocks (or a custom mapper), and an external pre-layout for simple grids. Nothing shows that either is insufficient. A library table would need a new content model with positioned segments, a new failure surface and a permanent test burden, all for unproven demand.

OSR-18 stays open in Linear. The return condition is the one on the card: a concrete table scenario with a permitted example and expected output. A synthetic study, including this one, is not that.

If such a consumer appears, run their example through this harness first. Accept an implementation card only if the external pre-layout or report-v1 is shown to be insufficient for that report. The card would then fix:

- the caps in the table above, or values measured on the real report;
- fixed widths, or first-N sizing with N, a byte cap and per-column minimum and maximum widths;
- the page rule for tall rows (whether to repeat a key column on continuation pages is open);
- exact causes and positions;
- the same text, raster and negative-control matrix;
- an unchanged ABI 0.2.

This spike does not deliver that card.

## Files

- `predictions.txt`: expected values, written before any measurement.
- `results.json`: every check record, expected fixture pages, caps, RSS and page measurements, and controls.
- `SHA256SUMS`: the binary, the font, the harness, the predictions and the fixture XML/PDF.

## Linear status snapshot

Read with the Linear tools on 2026-10-16 at about 13:44 MSK, after the measurements: OSR-18 is in state **Backlog** with the label `Исследовать`, `started_at` and `completed_at` null, last updated 2026-10-15T14:16:44Z. The card is marked "Deferred; не Ready для реализации". This spike did not change it. Its return condition, a concrete table scenario with a permitted example, is still unmet.
