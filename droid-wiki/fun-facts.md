# Fun facts
By Artem.

Small, verifiable details at **2026-10-17**, HEAD `d573069`. Facts about experiments are labeled as experiments; they are not new supported features.

## The PDF speaks Russian even when onboarding is English

`src/report.c` emits `Запись <id>`, `Всего записей: N`, `Выбрано записей: N` and `Пропущено записей: N`. Those labels survive from the initial public implementation. `examples/report-v1.xml` uses the title `Сводка`, while `examples/catalog_map.c` demonstrates an English custom mapper. Output language belongs to the mapping, not to a global locale switch.

The repository is similarly bilingual: `README.md` and `README.ru.md` are parallel onboarding documents, while the original contract and license/profile decisions remain Russian. See [report-v1](features/report-v1.md) and [custom mappers](features/custom-mappers.md).

## It can write the answer before it knows the length

PDF font and page content streams are compressed straight to the destination. Instead of seeking back to patch `/Length`, `src/pdf_write.c` writes a reference to the next object, then writes that length object after the compressed bytes. Every page therefore needs three objects: content, compressed-content length, and page.

ToUnicode takes the opposite route: its fixed-repertoire body is counted once, emitted once and left uncompressed, with a direct length. Neither technique requires a whole compressed PDF in memory. See the [PDF writer](systems/pdf-writer.md) and [streaming I/O](features/streaming-io.md).

## The font is both opened and distrusted

`FontFace` in `src/font.h` retains an open file, fixed metrics, its size and a 32-bit hash—not all 759,720 bytes of `fonts/DejaVuSans.ttf`. Before embedding, `font_stream_matches` in `src/font.c` rereads that same open file to check size and FNV-1a; `src/pdf_write.c` then deflates it in 16 KiB input chunks.

This is a change-detection check, not a cryptographic authenticity guarantee. The library still embeds the complete font, not a subset. See [layout and fonts](systems/layout-and-fonts.md).

## A word gets no extra suitcase

Word wrapping did not add an unbounded pending-word allocation. `Layout.line` in `src/layout.h` holds both committed line bytes and the pending word: `[0, len)` plus `[len, len + wlen)`. A long word falls back at complete UTF-8 code points; zero-advance marks stay with their base when the bounded line permits it.

That is why arbitrary token length does not imply arbitrary layout memory. It is also why a single oversized inseparable base-plus-marks cluster can still fail with a resource error. See [layout and fonts](systems/layout-and-fonts.md) and `tests/test_layout_wrap.c`.

## Metadata can say more than the body font

Title and producer strings use validated UTF-8 input and UTF-16BE hexadecimal PDF strings (`src/info.c`). They need no body-font glyphs. The same character can therefore be legal in `/Title` yet fail as `KI_CAUSE_FONT` if a mapper tries to draw it with the bounded body repertoire.

No date is invented: `CreationDate` is written only when explicitly supplied. UTF-16BE output strings do **not** imply UTF-16 XML input support. See [document information](features/document-information.md).

## The font boundary is not “English and Russian only”

The fixed body lookup slots cover U+0000–U+04FF plus 24 punctuation slots U+2010–U+2027 (`src/font.h`); drawable input must also be legal XML, and the selected font must provide a nonzero in-range GID. The range includes Greek as well as Latin and Cyrillic. U+04FF ends the base Cyrillic block, not every Cyrillic extension in Unicode.

An available glyph may have zero advance: “width zero” no longer means “missing.” No shaping or normalization is implied, and script addons remain an unimplemented ADR. See [font behavior](systems/layout-and-fonts.md) and [design decisions](background/design-decisions.md).

## Tests outweigh production—but research counts too

The measured `tests/` code is 16,147 lines against 6,437 production C/header lines, **2.51:1**. That numerator includes Python/shell harnesses, ABI canaries and research prototypes; it is not a coverage score. `tests/test_stream.c` alone has 3,655 lines, versus 1,482 in the largest production file, `src/xml_parse.c`.

There are also two deliberately broken tiny fonts: `tests/corpus/truncated-hmtx.ttf` (206 bytes) and `tests/corpus/truncated-cmap-184.ttf` (184 bytes). They test rejection, not typography. See [measurement scope](by-the-numbers.md) and [testing](how-to-contribute/testing.md).

## One scientific “no” saved a feature

The synthetic font-subsetting study measured the compressed font at 382,174 bytes in a 12,501,989-byte target report PDF: **3.06%**. Its best subset could save **2.92%** of that PDF, despite much larger percentage savings on tiny reports. OSR-10 therefore recorded no-go for an internal subsetter under the large-report goal.

Those numbers belong to the revision pinned in `docs/evidence/osr-10-font-spike/README.md`, not a fresh HEAD benchmark. The research also checked raster output: text extraction alone can survive an intentionally broken `.notdef` font. See [design decisions](background/design-decisions.md).

## Key source files

| Sources | Interesting detail |
| --- | --- |
| `src/report.c`, `examples/catalog_map.c` | Mapper-owned output language |
| `src/pdf_write.c`, `src/font.c`, `src/font.h` | Forward lengths, streamed font and change check |
| `src/layout.h`, `tests/test_layout_wrap.c` | Shared line/word storage |
| `src/info.c`, `tests/test_info.c` | Font-independent, explicitly dated metadata |
| `docs/evidence/osr-10-font-spike/README.md` | Revision-scoped subsetting experiment |

[Overview](overview/index.md) · [Architecture](overview/architecture.md) · [By the numbers](by-the-numbers.md) · [History](lore.md)
