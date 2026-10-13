# Configuration

The library is configured through `KiOptions`, the CLI through its flags, and both are bounded by compile-time constants. This page lists them all with the file each lives in.

## `KiOptions` fields

Defined in `include/ki_xml_pdf.h`. Call `ki_options_init` first: it zeroes the struct and sets `size`. Validation in `reject_options` (`src/convert.c`) rejects a `size` mismatch ("options size mismatch") and negative quotas, selection values or unknown profile numbers ("invalid option"), both as usage errors.

| Field | Type | Meaning |
| --- | --- | --- |
| `size` | `size_t` | Must stay `sizeof(KiOptions)` for the 0.y series; set by `ki_options_init` |
| `font_path` | `const char *` | Explicit font file; used directly with no fallback if it fails |
| `temp_dir` | `const char *` | Directory for the offset spool temp file; NULL or empty means `/tmp` |
| `max_pages` | `int` | Page quota; 0 adds no separate page limit |
| `max_output_bytes` | `long` | PDF byte quota; 0 disables it |
| `max_spool_bytes` | `long` | Offset-spool quota; 0 means the 64 MiB default |
| `profile` | `int` | `KI_PROFILE_RECORDS` (0) or `KI_PROFILE_REPORT` (1) |
| `record_offset` | `long` | report-v1: records to skip before selecting |
| `record_limit` | `long` | report-v1: records to select after the offset; 0 selects all |
| `cancelled`, `cancel_user` | callbacks | Cancellation poll; polled in the read path, event loop, PDF finish, and around flush/sync/close/rename |
| `map`, `map_user` | callbacks | Custom mapper; replaces both built-in profiles |
| `log`, `log_user` | callbacks | Diagnostics sink; without it the library prints nothing |
| `read_bytes`, `write_bytes`, `io_user` | callbacks | Callback I/O for `ki_convert` (both) and `ki_convert_to_file` (read only); ignored by `ki_convert_file` |

Read callback contract: return 0 only at EOF, a positive count for data (a short read is not EOF), negative on error. Write callback: accept the entire count or return negative; a short write fails. Callback resources stay with the caller.

## CLI flags

Parsed in `src/main.c`. Numbers go through `take_long`: non-negative decimal that fits a `long`, no trailing characters; `--max-pages` above `INT_MAX` is a usage error.

| Flag | Meaning |
| --- | --- |
| `--profile NAME` | `records-v1` (default) or `report-v1`; anything else exits 2 |
| `--record-offset N` | Skip N report-v1 records |
| `--record-limit N` | Select N records after the offset (0: all) |
| `--font PATH` | Use only this font, no fallback |
| `--max-pages N` | Page quota (0: no separate quota) |
| `--max-output-bytes N` | PDF byte quota |
| `--max-spool-bytes N` | Offset-file quota (0: 64 MiB) |
| `--temp-dir DIR` | Offset-file directory |
| `-` | stdin as input, stdout as output |
| `--` | End of options; following arguments are paths, even hyphen-leading ones |
| `-h`, `--help` | Usage on stdout, exit 0 |
| `--version` | Version line `xml2pdf <version> records-v1`, exit 0 |

Exit codes: 0 success, 1 XML/profile/font, 2 usage, 3 I/O, 4 quota or cancellation. Diagnostics go to stderr; stdout carries only PDF bytes.

## Environment variables

| Variable | Read by | Meaning |
| --- | --- | --- |
| `KI_XML_PDF_FONT` | `font_open_default` in `src/font.c` | Font path used when `font_path`/`--font` is not set; if it fails, the conversion fails, no fallback |
| `SCRATCH` | `tests/verify.sh` | Keeps the verify scratch directory in place for inspection |
| `PDFTOTEXT` | `Makefile`, `tests/cli_pipeline.py`, `tests/fuzz_smoke.py`, `tests/distcheck.sh` | Selects the PDF reader binary for checks |

`KI_XML_PDF_FONT` is the only environment variable the library itself reads. `LD_LIBRARY_PATH` and `PKG_CONFIG_PATH` matter for installed shared consumers, as documented in `README.md`.

## Build variables

Make (`Makefile`): `CC` (default `cc`), `CXX`, `CFLAGS` (default `-std=c99 -Wall -Wextra -Wpedantic -O2 -g -fPIC -D_POSIX_C_SOURCE=200809L`), `CXXFLAGS`, `PREFIX` (default `/usr/local`; feeds the compiled font path), `DESTDIR` (staging only, never changes the compiled font path or metadata prefixes), `PDFTOTEXT`, `DISTCHECK_ORACLE`, `DISTCHECK_BREAK`.

CMake (`CMakeLists.txt`): `BUILD_SHARED_LIBS` (default `ON`, picks the default `ki_xml_pdf::ki_xml_pdf` variant), `BUILD_TESTING` (default on), `CMAKE_BUILD_TYPE`, `CMAKE_INSTALL_PREFIX`, `CMAKE_INSTALL_LIBDIR`. The CMake font path is `${CMAKE_INSTALL_FULL_DATADIR}/ki_xml_pdf/DejaVuSans.ttf`.

## Compile-time constants

Changing any of these changes the accepted inputs or the output geometry, and they double as the documented limits.

| Constant | Value | File | Controls |
| --- | --- | --- | --- |
| `KI_XML_PDF_VERSION` | `0.2.0` | `include/ki_xml_pdf.h` | Single version source for CLI, CMake, pkg-config, archive |
| `KI_EVENT_ATTRS` | 8 | `include/ki_xml_pdf.h` | Attributes per `KiEvent` |
| `XML_BUF_SIZE` | 4096 | `src/xml_parse.h` | Parser read window |
| `XML_NAME_MAX` | 64 | `src/xml_parse.h` | Element/attribute name storage (63 usable bytes plus NUL) |
| `XML_ATTR_MAX` | 8 | `src/xml_parse.h` | Attributes per element |
| `XML_ATTR_VAL_MAX` | 256 | `src/xml_parse.h` | Decoded attribute value bytes (255 usable plus NUL) |
| `XML_TEXT_MAX` | 1024 | `src/xml_parse.h` | Text fragment storage (1023 usable bytes plus NUL) |
| `XML_MAX_DEPTH` | 64 | `src/xml_parse.h` | Open-element stack depth |
| `LAYOUT_LINE` | 1024 | `src/layout.h` | Bytes per wrapped line |
| `LAYOUT_LINES` | 50 | `src/layout.h` | Lines held per page |
| `FONT_BMP_MAX` | 0x500 | `src/font.h` | Code-point tables below U+0500 |
| `FONT_PUNCT_BASE` | 0x2010 | `src/font.h` | Start of the punctuation slots |
| `FONT_PUNCT_COUNT` | 24 | `src/font.h` | Punctuation slots (U+2010 through U+2027) |
| `MAX_OBJECTS` | 8,000,000 | `src/pdf_write.c` | Hard PDF object cap regardless of quotas |
| `PAGE_FONT_PT` | 10 | `src/pdf_write.c` | Type size |
| `PAGE_LEADING` | 14 | `src/pdf_write.c` | Line spacing |
| `PAGE_MARGIN` | 40 | `src/pdf_write.c` | Page margin |
| `PAGE_WIDTH` | 612 | `src/pdf_write.c` | Letter width in points |
| `PAGE_MAX_LINES` | 50 | `src/pdf_write.c` | Lines per page |
| `XML2PDF_PAGE_COLS` | 90 | `src/convert.h` | Legacy page-sink grid columns |
| `XML2PDF_PAGE_LINES` | 48 | `src/convert.h` | Legacy page-sink grid lines |
| `XML2PDF_PAGE_CAP` | 48 × 91 | `src/convert.h` | Legacy page-sink byte cap |
| `XML2PDF_FONT_PATH` | `$(PREFIX)/share/ki_xml_pdf/DejaVuSans.ttf` | `Makefile` (CPPFLAGS) | Compiled default font path |
| `XML2PDF_OK` / `XML2PDF_ERR_XML` / `_USAGE` / `_IO` / `_RESOURCE` | 0 / 1 / 2 / 3 / 4 | `src/convert.h` | Status and exit codes |

Message buffers are also fixed: `KiResult.message` is 160 bytes (`include/ki_xml_pdf.h`), and `XmlParser.errmsg`, `XmlEvent.error` and `KiDoc.error` match that size in `src/xml_parse.h` and `src/convert.c`.

## Related pages

- [Data models](data-models.md) for the structs these settings configure.
- [Debugging](../how-to-contribute/debugging.md) for option validation failures.
- [CLI](../features/cli.md) and [Resource limits](../features/resource-limits.md) for behavior.
- [Tooling](../how-to-contribute/tooling.md) for how the build consumes these variables.
