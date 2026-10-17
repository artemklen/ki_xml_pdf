# C API
Active contributors: Artem

The public header is `include/ki_xml_pdf.h`; its comments are the authoritative contract. Version 0.3.0 supports 64-bit Linux/glibc and C/C++ callers through an `extern "C"` guard.

## Entry signatures

```c
void ki_options_init(KiOptions *opt);
int ki_config_init(KiConfig *cfg, size_t size);

int ki_convert_file(const char *xml_path, const char *pdf_path,
                    const KiOptions *opt, KiResult *result);
int ki_convert_to_file(const char *pdf_path, const KiOptions *opt,
                       KiResult *result);
int ki_convert(const KiOptions *opt, KiResult *result);

int ki_convert_file_cfg(const char *xml_path, const char *pdf_path,
                        const KiOptions *opt, const KiConfig *cfg,
                        KiResult *result);
int ki_convert_to_file_cfg(const char *pdf_path, const KiOptions *opt,
                           const KiConfig *cfg, KiResult *result);
int ki_convert_cfg(const KiOptions *opt, const KiConfig *cfg,
                   KiResult *result);

int ki_doc_text(KiDoc *doc, const char *s, size_t n);
int ki_doc_break(KiDoc *doc);
int ki_doc_fail(KiDoc *doc, const char *rule);
```

## Initialization and early validation

`ki_options_init` zeroes options and sets `size = sizeof(KiOptions)`; NULL is a no-op.
A conversion rejects any different options size, negative quota/selection, or unknown profile.
File-to-file calls accept NULL options for defaults.
A result pointer may be NULL; otherwise every conversion resets it, even on early usage failure.

`ki_config_init(&cfg, sizeof cfg)` returns 0 and initializes Letter portrait, 40 pt margins, 10 pt font and no metadata.
It also accepts `KI_CONFIG_SIZE_GEOMETRY`, writing only that prefix.
NULL or an unknown size returns 2 without writing configuration bytes.
Conversions accept exactly those two sizes; a NULL config is identical to the old-call defaults.

Geometry and metadata validation precede file opening and input/output/map callbacks.
Font-dependent fitting follows font loading, still before input/output/map callbacks.
A valid numeric rectangle can fail usage validation if one line or a single repertoire glyph cannot fit its ink.
Metadata values are copied at call start.
See [Configuration](../reference/configuration.md) for values and rounding.

## I/O routes

| Route | Requirements | Ignored callbacks | Publication |
| --- | --- | --- | --- |
| `ki_convert_file[_cfg]` | Non-NULL XML/PDF paths | Both I/O callbacks | Atomic file |
| `ki_convert_to_file[_cfg]` | PDF path, options and `read_bytes` | `write_bytes` | Atomic file |
| `ki_convert[_cfg]` | Options, `read_bytes`, `write_bytes` | Neither | Partial output on failure |

The bracket notation describes paired names, not literal C identifiers.
File input rejects a directory or an input/output same-file identity, including links.
A literal `"-"` has no special library meaning; stdin/stdout adaptation belongs to `src/main.c`.

Atomic output uses a temporary file beside the destination.
Only complete validation, PDF completion, flush/fsync/close and successful rename publish it.
Handled failure preserves the old destination and removes the temporary.
`temp_dir` controls only the offset spool.
No callback, stdout or external sink can be rolled back.

## Callback contract

```c
int read_bytes(void *user, char *dst, int max);
int write_bytes(void *user, const char *src, int n);
int map(void *user, KiDoc *doc, const KiEvent *ev);
int cancelled(void *user);
void log(void *user, const char *msg);
```

- Read: return 0 only at EOF, 1..max for data, or a negative value for error. A short positive read is not EOF; over-counts fail I/O.
- Write: accept exactly n and return n, or fail. Short, zero, negative and over-count writes fail I/O.
- The first failed write is latched; `write_bytes` is never called again in that conversion, even if a later call could succeed.
- `output_bytes` counts actual accepted callback bytes, including a valid positive short count before failure.
- Caller state/resources are never closed or freed by the library.
- `map_user`, `cancel_user`, `log_user` and `io_user` are the corresponding caller-owned context pointers.

## Mapping and text

`KiOptions.map` overrides either built-in profile.
Events are START=1, END=2 and TEXT=3, with at most eight attributes.
All event pointers are borrowed for the callback only; copy bounded metadata if needed.
Long values span events; do not trim each fragment or retain event storage.

`ki_doc_text` accepts n UTF-8 bytes; `ki_doc_break` ends the current line.
Both return 0 on success and -1 on failure.
Text wraps at space/tab opportunities, then code-point boundaries for long words.
Zero-advance marks stay with their base; an indivisible cluster over 1023 bytes or wider than the frame fails as a resource error.
An unsupported/missing glyph is a font error, not XML syntax.

`ki_doc_fail` records `KI_CAUSE_PROFILE`, formats the rule plus consumed line/column/byte, and returns -1.
A NULL rule becomes `mapping failed`.
Returning nonzero from a mapper without recording a reason is less diagnostic; propagate document-helper failures directly.
See [Custom mappers](../features/custom-mappers.md).

## Results and diagnostics

| Status | Meaning | Cause values |
| --- | --- | --- |
| 0 | Success | NONE=0 |
| 1 | XML/profile/font | XML=1, FONT=5, PROFILE=7 |
| 2 | Usage | USAGE=2 |
| 3 | I/O/compressor | IO=3 |
| 4 | Resource/cancellation | RESOURCE=4, CANCEL=6 |

`KiResult.message[160]` holds a human-readable reason.
XML/profile positions are consumed coordinates: 1-based scalar line/column and 0-based raw byte offset, BOM included.
`KiEvent` adds no public line/column fields; only its existing `byte_offset` is exposed.
See [Debugging](../how-to-contribute/debugging.md) for delayed UTF-8 checks.

Counts describe work/output, not proof of publication on failure.
`offset_bytes` and `spool_bytes` report spool usage.
Report total/selected/skipped counters are filled only for the built-in report profile and remain zero for records/custom mapping.

## Cancellation and concurrency

Any nonzero cancellation response requests status 4 / cause CANCEL.
Checks occur around reads/events/publication, about every 64 objects during PDF finish, and after fsync/close before rename.
Cancellation after successful rename does not roll back the published file.

One context and its mutable callbacks/result are not safe for concurrent use; independent contexts may run together.
The library installs no signal handlers and prints nothing unless given a logger.
Its only library-specific environment setting is `KI_XML_PDF_FONT` when no explicit font path is supplied.

## ABI and installed consumers

`KiOptions`, `KiResult` and `KiEvent` retain their 0.2 layouts.
`KI_0_1`/`KI_0_2` exports remain; `KI_0_3` adds the config initializer and three `_cfg` conversions.
The soname stays `libki_xml_pdf.so.0`; old C/C++ binaries are exercised by `tests/abi_canary/run_canary.sh`.

General pre-1.0 policy still permits minor ABI changes.
CMake version matching accepts exact or older requested versions only within the installed same major/minor.
Static archive users need zlib (`-lz` or pkg-config `--static`); CMake targets propagate it.

## Key source files

| File | Role |
| --- | --- |
| `include/ki_xml_pdf.h` | Public types and functions |
| `src/convert.c` | Initializers, conversions, helpers and callback accounting |
| `libki_xml_pdf.map` | ELF version nodes; also retains legacy wrappers declared in `src/convert.h` |
| `tests/test_geometry.c`, `tests/test_info.c`, `tests/test_callback_io.c` | Config and callback regressions |

Related: [Data models](../reference/data-models.md), [Packaging](../features/packaging.md).
