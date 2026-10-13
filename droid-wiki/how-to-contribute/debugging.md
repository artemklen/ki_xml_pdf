# Debugging

Almost every failure reports itself with a message string: `KiResult.message` for library callers, stderr lines for the CLI. This page maps those messages to causes and fixes, then covers fonts, partial stream output, leftover temporaries, the log callback and sanitizer runs.

## Status, cause, message

Every public call fills a `KiResult` (reset first, even for early usage errors). The status matches the CLI exit code; the cause is finer.

| Status | CLI exit | Meaning |
| --- | --- | --- |
| 0 | 0 | Success, destination published |
| 1 (`XML2PDF_ERR_XML`) | 1 | XML, profile or font failure |
| 2 (`XML2PDF_ERR_USAGE`) | 2 | Invalid options or arguments |
| 3 (`XML2PDF_ERR_IO`) | 3 | I/O failure |
| 4 (`XML2PDF_ERR_RESOURCE`) | 4 | Quota or cancellation |

Causes: `KI_CAUSE_NONE`, `XML`, `USAGE`, `IO`, `RESOURCE`, `FONT`, `CANCEL`, `PROFILE` (numbers 0 to 7, defined in `include/ki_xml_pdf.h`). On success the CLI logs `xml2pdf: pages=%d objects=%d offset_bytes=%ld` to stderr; everything else goes there too, never to stdout.

## Error messages

Messages are set in `src/convert.c` (`set_result`, `fail_from_pdf`, the `convert_streams` exit paths).

| Message | Cause | What happened | Likely fix |
| --- | --- | --- | --- |
| `cancelled` | CANCEL (4) | The `cancelled` callback fired; checked in the read path, the event loop, while finishing the PDF, and around `fflush`/`fsync`/`close`/`rename` | Expected on SIGINT/SIGTERM; the destination is untouched |
| `read error` | IO (3) | `read_bytes` returned a negative value, or `fread` failed | Check the input; a callback must return 0 only at EOF and negative only on error |
| `xml: ...` | XML (1) | The parser rejected the input; its own text follows the prefix | Fix the XML; compare with the subset rules in `src/xml_parse.c` |
| `profile: ... at byte N` | PROFILE (1) | A report-v1 or custom mapper rule failed; `src/report.c` names the rule and the input byte offset | Fix the element/attribute, or adjust the mapper rule |
| `font: U+XXXX` | FONT (1) | A code point is outside the repertoire (U+0009, U+000A, U+000D, U+0020 to U+04FF, U+2010 to U+2027) or missing from the font | Remove or replace the character; see the font section below |
| `font: cannot load` | FONT (1) | The font file could not be opened or parsed (`fail_kind` 3 in `src/pdf_write.c`) | Check the font path and file; use the bundled `fonts/DejaVuSans.ttf` |
| `resource limit` | RESOURCE (4) | `max_pages`, `max_output_bytes` or `max_spool_bytes` quota hit, or the 8,000,000 object cap | Raise the quota, or select fewer records |
| `temp path too long` | IO (3) | Creating the temp file failed with `ENAMETOOLONG` | Shorten the destination path, or pass `--temp-dir` |
| `cannot create offset file` | IO (3) | The spool temp file could not be created (`errno` was set) | Check `--temp-dir` exists and is writable, or `/tmp` |
| `pdf write failed` | IO (3) | A write to the output failed | Check disk space and the output stream |
| `empty document` | XML (1) | Zero pages were produced, typically no text at all | Check that the profile emits text for this input |
| `pdf publish failed` | IO (3) | `fflush` or `fsync` failed while publishing | Check disk health and space; the destination is preserved |
| `cannot replace output ...` | IO (3) | The final `rename` over the destination failed | Check permissions on the destination directory |
| `input and output are the same file` | IO (3) | Both paths resolve to one inode, including hard or symbolic links | Write to a different file |
| `options size mismatch` | USAGE (2) | `KiOptions.size` differs from `sizeof(KiOptions)` | Call `ki_options_init` on the options and rebuild against the same header |
| `invalid option` | USAGE (2) | A negative quota/selection value or an unknown profile number | Reject negative values before setting options |

Rarer messages from the same file: `missing path`, `read callback is required` (`ki_convert_to_file`), `read and write callbacks are required` (`ki_convert`), `cannot open input ...`, `cannot read input ...` (the path is a directory), `cannot create temporary output for ...` (also `RESOURCE` on `ENOSPC`/`EDQUOT`), `cannot open temporary output ...`, `out of memory`, `cannot open write callback`, `parser init failed`, `layout init failed`, `page write failed`, `page flush failed`, `pdf end failed`, `pdf close failed`.

## Font troubleshooting

`font_open_default` in `src/font.c` resolves the font in this order when no explicit path is given: the `KI_XML_PDF_FONT` environment variable, then the compiled `XML2PDF_FONT_PATH`, then `fonts/DejaVuSans.ttf`, `../fonts/DejaVuSans.ttf` or `../share/ki_xml_pdf/DejaVuSans.ttf` beside the executable (resolved through `/proc/self/exe`), then `fonts/DejaVuSans.ttf` in the working directory.

Rules that bite:

- An explicit font (`--font`, `KiOptions.font_path`, or the environment variable) is never replaced if it fails. You get `font: cannot load`, not a silent fallback. `tests/prefix_switch.sh` and `tests/build_integration.sh` both assert this.
- The compiled path comes from `PREFIX` at build time. Build and install with the same `PREFIX`; the `src/.font_prefix` stamp forces `src/font.o` to rebuild when `PREFIX` changes. A stale stamp is what `tests/prefix_switch.sh` exists to catch.
- Distinguish the two font errors: `font: cannot load` means the file is missing or malformed (the reader needs `cmap` format 4 and `hmtx`; `tests/corpus/truncated-hmtx.ttf` is a truncated-hmtx sample that must be rejected). `font: U+XXXX` means the font loaded but the code point is outside the accepted repertoire.
- The bundled font is `fonts/DejaVuSans.ttf` with its notice in `fonts/DejaVuSans.copyright`. The reader keeps glyph tables for code points below U+0500 plus 24 punctuation slots at U+2010 (`FONT_BMP_MAX`, `FONT_PUNCT_BASE`, `FONT_PUNCT_COUNT` in `src/font.h`).

## Partial stdout PDFs

Output written to stdout or a `write_bytes` callback cannot be rolled back. A late XML or profile error, I/O error, quota or cancellation leaves the bytes already accepted, so the output is an incomplete PDF: it starts with `%PDF-` but lacks the trailing `%%EOF`. This is contract, not a bug; the help text says so and `tests/cli_pipeline.py` asserts the shape. Check the exit status before using streamed output, and when reporting a problem keep a partial PDF separate from successful results, with the status attached.

## Leftover temp files after SIGKILL

Handled failures unlink the temporary PDF beside the destination and the offset spool in `--temp-dir` or `/tmp`. `SIGKILL` cannot run that cleanup, so after a kill you may find `<destination>.XXXXXX` next to the destination and a spool file in the temp directory. Delete them by hand. The destination itself is still the previous file, because publication only happens on the `rename`. See [Atomic output](../features/atomic-output.md).

## Using the log callback

The library prints nothing unless `KiOptions.log` is set. `say()` in `src/convert.c` forwards every stage message to that callback, which is how the CLI produces its stderr lines (`cli_log` in `src/main.c`). Attach your own callback to capture the same messages in an application log, or to observe which stage produced a failure. `KiResult.message` holds the same last message after the call returns.

For fault injection there are static debug hooks: `xml2pdf_debug_fail_at_page`, `xml2pdf_debug_fail_before_publish` and `xml2pdf_debug_fail_after_bytes` (declared in `src/convert.h`) and `pdf_debug_fail_at_page` (in `src/pdf_write.h`). They are test hooks, reset when a conversion returns, and not exported by the shared library.

## Sanitizer and debugger runs

- `make test-san` runs the unit binary under ASan and UBSan with `halt_on_error=1` and leak detection, stopping at the first error with a stack trace. Use it for any memory-touching change.
- For stepping, build unoptimized with symbols: `make CFLAGS="-std=c99 -Wall -Wextra -Wpedantic -O0 -g -fPIC -D_POSIX_C_SOURCE=200809L"`, then `gdb --args ./xml2pdf --profile report-v1 input.xml out.pdf` or `gdb ./test_stream`.
- In-process RSS gates in `tests/test_stream.c` disable themselves under ASan, so a sanitizer run never doubles as a memory measurement; keep the two separate as `CONTRIBUTING.md` requires.

## Related pages

- [Testing](testing.md) for the suites that reproduce failures.
- [Conversion pipeline](../systems/conversion-pipeline.md) for where each message is produced.
- [Resource limits](../features/resource-limits.md) and [Pitfalls](../background/pitfalls.md) for quota and known-trap behavior.
- [Configuration](../reference/configuration.md) for every option, flag and constant.
