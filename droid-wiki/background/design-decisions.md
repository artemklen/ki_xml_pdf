# Design decisions
Active contributors: Artem

The project favors bounded sequential text conversion over a general document-layout engine. Current code, decision records and synthetic research have different authority.

## Own pipeline, narrow schema

`docs/contract.md` records the decision to continue the project's C pipeline.
The reason is direct control over parser/mapper/page state—not a measured speed or memory win over Apache FOP.
The original problematic converter was not supplied and no such comparison was performed.

`docs/profile-decision.md` selects report-v1 as a project-owned grammar.
`records-v1` remains the default diagnostic rendering.
Unknown third-party schemas require an explicit [custom mapper](../features/custom-mappers.md), not a compatibility claim.

No tables, global sorting, aggregates, forward references or whole-document model ship.
Record selection suppresses rendering only; the parser and profile still validate skipped input and the complete tail.

## Bounded state with disk growth

The parser uses a 4096-byte window and a depth-64 stack.
Layout holds at most 50 lines of 1024-byte storage plus bounded pending-word/UTF-8 state.
Report mapping retains counters and one bounded pending field name.

PDF xref offsets grow on disk, not in an in-memory per-object array.
The writer reconstructs page references from a fixed formula.
At this revision, eight initial object slots precede three objects per page:
content `9 + 3*i`, compressed-length object `+1`, page dictionary `+2`.
Optional `/Info` is appended after page objects.
This is not an arbitrary object allocator or a retained list of page IDs.

The cost is bounded spool space and append-only output complexity.
The hard cap is 8,000,000 objects, with a 64 MiB default spool quota.
Every emitted byte counts against the classic-xref ceiling of 9,999,999,999 bytes.

## Current memory lifecycle

`src/font.c` temporarily reads validated table spans, derives fixed repertoire metrics, then frees those spans.
`FontFace` retains an open file, source path, size and FNV-1a hash, not a full TTF copy.
Before embedding, the same open file is rechecked and streamed to Flate in 16 KiB chunks.

`src/pdf_write.c` emits ToUnicode by running the same bounded formatter twice:
first count, then write with a direct `/Length`.
It uses fixed automatic scratch arrays, including the 8 KiB GID-seen bitmap.
There is no heap CMap body or heap bitmap; the CMap remains uncompressed.
First/lowest code-point mapping per GID is preserved.

One zlib state/output window is reset and reused across compressed font/page streams.
One worst-case page-content buffer is allocated lazily and reused for the conversion.
`src/layout.c` passes its stored lines through page begin/line/end calls, without joining them into another page copy.
Bounded state does not mean no heap allocation or fixed RSS for every possible font.

The Massif snapshot in `docs/evidence/osr-82-heap-profile/README.md` measured `de8ba0e`.
Its full-font, CMap and joined-page assumptions must not be presented as current allocations.
The 4096 KiB ordinary-process synthetic gate is current acceptance policy, not a fresh Massif result.

## Linux/glibc and zlib deliberately remain dependencies

Only 64-bit Linux/glibc is supported.
GNU cookie streams let file and callback output share counted `FILE *` writing.
The code also uses Linux executable-relative font discovery, ELF version scripts and GNU linker test wrappers.

zlib compresses the full embedded font and page content.
Static consumers must include `-lz`; installed metadata carries that dependency.
The runtime is not dependency-free.
See [Dependencies](../reference/dependencies.md).

## Atomic files versus partial streams

A temporary beside the destination is renamed only after complete input validation, flush/fsync/close and cancellation checks.
That prevents a late error from replacing a good file.
No equivalent rollback is possible for callback/stdout output, which can receive font bytes before input is read.

Explicit fonts never silently fall back.
Unsupported body code points fail rather than becoming missing glyphs.
Zero-advance marks are valid; wrapping keeps them with the preceding base without adding shaping or composition.

## Additive configuration and reproducible metadata

0.3 geometry and `/Info` use `KiConfig`, preserving 0.2 options/result/event layouts and older ELF exports.
The geometry-only size remains accepted alongside the full current config.
CMake nevertheless keeps the conservative pre-1.0 same-minor package policy.

No CreationDate is inferred from the clock.
Absent metadata produces no `/Info`; there is no implicit XMP, trailer ID, PDF/A or accessibility-conformance claim.
This avoids hidden nondeterminism and claims the converter cannot establish.

## Accepted addon design is not implemented

`docs/adr/0001-font-script-addons.md` proposes caller-supplied Unicode ranges, lazy first-use fonts, mixed-face scaling and a different page-ID strategy.
The public header has no addon list or `KiFontAddon`.
The writer still uses one base face and the fixed page formula above.

The ADR explicitly describes its older `9595c0e` snapshot.
Its buffered-CMap passages and proposed stored page IDs are not current behavior.
Any implementation needs a separate API/numbering/layout/test change.

## Deferred research and removed experiment

| Work | Decision and boundary |
| --- | --- |
| `tests/research_font_subset.py` | No-go for an internal subsetter on the studied large-report goal; smaller caller-supplied fonts are possible |
| `tests/research_utf16.py`, `tests/research_utf16_adapter.c` | Decoder/adapter feasibility only; shipping input remains UTF-8 |
| `tests/research_table.py` | Bounded-table feasibility with explicit caps; no table API/profile ships |
| Removed page-sink experiment | Older byte-grid buffer outside the active font-aware path; removed rather than maintained as a second renderer |

The subset spike's missing-outline negative control extracted the same text but rendered differently.
Any future subsetter therefore needs visual as well as text oracles.
Deferred features require concrete permitted consumer inputs and separate acceptance work, not automatic promotion from synthetic research.

## Key source files

| File | Role |
| --- | --- |
| `src/xml_parse.h`, `src/layout.h`, `src/report.h` | Bounded state |
| `src/font.c`, `src/pdf_write.c` | Current font/CMap/compression/object design |
| `include/ki_xml_pdf.h`, `libki_xml_pdf.map` | Additive API and ABI |
| `docs/adr/0001-font-script-addons.md` | Design-only extension |
| `docs/license-decision.md`, `fonts/DejaVuSans.copyright` | Source/font licensing distinction |

Related: [Pitfalls](pitfalls.md), [Lore](../lore.md), [PDF writer](../systems/pdf-writer.md).
