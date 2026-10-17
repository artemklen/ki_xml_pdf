# Background
Active contributors: Artem

This section separates implemented design, historical measurements and proposed work. A decision record can explain intent without being a description of the current writer.

## Pages

| Page | Purpose |
| --- | --- |
| [Design decisions](design-decisions.md) | Streaming/state trade-offs, ABI evolution, memory reductions, deferred research and design-only addons |
| [Pitfalls](pitfalls.md) | Integration assumptions that break streams, ownership, selection, fonts, geometry or packaging |

## Read evidence by revision

| Record | Scope |
| --- | --- |
| `docs/contract.md` | Original streaming rationale and explicitly dated early measurements |
| `docs/profile-decision.md` | Project-owned report-v1 choice |
| `docs/license-decision.md` | MIT source licensing versus separate font terms |
| `docs/evidence/osr-23-baseline-075455c/README.md` | Reproducible full-output and fixed-selection baseline |
| `docs/evidence/osr-25-callback-1193cdc/README.md` | Final OSR-25 acceptance on its named revision |
| `docs/evidence/osr-82-heap-profile/README.md` | Heap/RSS snapshot of `de8ba0e`, before subsequent reductions |
| `docs/adr/0001-font-script-addons.md` | Accepted design only; no addon implementation |

A baseline result is not a current benchmark.
A selected-output run does not prove full-output performance at the same input size.
Massif heap and ordinary-process RSS measure different things.
Use [Testing](../how-to-contribute/testing.md) to produce new revision-specific evidence.

## Current versus deferred

The shipping 0.3 API includes word wrapping, page geometry and optional `/Info`.
Subsequent memory work streams the open font file, writes ToUnicode in two passes and reuses one page-content buffer.

The UTF-16, fixed-column table and internal font-subsetting studies did not add supported product features.
Their synthetic success is feasibility evidence, not a consumer requirement.
The addon ADR similarly does not introduce addon fields or mixed-font rendering.

The old page-sink experiment was removed from the implementation.
Its historical role belongs in this background section, not in the current systems map or data-model inventory.

## Navigation

- [Lore](../lore.md): dated development history.
- [Architecture](../overview/architecture.md): current conversion flow and ownership.
- [Security](../security.md): validation boundaries and private reporting.
- [Cleanup opportunities](../cleanup-opportunities.md): current maintenance work rather than already-fixed historical allocations.

## Key source files

| File | Role |
| --- | --- |
| `include/ki_xml_pdf.h` | Actual shipping API/features |
| `src/font.c`, `src/pdf_write.c`, `src/layout.c` | Current memory lifecycle |
| `docs/adr/0001-font-script-addons.md` | Future design with an explicitly older source snapshot |
| `docs/evidence/osr-82-heap-profile/README.md` | Revision-pinned historical heap measurements |
