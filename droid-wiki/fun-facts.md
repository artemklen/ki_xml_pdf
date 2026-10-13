# Fun facts

Five small things about this repository that are easy to miss and easy to verify. Each one below was checked against the files cited.

## A gibibyte goes in, 2.3 MiB stays resident

The historical large-proof run in `docs/contract.md` feeds the converter a synthetic report of 1,073,741,815 bytes, one GiB minus exactly 9 bytes. Peak converter RSS was 2,372 KiB. The input is roughly 442 times the memory used to process it, and the peak sits at 3.6% of the 64 MiB acceptance ceiling. The run read all 19,522,650 records to select the first 1,000, produced 41 pages and 1,009,241 PDF bytes in 7.61 seconds, and left an offset spool of just 720 bytes: about 18 bytes of on-disk bookkeeping per page. The whole point of the architecture is that none of these numbers grow with the input; the parser works through a fixed 4096-byte window (`src/xml_parse.c`).

## The PDF answers in Russian

Ask for `--profile report-v1` and the generated PDF speaks Russian regardless of the input. The labels are hard-coded in `src/report.c`: every record is headed "Запись <id>", and the footer reads "Всего записей: N", "Выбрано записей: N" and "Пропущено записей: N". The 0.2.0 entry in `CHANGELOG.md` states that existing report-v1 labels are unchanged, so this is policy, not accident. The bundled fixture `examples/report-v1.xml` plays along, with title "Сводка" and field names like "Название" and "Статус". The repository itself is bilingual the same way: `README.md` and `docs/report-v1.md` are English, `README.ru.md`, `docs/contract.md` and both decision records are Russian, and `CHANGELOG.md` switches from a Russian 0.1.0 section to an English 0.2.0 section in the middle of the file.

## The biggest file in the repository is a test file

`tests/test_stream.c` stands at 3,259 lines, 2.8 times the largest production file (`src/xml_parse.c`, 1,163 lines), and nearly 28% of every tracked text line in the tree. All test harnesses together (C, Python and shell under `tests/`) total 5,378 lines against 4,484 production C lines, about 1.2 to 1. The test file contains 38 `XXXXXX` scratch-path templates for temporary files, more than the rest of the repository combined, because each test creates and cleans up its own scratch files rather than sharing state.

## Zero TODOs, and one deliberately fake font

A whole-word search for `TODO` and `FIXME` across the tracked tree returns nothing. Not one marker, in any file, in any language. The nearest thing to debt is a 206-byte file named `tests/corpus/truncated-hmtx.ttf`, which is not a font at all: it is hand-built binary that mimics a TrueType file with its `hmtx` metrics table cut short, so `src/font.c` can prove it rejects broken metrics instead of rendering with them. The real font, `fonts/DejaVuSans.ttf`, is 759,720 bytes, about 3,700 times larger than the impostor.

## The font ceiling stops at U+04FF, exactly where Cyrillic ends

`src/font.h` fixes the glyph table at `FONT_BMP_MAX 0x500`, so the accepted repertoire is U+0009, U+000A and U+000D, U+0020 through U+04FF, plus exactly 24 punctuation code points at U+2010 through U+2027, and nothing else. U+04FF happens to be the last code point of the Cyrillic block, which makes the repertoire a clean fit for English plus Russian and nothing beyond. The bundled DejaVu Sans actually contains thousands more glyphs (Greek, Armenian, Georgian and others), but the fixed lookup tables decline to use them: an Armenian character fails with `KI_CAUSE_FONT` and a `U+` code point rather than rendering. The design trades repertoire for a font face that cannot grow, whatever the document contains.

## Related pages

- [By the numbers](by-the-numbers.md) for the measurements behind these facts
- [Layout and fonts](systems/layout-and-fonts.md) for how the repertoire is enforced
- [The report-v1 profile](features/report-v1.md) for the labels and grammar
- [Testing](how-to-contribute/testing.md) for the large-proof and fuzz harnesses
