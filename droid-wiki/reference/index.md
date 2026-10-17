# Reference

This section is the lookup shelf: every option, flag, environment variable and compile-time constant in one place, the structs with their fields, and the dependency inventory. Values here come from the headers and build files, not from memory.

| Page | Contents |
| --- | --- |
| [Configuration](configuration.md) | `KiOptions` fields, CLI flags, environment and build variables, compile-time constants |
| [Data models](data-models.md) | Field tables for `KiOptions`, `KiResult`, `KiEvent`, `XmlEvent`, `XmlParser`, `Layout`, `FontFace`, `PdfConfig`, `PdfWriter`, `Report`, `CatalogMap` |
| [Dependencies](dependencies.md) | Runtime, build and test dependencies; CI actions and their versions |

## Quick facts

| Fact | Value | Source |
| --- | --- | --- |
| Version | 0.2.0, single source `KI_XML_PDF_VERSION` | `include/ki_xml_pdf.h` |
| Platform | 64-bit Linux with glibc | `CMakeLists.txt` |
| Language | C99, `-Wall -Wextra -Wpedantic`, `_POSIX_C_SOURCE=200809L` | `Makefile` |
| Exit codes | 0 success, 1 XML/profile/font, 2 usage, 3 I/O, 4 quota/cancel | `src/convert.h`, `src/main.c` |
| Causes | `KI_CAUSE_NONE/XML/USAGE/IO/RESOURCE/FONT/CANCEL/PROFILE` = 0 to 7 | `include/ki_xml_pdf.h` |
| Profiles | `records-v1` (default), `report-v1` | `include/ki_xml_pdf.h`, `src/main.c` |
| Memory ceiling | 4096 KiB (4 MiB) converter RSS on the documented synthetic workload | `tests/large_proof.py` |

## Where to look next

- Public function contracts: [C API](../api/c-api.md) and the comments in `include/ki_xml_pdf.h`.
- Behavior pages: [CLI](../features/cli.md), [report-v1](../features/report-v1.md), [Custom mappers](../features/custom-mappers.md), [Atomic output](../features/atomic-output.md), [Streaming I/O](../features/streaming-io.md), [Resource limits](../features/resource-limits.md), [Packaging](../features/packaging.md).
- Internals: [Conversion pipeline](../systems/conversion-pipeline.md), [XML parser](../systems/xml-parser.md), [Layout and fonts](../systems/layout-and-fonts.md), [PDF writer](../systems/pdf-writer.md).
- Terms: [Glossary](../overview/glossary.md). Release flow: [Release and distribution](../deployment.md).
