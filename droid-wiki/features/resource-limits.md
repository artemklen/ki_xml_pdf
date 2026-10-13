# Resource limits

Active contributors: Artem Klen

Every stage of the pipeline has a fixed memory footprint, and the run has three optional quotas plus an absolute object cap. The acceptance gate requires the converter process to stay at or below 64 MiB of resident memory on the synthetic workloads. This page lists the bounds, where each quota is enforced, and how the RSS gate is measured.

## Purpose

The library is built to convert very large XML with bounded memory. Nothing in the pipeline grows with the number of records: the parser pulls through a fixed window, the mappers keep fixed state, the layout holds one page, and the PDF writer stores object offsets in a temporary file instead of in RAM. Quotas let a caller stop a run that would exceed a page, output-byte or spool-byte budget.

## Key abstractions

| Abstraction | Path | Role |
| --- | --- | --- |
| Quota fields | `include/ki_xml_pdf.h` | `max_pages`, `max_output_bytes`, `max_spool_bytes`, `temp_dir` |
| `reject_options` | `src/convert.c` | Rejects negative quotas and unknown profiles |
| `PdfConfig`, `PdfWriter` | `src/pdf_write.h` | Writer limits and failure state |
| `note_offset` | `src/pdf_write.c` | Object cap and spool-byte quota |
| `cap_write` | `src/pdf_write.c` | Output-byte quota |
| `pdf_add_page` | `src/pdf_write.c` | Page quota |
| `pdf_end` | `src/pdf_write.c` | Cancellation checks while finishing |
| Parser bounds | `src/xml_parse.h` | Depth, attribute, name, text and window sizes |
| Font and layout bounds | `src/font.h`, `src/layout.h` | Repertoire tables and line storage |
| `tests/verify.sh` | | RSS gate on a 16 MiB input |
| `tests/large_proof.py` | | 64 MiB RSS, page, PDF and spool budgets |

## How it works

### Fixed bounds

| Bound | Value | Defined in |
| --- | --- | --- |
| XML nesting depth | 64 | `src/xml_parse.h` (`XML_MAX_DEPTH`) |
| Attributes per element | 8 | `src/xml_parse.h` (`XML_ATTR_MAX`) |
| Element name | 63 bytes | `src/xml_parse.h` (`XML_NAME_MAX`) |
| Attribute value | 255 bytes | `src/xml_parse.h` (`XML_ATTR_VAL_MAX`) |
| Text fragment | 1023 bytes | `src/xml_parse.h` (`XML_TEXT_MAX`) |
| Input window | 4096 bytes | `src/xml_parse.h` (`XML_BUF_SIZE`) |
| Font repertoire table | U+0000 to U+04FF plus 24 punctuation slots from U+2010 | `src/font.h` (`FONT_BMP_MAX`, `FONT_PUNCT_COUNT`) |
| Layout line | 1024 bytes, 50 lines | `src/layout.h` (`LAYOUT_LINE`, `LAYOUT_LINES`) |
| Objects in one PDF | 8,000,000 | `src/pdf_write.c` (`MAX_OBJECTS`) |
| Lines per page in the writer | 50 | `src/pdf_write.c` (`PAGE_MAX_LINES`) |

`MAX_OBJECTS` is absolute: `note_offset` in `src/pdf_write.c` sets `hit_limit` and `fail_kind = 1` for any object number at or above it, whatever the quotas say. Since each page uses two object numbers plus a fixed font block, this is roughly four million pages.

The `XML2PDF_PAGE_COLS` (90), `XML2PDF_PAGE_LINES` (48) and `XML2PDF_PAGE_CAP` constants in `src/convert.h` describe the older page sink grid and are used only by tests; the conversion path sizes pages from the font metrics in `src/layout.c`.

### Quotas

| Option | Field | Enforced in | 0 means | Failure |
| --- | --- | --- | --- | --- |
| `--max-pages N` | `max_pages` | `pdf_add_page` in `src/pdf_write.c` | no separate page quota | `hit_limit`, `fail_kind = 1` |
| `--max-output-bytes N` | `max_output_bytes` | `cap_write` in `src/pdf_write.c` | no output-byte quota | `hit_limit`, `fail_kind = 4`, `errno = EFBIG` |
| `--max-spool-bytes N` | `max_spool_bytes` | `note_offset` in `src/pdf_write.c` | 64 MiB | `hit_limit`, `fail_kind = 5` |
| `--temp-dir DIR` | `temp_dir` | `pdf_begin_cfg` in `src/pdf_write.c` | `/tmp` | I/O failure or `ENAMETOOLONG` |

The page quota is checked before a page is written. The output-byte quota wraps the raw destination stream: `cap_write` fails when the bytes already written exceed the quota or the next chunk would cross it, so a run stops at the first byte over the limit. The spool quota is checked per object in `note_offset`, which computes the offset-file size as `(num + 1) * 8` bytes before writing. `pdf_begin_cfg` replaces a zero or unset spool limit with 64 MiB, so the spool is always bounded even when the caller sets nothing. The offset file is created as `ki-xml-pdf-off-XXXXXX` in `temp_dir`; a path that does not fit is reported as `temp path too long`.

All three quota failures come back through `fail_from_pdf` in `src/convert.c` as status 4 with `KI_CAUSE_RESOURCE` and the message `resource limit`. Quota exhaustion and cancellation share status 4; the `cause` and message tell them apart. The CLI parses `--max-pages` and the other numeric options with `take_long` in `src/main.c`, which rejects negative and non-numeric values with status 2, and rejects `--max-pages` above `INT_MAX` before the conversion starts.

`reject_options` in `src/convert.c` also refuses a negative `max_pages`, `max_output_bytes`, `max_spool_bytes`, `record_offset` or `record_limit`, an unknown `profile`, and a `size` that does not match `sizeof(KiOptions)`.

### Cancellation

Cancellation is not a quota but it stops a run the same way. The `cancelled` callback in `KiOptions` is checked:

- in `conv_read` in `src/convert.c`, before and after each read;
- at the top of every driver loop iteration in `convert_streams` in `src/convert.c`;
- after `layout_finish`, after the empty-document check, and between the publish steps (`fflush`, `fsync`, `fclose`, before `rename`);
- in `pdf_end` in `src/pdf_write.c`, at the start, every 64 pages while writing the page tree, and every 64 objects while writing the xref.

A cancelled run returns status 4 with `KI_CAUSE_CANCEL`. After a successful `rename` the file is already the destination, so cancellation cannot roll it back.

### RSS gate

The memory gate measures the converter process alone. `tests/verify.sh` runs `/usr/bin/time -v` on `./xml2pdf` for a generated input of at least 16 MiB and a small fixture, and reads the `Maximum resident set size (kbytes)` line from each report. The XML generator and `pdftotext` are outside the measurement. The gate passes when the large run's RSS is below half the large input size and at or below the small run's RSS plus 4 MiB, and when the output PDF is larger than the converter's RSS.

`tests/large_proof.py` measures with `/usr/bin/time -f "rss_kib=%M elapsed=%e"` and applies fixed budgets: RSS at most 65536 KiB (64 MiB), at most 10000 pages, at most 256 MiB of PDF and at most 64 MiB of offset spool. It then reads selected pages with `pdftotext` under its own `ulimit -v` of 1 GiB and compares the text against a generator oracle. `--self-test` checks the gate logic without a large file. The documented historical measurement on revision `7c62fe4` is 2372 KiB of RSS for a 1 GiB `report-v1` input; see `docs/contract.md`.

The library logs `xml2pdf: pages=N objects=N offset_bytes=N` on a successful publish, and `tests/large_proof.py` parses `offset_bytes=` from stderr as the spool size.

## Integration points

- Quota and cancellation failures are mapped in [Conversion pipeline](../systems/conversion-pipeline.md).
- The offset spool file and its lifecycle are described with [Atomic output](../features/atomic-output.md).
- The parser bounds belong to [XML parser](../systems/xml-parser.md); the repertoire and page grid to [Layout and fonts](../systems/layout-and-fonts.md); the object cap and spool to [PDF writer](../systems/pdf-writer.md).
- The CLI options are listed on [CLI](../features/cli.md), and the test gates on [Testing](../how-to-contribute/testing.md).

## Entry points for modification

- Change a parser bound in `src/xml_parse.h`.
- Change the object cap or a quota check in `note_offset`, `cap_write` and `pdf_add_page` in `src/pdf_write.c`.
- Change the default spool size or the offset temp path in `pdf_begin_cfg` in `src/pdf_write.c`.
- Change option validation in `reject_options` in `src/convert.c`.
- Change the RSS or budget gates in `tests/verify.sh` and `tests/large_proof.py`.

## Key source files

| File | Role |
| --- | --- |
| `include/ki_xml_pdf.h` | Quota and cancellation fields |
| `src/convert.c` | Option validation, cancellation checkpoints, cause mapping |
| `src/pdf_write.c`, `src/pdf_write.h` | Object cap, quotas, spool file, cancellation in `pdf_end` |
| `src/xml_parse.h` | Parser bounds |
| `src/font.h`, `src/layout.h` | Repertoire and layout bounds |
| `tests/verify.sh`, `tests/large_proof.py` | RSS and budget gates |

## Related pages

- [Conversion pipeline](../systems/conversion-pipeline.md), [XML parser](../systems/xml-parser.md), [Layout and fonts](../systems/layout-and-fonts.md), [PDF writer](../systems/pdf-writer.md).
- [Atomic output](../features/atomic-output.md), [Streaming I/O](../features/streaming-io.md), [CLI](../features/cli.md).
- [Testing](../how-to-contribute/testing.md), [Glossary](../overview/glossary.md), [Security](../security.md).
