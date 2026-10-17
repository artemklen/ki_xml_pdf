# Glossary

Terms used in the code, the docs and this wiki.

| Term | Meaning |
| --- | --- |
| **Profile** | A built-in way to turn XML events into text. Selected by `KiOptions.profile` or `--profile`. Two exist: `records-v1` and `report-v1`. |
| **records-v1** | Default diagnostic profile. Prints each element name on its own line, each attribute as `name: value`, then text. Implemented by `map_event` in `src/convert.c`. |
| **report-v1** | Project-owned report schema (`report` / `record` / `field`) with record selection and summary counters. Implemented in `src/report.c`, specified in `docs/report-v1.md`. |
| **Mapper** | A callback `int map(void *user, KiDoc *doc, const KiEvent *ev)` set in `KiOptions.map`. It replaces the built-in profiles and emits text through `ki_doc_text` / `ki_doc_break`. |
| **KiDoc** | Opaque handle a mapper writes to. Wraps the `Layout` and holds the mapper's error and cause (`src/convert.c`). |
| **KiEvent** | Public event passed to a mapper: kind, element name, up to 8 attributes, text fragment, `from_cdata`, `byte_offset`. Pointers are borrowed for the callback only. |
| **XmlEvent** | Internal parser event with inline buffers (`src/xml_parse.h`). `convert_streams` turns it into a `KiEvent`. |
| **Text fragment** | One `TEXT` event, at most 1023 bytes. Field values can span many fragments; nothing collects a whole field. |
| **Selection** | `record_offset` / `record_limit` in `report-v1`. Skipped records are still parsed and validated. |
| **Spool / offset file** | Temporary file where the PDF writer stores 8 bytes per object offset, used to build the xref table at the end. Limited by `max_spool_bytes` (default 64 MiB). |
| **Atomic publication** | File output is written to `<dest>.XXXXXX`, flushed, `fsync`ed, closed and renamed over the destination only on success. |
| **Partial output** | Callback or stdout output cannot be rolled back; a late failure leaves an incomplete PDF. |
| **Cause** | `KiResult.cause`: `KI_CAUSE_XML`, `USAGE`, `IO`, `RESOURCE`, `FONT`, `CANCEL`, `PROFILE`. Finer than the exit status. |
| **Status / exit code** | 0 OK, 1 XML/profile/font, 2 usage, 3 I/O, 4 resource or cancel (`XML2PDF_ERR_*` in `src/convert.h`). |
| **Repertoire** | The code points the layout accepts: U+0009, U+000A, U+000D, U+0020–U+04FF, U+2010–U+2027. Anything else is a font error with its `U+` code. |
| **Oracle** | An independently produced expected text (for example `examples/report-v1.expected.txt`) that tests compare against `pdftotext` output. |
| **RSS gate** | The acceptance rule that peak resident memory of `xml2pdf` stays at or below 64 MiB on the synthetic workloads. |
| **distcheck** | `make distcheck`: build, install and test from the `git archive` of HEAD, without a Git checkout (`tests/distcheck.sh`). |
| **OSR-n / KW-n** | Task identifiers from the owner's tracker that appear in commit messages and docs (for example `OSR-4`, `KW-1022`). |
| **Page sink** | Removed older fixed byte-grid page buffer (`src/page_sink.c`). Not used by the conversion path. |
