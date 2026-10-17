# ki_xml_pdf

ki_xml_pdf is a C99 library and the `xml2pdf` command-line tool. It converts a documented UTF-8 XML subset into sequential PDF text without retaining the input document or a growing list of pages. The current version is 0.3.0, defined in `include/ki_xml_pdf.h`.

## Capabilities

- [Report rendering](../features/report-v1.md) validates a small report grammar and supports record offset/limit selection. Skipped records and the rest of the input are still validated.
- [Diagnostic rendering](../features/records-v1.md) prints element names, attributes and text.
- [Custom mappers](../features/custom-mappers.md) adapt other schemas through borrowed parser events and the public text API.
- [Streaming I/O](../features/streaming-io.md) accepts callbacks and stdin/stdout. [File destinations](../features/atomic-output.md) are published only after successful completion.
- [Page geometry](../features/page-geometry.md), word wrapping and optional [document information](../features/document-information.md) are configured separately from the older options structure.
- Page content and the complete embedded font use Flate compression. Object offsets are spooled to disk. See the [PDF writer](../systems/pdf-writer.md).

The implementation lives in `src/`; its public contract is `include/ki_xml_pdf.h`. Working examples are in `examples/`, and `docs/report-v1.md` and `docs/mapper-guide.md` describe the supported grammars.

## Supported environment

The supported platform is **64-bit Linux with glibc**, GCC or Clang, and a GNU-compatible linker. zlib is required. GNU cookie streams, `/proc/self/exe` font discovery and ELF symbol versioning are deliberate platform dependencies. macOS, musl/Alpine, BSD and Windows are unsupported; see `README.md`, `src/platform.h` and `CMakeLists.txt`.

The converter RSS acceptance ceiling is **4096 KiB on the documented synthetic workloads and Linux environment**, not a guarantee for arbitrary fonts, inputs or machines. Historical measurements remain tied to their source revisions. Start with [Resource limits](../features/resource-limits.md).

## Non-goals

The current library does not provide tables, sorting, XSL-FO, UTF-16 input, shaping, bidi or automatic support for arbitrary schemas. Optional font-script addons are an accepted **design only**, not a public API or implemented feature (`docs/adr/0001-font-script-addons.md`).

## Navigation

- [Architecture](architecture.md) and [Getting started](getting-started.md)
- [Systems](../systems/index.md) and [Features](../features/index.md)
- [C API](../api/c-api.md) and [Configuration](../reference/configuration.md)
- [Contribution workflow](../how-to-contribute/index.md) and [Testing](../how-to-contribute/testing.md)
- [History](../lore.md) and [Codebase statistics](../by-the-numbers.md)
