# OSR-13 font names and zero-advance glyphs

Source: commit `ecfe429` plus the evidence commit that adds this directory.
Bundled font `fonts/DejaVuSans.ttf` SHA-256
`57f73e11f51999432bf7ab22ce55b6f945d5eca1bf824404cfa9ec2e3718c84e`; second
permitted font Debian `fonts-dejavu-core` `DejaVuSans-Bold.ttf` SHA-256
`a4c5bc453ca281d90ea079e596da7ae0dfeb5777497c29ec254e76d97ff6f890` (used only
when installed; not shipped).

## Policy

- `BaseFont` (Type0 and CIDFontType2) and `FontName` use name ID 6. Preference:
  Windows Unicode en-US (3,1,0x409), then Macintosh Roman (1,0), then other
  Windows (3,0|1). A candidate qualifies only if it is 1–63 printable ASCII
  characters (UTF-16 records: even length, every unit below 0x80). The writer
  escapes space, `#`, and `()<>[]{}/%` as `#XX`.
- No qualifying record, or no `name` table: `KiFont-XXXXXXXX`, FNV-1a 32 of the
  whole file. Equal bytes give the same name; it never borrows DejaVu's name.
- `name` header, record array, storage offset or any record string outside the
  table, `maxp` shorter than 6 bytes or numGlyphs 0: font rejected,
  `KI_CAUSE_FONT`. An explicit font never falls back to the bundled one.
- A code point is present when cmap gives GID != 0 and GID < maxp numGlyphs
  (maxp optional). Its advance may be 0; `/W` then lists `gid [0]`. Each GID
  maps to one code point in ToUnicode (lowest wins).
- A layout line holds at most 1023 UTF-8 bytes. Zero-advance glyphs never
  trigger a wrap, so a mark stays with its base; a run that exceeds 1023 bytes
  fails with exit 4 / `KI_CAUSE_RESOURCE` ("layout: line over 1023 bytes").
  The page stream buffer is sized for 50 such lines.

No shaping, composition, normalization or repertoire change.

## Results

- `font-metrics.json`: output of `python3 tests/font_metrics_check.py`
  (independent cmap/hmtx/maxp/name reader, `pdffonts`, `pdftotext`, 300 dpi
  `pdftoppm` raster). Two fonts named `DejaVuSans` / `DejaVuSans-Bold`; GID and
  width truth for U+0301 (690), U+0306 (695), U+0483 (1048) with advance 0;
  extracted code points equal the input order; acute ink lies above and within
  the `e` columns; U+1E00/U+0500 exit 1; 511 marks after `a` (1023 bytes) exit 0
  with the line intact, 512 marks exit 4; 120 records with marks span 5 pages.
  The 0.2.0 binary fails this check (`BaseFont /DejaVuSans` for the Bold font).
- `test-san-gcc.txt`, `test-san-clang.txt`: `make CC=gcc|clang test-san` in
  expendable clones, including `font_names`, `font_zero_advance`,
  `font_mark_runs` (malformed name spans, fallback cases, maxp, synthetic
  missing GIDs, one-byte reads, TEXT/CDATA/reference equivalence, wrap,
  pages, 50 full zero-advance lines in one page). No sanitizer reports.
- `bash tests/verify.sh` in the same clone: ok, 16 MiB run RSS 2460 KiB.
