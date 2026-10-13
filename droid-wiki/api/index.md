# API

ki_xml_pdf exposes one C header, `include/ki_xml_pdf.h`: three conversion entry points, one mapper callback interface, and fixed options and result structs. The same header works from C and C++ (`extern "C"` guarded), and the shared library exports it through the versioned symbol script `libki_xml_pdf.map`.

| Page | Summary |
| --- | --- |
| [C API](c-api.md) | Full reference: types, fields, functions, constants, return codes, ownership and threading rules, examples |

Feature pages that use the API:

- [records-v1](../features/records-v1.md) and [report-v1](../features/report-v1.md) for the built-in profiles and their options.
- [Custom mappers](../features/custom-mappers.md) for the `KiOptions.map` callback and the catalog example.
- [Streaming I/O](../features/streaming-io.md) for the `read_bytes` / `write_bytes` callbacks and [Atomic output](../features/atomic-output.md) for the file publication lifecycle.
- [Packaging](../features/packaging.md) for consuming an installed library through CMake or pkg-config.

For how the API is implemented internally, see [Conversion pipeline](../systems/conversion-pipeline.md) and [Architecture](../overview/architecture.md).
