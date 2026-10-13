# C API

Active contributors: Artem Klen

The public API is the single header `include/ki_xml_pdf.h`: `KiOptions` describes one conversion, `KiResult` reports its outcome, and `ki_convert_file`, `ki_convert_to_file` and `ki_convert` run the pipeline. Everything else in the library is internal; the shared library exports only the symbols listed in `libki_xml_pdf.map`.

## Purpose

Let C and C++ applications convert XML to PDF with explicit control over the profile, mapper, quotas, fonts, I/O and cancellation, and get back a status, a cause, a message and counters. The header comment is the contract; this page restates it reference-style.

## Constants and enumerations

| Name | Value | Meaning |
| --- | --- | --- |
| `KI_XML_PDF_VERSION` | `"0.2.0"` | Library version, the single source for CMake, pkg-config and the CLI |
| `KI_XML_PDF_PROFILE_RECORDS` | `"records-v1"` | Default profile name |
| `KI_XML_PDF_PROFILE_REPORT` | `"report-v1"` | Report profile name |
| `KI_XML_PDF_PROFILE` | `"records-v1"` | The default profile as one macro |
| `KI_EVENT_ATTRS` | `8` | Attribute slots in `KiEvent`, matching the parser limit |
| `KI_EV_START` / `KI_EV_END` / `KI_EV_TEXT` | 1 / 2 / 3 | Event kinds passed to a mapper |

Causes, reported in `KiResult.cause`:

| Cause | Value | Meaning |
| --- | --- | --- |
| `KI_CAUSE_NONE` | 0 | Success |
| `KI_CAUSE_XML` | 1 | Malformed or unsupported XML |
| `KI_CAUSE_USAGE` | 2 | Invalid options, paths or callbacks |
| `KI_CAUSE_IO` | 3 | Input, output or filesystem error |
| `KI_CAUSE_RESOURCE` | 4 | A quota ran out or memory failed |
| `KI_CAUSE_FONT` | 5 | Font file unusable or a character outside the repertoire |
| `KI_CAUSE_CANCEL` | 6 | The `cancelled` callback asked to stop |
| `KI_CAUSE_PROFILE` | 7 | A mapper schema rule failed |

Profiles, reported in `KiOptions.profile`: `KI_PROFILE_RECORDS` (0, default) and `KI_PROFILE_REPORT` (1).

## Types

### `KiDoc`

An opaque handle passed to a mapper. Mappers write text with `ki_doc_text`, break lines with `ki_doc_break`, and fail with `ki_doc_fail`. The definition lives in `src/convert.c`; callers only hold the pointer during a callback.

### `KiEvent`

One parser event delivered to a mapper. Fields: `kind` (one of `KI_EV_*`), `name` (element name, colon included because namespaces are not resolved), `nattr` with `attr_name[]` and `attr_val[]` arrays (at most `KI_EVENT_ATTRS` entries), `text` with `text_len` for `KI_EV_TEXT`, `from_cdata` for CDATA fragments, and `byte_offset`.

All pointers are valid only for the duration of the map callback. `byte_offset` is the parser position after this event's bytes were consumed; an empty element's synthetic END uses the same position as its START, and EOF is the position at the end of input. It is a byte count, not a character index.

### `KiOptions`

Call `ki_options_init` before filling any field. `size` must remain `sizeof(KiOptions)`; a mismatch is a usage error, which is how the library detects a header built against an incompatible struct. The 0.x API is unstable until 1.0: a 0.y bump may change this struct.

| Field | Type | Meaning |
| --- | --- | --- |
| `size` | `size_t` | Must equal `sizeof(KiOptions)`; set by `ki_options_init` |
| `font_path` | `const char *` | Use this font only, no fallback; a failure to load it is an error |
| `temp_dir` | `const char *` | Directory for the offset spool file; `NULL` means `/tmp` |
| `max_pages` | `int` | Page quota; 0 means no separate quota |
| `max_output_bytes` | `long` | PDF byte quota; 0 means no separate quota |
| `max_spool_bytes` | `long` | Offset spool quota; 0 means the 64 MiB default |
| `profile` | `int` | `KI_PROFILE_RECORDS` or `KI_PROFILE_REPORT` |
| `record_offset` | `long` | `report-v1`: number of initial records to skip |
| `record_limit` | `long` | `report-v1`: maximum records selected after the offset; 0 selects all |
| `cancelled` / `cancel_user` | callback | Polled at documented checkpoints; nonzero means stop |
| `map` / `map_user` | callback | Custom mapper; replaces both built-in profiles when set |
| `log` / `log_user` | callback | Diagnostics sink; without it the library prints nothing |
| `read_bytes`, `write_bytes`, `io_user` | callbacks | Callback I/O; see the contract below |

Negative values in any numeric field, or a profile outside the two known ones, are usage errors. A `NULL` options pointer is accepted by `ki_convert_file` and means all defaults.

### `KiResult`

Initialized by every call that receives a non-NULL pointer, including early argument errors, so counters never carry a previous call's values.

| Field | Type | Meaning |
| --- | --- | --- |
| `status` | `int` | Return code, 0 to 4 |
| `cause` | `int` | One of the `KI_CAUSE_*` values |
| `message` | `char[160]` | Human-readable reason; profile failures name the rule and byte offset, font failures name a `U+` code point |
| `pages` | `int` | Pages written |
| `objects` | `int` | PDF objects written (at most 8,000,000) |
| `offset_bytes` | `long` | Offset spool size in bytes |
| `output_bytes` | `long` | PDF bytes written so far |
| `spool_bytes` | `long` | Offset spool size, the same value as `offset_bytes` |
| `report_total` | `unsigned long long` | `report-v1`: records seen |
| `report_selected` | `unsigned long long` | `report-v1`: records rendered |
| `report_skipped` | `unsigned long long` | `report-v1`: total minus selected |

The `report_*` fields are filled only when the built-in `report-v1` profile ran: the driver copies them from the `Report` state at the end of a conversion, and they stay zero for `records-v1` or a custom mapper (which owns its own counters in its state).

## Functions

### `void ki_options_init(KiOptions *opt)`

Zeroes the struct and sets `size` to `sizeof(KiOptions)`. Accepts `NULL` by doing nothing. Call this before setting any other field.

### `int ki_doc_text(KiDoc *doc, const char *s, size_t n)`

Appends `n` bytes of UTF-8 text to the current line. Returns 0 on success, -1 on failure. A character outside the font repertoire fails with `KI_CAUSE_FONT` and a message like `font: U+1F600`; this is a font error, not an XML syntax error. Mappers send raw fragments here, including fragments that split a UTF-8 character across callbacks; the layout handles the boundary.

### `int ki_doc_break(KiDoc *doc)`

Ends the current line. Returns 0 on success, -1 on failure.

### `int ki_doc_fail(KiDoc *doc, const char *rule)`

Records a profile failure (`KI_CAUSE_PROFILE`) with the given rule text, or `mapping failed` when `rule` is `NULL`, and returns -1. Callers compose the message; `ki_doc_fail` does not append a byte offset.

### `int ki_convert_file(const char *xml_path, const char *pdf_path, const KiOptions *opt, KiResult *result)`

Converts a file to a file. `opt` may be `NULL` for defaults. Ignores `read_bytes` and `write_bytes`. Rejects a `NULL` path, an options-size mismatch, invalid option values, an input that is a directory, and an input and destination that are the same file (compared by inode, so hard and symbolic links are caught) with a usage or I/O error. Publication is atomic: a temporary file beside the destination, then `rename` after the whole input is validated and the PDF is flushed, synced and closed. On failure the previous destination survives and the temporary file is removed.

### `int ki_convert(const KiOptions *opt, KiResult *result)`

Converts callback input to callback output. Requires both `read_bytes` and `write_bytes`; a missing callback is a usage error. The library wraps `write_bytes` in a glibc `fopencookie` stream (the reason the library is Linux/glibc only). There is no destination path, so nothing is renamed: bytes already accepted by `write_bytes` stay in place when a later step fails, and the caller must check the return code before using the output.

### `int ki_convert_to_file(const char *pdf_path, const KiOptions *opt, KiResult *result)`

Converts callback input to an atomic file destination. Requires `opt` and `opt->read_bytes`; `write_bytes` is ignored. Uses the same temporary-file, full-input-validation, flush/fsync/close and rename lifecycle as `ki_convert_file`, so a pipe or application stream can still produce a published-or-preserved file. The caller keeps ownership of input and callback resources.

### Legacy exports

`xml2pdf_convert(xml_path, pdf_path)` and `xml2pdf_convert_cap(xml_path, pdf_path, page_cap)` are declared in `src/convert.h` and exported in the `KI_0_1` node for compatibility. Both call `ki_convert_file` with a stderr log; the cap variant sets `max_output_bytes` from `page_cap` (0 for no cap) and clears the debug failure hooks. New code should use the `ki_` functions.

## Return codes

| Status | Meaning | Typical causes |
| --- | --- | --- |
| 0 | Success | `KI_CAUSE_NONE` |
| 1 | XML, profile or font error | `KI_CAUSE_XML`, `KI_CAUSE_PROFILE`, `KI_CAUSE_FONT` |
| 2 | Usage error | `KI_CAUSE_USAGE` |
| 3 | I/O error | `KI_CAUSE_IO` |
| 4 | Resource quota, allocation failure or cancellation | `KI_CAUSE_RESOURCE`, `KI_CAUSE_CANCEL` |

The constants also exist internally as `XML2PDF_OK` and `XML2PDF_ERR_*` in `src/convert.h`. The CLI returns the status directly as its exit code.

## Callback contract

`read_bytes(void *user, char *dst, int max)` returns 0 only at EOF, `1..max` for data (a short positive read is not EOF), or a negative value for an error. `write_bytes(void *user, const char *src, int n)` must return `n` after accepting the entire count or a negative value for an error; a short write also fails. Bytes accepted by `write_bytes` are never rolled back. Callback state and resources belong to the caller; the library never closes or frees them. `ki_convert_file` ignores both callbacks; `ki_convert_to_file` ignores `write_bytes`.

## Threading and global behavior

A context (options plus mutable mapper and I/O state) is not safe for concurrent use. Two contexts may run at once, each with its own `KiOptions`, `KiResult` and mapper state. The library installs no signal handlers, reads no environment variable except `KI_XML_PDF_FONT` when no font path is set, and writes nothing to the terminal unless a `log` callback is supplied.

## Cancellation

The `cancelled` callback is polled before publication, about every 64 objects while the PDF is finished, and again after fsync and close, before rename. A positive return stops the conversion with status 4 and `KI_CAUSE_CANCEL`; a file destination keeps its previous content. After a successful rename, a later cancel does not roll the file back.

## Symbol versioning

`libki_xml_pdf.map` fixes the shared-library ABI with versioned nodes: `KI_0_1` exports `ki_convert`, `ki_convert_file`, `ki_doc_text`, `ki_doc_break`, `ki_doc_fail`, `ki_options_init` and the legacy `xml2pdf_convert` pair; `KI_0_2` adds `ki_convert_to_file`. Everything else is local. Adding a public function means adding it to a new node.

## Examples

The minimal C consumer, `examples/consume.c`, passes `NULL` options and prints the page count:

```c
KiResult result;
int rc = ki_convert_file(in, out, NULL, &result);
if (rc != 0) {
    fprintf(stderr, "consume: %s\n", result.message);
    return rc;
}
printf("pages=%d\n", result.pages);
```

`examples/consume.cpp` is the same program in C++; the header's `extern "C"` block makes linking work without extra flags. The report example, `examples/report.c`, selects the profile and reads the counters:

```c
KiOptions opt;
ki_options_init(&opt);
opt.profile = KI_PROFILE_REPORT;
KiResult result;
int rc = ki_convert_file(in, out, &opt, &result);
if (rc != 0) {
    fprintf(stderr, "report: %s\n", result.message);
    return rc;
}
printf("pages=%d total=%llu selected=%llu skipped=%llu\n",
       result.pages, result.report_total,
       result.report_selected, result.report_skipped);
```

For a custom mapper, see [Custom mappers](../features/custom-mappers.md); for quota and stream behavior, [Resource limits](../features/resource-limits.md) and [Streaming I/O](../features/streaming-io.md).

## Key source files

| File | Contents |
| --- | --- |
| `include/ki_xml_pdf.h` | The public contract; the header comment is normative |
| `src/convert.c` | Implementations of all `ki_*` functions, `KiDoc` and error mapping |
| `src/convert.h` | Status constants and legacy declarations |
| `libki_xml_pdf.map` | Exported symbol list with version nodes |
| `examples/consume.c`, `examples/consume.cpp`, `examples/report.c` | Compilable API examples |
| `tests/consumer/report.c` | Installed-library consumer used by packaging checks |
