# Patterns and conventions

The code is small, C99 and deliberately plain. These patterns show up across `src/` and are expected in new code.

## Bounded state everywhere

Every structure has a fixed size set by a `#define`: `XML_BUF_SIZE`, `XML_MAX_DEPTH`, `XML_TEXT_MAX` in `src/xml_parse.h`, `LAYOUT_LINE` and `LAYOUT_LINES` in `src/layout.h`, `FONT_BMP_MAX` in `src/font.h`, `MAX_OBJECTS` in `src/pdf_write.c`. Exceeding a bound is an error, never a reallocation. `CONTRIBUTING.md` treats this as part of the public contract: parser and mapper state must not grow with input size.

The only heap allocations in the conversion path are the font file bytes, path strings and the `fopencookie` sink in `ki_convert`.

## Return codes, not exceptions

Internal functions return 0 on success and -1 (or a negative value) on failure. The detailed reason is stored on the owning struct:

- `XmlParser.errmsg` and `XmlEvent.error` for parse errors.
- `PdfWriter.fail_kind`, `fail_errno` and `hit_limit` for writer failures.
- `Layout.bad_cp` for an unsupported code point.
- `KiDoc.error` and `KiDoc.cause` for mapper failures.

`convert_streams` in `src/convert.c` inspects those fields after a failure and maps them to a status and a `KI_CAUSE_*` value (`fail_from_pdf` does this for the writer). Keep that split: low-level code records what happened, the driver decides how to report it.

## goto-based cleanup

Functions with several resources use a single exit label (`done:` / `open_fail:` in `src/convert.c`). Temporary files are unlinked there unless `published` is set.

## Results are always reset

Every public entry point zeroes a non-NULL `KiResult` before validating anything (`ki_convert_file`, `ki_convert_to_file`, `ki_convert`). This was fixed in OSR-2 so reused results never carry stale counters. Tests check it.

## Options validation

`ki_options_init` zeroes `KiOptions` and sets `size`. `reject_options` rejects a size mismatch, negative quotas or selection values, and unknown profiles with `KI_CAUSE_USAGE`. New option fields must keep `KiOptions` layout stable within a 0.y series.

## Cancellation checkpoints

The `cancelled` callback is polled in the read path (`conv_read`), at the top of the event loop, inside the PDF writer while finishing, and again around `fflush`, `fsync`, `close` and `rename`. A new long-running step should add a checkpoint.

## No global side effects in the library

The library never installs signal handlers, never prints unless `KiOptions.log` is set, and reads only one environment variable (`KI_XML_PDF_FONT`, when no `font_path` is given). Signal handling lives in `src/main.c` and restores the previous handlers before exit.

The exception is a set of static test hooks (`xml2pdf_debug_fail_*` in `src/convert.c`, `pdf_debug_fail_at_page` in `src/pdf_write.c`). They are reset when a conversion returns and are not in the public header.

## Symbol visibility

Public functions are marked `KI_API` (default visibility). The shared library's exports are fixed by `libki_xml_pdf.map` with versioned nodes `KI_0_1` and `KI_0_2`; everything else is `local`. Adding a public function means adding it to a new version node.

## Comments

Comments explain constraints, not mechanics. Header comments state ownership and lifetime rules (for example "Pointers are valid only for the duration of the map callback" in `include/ki_xml_pdf.h`). Match that style.

## Style

- C99 with `-Wall -Wextra -Wpedantic` and `_POSIX_C_SOURCE=200809L`; `src/convert.c` and `src/pdf_write.c` use `_GNU_SOURCE` for `fopencookie`.
- Four-space indentation, braces on the same line, `snake_case` functions, `PascalCase` structs.
- Public names use the `ki_` / `Ki` / `KI_` prefix. Internal modules use their own prefix (`xml_`, `pdf_`, `layout_`, `font_`, `report_`).
- No third-party C dependencies.

## Tests as evidence

A behavior change needs a regression that fails before the fix and checks an observable result, preferably through an independent reader (`pdftotext`) rather than by matching writer bytes. See [Testing](testing.md).

## Related pages

- [Conversion pipeline](../systems/conversion-pipeline.md) for the error mapping in context.
- [C API](../api/c-api.md) for the public contract these patterns protect.
