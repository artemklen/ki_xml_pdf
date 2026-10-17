# Debugging
Active contributors: Artem

Start with `KiResult.status`, `cause` and `message`, or the CLI's exit status and stderr. Preserve the exact source revision and synthetic reproducer before changing options.

## Status is not cause

| Status / CLI exit | Meaning | Causes |
| --- | --- | --- |
| 0 | Successful conversion | `KI_CAUSE_NONE` |
| 1 | XML, profile or font failure | `KI_CAUSE_XML`, `KI_CAUSE_PROFILE`, `KI_CAUSE_FONT` |
| 2 | Invalid arguments/configuration | `KI_CAUSE_USAGE` |
| 3 | I/O or non-memory compressor failure | `KI_CAUSE_IO` |
| 4 | Quota, allocation or cancellation failure | `KI_CAUSE_RESOURCE`, `KI_CAUSE_CANCEL` |

Cause enum values are NONE=0, XML=1, USAGE=2, IO=3, RESOURCE=4, FONT=5, CANCEL=6, PROFILE=7.
Do not confuse cause 6 with an exit code.
Conversion entry points reset a non-NULL result even on early usage failures.
Progress counters on failure do not mean that a file was published.

## Consumed positions

XML/profile errors, including `ki_doc_fail`, end with `at line L, column C, byte B`.
Positions describe **consumed input**, not the beginning of the offending token:

- Line and column start at 1; columns count Unicode scalars, and a tab is one column.
- Bytes start at 0 and include the UTF-8 BOM; the leading BOM adds no column.
- CR, LF and CRLF each count as one line break.
- References and CDATA count their source spelling, not just the decoded output.
- An empty element's synthetic END reuses its START position.
- `KiEvent` exposes only `byte_offset`; line/column remain internal and appear in diagnostics.

Malformed UTF-8 can be reported after a reference's semicolon or a tag's closing `>`, because those constructs perform the validity check after consumption.
A syntax/reference error detected first takes precedence.
For example, the header documents `<r>\xC3&amp;</r>` as line 1, column 10, byte 9.
Use `tests/test_xml_coords.c` for exact position regressions, not a guessed token-start offset.

## Common diagnoses

| Message or symptom | Check |
| --- | --- |
| `options size mismatch` / `config size mismatch` | Use the matching header and initializer; keep supported size values |
| `page geometry: ...` | Finite point values, named/custom-size rules, margin sums and room for the loaded font's ink |
| Invalid document information | UTF-8/control/length rules or explicit Gregorian date/time syntax |
| `font: U+...` | Body repertoire and nonzero mapped GID; zero advance alone is not missing |
| `font: cannot load` | Path, 8 MiB cap, table spans, name/metrics and font mutation before embedding |
| `resource limit` | Page/output/spool quotas, hard object cap and xref ceiling |
| `layout: line over 1023 bytes` | An indivisible base-plus-mark cluster exceeded bounded line storage |
| `layout: glyph cluster wider than the line` | Cluster ink/advance cannot fit the validated frame |
| `pdf compression failed` | zlib failure; not an XML error |
| `read error` | Callback negative/over-count return or file read failure |
| `pdf publish failed` / `pdf close failed` | Flush, fsync or close failure before rename |
| `input and output are the same file` | Same inode, including links |
| Partial callback/stdout PDF | Inspect failure status; accepted output is not rolled back |

`--temp-dir` controls the **offset spool**, not the temporary PDF beside the destination.
Changing it will not fix an unwritable destination directory.

## Font lifecycle

An explicit `KiOptions.font_path` or nonempty `KI_XML_PDF_FONT` is authoritative: failure does not trigger fallback.
Otherwise `src/font.c` tries the compiled install path, executable-relative candidates and the source-tree working-directory font.
`DESTDIR` stages an installation without changing its runtime prefix.

`FontFace` retains an open font file and fixed metrics, not the entire TTF.
Immediately before embedding, size and FNV-1a are checked again on that open file.
A pathname replacement does not reopen the new path; in-place mutation can fail the check.
Keep fonts stable throughout a call.

## Output and cancellation

File routes publish only after full input validation, writer completion, flush, fsync and close, followed by rename.
Handled failure removes temporaries and preserves the previous destination.
Callback output may already contain font bytes before the first input read.
The first failed write is latched: the library never retries the caller's callback afterward.
`KiResult.output_bytes` counts accepted callback bytes, including a positive short write.

The library does not install signal handlers.
The CLI translates SIGINT/SIGTERM to cancellation and temporarily ignores SIGPIPE for stdout output.
SIGKILL cannot run cleanup; inspect abandoned temporaries rather than assuming cleanup ran.
A successful rename is not rolled back by later cancellation.

## Instrumentation

Set `KiOptions.log` to capture diagnostics; the library is otherwise silent.
Not every early validation return calls the logger, so always inspect the result too.
Run `make test-san` for fatal ASan/UBSan diagnostics.
For stepping, use a clean debug build with `-O0 -g` and invoke `gdb --args ./xml2pdf input.xml output.pdf`.

Internal failure hooks declared in `src/convert.h` and `src/pdf_write.h` are not public shared-library API.
The conversion hooks are process globals; public `ki_*` calls do not automatically clear them.
Tests must explicitly reset hooks and must not arm them during concurrent conversions.
Do not infer current memory ownership from the historical `de8ba0e` heap snapshot.

## Key source files

| File | Role |
| --- | --- |
| `include/ki_xml_pdf.h` | Public status/cause and position contract |
| `src/convert.c` | Error precedence, callbacks and publication |
| `src/xml_parse.c`, `src/xml_parse.h` | Consumed-coordinate formatting and parser state |
| `src/font.c`, `src/pdf_write.c` | Font, quota and compression failures |
| `src/main.c` | CLI diagnostics and signals |

Related: [C API](../api/c-api.md), [Testing](testing.md), [Pitfalls](../background/pitfalls.md).
