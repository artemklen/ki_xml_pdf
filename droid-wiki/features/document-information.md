# Document information
Active contributors: Artem

ki_xml_pdf 0.3.0 can add an ordinary PDF `/Info` dictionary containing Title, Producer and an explicit CreationDate. No dictionary or clock-derived value is added by default, and metadata does not change page text or layout.

## Use it

```sh
./xml2pdf --title "Отчёт за октябрь" --producer "billing export" \
  --creation-date 2026-10-16T12:34:56+03:00 input.xml titled.pdf
```

Each CLI option may appear once. In C, initialize a full `KiConfig`, set `title`, `producer` and/or `creation_date`, then call one of the `_cfg` conversion functions:

```c
KiConfig cfg;
if (ki_config_init(&cfg, sizeof cfg) != 0)
    return 2;
cfg.title = "October report";
cfg.producer = "billing export";
cfg.creation_date = "2026-10-16T12:34:56Z";
/* Pass &cfg to ki_convert_file_cfg, ki_convert_to_file_cfg or ki_convert_cfg. */
```

A NULL field is omitted; an empty string is invalid, not an omission. A NULL config, the original calls, and a geometry-only `KI_CONFIG_SIZE_GEOMETRY` config have no Info.

## Text and date contract

| Field | Accepted input | PDF representation |
| --- | --- | --- |
| `title` / `producer` | 1–1024 UTF-8 bytes, valid Unicode scalars; no C0/DEL/C1 controls except tab, LF and CR | UTF-16BE hexadecimal string with FEFF BOM; astral characters use surrogate pairs |
| `creation_date` | Exactly `YYYY-MM-DDTHH:MM:SSZ` or `YYYY-MM-DDTHH:MM:SS+HH:MM` / `-HH:MM` | `D:YYYYMMDDHHmmSSZ` or `D:YYYYMMDDHHmmSS+HH'mm'` / negative offset |

Dates use Gregorian month/leap-year rules, hours 00–23, minutes/seconds 00–59 and offset up to 23:59. Fractional seconds, leap seconds, lowercase `z`, missing timezone and a preformatted PDF date are rejected. The supplied wall time and offset are preserved, not converted to UTC (`src/info.c`).

Metadata has a broader Unicode range than body text and does not need corresponding font glyphs. Parentheses, backslashes, line breaks and PDF-looking syntax remain data because strings are hex-encoded, not inserted into PDF syntax.

The library adds no ModDate, XMP, trailer ID, PDF/A or PDF/UA claim. This feature is unrelated to the visible `report` element's `title` attribute.

## Validation and ownership

`reject_config` in `src/convert.c` calls `info_set` before opening input/font/output files or running read/write/map callbacks. Validation failures are status 2 / `KI_CAUSE_USAGE`, even when another path/font is invalid. Optional log diagnostics may be invoked.

`PdfInfo` in `src/info.h` copies validated strings into fixed 1025-byte buffers and stores a bounded formatted date at call entry. Callback code cannot change metadata by later mutating the original strings. There is no metadata-sized heap allocation.

At `pdf_end`, `src/pdf_write.c` adds one object after Pages/Catalog and references it from the trailer. Pages and body streams are unchanged. The extra object, xref entry and trailer bytes count against output/spool quotas; a quota failure preserves an atomic file destination.

## ABI and modification points

The Info pointers extend the separate `KiConfig`, not the established 0.2 options/result/event structs. Both the full size and the geometry-only prefix size remain recognized; the short form must never be read or written past its declared end.

The module is one implementation unit with one dedicated C test file (`tests/test_info.c`, 1:1). Change validation/serialization in `src/info.c`, config copying in `src/convert.c`, object/trailer wiring in `src/pdf_write.c`, and CLI duplicate/syntax handling in `src/main.c`.

Tests check exact encodings, syntax-like text, broad Unicode, size boundaries, invalid input before I/O, unchanged bodies and exact output quotas. `tests/info_check.py` independently reads xref/Info, decodes values and compares Poppler output. `tests/integration_check.py` combines Info with geometry, wrapping, Flate and all stream/file routes.

## Key source files

| File | Role |
| --- | --- |
| `include/ki_xml_pdf.h` | Optional config fields, byte limit and size compatibility |
| `src/info.c`, `src/info.h` | Bounded validation, copied values and safe serialization |
| `src/convert.c` | Early validation and call-owned config |
| `src/pdf_write.c` | Final Info object, trailer reference and quota accounting |
| `src/main.c` | Title/producer/date options |
| `tests/test_info.c`, `tests/info_check.py` | Internal and independent metadata checks |

Related: [Page geometry](page-geometry.md), [PDF writer](../systems/pdf-writer.md), [C API](../api/c-api.md), [Configuration](../reference/configuration.md).
