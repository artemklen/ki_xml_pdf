# Pitfalls

These behaviors are deliberate and documented, but each one is easy to get wrong the first time. Every entry names what bites, why the code is that way, and what to do instead.

## 1. Partial output to stdout and write callbacks

Choosing `-` as the CLI output, or calling `ki_convert`, means output cannot be rolled back: bytes already accepted by `write_bytes` stay in place when the conversion later fails, leaving an incomplete PDF. The CLI makes stdout unbuffered and checks the final `fflush` (a failure returns exit 3), but only the exit status distinguishes a complete file from a truncated one. Check the status before using streamed output, or use `ki_convert_to_file` to get atomic publication with callback input. The usage text in `src/main.c` states this directly: "stdout may contain a partial PDF on failure". See [Streaming I/O](../features/streaming-io.md).

## 2. PREFIX is baked in at build time

The default font path `<PREFIX>/share/ki_xml_pdf/DejaVuSans.ttf` is compiled into the library as `XML2PDF_FONT_PATH` (Makefile `CPPFLAGS`). The `src/.font_prefix` stamp makes a PREFIX change rebuild `font.o`, and `tests/prefix_switch.sh` proves installing to a new prefix without a clean rebuild works. What the stamp cannot fix: artifacts copied by hand, or a build cache that skips the stamp, carry a stale font path, and `DESTDIR` stages files without changing the compiled runtime path (the header comment in `include/ki_xml_pdf.h` says so). The symptom is "font: cannot load" for consumers running outside the source tree. Build and install with the same prefix, or set `KiOptions.font_path` or `KI_XML_PDF_FONT` explicitly. See [Packaging](../features/packaging.md).

## 3. `KiOptions.size` must match

`ki_options_init` sets `size` to `sizeof(KiOptions)`, and `reject_options` in `src/convert.c` refuses any other value with "options size mismatch" (exit 2). A caller that memsets the struct by hand, or links against a header from a different release, fails here. The 0.x API is unstable until 1.0, so a 0.y bump may change the struct; always initialize with `ki_options_init`.

## 4. `KiEvent` pointers are borrowed

The name, attribute and text pointers in a `KiEvent` point into the parser's per-iteration `XmlEvent`; they are valid only inside the current `map` callback and die on the next parser step. Retaining them is a use-after-return. If a schema needs metadata later, copy bounded metadata into mapper state, as the catalog example does; `docs/mapper-guide.md` spells this out.

## 5. A short read is not EOF

A `read_bytes` callback returns 0 only at end of input; 1 through `max` bytes is data, and the parser asks again. One-byte reads are exercised end to end by the catalog example. A callback that returns 0 on a short read silently truncates the input, and the resulting XML error surfaces far from the cause. `conv_read` in `src/convert.c` and the contract in `include/ki_xml_pdf.h` both treat a positive count as data.

## 6. Test hooks are process globals

`xml2pdf_debug_fail_at_page`, `xml2pdf_debug_fail_before_publish` and `xml2pdf_debug_fail_after_bytes` are static globals in `src/convert.c`, armed for any conversion running in the process. They are not in the public header, they are not thread-safe, and they are reset only by the legacy `xml2pdf_convert_cap` wrapper after its own conversion. The header comment in `src/convert.h` ("Cleared when a conversion returns") describes that wrapper's behavior; through `ki_convert_file` the hooks persist between calls. Tests set a hook, run one conversion, then clear the hooks. Concurrent conversions with an armed hook are undefined.

## 7. Whitespace and characters across fragment boundaries

A long text node arrives as many events of at most 1023 bytes, CDATA sections arrive as separate `from_cdata` text events, and a fragment does not necessarily end on a UTF-8 character boundary: `layout_text` holds a partial sequence of up to 4 bytes in `Layout.hold` until the character completes. Two consequences. First, a mapper must feed every fragment to `ki_doc_text` and never trim, because a space between two fragments must survive (`tests/corpus/split_whitespace.xml` covers this). Second, changing the read chunk size must not change the output, which is why the catalog example is checked with 1-byte and 4096-byte reads.

## 8. `xml:space` means different things per profile

In `records-v1`, `xml:space="preserve"` is inherited state: it stops whitespace-only indentation containing a newline from being dropped on an empty line, and CDATA always counts as preserved (`map_event` in `src/convert.c`). In `report-v1`, the attribute is allowed only on `field`, accepts only `default` or `preserve`, and both values keep the value verbatim because the profile never removes indentation inside a field (`src/report.c`, `docs/report-v1.md`). The same attribute means different things depending on the profile, and a custom mapper gets the raw events and owns the rule entirely.

## 9. Zero is "no extra cap", except the spool

`max_pages = 0` and `max_output_bytes = 0` disable those quotas. `max_spool_bytes = 0` does not disable anything: `pdf_begin_cfg` in `src/pdf_write.c` substitutes the 64 MiB default cap. The 8,000,000 object ceiling applies regardless of all three. Passing 0 to mean "unlimited" for the spool gives you a hard failure on a very large document.

## 10. The version comes from the header

`KI_XML_PDF_VERSION` in `include/ki_xml_pdf.h` is the single version source: the Makefile extracts it with `sed`, CMake extracts it with `file(STRINGS ...)`, `xml2pdf --version` prints it, and the pkg-config and CMake package metadata are configured from it. Hardcoding the version anywhere else desynchronizes the archive name, the installed metadata and the CLI output. A related CI step touches `src/layout.h` and requires the CLI to rebuild, so edits that should trigger a rebuild cannot be silently skipped.

## 11. `make dist` archives HEAD, not the working tree

`DIST_VERSION` is read with `git show HEAD:include/ki_xml_pdf.h` and the archive is `git archive HEAD`, with the Makefile comment "A source archive always describes HEAD, including when the working tree is dirty". If you bump the version or edit sources without committing, `make dist` quietly ships the committed version and the committed content. Commit first, then dist. `make distcheck` then validates the archive, and CI's source job verifies the archive's embedded commit against `SOURCE_COMMIT`.

## Related pages

- [Design decisions](design-decisions.md) for why these behaviors exist.
- [Debugging](../how-to-contribute/debugging.md) for the failure-injection hooks and how to use them safely.
- [C API](../api/c-api.md) for the callback contracts in one place.
- [Security](../security.md) for the bounds and file lifecycle these pitfalls orbit.
