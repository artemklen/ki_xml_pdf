# Data models

Every struct in the pipeline has a fixed size, and most carry their data inline. This page lists the public types from `include/ki_xml_pdf.h` and the internal ones from `src/` and `examples/`, with their fields. The 0.x ABI is unstable until 1.0: a 0.y bump may change `KiOptions`, so `size` is checked on every call.

## Public types

### `KiOptions` (`include/ki_xml_pdf.h`)

The per-call configuration. Call `ki_options_init` before filling it; `size` must stay `sizeof(KiOptions)` or the call fails with a usage error.

| Field | Type | Role |
| --- | --- | --- |
| `size` | `size_t` | Struct size check, set by `ki_options_init` |
| `font_path` | `const char *` | Explicit font; no fallback if it fails |
| `temp_dir` | `const char *` | Spool temp directory; NULL means `/tmp` |
| `max_pages` | `int` | Page quota |
| `max_output_bytes` | `long` | PDF byte quota |
| `max_spool_bytes` | `long` | Spool quota; 0 selects the 64 MiB default |
| `profile` | `int` | `KI_PROFILE_RECORDS` or `KI_PROFILE_REPORT` |
| `record_offset`, `record_limit` | `long` | report-v1 selection |
| `cancelled`, `cancel_user` | callbacks | Cancellation poll |
| `map`, `map_user` | callbacks | Custom mapper, replaces built-in profiles |
| `log`, `log_user` | callbacks | Diagnostic sink |
| `read_bytes`, `write_bytes`, `io_user` | callbacks | Stream I/O; caller keeps ownership |

Semantics of each field are on [Configuration](configuration.md).

### `KiResult` (`include/ki_xml_pdf.h`)

Reset at the start of every public call, including early usage errors, so a reused result never carries stale counters.

| Field | Type | Role |
| --- | --- | --- |
| `status` | `int` | `XML2PDF_OK` or one of the four error codes; matches the CLI exit code |
| `cause` | `int` | `KI_CAUSE_*`, finer than the status |
| `message` | `char[160]` | Last diagnostic, the same string the log callback receives |
| `pages` | `int` | Page count |
| `objects` | `int` | PDF object count |
| `offset_bytes` | `long` | Spool bytes used (same value as `spool_bytes`) |
| `output_bytes` | `long` | PDF bytes written |
| `spool_bytes` | `long` | Spool bytes used |
| `report_total`, `report_selected`, `report_skipped` | `unsigned long long` | report-v1 counters; zero for other profiles and mappers |

### `KiEvent` (`include/ki_xml_pdf.h`)

The event a mapper receives. All pointers are borrowed for the duration of the callback only; the data lives in the parser's `XmlEvent` until the next event.

| Field | Type | Role |
| --- | --- | --- |
| `kind` | `int` | `KI_EV_START`, `KI_EV_END` or `KI_EV_TEXT` |
| `name` | `const char *` | Element name |
| `nattr` | `int` | Attribute count, at most `KI_EVENT_ATTRS` (8) |
| `attr_name`, `attr_val` | `const char *[8]` | Attribute names and decoded values |
| `text`, `text_len` | `const char *`, `size_t` | Text fragment, at most 1023 bytes |
| `from_cdata` | `int` | Set when the fragment came from a CDATA section |
| `byte_offset` | `size_t` | Parser position after this event's bytes; a synthetic end of an empty element reuses the start's position, EOF is end of input |

### `KiDoc` (`src/convert.c`, opaque in `include/ki_xml_pdf.h`)

The handle a mapper writes through: `Layout *lay`, `char error[160]`, `int cause`. `ki_doc_text` and `ki_doc_break` push text; `ki_doc_fail` records the rule text and cause that `convert_streams` later surfaces.

## Internal types

### `XmlEvent` (`src/xml_parse.h`)

Unlike `KiEvent`, the storage is inline in the event: `kind`, `name[64]`, `nattr`, `attr_name[8][64]`, `attr_val[8][256]`, `text[1024]`, `text_len`, `from_cdata`, `byte_offset`, `error[160]`.

### `XmlParser` (`src/xml_parse.h`)

The pull parser state: `read` callback plus `user`, the 4096-byte `buf` window (`len`, `pos`, `eof`), `depth` with the 64 × 64-byte open-element `stack` and `end_name`, scanner flags (`pending_end`, `failed`, `done`, `bom_checked`, `in_cdata`, `cdata_mark`, `seen_root`, `document_start`, `bracket_run`), UTF-8 decoding state (`utf8_need`, `utf8_bad`, `utf8_lead`), the running byte `offset`, and `errmsg[160]`.

### `Layout` (`src/layout.h`)

The page under construction: the owning `PdfWriter *pdf`, the current `line[1024]` with `len` and glyph-width `units`, the finished `page[50][1024]` with per-line lengths `plen[50]` and `nlines`, a 4-byte UTF-8 continuation `hold` buffer, and `bad_cp` for the rejected code point.

### `FontFace` (`src/font.h`)

The loaded TrueType face: the font file bytes `ttf` and `ttf_len` (the only heap allocation in the conversion path), metrics `units`, `ascent`, `descent`, `bbox[4]`, glyph and advance tables `gid`/`adv` for code points below `FONT_BMP_MAX` (0x500), and `punct_gid`/`punct_adv` for 24 punctuation slots at `FONT_PUNCT_BASE` (0x2010).

### `PdfConfig` (`src/pdf_write.h`)

Writer configuration filled from `KiOptions` by `convert_streams`: `font_path`, `temp_dir`, `max_pages`, `max_output`, `max_spool`, the `debug_fail_page` test hook, and the `cancelled`/`cancel_user` pair.

### `PdfWriter` (`src/pdf_write.h`)

The append-only writer: buffered output `fp` over the raw stream `raw`, the spool `offsets` file and its `off_path`, the embedded `FontFace`, counters `nobj`/`npages`/`written`/`spool_bytes`/`live_page_bytes`, quotas `max_pages`/`max_output`/`max_spool`, failure detail `hit_limit`/`fail_kind`/`fail_errno`, the `debug_fail_page`/`debug_pages` test hooks, and the cancellation pair.

### `Report` (`src/report.h`)

The report-v1 mapper state, fixed regardless of record count: flags `in_report`, `in_record`, `in_field`, `saw_field`, `skipping`, `value_started`; counters `index`, `total`, `selected`; selection `offset`, `limit`; the current byte position `at`; and the 256-byte `pending_name` for the field being collected.

### `CatalogMap` (`examples/catalog_map.h`)

The example mapper from the mapper guide: `state` (a grammar enum), `value_started`, and an `items` counter. One enum, one flag and one counter, no field or item storage; `catalog_map_init` prepares it and `catalog_map_event` is the `KiOptions.map` callback.

### Small driver types (`src/convert.c`)

`MapState` (a 64-entry `xml:space` preserve stack behind records-v1), `ReadState` (the parser's read adapter: input stream, options, byte count, I/O and cancellation flags) and `ByteSink` (the `fopencookie` adapter that turns `write_bytes` into a `FILE *` for `ki_convert`).

### `PageSink` (`src/page_sink.h`)

The older fixed byte-grid page buffer (`buf`, `cap`, `len`, `col`, `lines`, `max_cols`, `max_lines`, `pages_flushed`, `flush`, `user`). It is compiled into the library and unit-tested (`test_page_sink_flushes`), but the conversion path no longer uses it.

## Ownership rules

- The caller owns callback resources, input streams, option strings and result storage. The library closes only what it created: the temp PDF, the spool file, and its internal `FILE *` handles.
- `KiEvent` pointers die with the callback. Copy anything a mapper needs later.
- The font bytes are heap-allocated by `font_load` and released by `font_free` inside `pdf_free`; everything else in the pipeline is stack or inline storage.
- A context (options plus result) is not safe for concurrent use; two independent contexts may convert at once, which `tests/test_stream.c` exercises with pthreads.

## Related pages

- [C API](../api/c-api.md) for the functions that move these structs.
- [Conversion pipeline](../systems/conversion-pipeline.md) for how `convert_streams` connects them.
- [Configuration](configuration.md) for what the option fields do.
- [Custom mappers](../features/custom-mappers.md) for writing a mapper against `KiEvent` and `KiDoc`.
