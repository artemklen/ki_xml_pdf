# Security

ki_xml_pdf reads inputs it does not fully trust: XML bytes, a font file, and the surrounding filesystem. Every stage is built so hostile input becomes a rejected conversion, never a crash or a corrupted destination, and all claims below were verified against the code.

## Trust boundaries

| Boundary | What crosses it | Where it is bounded |
| --- | --- | --- |
| XML input | Arbitrary bytes from a file path or the `read_bytes` callback | `src/xml_parse.c` |
| Font file | Contents of `font_path`, `KI_XML_PDF_FONT` or the installed default | `src/font.c` |
| Filesystem | Output path, `temp_dir`, symlinks, other files, disk space | `src/convert.c`, `src/pdf_write.c` |
| Caller callbacks | `read_bytes`, `write_bytes`, `map`, `cancelled`, `log` | Contract in `include/ki_xml_pdf.h` |

The library performs no network access. Its file I/O is limited to the XML input path it opens itself (`ki_convert_file`) or the caller's read callback, the font file it loads, the temporary PDF it creates beside the destination, and the offset spool in the temp directory, all described below. The public header commits to reading the environment only for `KI_XML_PDF_FONT`, installing no signal handlers, and writing to no terminal unless a log callback is supplied.

## XML attack surface

Two classic XML attacks are structurally impossible here, not merely mitigated.

**XXE cannot happen because nothing is ever fetched.** A `<!DOCTYPE ...>` is accepted only at the document start, before any element, and only skipped textually: `skip_bang` in `src/xml_parse.c` checks placement, then `skip_decl_rest` consumes up to the matching `>` while tracking quotes and internal-subset brackets. `SYSTEM` and `PUBLIC` identifiers inside it are scanned past and never resolved. The parser has no file or network code at all; every byte arrives through the single `XmlReadFn` callback the caller installed.

**Billion laughs cannot happen because there is no entity table.** `try_named` in `src/xml_parse.c` accepts exactly the five predefined names `amp`, `lt`, `gt`, `quot` and `apos`, scanning at most six bytes ahead for the semicolon. Every other name is rejected with "unknown entity", in text and in attribute values alike. Entity declarations inside a skipped internal subset are dropped rather than registered, so a reference to them later fails instead of expanding. Character references are the only expansion that exists: `try_numeric` caps the value at U+10FFFF with explicit overflow checks on every digit and passes it through `xml_cp_ok`, which rejects surrogates, NUL and control characters other than tab, LF and CR.

The remaining parser surface is closed by rejection rules, all of which produce an error with a byte offset rather than undefined behavior:

- Comments may not contain `--` and must end with `-->` (`skip_comment`).
- `]]>` is rejected in ordinary text (`note_char_data`); CDATA is handled by its own path.
- Raw `<` inside an attribute value is rejected (`read_quoted`).
- Duplicate attributes, missing space between attributes, more than 8 attributes, bad end tags and multiple root elements are all errors.
- Processing instructions are capped at 255 bytes, and a reserved `xml`-like one is rejected unless it is a valid XML declaration at the very start of the document; the declaration accepts only version 1.0, UTF-8 encoding and yes/no standalone (`xml_decl_ok`).
- Text outside the document element must be whitespace.
- Any other `<!` declaration is rejected as unsupported.

## Parser bounds

The parser never allocates. All buffers are fixed arrays in `XmlParser` (`src/xml_parse.h`), so hostile input cannot grow memory:

| Limit | Value | Rejected with |
| --- | --- | --- |
| Open elements | 64 | "element nesting too deep" |
| Element or attribute name | 63 bytes | "bad start tag", "bad attribute", "bad end tag" |
| Attributes per element | 8 | "too many attributes" |
| Decoded attribute value | 255 bytes | "bad attribute value" |
| Text event fragment | 1023 bytes | split across events, never overflows |
| Input window | 4096 bytes | refilled from the read callback |

## UTF-8 validation

`feed_utf8` validates every byte as it passes through the window: NUL is rejected, overlong encodings are rejected (C0/C1 leads, and the E0/F0 lead short forms), surrogate halves are rejected (ED with a continuation above 0x9F), values above U+10FFFF are rejected (leads above F4, and F4 with a continuation above 0x8F), and stray continuation bytes are rejected. A sequence truncated by end of input fails with "illegal character". Decoded control characters are filtered by `xml_cp_ok`; only tab, LF and CR pass. Errors carry the byte offset (`fail_at`).

A UTF-8 BOM is consumed, and a UTF-16 BOM is rejected as "unsupported encoding" (`consume_bom`). Layout and the PDF writer each decode UTF-8 again independently (`utf8_next` in `src/layout.c` and `src/pdf_write.c`), so a bad sequence that somehow survived one stage would fail at the next.

## Font file handling

The font file is treated as untrusted structured data. `font_load` in `src/font.c` rejects anything below 12 bytes or above 8 MiB, bounds-checks the table directory against the file length in `find_table` (table count, and every table offset plus length), requires minimum lengths for `head` and `hhea`, requires the Windows Unicode BMP subtable (format 4, platform 3, encoding 1), and range-checks every segment, glyph and advance lookup (`glyph_of`, `advance_of`, `format4`). A font lacking a glyph for `A` or for space is rejected. There is no fallback font by design, so a rejected font fails the whole conversion as `KI_CAUSE_FONT` with exit status 1.

Only metrics that survived validation influence output, and the accepted font's bytes are embedded verbatim as PDF object 6 (`write_font_objects` in `src/pdf_write.c`). A malformed-but-accepted font therefore cannot corrupt the PDF structure itself. The synthetic malformed corpus is `tests/corpus/truncated-hmtx.ttf`, a 206-byte test font that exercises the truncated-`hmtx` path; `docs/license-decision.md` records that it is project-made and not a DejaVu font.

## File safety

The publication lifecycle in `src/convert.c` is the main filesystem safeguard:

- The PDF is written to `<destination>.XXXXXX`, created by `mkstemp` in `make_temp`. Placing the temp beside the destination keeps the final `rename` on one filesystem, so it is atomic, and `mkstemp` gives the unfinished file 0600 permissions.
- Publication happens only after the entire input validated, the PDF completed, and the output flushed, `fsync`ed and closed, with cancellation re-checked just before the `rename`. Every failure path unlinks the temp, and the previous destination is preserved byte for byte.
- The same-file check runs before anything is opened. `same_file` compares `st_dev` and `st_ino` from `stat`, which catches a hard link or symlink pointing at the input, and falls back to comparing `realpath` identities for a not-yet-existing output. Converting a file onto itself is refused with "input and output are the same file".
- A destination that is a symlink to some other file is replaced, not followed: `rename` swaps the link itself, so the link target is untouched.
- The offset spool is `<temp_dir>/ki-xml-pdf-off-XXXXXX`, or `/tmp/...` when no temp dir is set, also created with `mkstemp` and unlinked by `pdf_free` on every cleanup path in `src/pdf_write.c`. A temp path too long for the spool is reported as `KI_CAUSE_IO` ("temp path too long") rather than silently falling back.
- `SIGKILL`, a crash or power loss cannot run cleanup. An unfinished `<destination>.XXXXXX` beside the destination and a `ki-xml-pdf-off-XXXXXX` in the temp directory can be left behind. `docs/contract.md` classifies this as a process break outside the library's lifecycle; the existing destination is still safe, and the leftovers can be deleted by an operator.
- A directory in place of the input file is refused (`S_ISDIR` check in `ki_convert_file`).

Quotas stop runaway output before disk exhaustion: pages, output bytes and spool bytes each have a cap, and the object count is capped at 8,000,000 no matter what (`note_offset` in `src/pdf_write.c`). See [Resource limits](features/resource-limits.md).

## Signals and cancellation

The library installs no signal handlers; it polls the caller's `cancelled` callback. The CLI front end (`src/main.c`) installs `SIGINT` and `SIGTERM` handlers that set a `volatile sig_atomic_t` flag, and ignores `SIGPIPE` only while stdout output is active, so a closed pipe becomes an ordinary write failure instead of killing the process. The previous handlers are saved and restored after the conversion. Cancellation is checked in the event loop, on every read, before the PDF trailer, every 64 objects while the xref is emitted, around `fsync`, and once more before the `rename`. A cancelled conversion returns exit 4 with `KI_CAUSE_CANCEL` and publishes nothing.

## Verification

- **Sanitizers**: `make test-san` compiles the whole library plus `tests/test_stream.c` with AddressSanitizer and UndefinedBehaviorSanitizer, halting on the first error with leak detection. CI runs this in dedicated gcc and clang jobs (`.github/workflows/ci.yml`).
- **Fuzz**: `tests/fuzz_smoke.py` runs 24 seeded mutants of `examples/report-v1.xml` (1 to 8 random byte flips, truncated to 4096 bytes), truncations at 0, 1, 4095, 4096, 4097 and the last byte, and mutated or truncated font files including `tests/corpus/truncated-hmtx.ttf`. Every run executes under `ulimit -v` of 256 MiB with a 5-second timeout. Exit codes must be documented ones (0 to 4), a success must be readable by Poppler, and a failure must preserve the old destination exactly and leave no temp or spool file behind. Crash inputs are saved under `fuzz-crash/` for triage.
- **Resource gate**: the CI resources job runs `make check` and `tests/large_proof.py` on the ordinary binary, measuring converter RSS with `/usr/bin/time` against the 64 MiB ceiling, separately from the XML generator and the PDF reader.
- **Independent reading**: `tests/verify.sh` and the other check scripts read produced PDFs through Poppler, so a structurally complete but textually wrong PDF still fails.

## Reporting

Report security problems privately to Artem Klen at artem.klen@icloud.com, as `CONTRIBUTING.md` directs. Include the affected version or commit, the impact, reproduction steps and the smallest input you are permitted to share. Do not put security details in public GitHub issues, and do not attach third-party documents.

## Related pages

- [XML parser](systems/xml-parser.md) and [PDF writer](systems/pdf-writer.md) for the stage internals.
- [Atomic output](features/atomic-output.md) and [Resource limits](features/resource-limits.md) for the file lifecycle and quotas.
- [Design decisions](background/design-decisions.md) for why there is no fallback font and no external XML machinery.
- [Testing](how-to-contribute/testing.md) for running the sanitizer and fuzz suites yourself.
