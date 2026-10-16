# OSR-10 TTF subsetting spike after Flate (56d30ef)

**Decision: no-go for an internal TTF subsetter under the OSR-8 large-report goal; defer the short-report case until a real consumer asks for it.** The measured residual font share of the agreed large report after OSR-9 Flate is 3.06%, and the best subset measured here would remove 2.92% of that PDF. No subsetter was implemented or shipped. `tests/research_font_subset.py` is a research harness that sizes subsets and splices them into copies of existing PDFs; the converter does not use it.

## Timebox

The spike started at 2026-10-16 06:36:54 MSK, after the OSR-9 results (`docs/evidence/osr-9-flate-3acf0cb`) were committed. Measurements finished at 06:43:31 MSK and the record was written by about 06:50. The total is well under the four-hour limit. `predictions.txt` lists the expected values written before any measurement.

## Inputs and commands

- Source: `56d30ef` (OSR-9 Flate evidence on top of `3acf0cb`); `make -j2 xml2pdf`. The binary hash is in `SHA256SUMS`.
- Font: `fonts/DejaVuSans.ttf`, 759,720 bytes, SHA-256 `57f73e11f51999432bf7ab22ce55b6f945d5eca1bf824404cfa9ec2e3718c84e`. It has 6,253 glyphs, long `loca`, `post` format 2, `maxComponentDepth` 4 and OS/2 `fsType` 0 (installable embedding, subsetting allowed).
- Fixtures: the OSR-23 recipe `tests/resource_recipe.py --profile report-v1 --target-bytes N` with N = 4096 (short, 67 records, 3 pages), 1 MiB (15,981 records, 640 pages) and 16 MiB (agreed large target, 246,368 records, 9,855 pages). The fixture JSON files hold the recipe metadata. The 16 MiB fixture hash `2cc851a4…` and the PDF size 12,501,989 bytes match OSR-9 exactly.
- Conversion: `xml2pdf --profile report-v1 report-N.xml report-N.pdf`.
- Measurement: `python3 tests/research_font_subset.py report-short.pdf report-1MiB.pdf report-16MiB.pdf > measure.json` (3.2 s, 62,992 KiB RSS for the harness itself).
- Splices: `python3 tests/research_font_subset.py --splice IN OUT {usage|repertoire|notdef-only}`. Comparison: `pdftotext` and `pdftoppm -r 100` (Poppler 25.03), with results in `poppler-comparison.txt`.
- External route: a GID-preserving repertoire subset file `dejavu-repertoire.ttf` (SHA-256 `4ecf8b6c…`), passed through the existing `xml2pdf --font`.
- Harness self-test: `python3 tests/research_font_subset.py --self-test fonts/DejaVuSans.ttf`. It checks that `.notdef` is kept, that the whole-font checksum equals `0xB1B0AFBA`, that `numGlyphs` is preserved and that the composite closure of U+00C0 (components 36 and 5925) is complete. Its control shows that a closure without the glyph misses the components. It also checks that four damaged fonts are rejected: a cut table directory, a cut `loca`, a cut `glyf` and a `loca` offset past `glyf`.

## Measured residual font share after Flate

The compressed FontFile2 stream (zlib default level, the writer's settings) is a constant 382,174 bytes in every PDF.

| Report | PDF bytes | Font Flate bytes | Font share |
|---|---:|---:|---:|
| short (4 KiB XML, 3 pages) | 415,944 | 382,174 | 91.88% |
| 1 MiB XML, 640 pages | 1,189,642 | 382,174 | 32.13% |
| **16 MiB XML, 9,855 pages (target)** | **12,501,989** | 382,174 | **3.06%** |

The small report does not stand in for the target. The share falls in proportion to the input, because the page streams grow and the font does not.

## Potential savings (GID-preserving subsets, measured)

The subsets keep `numGlyphs`, `hmtx` and CID = GID, and they empty the `glyf` entries that are not used. The writer's `/CIDToGIDMap /Identity`, `/W` and ToUnicode therefore stay valid without changes. The glyph sets:

- **usage**: `.notdef`, the 48 GIDs the page streams reference, and their composite closure, 53 glyphs in total.
- **repertoire**: everything xml2pdf can emit (U+0000–04FF, U+2010–2027) plus closure, 1,225 glyphs. This set is known when the font loads.

The table variants:

- **keep**: all tables kept.
- **drop-layout**: GSUB, GPOS, GDEF, kern and FFTM removed.
- **pdf-min**: only the ISO 32000 FontFile2 tables (head, hhea, hmtx, loca, maxp, glyf, cvt, fpgm, prep).

| Variant | Font Flate bytes | Saving | Short PDF → | 16 MiB PDF → | Saving, share of 16 MiB PDF |
|---|---:|---:|---:|---:|---:|
| full (today) | 382,174 | 0 | 415,944 | 12,501,989 | 0 |
| repertoire / keep | 150,597 | 231,577 | 184,367 | 12,270,412 | 1.85% |
| repertoire / drop-layout | 125,843 | 256,331 | 159,613 | 12,245,658 | 2.05% |
| repertoire / pdf-min | 87,353 | 294,821 | 121,123 | 12,207,168 | 2.36% |
| usage / keep | 80,158 | 302,016 | 113,928 | 12,199,973 | 2.42% |
| usage / drop-layout | 55,170 | 327,004 | 88,940 | 12,174,985 | 2.62% |
| usage / pdf-min (upper bound) | 16,904 | 365,270 | 50,674 | 12,136,719 | 2.92% |

Remaining bytes in the usage subsets come from the tables that the GID-preserving layout keeps whole. Compressed, `post` is 28,000 bytes, `loca` 15,382, `hmtx` 9,272, `cmap` 4,922 and `name` 3,816. The other per-table numbers are in `measure.json`. Renumbering GIDs would also shrink `loca` and `hmtx`, but at most by part of the remaining 16.9 KB, so it does not change the large-report conclusion.

## Correctness evidence (prototype only)

- Spliced copies of the short report with the usage and repertoire pdf-min subsets have byte-identical `pdftotext` output and byte-identical 100 dpi renders of all pages, with no Poppler warnings. The same holds for the 1 MiB usage splice: identical text, and identical renders of pages 1–40 at 60 dpi. The spliced short PDF is 50,671 bytes, against the 50,674-byte estimate; the difference is shorter `/Length1` and length digits.
- **Negative control:** a `.notdef`-only subset changes the render hash (glyphs disappear), but the `pdftotext` output stays identical, because ToUnicode is independent of the outlines. **A subset contract must therefore require a visual check. Text extraction cannot detect missing glyphs.**
- **External route:** the existing `xml2pdf --font dejavu-repertoire.ttf` produces exactly the predicted sizes, 159,613 bytes (short) and 12,245,658 bytes (16 MiB). The text is identical to the full-font output, including all 9,855 pages of the 16 MiB report, and the short-report render is identical. Converter RSS fell from 2,628/2,688 KiB to 2,372/2,428 KiB. This route needs no code change. A user who needs small PDFs can embed a smaller font today.

## Alternatives compared

| | Keep full compressed TTF (today) | Internal subsetter in `src/` | External subsetter |
|---|---|---|---|
| Size effect on 16 MiB target | 0 | −1.85% to −2.92% | same as internal when the font is pre-subset |
| Size effect on short report | 0 | −56% to −88% | same |
| Build/dependency | none new (zlib already) | none, but an estimated 400–700 lines of new C TTF writer plus tests and fuzzing | HarfBuzz `hb-subset` (MIT core, Debian 10.2.0 runtime present, **no dev headers installed**; C++ and pulls freetype, glib, graphite2, png and brotli; a 1.5 MB `.so`) would break the zlib-only Make/CMake/pkg-config/static-consumer story. fontTools `pyftsubset` (MIT, Python, not installed) cannot be linked into a C library. Both are fine as **offline user tools** before `--font`. |
| Licensing | Bitstream Vera/DejaVu allows embedding; notice already shipped | Modifying the font: the Vera terms only forbid the names "Bitstream"/"Vera" for modified fonts, so DejaVu is fine. Arbitrary `--font` fonts need `fsType` checks (0x0002 restricted, 0x0100 no-subsetting, 0x0200 bitmap-only), plus OFL Reserved-Font-Name care | the user runs the tool and accepts that font's license |
| Converter memory | one TTF copy ≤ 8 MiB cap (760 KB here) | + an 8 KiB GID bitmap + closure work list ≤ 128 KiB. The output buffer can be avoided by a two-pass size/checksum then stream-to-deflate, else ≤ TTF + 2·(numGlyphs+1) bytes (≈ 8.1 MiB worst case at the cap) | none in the converter; a smaller loaded font lowers RSS (measured −256 KiB) |
| Streaming order | font written in `pdf_begin` before pages | repertoire subset: computed at load, order unchanged. Usage subset: the font must move after the last page (forward references are legal; no seek is needed, but a late font failure would occur after the callback has already accepted page bytes) | unchanged |
| Maintenance/risk | none | malformed-font surface grows (composite depth/cycles, loca/glyf consistency, checksum, hinting-instruction references to dropped glyphs); visual oracle needed | external |

## Feasibility checklist (for any future contract)

- **`.notdef`:** GID 0 is always kept (the harness asserts it). The PDF reader draws it for missing glyphs.
- **Composite closure:** 2,607 of 6,253 DejaVu glyphs are composite, and repertoire glyphs reference non-repertoire components (U+00C0 → GID 5925). The closure must be transitive, bounded by `maxComponentDepth` (4 here) and cycle-safe. `.notdef` and the closure take the usage set from 48 to 53 glyphs.
- **Tables:** `glyf` and `loca` must be rewritten; the harness writes long `loca` and sets `head.indexToLocFormat` = 1. `maxp` stays valid when GIDs are preserved; with renumbering, `numGlyphs` changes and `hmtx` must be cut consistently with `hhea.numberOfHMetrics`. `head.checkSumAdjustment` and every table checksum must be recomputed (the harness verifies `0xB1B0AFBA`). `cmap` is not needed in a CIDFontType2 FontFile2; the loader still needs it in the source font. `cvt`/`fpgm`/`prep` must be kept: hinting programs do not reference GIDs, but `glyf` instructions use `cvt`.
- **GID↔CID↔ToUnicode:** GID-preserving keeps `/CIDToGIDMap /Identity`, `/W` and ToUnicode unchanged, which is the lowest-risk design. Renumbering requires a CIDToGIDMap stream, or rewriting the CIDs in every page stream, `/W` and ToUnicode, for a gain of at most ~17 KB.
- **Font name rules (OSR-13):** ISO 32000-1 9.6.4 requires a subset's BaseFont/FontName to carry a six-uppercase-letter tag plus `+` (e.g. `ABCDEF+DejaVuSans`). The safe-name escaping must accept the tag, and the tag must be deterministic so that output hashes stay reproducible.
- **Malformed TTF:** today's OSR-24 range checks cover the tables the loader reads; a subsetter also reads every kept `glyf` entry and composite record. The harness rejects truncated directories, `loca` and `glyf`, and out-of-range `loca`. A product version needs the same `FONT` failure before any callback bytes, plus sanitizer and fuzz coverage.

## Why no-go, and when to reopen

The user goal in OSR-8/OSR-10 is large XML reports. On the agreed 16 MiB target, Flate already removed 85.6% of the PDF (86,884,343 → 12,501,989 bytes). The font is 3.06% of what remains, and the most aggressive subset measured here saves 2.92%; the repertoire subset that keeps the streaming order saves 2.36%. That small, constant saving does not justify a new TTF writer, a larger malformed-input surface, `fsType`/license policy, or a moved font object with late failures after callback bytes have been accepted. The external `--font` route already gives the same bytes to anyone who needs them.

Reopen as a separate implementation card only if a real consumer needs short or many-small-report output, where the font is 32–92% of the PDF. That card should be scoped to a **load-time, GID-preserving repertoire subset**. It keeps the font first and CID = GID, with no output buffer (two-pass stream to deflate). Its testable contract would include:

- exact expected sizes for these fixtures;
- identical `pdftotext` output **and** identical page renders against the full font, with the `.notdef`-only negative control;
- checksum validation;
- a deterministic subset tag;
- `fsType` refusal or full-embed fallback;
- `FONT` failures before output on malformed composite/`loca` input;
- RSS within the OSR-23 gates;
- ABI 0.2 unchanged.

This spike does not deliver that card.

## Files

- `measure.json`: raw per-PDF table sizes, compressed table sizes and all variants.
- `poppler-comparison.txt`: text and render hashes for the originals, splices, the negative control and the external-font outputs.
- `SHA256SUMS`: fixtures, PDFs, the subset font, the source font and the binary. Large files stayed in project-disk scratch outside the repository.
- `fixture-*.json`: recipe metadata. `predictions.txt`: values expected before measuring.
