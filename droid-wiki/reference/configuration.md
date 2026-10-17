# Configuration
Active contributors: Artem

`KiOptions` controls conversion and I/O; additive `KiConfig` controls geometry and optional document information. The CLI maps flags to both structures.

## Options and defaults

Call `ki_options_init` from `include/ki_xml_pdf.h`; it zeroes fields and sets the required size.
`src/convert.c` rejects size mismatch, negative quotas/selection and unknown profile values.

| Field | Default | Meaning / CLI |
| --- | --- | --- |
| `size` | `sizeof(KiOptions)` | Exact required layout size |
| `font_path` | NULL | Nonempty explicit font, no fallback; `--font PATH` |
| `temp_dir` | NULL | Offset spool directory; NULL/empty means `/tmp`; `--temp-dir DIR` |
| `max_pages` | 0 | No separate page quota; `--max-pages N` |
| `max_output_bytes` | 0 | No separate output quota, but xref ceiling remains; `--max-output-bytes N` |
| `max_spool_bytes` | 0 | 64 MiB default, not unlimited; `--max-spool-bytes N` |
| `profile` | `KI_PROFILE_RECORDS` (0) | `KI_PROFILE_REPORT` is 1; `--profile records-v1|report-v1` |
| `record_offset` | 0 | Initial report records not rendered; `--record-offset N` |
| `record_limit` | 0 | All remaining report records; positive selects at most N; `--record-limit N` |
| `cancelled`, `cancel_user` | NULL | Nonzero cancellation poll; caller-owned context |
| `map`, `map_user` | NULL | Custom mapper replaces either built-in profile |
| `log`, `log_user` | NULL | Diagnostic sink; no library terminal output by default |
| `read_bytes`, `write_bytes`, `io_user` | NULL | Callback I/O and caller-owned context |

Selection does not bypass input/profile validation.
File-to-file ignores both I/O callbacks; callback-to-file ignores the write callback.
See [C API](../api/c-api.md) for exact read/write return rules.

## Config sizes and geometry

Call `ki_config_init(&cfg, sizeof cfg)` and check its return.
Known sizes are `sizeof(KiConfig)` and `KI_CONFIG_SIZE_GEOMETRY = offsetof(KiConfig, title)`.
The prefix form has geometry but no `/Info`.
A NULL config preserves the older conversion functions' defaults.
Do not copy a numeric struct size from another architecture/header.

| Field | Default | Accepted values / CLI |
| --- | --- | --- |
| `size` | Initializer's size argument | Exactly a known size |
| `page_size` | `KI_PAGE_LETTER` (0) | `KI_PAGE_A4` (1), `KI_PAGE_CUSTOM` (2); `--page-size letter|a4` |
| `orientation` | `KI_ORIENT_PORTRAIT` (0) | Landscape=1 for named sizes only; `--orientation portrait|landscape` |
| `page_width`, `page_height` | 0 | Named: both zero; custom: each 3..14400 pt; `--page-width`, `--page-height` |
| `margin_top/right/bottom/left` | 40 pt each | 0..14400 pt; `--margin PT` or `--margin T,R,B,L` |
| `font_size` | 10 pt | 1..144 pt; `--font-size PT` |

Points are 1/72 inch.
Letter is 612 × 792 pt; A4 is 595.276 × 841.89 pt.
Named landscape swaps sides; custom dimensions remain as given and require portrait.
All numeric values must be finite.
Values round to the nearest 0.001 pt; leading is 1.4 times the rounded font size, rounded down to 0.001 pt.
Opposing margin sums must be strictly smaller than the rounded page dimensions.

After loading the font, the frame must fit a full line vertically and every single supported glyph horizontally.
Layout and PDF MediaBox/placement use the same rounded geometry and ink metrics.
A page holds however many complete lines fit, never more than 50.
Invalid config is usage status 2; font fitting still occurs before input/output/map callbacks.

## Optional `/Info`

These fields exist only in a full-size `KiConfig`.

| Field / CLI | Default | Validation / serialization |
| --- | --- | --- |
| `title` / `--title TEXT` | NULL, omitted | 1..1024 UTF-8 bytes; UTF-16BE PDF hex string |
| `producer` / `--producer TEXT` | NULL, omitted | Same text rules |
| `creation_date` / `--creation-date DATE` | NULL, omitted | Exact explicit date/time syntax, converted to a PDF date string |

Text accepts Unicode scalars except C0/C1 controls and DEL, with tab/LF/CR allowed.
Malformed UTF-8, surrogates and empty strings fail.
Metadata Unicode does not require body-font glyphs.
Values are copied at call start; `/Info` output counts against the normal output-byte quota.

CreationDate is exactly `YYYY-MM-DDTHH:MM:SSZ` or `YYYY-MM-DDTHH:MM:SS+HH:MM` / `-HH:MM`.
Use a valid Gregorian month/day/time, seconds 00..59 and offset up to 23:59.
The offset is retained rather than normalized.
No implicit clock/date, ModDate, XMP, trailer ID or PDF/A claim is added.

## CLI parsing details

`src/main.c` accepts two paths after options; `-` selects stdin/stdout and `--` ends options.
`-h`/`--help` and `--version` return 0; conversion diagnostics go to stderr.
General quota/selection numbers use `strtol` with full consumption, range and nonnegative checks; max pages must also fit `int`.

Geometry numbers are more restrictive: plain decimal digits with an optional nonempty fractional part, maximum 32 characters.
Signs, exponents, hex, whitespace, NaN and infinity are not CLI point syntax.
Each geometry or metadata option may appear only once.
Custom width/height must occur together and exclude explicit page-size/orientation flags.
See [CLI](../features/cli.md).

## Hard bounds

| Constant / bound | Value | Source |
| --- | --- | --- |
| `KI_XML_PDF_VERSION` | `"0.3.0"` | `include/ki_xml_pdf.h` |
| `KI_EVENT_ATTRS`, `KI_INFO_TEXT_MAX` | 8 attributes; 1024 metadata bytes | `include/ki_xml_pdf.h` |
| `XML_BUF_SIZE`, `XML_MAX_DEPTH` | 4096-byte window; 64 levels | `src/xml_parse.h` |
| `XML_NAME_MAX`, `XML_ATTR_VAL_MAX` | 64/256 storage bytes, 63/255 usable | `src/xml_parse.h` |
| `XML_TEXT_MAX`, `PDF_LINE_BYTES` | 1024 storage bytes, 1023 usable | `src/xml_parse.h`, `src/pdf_write.h` |
| `GEOM_MAX_LINES` | 50 | `src/geometry.h` |
| `FONT_BMP_MAX` | 0x500 fixed slots | `src/font.h` |
| `FONT_PUNCT_BASE`, `FONT_PUNCT_COUNT` | U+2010, 24 slots through U+2027 | `src/font.h` |
| Font file cap | 8 MiB | `src/font.c` |
| `MAX_OBJECTS` | 8,000,000 | `src/pdf_write.c` |
| `PDF_XREF_MAX_OFFSET` | 9,999,999,999 total-output ceiling | `src/pdf_write.h` |
| Default spool quota | 64 MiB | `src/pdf_write.c` |
| `FLATE_CHUNK` | 16,384 bytes | `src/pdf_write.c` |

The body repertoire is tab/LF/CR, U+0020–U+04FF and U+2010–U+2027, subject to actual font mappings.
These limits are independent of the synthetic 4096 KiB RSS acceptance gate.

## Environment and build settings

The library reads nonempty `KI_XML_PDF_FONT` only when no nonempty explicit path is supplied.
It does not silently replace a failing explicit/environment font.
The compiled default is `<prefix>/share/ki_xml_pdf/DejaVuSans.ttf`.

Make variables include `CC`, `CXX`, `CFLAGS`, `CXXFLAGS`, `PREFIX` (default `/usr/local`), `DESTDIR`, `PDFTOTEXT`, `DISTCHECK_ORACLE` and `DISTCHECK_BREAK`.
CMake settings include `BUILD_SHARED_LIBS` (ON), `BUILD_TESTING` (ON), install prefix/libdir and build type.
Installed consumers may set `ki_xml_pdf_USE_STATIC_LIBS`, `CMAKE_PREFIX_PATH`, `PKG_CONFIG_PATH` or loader paths.
Test `SCRATCH`/`PDFTOTEXT` settings are not library configuration.
`DESTDIR` never rewrites the compiled runtime font path.

## Key source files

| File | Role |
| --- | --- |
| `include/ki_xml_pdf.h`, `src/convert.c` | Public configuration and validation |
| `src/main.c` | Exact CLI syntax |
| `src/geometry.c`, `src/info.c` | Geometry rounding and metadata rules |
| `src/pdf_write.c`, `src/font.c` | Quotas, compression and font lookup |

Related: [Data models](data-models.md), [Dependencies](dependencies.md), [Resource limits](../features/resource-limits.md).
