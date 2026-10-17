# Features

ki_xml_pdf 0.3.0 renders sequential XML text reports with bounded converter state on 64-bit Linux/glibc. These pages describe user-facing behavior; the [systems pages](../systems/index.md) explain its implementation.

| Page | Use it for |
| --- | --- |
| [CLI](cli.md) | Options, pipelines, signals and exit codes |
| [records-v1](records-v1.md) | Default diagnostic rendering of element names, attributes and text |
| [report-v1](report-v1.md) | Report grammar, field labels, explicit selection and totals |
| [Custom mappers](custom-mappers.md) | Adapting an application-owned schema through the public API |
| [Page geometry](page-geometry.md) | Letter/A4/custom pages, margins, type size and ink fitting |
| [Document information](document-information.md) | Optional safe PDF Title, Producer and explicit CreationDate |
| [Streaming I/O](streaming-io.md) | Short reads, callback ownership, accepted bytes and latched failures |
| [Atomic output](atomic-output.md) | Temporary files, fsync, rename and preserved destinations |
| [Resource limits](resource-limits.md) | Fixed state, quotas, xref ceiling and synthetic RSS gates |
| [Packaging](packaging.md) | Installed C/C++ use, CMake/pkg-config, zlib and additive ABI |

Word wrapping belongs to [Layout and fonts](../systems/layout-and-fonts.md): words break at spaces/tabs, long words use a bounded code-point fallback, and the pending word shares the existing line buffer. There is no table layout, XSL-FO, shaping engine, UTF-16 XML input or automatic mapping for unknown business schemas.

## Key source files

| File | Role |
| --- | --- |
| `include/ki_xml_pdf.h` | Public feature/configuration contract |
| `src/main.c` | CLI exposure |
| `src/convert.c`, `src/report.c` | Built-in profiles, dispatch and I/O/publication |
| `src/geometry.c`, `src/info.c` | Geometry and document-information validation |

Start with [Getting started](../overview/getting-started.md), [C API](../api/c-api.md) or [Configuration](../reference/configuration.md).
