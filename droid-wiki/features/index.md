# Features

User-visible capabilities of ki_xml_pdf: the CLI, the two built-in profiles, custom schemas, and the streaming, publication, quota and packaging behavior behind them.

| Page | Summary |
| --- | --- |
| [CLI](cli.md) | `xml2pdf` options, stdin/stdout streams, signals, exit codes and which API each mode uses |
| [records-v1](records-v1.md) | The default diagnostic profile: element names, attributes and text with the whitespace and `xml:space` rules |
| [report-v1](report-v1.md) | The report profile: grammar, validation messages, record selection, Russian labels and counters |
| [Custom mappers](custom-mappers.md) | Writing a `KiOptions.map` callback with fixed state, borrowed event pointers and byte-offset diagnostics |
| [Streaming I/O](streaming-io.md) | `read_bytes` and `write_bytes` callbacks, short reads, and the stdin/stdout pipelines |
| [Atomic output](atomic-output.md) | Temporary file, fsync and rename publication, and what survives a failure |
| [Resource limits](resource-limits.md) | Page, output-byte and spool quotas, parser bounds and the RSS gate |
| [Packaging](packaging.md) | Make and CMake installs, CMake and pkg-config discovery, the compiled font path and `DESTDIR` |

For how these features are implemented, see the [systems pages](../systems/index.md); for the public contract they are built on, see the [API](../api/index.md).
