# Design decisions

Each of these choices is a recorded decision rather than an accident, and the reasoning survives in the repository. Three of the decision records are written in Russian (`docs/contract.md`, `docs/profile-decision.md`, `docs/license-decision.md`); this page summarizes them in English and names the code that implements each one.

## Own streaming pipeline, not Apache FOP (KW-1004)

The project keeps its own C pipeline rather than embedding an XML formatter such as Apache FOP. `docs/contract.md` is careful about what this is not: it is not a claim of being faster or lighter than any other converter, and no comparison with FOP was measured, because Java and FOP were absent from the test bench and the original problematic process was never attached. What drove the decision is the hard requirement, a fixed memory ceiling on huge XML, plus a wish to control every allocation directly. The result is about 4,500 lines of C99 in `src/` where no stage keeps a list that grows with the document. See [Architecture](../overview/architecture.md).

## Bounded memory as the organizing rule

Every stage holds fixed state: a 4096-byte parser window and a 64x64 element stack (`src/xml_parse.h`), a 64-entry mapper stack (`MapState` in `src/convert.c`), counters plus a 256-byte pending name for report-v1 (`src/report.h`), 50 lines of at most 1024 bytes in layout (`src/layout.h`), fixed glyph tables for a fixed repertoire (`src/font.h`), and a PDF writer that drops each page when it is written. The acceptance rule is measured, not inferred: CI's resources job measures converter RSS with `/usr/bin/time` against a 64 MiB ceiling; the recorded measurement on revision `7c62fe4` was 2372 KiB for a 1 GiB input.

One consequence is deliberate scope exclusion. Anything that needs document-wide state, such as tables, global sorting, aggregates, forward references or unique-ID checks, is outside the contract (`docs/contract.md`). The design rule is stated up front so feature requests can be answered with a reason.

## Object offsets spooled to disk

A PDF needs an xref entry per object, and 41,944 pages mean more than 80,000 objects. Instead of an array that grows with the page count, `note_offset` in `src/pdf_write.c` writes each object's offset as 8 bytes into a temporary file at slot `num * 8`, and `pdf_end` reads the slots back while emitting the xref. Memory stays flat regardless of document size; the cost is one bounded temp file with a default quota of 64 MiB and an absolute ceiling of 8,000,000 objects. The recorded 16 MiB workload produced a 671,168-byte spool, and the 1 GiB report-v1 run produced 720 bytes.

## Fixed object numbering

Objects 1 through 7 are a fixed block (catalog, pages tree, Type0 font, CID font, descriptor, embedded font file, ToUnicode CMap), and every page after that consumes exactly two objects starting at 8: a content stream and a page dictionary. A page's object numbers are computed from its index (`OBJ_FIRST + 2 * i` and `+1`), never stored. The writer's state reduces to three counters. This removes the last structure that could have grown with the document.

## report-v1 as a project-owned schema (KW-1021)

`docs/profile-decision.md` records the owner's explicit choice, "MIT + report-v1 (recommended)", accepted on 07.10.2026. For the first public release the project ships its own small report grammar (`report`/`record`/`field` with explicit record selection) rather than guessing at someone else's schema. `records-v1` stays the default diagnostic profile. An unknown custom format is expected to arrive through the public mapper callback, following `docs/mapper-guide.md` and the catalog example; compatibility with any unknown third-party schema is explicitly not claimed. The record also notes that choosing the profile was not by itself permission to publish, since release acceptance was a separate item (KW-1025).

## MIT for the sources (KW-1024)

`docs/license-decision.md` records MIT, chosen over Apache-2.0 for an embeddable library after weighing Apache-2.0's explicit patent grant and its NOTICE obligations against MIT's minimal notice requirement. The scope is the sources, the public API, the mapper and the project-made examples. The bundled DejaVu Sans font is outside that scope: it ships with its own Bitstream Vera/DejaVu notice at `fonts/DejaVuSans.copyright` and is distributed only together with that notice. The malformed test font `tests/corpus/truncated-hmtx.ttf` is synthetic and not a DejaVu font.

## Linux/glibc only

`ki_convert` wraps the caller's `write_bytes` callback in a `FILE *` through glibc's `fopencookie`, and the PDF writer wraps its output stream the same way to enforce the output byte quota at the stream layer (`sink_write` in `src/convert.c`, `cap_write` in `src/pdf_write.c`). Reusing one `FILE *`-based write path for files, stdout and callbacks is the reason the platform is 64-bit Linux with glibc; `CMakeLists.txt` fails configuration on any other target and names `fopencookie` as the cause. Other Linux-specific touches include `readlink("/proc/self/exe")` in the font search (`src/font.c`) and the GNU linker's `--wrap=fsync` in the test build. No portability layer exists, and other systems are explicitly unvalidated.

## Atomic publication

File output goes to a temp beside the destination and is renamed over it only after the entire input validated and the PDF was flushed, `fsync`ed and closed. The reasoning: the converter is a batch step, so a half-written PDF must never replace a good one, and a late profile error in the last record of a million must not publish a truncated success. The details of the temp lifecycle, the same-file check and symlink behavior are on [Security](../security.md) and [Atomic output](../features/atomic-output.md).

## Selection still validates everything

`record_offset` and `record_limit` choose what gets rendered, not what gets parsed. After the limit is reached the entire input is still read and validated, including skipped records: `report_map` in `src/report.c` keeps counting and grammar-checking skipped records and merely suppresses their output. A selection tool must not turn a corrupt file into a success, so a late error after the selected portion is still an error and publishes nothing.

## No font fallback

An explicit `font_path` or `KI_XML_PDF_FONT` that fails to load is a hard error, never silently replaced with the default. `font_open_default` in `src/font.c` only walks its candidate list when no explicit font was requested. The repertoire is fixed (tab, LF, CR, U+0020 through U+04FF, U+2010 through U+2027), and a character outside it fails with `KI_CAUSE_FONT` and the `U+` code point rather than being dropped. Silent font substitution would change how text renders without anyone noticing; predictability won over convenience.

## Related pages

- [Pitfalls](pitfalls.md) for the traps these decisions create for users.
- [Security](../security.md) for how the bounds and file rules behave as hardening.
- [Lore](../lore.md) for the measurement history behind the memory rule.
- [Cleanup opportunities](../cleanup-opportunities.md) for what the decisions left behind.
