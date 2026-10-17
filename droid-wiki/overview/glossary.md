# Glossary

These terms describe the parser-to-PDF pipeline and its public contracts. Follow the links for behavior and modification points.

| Term | Meaning and source |
| --- | --- |
| records-v1 | Default diagnostic mapper: element names, attributes and text (`src/convert.c`). See [Records](../features/records-v1.md). |
| report-v1 | Built-in report grammar with fields and record selection (`src/report.c`, `docs/report-v1.md`). |
| Mapper | Callback translating borrowed `KiEvent` values into `ki_doc_text`, `ki_doc_break` or `ki_doc_fail` calls (`include/ki_xml_pdf.h`). |
| Borrowed event | Parser-owned pointers valid only during one mapper callback (`include/ki_xml_pdf.h`). |
| Consumed position | Line/column/byte position after accepted source bytes, not a lookahead position (`src/xml_parse.h`). |
| KiOptions | Per-call I/O, mapper, selection, quota and cancellation settings; initialize its exact size (`include/ki_xml_pdf.h`). |
| KiConfig | Separate size-checked geometry and optional metadata structure, preserving older options/result/event layouts (`include/ki_xml_pdf.h`). |
| mpt | Integer thousandth of a PostScript point; shared by layout and PDF placement (`src/geometry.h`). |
| InkRun | Advance and glyph-outline bounds used to fit text between margins (`src/geometry.h`). |
| Zero-advance mark | Present glyph with width zero, retained with its base during wrapping; no shaping or normalization (`src/font.c`, `src/layout.c`). |
| GID / CID | Font glyph ID / PDF character ID; this writer uses CID = GID (`src/pdf_write.c`). |
| ToUnicode | Uncompressed PDF CMap for mapping GIDs to Unicode, counted then streamed without a CMap buffer (`src/pdf_write.c`). |
| ActualText | PDF marked-content text preserving the original text where GID mapping or whitespace alone is insufficient (`src/pdf_write.c`). |
| Flate | zlib compression for page and complete-font streams, with indirect stream lengths (`src/pdf_write.c`). |
| Offset spool | Temporary on-disk object-offset records used to emit the classic xref without a growing RAM array (`src/pdf_write.c`). |
| Atomic publication | Flush, fsync, close, then rename the temporary PDF beside its destination (`src/convert.c`). |
| Partial output | Already accepted callback/stdout bytes remain after failure (`src/convert.c`). |
| RSS gate | 4096 KiB converter peak RSS on documented synthetic workloads, measured without sanitizers (`tests/verify.sh`, `tests/large_proof.py`). |
| Font-script addon | Accepted future design, not an implemented API (`docs/adr/0001-font-script-addons.md`). |

See [Architecture](architecture.md) for how these pieces connect.
