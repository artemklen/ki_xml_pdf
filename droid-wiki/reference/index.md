# Reference
Active contributors: Artem

This section is the lookup shelf for the 0.3.0 public contract, configuration values, bounded state and external dependencies. `include/ki_xml_pdf.h` is authoritative for callers.

## Pages

| Page | Contents |
| --- | --- |
| [Configuration](configuration.md) | Options/config defaults, geometry, metadata, CLI equivalents and hard bounds |
| [Data models](data-models.md) | Public structs, ownership and important private state |
| [Dependencies](dependencies.md) | Linux/glibc, zlib, build/test tools and licensing |

## Current facts

| Fact | Value | Source |
| --- | --- | --- |
| Library version | 0.3.0 | `include/ki_xml_pdf.h` |
| Supported platform | 64-bit Linux with glibc only | `README.md`, `CMakeLists.txt`, `src/platform.h` |
| Language | C99 core; C/C++ public consumers | `Makefile`, `include/ki_xml_pdf.h` |
| Compression dependency | zlib, including static link flags | `Makefile`, `cmake/ki_xml_pdf.pc.in` |
| Profiles | records-v1 default; report-v1 | `include/ki_xml_pdf.h` |
| New configuration | `KiConfig` geometry and optional `/Info` | `include/ki_xml_pdf.h` |
| Shared ABI | Soname 0; retained older exports plus `KI_0_3` | `libki_xml_pdf.map`, `CMakeLists.txt` |
| CMake version policy | Same minor before 1.0, not cross-minor matching | `cmake/ki_xml_pdfConfigVersion.cmake.in` |
| Synthetic converter RSS gate | ≤4096 KiB on specified ordinary-process workloads | `tests/verify.sh`, `tests/large_proof.py` |

## Contract boundaries

The unchanged 0.2 `KiOptions`, `KiResult` and `KiEvent` layouts coexist with additive 0.3 functions.
Supported `KiConfig` sizes are full current size and the geometry-only prefix.
Defaults omit document metadata and implicit timestamps.

The library is not a full XML implementation or general document-layout engine.
UTF-16 XML, tables, font addons and internal subsetting remain unimplemented/deferred.
UTF-16BE `/Info` serialization is separate from XML input encoding.

File publication is atomic on handled failure; callback/stdout output is partial on failure.
Selection changes rendering, not complete-input validation.
Body glyph support is narrower than accepted metadata Unicode.
Use [C API](../api/c-api.md) for ownership, callbacks and status/cause semantics.

## Navigation

Behavior: [Features](../features/index.md).
Implementation: [Systems](../systems/index.md).
Failure triage: [Debugging](../how-to-contribute/debugging.md).
Measurement provenance: [Background](../background/index.md).
Installed consumers: [Packaging](../features/packaging.md).

## Key source files

| File | Role |
| --- | --- |
| `include/ki_xml_pdf.h` | Public declarations, version and comments |
| `src/convert.c` | Options/config validation and defaults |
| `src/geometry.h`, `src/info.h` | Private normalized geometry/metadata |
| `Makefile`, `CMakeLists.txt`, `cmake/ki_xml_pdf.pc.in` | Build, install and link requirements |
