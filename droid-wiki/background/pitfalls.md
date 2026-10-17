# Pitfalls
Active contributors: Artem

These are integration traps in the current API, not proposed features. Check the public header before carrying assumptions from older evidence or experimental tools into an application.

## Streams are not transactions

`ki_convert` and `ki_convert_cfg` leave accepted callback bytes in place on failure.
The font is emitted before input is read, so even an immediate XML error can leave output.
Do not accept a PDF merely because it starts with `%PDF-`; require status 0.
Use the callback-to-file route when publication must preserve an existing destination.

A short positive read is data, not EOF.
A write callback must accept the entire request; short/zero/negative/over-count returns fail.
The first failed write is terminal and is never retried.
`output_bytes` is delivered-byte accounting, not a recovery cursor or proof of a complete PDF.

## Size fields are contracts

Call `ki_options_init`; zeroing `KiOptions` alone leaves an invalid size.
Call `ki_config_init` with the actual supported allocation size.
Only full current `sizeof(KiConfig)` and `KI_CONFIG_SIZE_GEOMETRY` are accepted.
A geometry-only config has no metadata tail.

0.3 preserves 0.2 options/result/event layouts, but pre-1.0 policy permits future minor ABI changes.
CMake therefore rejects a request from a different minor, even though concrete 0.2 binaries have canary coverage against this library.
Package version matching and proven binary continuity are not the same claim.

## Borrowed events and fragmented text

`KiEvent` strings live only during the mapper callback.
Copy bounded metadata; never retain its text/name pointers.
The public event has a byte offset, not line/column fields.

The parser emits long values in chunks and does not split valid UTF-8 scalars.
Custom mapper calls may still split a scalar across `ki_doc_text` calls; layout holds bounded continuation state.
Do not trim each fragment: spaces between TEXT, CDATA and references can be significant.
CDATA preserves reference spelling; ordinary text references are decoded.

`xml:space` is profile-specific.
Records rendering inherits preserve state; report fields keep their value text and validate the allowed attribute values.
A custom mapper owns its own whitespace/schema rule.

## Selection is not early termination

`record_offset`/`record_limit` affect built-in report rendering.
They do not skip parsing, grammar checks, counters or invalid tails.
A late error beyond the selected portion still fails and prevents file publication.
With a custom mapper, selection and report counters are the mapper's responsibility.
Do not use selected-output resource evidence as a full-output benchmark.

## Quota zero does not mean unlimited

Zero page quota adds no separate page cap.
Zero output quota still obeys the 9,999,999,999-byte classic-xref ceiling.
Zero spool quota selects the 64 MiB default, not unlimited disk.
The 8,000,000-object cap always applies.

The 4096 KiB RSS gate is synthetic ordinary-converter acceptance, not an allocator quota.
Fonts can be as large as 8 MiB, loading uses temporary table spans, and callbacks can allocate their own memory.
Sanitizer RSS and historical heap snapshots are not substitutes for ordinary current-process measurements.

## Font identity is not fallback

An explicit path or nonempty `KI_XML_PDF_FONT` that fails is an error.
A valid larger Unicode font does not expand the fixed body repertoire.
Zero advance is present; GID 0/missing is not.
No shaping, bidi, composition, internal subsetting or addon font switching ships.

The open font handle survives metric loading.
Do not mutate its file during conversion or assume changing a pathname changes the already-open font.
The FNV check is a consistency check, not cryptographic authentication or a concurrency lock.

## Geometry and metadata are separate from options

Custom dimensions require both sides and portrait orientation; named sizes require zero custom sides.
Margins must leave positive width/height after rounding.
Font fitting can reject valid numeric geometry before input callbacks if a line or repertoire glyph cannot fit.
Layout considers negative bearings and ink, not only advances.

`title` and `producer` are metadata, not rendered body text; they do not require body-font glyphs.
Empty strings are invalid rather than a synonym for omission; use NULL.
Dates must be supplied explicitly; the library does not add today's date or claim PDF/A.
UTF-16BE metadata serialization does **not** mean UTF-16 XML input is supported.

## Prefixes, temporaries and publication

`PREFIX`/CMake's install prefix affects the compiled font path.
`DESTDIR` only stages files.
Static archive users must link zlib; pkg-config needs `--static` for private dependency flags.

`temp_dir` affects the spool, not the beside-destination temporary PDF.
Rename replaces the destination entry, including a symlink; it is not writing through the symlink.
Atomic replacement is not directory-fsync power-loss durability.
SIGKILL cannot clean temporary files, and cancellation after rename cannot restore the previous output.

## Development and release traps

Internal debug hooks are process globals, not public thread-safe controls.
Public conversions do not clear the conversion hooks; tests must reset them explicitly.
Ordinary independent contexts are supported without armed debug hooks.

`make dist` archives committed HEAD, ignoring uncommitted fixes and version bumps.
A green `main` does not create a release.
The tag workflow checks exact version naming and provenance, but does not rerun CI or check main-green status.
The maintainer must verify those preconditions first.

## Key source files

| File | Role |
| --- | --- |
| `include/ki_xml_pdf.h`, `src/convert.c` | Lifetime, size, callback and publication contracts |
| `src/font.c`, `src/layout.c`, `src/geometry.c` | Font/fragment/ink behavior |
| `src/pdf_write.c`, `src/info.c` | Quotas and metadata |
| `Makefile`, `.github/workflows/release.yml` | Archive and tag behavior |

Related: [Debugging](../how-to-contribute/debugging.md), [C API](../api/c-api.md), [Design decisions](design-decisions.md).
