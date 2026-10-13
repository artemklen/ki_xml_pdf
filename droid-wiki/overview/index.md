# ki_xml_pdf overview

ki_xml_pdf is a C99 library and command-line tool (`xml2pdf`) that converts very large XML inputs to text PDFs while keeping converter memory bounded. The parser, the mapper, the layout and the PDF writer all use fixed-size state, so a 1 GiB XML file converts in a few megabytes of RSS.

## What it does

- Reads XML through a 4096-byte window and emits start, end and text events (`src/xml_parse.c`).
- Maps those events to text with one of two built-in profiles (`records-v1`, `report-v1`) or a caller-supplied mapper callback (`src/convert.c`, `src/report.c`).
- Wraps text with real glyph widths from the bundled DejaVu Sans font and fills Letter pages (`src/layout.c`, `src/font.c`).
- Writes an append-only PDF with an embedded TrueType font, spooling object offsets to a temporary file instead of keeping them in RAM (`src/pdf_write.c`).
- Publishes file output atomically: a temporary PDF next to the destination, then `fsync` and `rename` (`src/convert.c`).

Version 0.2.0 is declared in `include/ki_xml_pdf.h` and is the single version source for the CLI, CMake, pkg-config and the source archive.

## Who uses it

- **Application developers** embed `libki_xml_pdf` (static or shared) through the public header `include/ki_xml_pdf.h`, CMake (`find_package(ki_xml_pdf)`) or pkg-config.
- **Operators and scripts** run `xml2pdf` with files or stdin/stdout pipelines (`src/main.c`).
- **Schema owners** write a mapper callback for their own XML format, following `docs/mapper-guide.md` and the catalog example in `examples/catalog_map.c`.

The supported platform is 64-bit Linux with glibc. The CMake build refuses other targets because `ki_convert` depends on glibc's `fopencookie`.

## Quick links

| Topic | Page |
| --- | --- |
| How the pieces fit together | [Architecture](architecture.md) |
| Build, run and test | [Getting started](getting-started.md) |
| Terms such as profile, mapper, spool | [Glossary](glossary.md) |
| The conversion core | [Systems](../systems/index.md) |
| CLI, profiles, atomic output, quotas | [Features](../features/index.md) |
| Public C API | [API](../api/index.md) |
| How to change the code | [How to contribute](../how-to-contribute/index.md) |

## Documentation in the repository

| File | Contents |
| --- | --- |
| `README.md` / `README.ru.md` | English and Russian onboarding, install and API summary |
| `CONTRIBUTING.md` | Bug and security reporting, check expectations |
| `docs/report-v1.md` | Grammar and rendering contract for the `report-v1` profile |
| `docs/mapper-guide.md` | How to write a custom-schema mapper |
| `docs/contract.md` | Historical `records-v1` contract and measurements (Russian) |
| `docs/release-checklist.md` | Source distribution acceptance gates for 0.2.0 |
| `docs/profile-decision.md`, `docs/license-decision.md` | Recorded decisions on the profile and MIT license (Russian) |
| `CHANGELOG.md` | Release notes for 0.1.0 and 0.2.0 |
