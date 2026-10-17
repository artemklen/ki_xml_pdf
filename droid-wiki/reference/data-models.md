# Data models
Active contributors: Artem

There is no database or retained document tree. Public structures describe one call; private structures hold bounded parser, mapper, font, page and writer state.

## ABI and size checks

`include/ki_xml_pdf.h` defines public types with C/C++ linkage support.
Version 0.3 retains the 0.2 `KiOptions`, `KiResult` and `KiEvent` layouts.
Do not assume the same ABI across all future pre-1.0 minor versions.

`KiOptions.size` must equal `sizeof(KiOptions)`.
`KiConfig.size` accepts the full current size or `KI_CONFIG_SIZE_GEOMETRY`, the prefix ending before `title`.
Use the initializers and symbolic sizes, not hardcoded byte counts.
No public addon-font or table structures exist.

## Public call state

| Type | Fields / role | Lifetime |
| --- | --- | --- |
| `KiOptions` | Size, font/temp paths, quotas, profile, record offset/limit, cancellation/map/log/I/O callbacks and user pointers | Caller-owned for the synchronous call |
| `KiConfig` | Size, page-size/orientation enums, custom dimensions, four margins, font size, optional Title/Producer/CreationDate pointers | Caller-owned; geometry validated and metadata copied at call start |
| `KiResult` | Status/cause/message, page/object/output/spool counts, report counters | Caller storage, reset by every conversion when non-NULL |
| `KiEvent` | Kind, name, attribute pointers, text fragment, CDATA flag and consumed byte offset | Borrowed for one map callback |
| `KiDoc` | Opaque text/break/failure handle | Only used during mapping; implementation remains private |

See [Configuration](configuration.md) for option defaults and limits.

## Result fields

| Fields | Meaning |
| --- | --- |
| `status`, `cause` | Coarse return/exit status plus finer `KI_CAUSE_*` |
| `message[160]` | Human-readable diagnostic or success summary |
| `pages`, `objects` | Counts from conversion, not proof of publication on failure |
| `output_bytes` | Bytes written; callback route reports exactly bytes accepted by the caller |
| `offset_bytes`, `spool_bytes` | Offset spool usage; both report the same spool measure |
| `report_total`, `report_selected`, `report_skipped` | `unsigned long long` counters, filled only for built-in report-v1 |

All counters are per call.
A reused result does not retain previous success counts on an early usage error.
A custom mapper owns its own domain counters; the built-in report counters remain zero.

## Mapper events

`KiEvent.kind` is START=1, END=2 or TEXT=3.
`nattr` is at most eight; `attr_name[8]` and `attr_val[8]` point into temporary event storage.
TEXT uses `text` plus `text_len`; a long value arrives in multiple fragments.
`from_cdata` distinguishes literal CDATA text.

`byte_offset` is raw consumed input bytes, including a BOM.
An empty element's synthetic END uses its START position.
There are no public line/column members.
Those positions live in the parser and opaque document handle and appear in XML/profile diagnostics.

`KiDoc` in `src/convert.c` holds a layout pointer, error/cause and current line/column/byte.
`ki_doc_fail` uses that event position when formatting its profile failure.
A mapper must not retain `KiEvent` pointers or the `KiDoc` handle after the callback.

## Parser and mapper state

| Private type | Source | Bounded contents |
| --- | --- | --- |
| `XmlParser` | `src/xml_parse.h` | Read callback, 4096-byte window, 64×64-byte open-name stack, UTF-8/declaration/CDATA scanner flags and consumed counters |
| `XmlEvent` | `src/xml_parse.h` | Inline name[64], attribute names[8][64], values[8][256], text[1024], scalar line/column and byte position |
| `MapState` | `src/convert.c` | Depth and 64-entry inherited records `xml:space` preserve stack |
| `Report` | `src/report.h` | Grammar/selection flags, index/total/selected counters, offset/limit and pending_name[256] |
| `CatalogMap` | `examples/catalog_map.h` | Grammar state, value-started flag and item counter |

No field value is accumulated in the report mapper.
Skipped records retain validation state without retained record objects.
The custom catalog example similarly streams values rather than buffering an item.

## Geometry, font and page state

| Private type | Source | Role |
| --- | --- | --- |
| `Geometry` | `src/geometry.h` | Width/height/margins/font/leading as integer thousandths of a point |
| `PageFrame` | `src/geometry.h` | Fitted geometry, font units, first baseline and maximum line count |
| `InkRun` | `src/geometry.h` | Advances, actual PDF-width pen, last non-space box and ink bounds |
| `Layout` | `src/layout.h` | Current/pending word in one line[1024], base/mark split state, page[50][1024], lengths, UTF-8 hold[4] and error flags |
| `FontFace` | `src/font.h` | Open file, owned path, size/hash, fixed GID/advance/ink tables, vertical metrics and safe PostScript-name data |
| `PdfInfo` | `src/info.h` | Presence flags, copied title/producer[1025] and normalized PDF date[32] |

`FontFace` has no retained full-TTF buffer.
`src/font.c` frees temporary table spans after deriving metrics.
`font_free` closes its own open file and releases the owned path.
Ink tables account for negative bearings and vertical extremes, not merely glyph advances.

## Writer and adapters

`PdfConfig` in `src/pdf_write.h` carries font/temp paths, quotas, cancellation, internal debug setting and geometry/Info references.
`PdfWriter` owns the counted output wrapper, spool/path, `FontFace`, fitted frame and byte/object/page/failure counters.
It also owns one reusable zlib state/window and one lazily allocated reusable page-content buffer.
It retains no array of all pages or object offsets in RAM.

ToUnicode uses fixed automatic scratch arrays in a count-then-write pass, not a retained CMap allocation.
Layout passes page lines directly to the writer, avoiding an extra joined-page copy.
Optional `PdfInfo` lives in the entry point's validated `Setup` until the writer completes.

`ReadState` in `src/convert.c` adapts owned file input or caller reads and latches I/O/cancellation state.
`ByteSink` wraps caller output, records accepted bytes and latches the first failed write.
Closing the cookie stream does not close/free caller resources.

## Ownership and concurrency

The library closes files it opened and frees internal allocations.
Callback resources, option/config storage and user state remain caller-owned.
Metadata is copied early; other pointers should remain valid throughout the call.
Independent contexts may run concurrently; sharing mutable result/mapper/I/O state is not safe.
Internal global failure hooks are test-only exceptions, not ordinary public context state.

## Key source files

| File | Role |
| --- | --- |
| `include/ki_xml_pdf.h` | Public layouts and ownership contract |
| `src/convert.c` | Opaque document, validated setup and adapters |
| `src/xml_parse.h`, `src/report.h`, `src/layout.h` | Parser/mapper/page storage |
| `src/font.h`, `src/geometry.h`, `src/info.h`, `src/pdf_write.h` | Font, geometry, metadata and writer storage |

Related: [C API](../api/c-api.md), [Systems](../systems/index.md), [Conversion pipeline](../systems/conversion-pipeline.md).
