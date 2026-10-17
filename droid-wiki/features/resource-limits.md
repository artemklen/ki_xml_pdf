# Resource limits
Active contributors: Artem

Converter state stays bounded as XML and page counts grow: fixed parser/mapper/layout storage, a reused content buffer/compressor, an open font with fixed metrics, and disk-spooled offsets. The current synthetic-workload acceptance gate is 4096 KiB converter RSS, not a universal memory guarantee for every font, custom mapper or host.

## What stays fixed, what grows

| Component | Current storage strategy |
| --- | --- |
| Parser | 4096-byte input window, bounded event and depth stack; no tree |
| Built-in mappers | Fixed flags/counters and bounded current metadata |
| Layout | 1024-byte shared line/pending-word buffer, fixed 50-line page grid |
| PDF content | One lazy 416256-byte buffer reused for the conversion, no joined-page copy |
| Compression | One reused zlib state and 16 KiB output window |
| Font | Temporary table spans at initialization, then fixed metrics plus retained open file; 16 KiB streamed embedding |
| ToUnicode | Fixed stack arrays, count pass then direct write; no heap CMap |
| Object offsets | Disk file with 8-byte slots, grows with object count |
| PDF and elapsed work | Grow with rendered content; skipped records still require parsing |

The eight library C implementation units have eleven `tests/test_*.c` files (11:8 file ratio, excluding helpers/fixtures), plus Python/shell oracles. Test files share one C harness; this ratio is not a coverage percentage. The memory/resource feature crosses all units rather than owning a separate source module.

Font loading can temporarily allocate large tables and an external mapper can allocate arbitrary memory. The built-in bounded-state design does not constrain caller-owned readers/writers or make all font workloads fit the synthetic RSS gate.

## Hard bounds

| Bound | Value / source |
| --- | --- |
| XML nesting / attributes | 64 / 8, `src/xml_parse.h` |
| Element and attribute names | 63 UTF-8 bytes |
| Decoded attribute value | 255 UTF-8 bytes |
| Parser text fragment / layout line content | At most 1023 bytes each |
| Font file | 12 bytes through 8 MiB, `src/font.c` |
| Body repertoire slots | U+0000–U+04FF and U+2010–U+2027, subject to XML legality/font coverage |
| Page lines | At most 50, `src/geometry.h` |
| PDF object IDs | Positive and strictly below 8,000,000, `src/pdf_write.c` |
| Total emitted PDF bytes | At most 9,999,999,999, `src/pdf_write.h` |
| Info text | At most 1024 UTF-8 bytes per Title/Producer, `include/ki_xml_pdf.h` |

Text fragments/lines do not impose a field or word size limit. Long words split incrementally; an unsplittable base/zero-advance-mark cluster over a line's byte/width bound is a resource error.

Each page consumes three object IDs plus the fixed eight-object block; Info adds one object. Offsets are ten-digit classic-xref fields, so the total-output ceiling applies even with the user output quota disabled.

## Caller quotas

| Option / `KiOptions` field | Zero means | Enforcement |
| --- | --- | --- |
| `--max-pages` / `max_pages` | No separate page quota | `pdf_page_begin` before page emission |
| `--max-output-bytes` / `max_output_bytes` | Only hard xref ceiling | `account_bytes` before forwarding any chunk |
| `--max-spool-bytes` / `max_spool_bytes` | 64 MiB, not unlimited | `note_offset`, projected `(object_id + 1) * 8` |
| `--temp-dir` / `temp_dir` | NULL/empty chooses `/tmp` | Offset spool creation |

Output quota includes font, page streams, Info, xref and trailer. A chunk crossing the budget is rejected whole, not partially forwarded up to the cap. An exact finished-PDF-size budget succeeds; one byte less fails. File failure preserves the old destination; callback `output_bytes` reports accepted bytes, which can be less than internal buffered counts.

Negative quotas are usage errors. Page/object/output/spool limits and layout-cluster exhaustion return status 4 / `KI_CAUSE_RESOURCE`. Cancellation also returns 4 but uses `KI_CAUSE_CANCEL`. Non-memory zlib failure is status 3 / I/O. See [Conversion pipeline](../systems/conversion-pipeline.md).

## Current RSS gates

`tests/verify.sh` measures the ordinary converter with `/usr/bin/time`, excluding generator and PDF-reader processes. Its default-profile large input is at least 16 MiB. The gate requires:

- Converter RSS ≤4096 KiB.
- Large RSS ≤small-reference RSS +4096 KiB.
- Large RSS below half the input size.
- Output larger than converter RSS, alongside completeness/PDF checks.

`tests/resource_baseline.py` separates full-output 1/16/64 MiB series for both profiles from fixed-1000 selection on 16/256/1024 MiB inputs. It measures converter, generator, reader and streaming oracle independently and keeps hashes/environment/commands. `tests/large_proof.py` checks a distinct 1 GiB selection recipe, with 4096 KiB converter RSS, ≤10,000 pages, ≤256 MiB PDF and ≤64 MiB spool; selected-page Poppler reads have their own 1 GiB address-space limit.

ASan/UBSan RSS is not used as production RSS evidence. The reader's address-space limit, reader RSS, converter RSS and disk size are different measures.

## Revision-scoped evidence

`docs/evidence/osr-25-callback-1193cdc/README.md` records the final callback-fixed 0.3.0 run on **1193cdc**, not a rerun of current HEAD. Its nine resource cases recorded:

| Series | Converter RSS | Output facts |
| --- | --- | --- |
| Full output | 2804–2976 KiB | 64 MiB records: 115,552 pages / 112,864,984 PDF bytes; report: 39,033 pages / 48,620,561 bytes |
| Select 1000 from 16/256/1024 MiB | 2764–2892 KiB | 41 pages, about 461 KB PDF |

The comparison baseline is **075455c**. Fixtures/options/font were matched; 16 MiB report output was 14.44% of its baseline size after Flate, with extra compression CPU time. Full output from 1 GiB was not measured. Selection numbers must never be presented as full-output conversion.

Historical evidence can contain older acceptance budgets; current runner constants and CI are authoritative for today's 4096 KiB gate. The OSR-82 heap profile at **de8ba0e** also predates later font/CMap/page-buffer memory reductions; it is not a measurement of today's allocation layout.

## Change entry points and checks

Change quotas/accounting in `src/pdf_write.c`, validation/cause mapping in `src/convert.c`, parser bounds in `src/xml_parse.h`, and page/repertoire bounds in their shared headers. Retain the one-buffer and streaming invariants when changing output syntax.

`tests/test_pdf_bounds.c` covers exact quotas and hard ceilings without generating a 10 GB file; `tests/test_flate.c` covers compression expansion/error controls. `tests/test_callback_io.c` checks accepted-byte failures. Runner unit tests and negative controls verify the measurement/oracle logic itself. Reproduce using [Testing](../how-to-contribute/testing.md), on disk-backed scratch with sufficient space.

## Key source files

| File | Role |
| --- | --- |
| `src/pdf_write.c`, `src/pdf_write.h` | Fixed buffers, compressor, object/output/spool limits |
| `src/convert.c`, `include/ki_xml_pdf.h` | Public quotas, cancellation and accepted-byte results |
| `src/xml_parse.h`, `src/layout.h`, `src/font.h`, `src/geometry.h` | Fixed storage bounds |
| `tests/verify.sh`, `tests/large_proof.py` | Converter RSS and selection gates |
| `tests/resource_baseline.py`, `tests/resource_controls.py` | Reproducible series and negative controls |
| `docs/evidence/osr-25-callback-1193cdc/README.md` | Revision-scoped final comparison |

Related: [PDF writer](../systems/pdf-writer.md), [Layout and fonts](../systems/layout-and-fonts.md), [Atomic output](atomic-output.md), [Design decisions](../background/design-decisions.md).
