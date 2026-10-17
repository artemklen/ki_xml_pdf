# API
Active contributors: Artem

`include/ki_xml_pdf.h` is the public C/C++ contract for ki_xml_pdf 0.3.0. It exposes three I/O routes, each with an additive configuration variant, plus initialization and mapper helpers.

## Choose an entry point

| Input → output | Default geometry | Geometry and optional `/Info` |
| --- | --- | --- |
| File → atomic file | `ki_convert_file` | `ki_convert_file_cfg` |
| Callback → atomic file | `ki_convert_to_file` | `ki_convert_to_file_cfg` |
| Callback → callback | `ki_convert` | `ki_convert_cfg` |

File destinations are published only after successful full-input validation and completion.
Callback output is not transactional: accepted bytes remain on failure.
The CLI's four file/stdin → file/stdout combinations are adapters over these routes.

## Configuration objects

| Type | Role |
| --- | --- |
| `KiOptions` | Profile/custom mapper, font, quotas, selection, cancellation, logging and callback I/O |
| `KiConfig` | Page size/orientation, margins, type size and optional Title/Producer/CreationDate |
| `KiResult` | Status/cause/message, output/spool/page/object counts and report counters |
| `KiEvent` | Borrowed mapper event with consumed byte position |
| `KiDoc` | Opaque mapper output/failure handle |

Always call `ki_options_init` and `ki_config_init` rather than inventing size values.
A NULL config preserves the older calls' Letter/40 pt/10 pt defaults without `/Info`.
A NULL options pointer is usable for file-to-file defaults; callback routes require their input/output callbacks.

## Compatibility boundary

Version 0.3 keeps the 0.2 `KiOptions`, `KiResult` and `KiEvent` layouts.
`KiConfig` is separate, with full-size and geometry-only accepted forms.
`libki_xml_pdf.map` retains `KI_0_1` and `KI_0_2`, adding four functions under `KI_0_3`.
The shared soname remains `libki_xml_pdf.so.0`.

This specific continuity does not override the general pre-1.0 policy: a new 0.y minor may change ABI.
Installed CMake package matching deliberately stays within the same minor.
Use [Packaging](../features/packaging.md) for discovery and static zlib linkage.

## Detailed contracts

- [C API](c-api.md): signatures, initialization, ownership, callbacks, error/cancellation and ABI.
- [Configuration](../reference/configuration.md): exact defaults, limits, CLI equivalents and metadata rules.
- [Data models](../reference/data-models.md): public and bounded private state.
- [Custom mappers](../features/custom-mappers.md): a public-only schema adapter.
- [Streaming I/O](../features/streaming-io.md) and [Atomic output](../features/atomic-output.md): publication semantics.
- [Page geometry](../features/page-geometry.md) and [Document information](../features/document-information.md): 0.3 additions.

## Key source files

| File | Role |
| --- | --- |
| `include/ki_xml_pdf.h` | Normative public declarations and comments |
| `src/convert.c` | Entry points, mapper bridge and error/publication lifecycle |
| `libki_xml_pdf.map` | Versioned exported symbols |
| `examples/consume.c`, `examples/consume.cpp` | Minimal C/C++ callers |
| `examples/catalog_map.c` | Caller-owned custom mapper |
