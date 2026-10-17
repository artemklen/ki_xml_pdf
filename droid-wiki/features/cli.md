# CLI
Active contributors: Artem

`xml2pdf` is a thin command-line adapter over the library's configured conversion calls. `src/main.c` handles arguments, stdin/stdout, diagnostics and signals; the library owns parsing, mapping, layout and PDF output.

## Invocation

```text
xml2pdf [options] [--] input.xml output.pdf
xml2pdf -h | --help | --version
```

Options end at the first positional argument, `-`, or `--`; exactly two paths follow. Put options before paths. `--` allows filenames beginning with a hyphen. Help/version print to stdout and return 0; current version output is `xml2pdf 0.3.0 records-v1`.

### Profile and resource options

| Option | Meaning |
| --- | --- |
| `--profile records-v1|report-v1` | Diagnostic default or report grammar |
| `--record-offset N` | Skip the first N report records |
| `--record-limit N` | Select at most N after the offset; 0 means all remaining |
| `--font PATH` | Explicit font, no silent fallback if loading fails |
| `--max-pages N` | Separate page quota; 0 disables it |
| `--max-output-bytes N` | PDF quota; 0 retains only the 9,999,999,999-byte hard ceiling |
| `--max-spool-bytes N` | Offset-file quota; 0 means 64 MiB |
| `--temp-dir DIR` | Offset spool directory, not the temporary PDF location |

These numeric options use `strtol` validation for a nonnegative decimal `long`; page count must also fit `int`. Invalid/trailing/overflowing values return 2. Selection is meaningful only to [report-v1](report-v1.md).

### Geometry and document information

| Option | Meaning |
| --- | --- |
| `--page-size letter|a4` | Named dimensions |
| `--orientation portrait|landscape` | Orientation for named sizes |
| `--page-width PT --page-height PT` | Custom size; excludes both named-size and orientation options |
| `--margin PT` or `--margin T,R,B,L` | All margins or top/right/bottom/left |
| `--font-size PT` | Type size, 1–144 pt |
| `--title TEXT`, `--producer TEXT` | Optional 1–1024-byte UTF-8 Info strings |
| `--creation-date DATE` | Explicit timestamp with `Z` or signed `HH:MM` offset |

Each geometry/metadata option may appear once. Points accept plain unsigned decimals, at most 32 characters, with digits on both sides of a supplied dot. Signs, exponent notation, units, whitespace, NaN and infinity are rejected. Metadata is validated by the library before input is read. See [Page geometry](page-geometry.md) and [Document information](document-information.md).

## Four I/O routes

| Input / output | Configured API in `src/main.c` | Result |
| --- | --- | --- |
| File → file | `ki_convert_file_cfg` | Atomic replacement |
| stdin (`-`) → file | `ki_convert_to_file_cfg` | Atomic replacement |
| File → stdout (`-`) | `ki_convert_cfg` | Non-rollback stream |
| stdin (`-`) → stdout (`-`) | `ki_convert_cfg` | Non-rollback stream |

For callback routes, `cli_read` opens a named input lazily on the first read. This lets invalid configuration fail before opening that input. Stdout is unbuffered during conversion, `cli_write` rejects short writes, and a final flush is checked.

Conversion stdout contains only PDF bytes; diagnostics and the success counters go to stderr. Late failures can leave partial stdout output. A shell redirect can itself create/truncate its target before the converter runs, so stdout redirection is not an atomic file destination.

## Signals and exit status

`SIGINT`/`SIGTERM` set a `sig_atomic_t` flag polled through cancellation. `SIGPIPE` is ignored only for stdout conversion, turning a closed pipe into an I/O failure. Prior handlers are restored after conversion. The library does not install handlers itself.

| Exit | Meaning |
| --- | --- |
| 0 | Success |
| 1 | XML, profile or font error |
| 2 | Usage/configuration error |
| 3 | I/O/compression/publication error |
| 4 | Resource limit or cancellation |

## Examples

```sh
./xml2pdf --profile report-v1 examples/report-v1.xml report.pdf
./xml2pdf --page-size a4 --orientation landscape --title "Export" input.xml a4.pdf
./xml2pdf --page-width 360 --page-height 504 --margin 24 --font-size 12 input.xml small.pdf
cat examples/report-v1.xml | ./xml2pdf --profile report-v1 - - > streamed.pdf
./xml2pdf --profile report-v1 -- --input.xml --output.pdf
```

Use [Atomic output](atomic-output.md) when failure must preserve an existing destination.

## Change entry points and checks

Argument branches, `take_long`, `take_points`, `take_margins`, `usage` and route selection all live in `src/main.c`. Keep help, public config validation and CLI rejection behavior aligned.

The CLI is one C implementation unit with one dedicated scenario runner (`tests/cli_pipeline.py`, a 1:1 file ratio, not code coverage). Geometry/info/wrap runners and `tests/integration_check.py` add independent Poppler checks and route-byte-equivalence tests.

## Key source files

| File | Role |
| --- | --- |
| `src/main.c` | Arguments, lazy input adapter, stdout and signals |
| `include/ki_xml_pdf.h` | Configured API, version and options |
| `tests/cli_pipeline.py` | Routes, usage, partial output, signals and preservation |
| `tests/geometry_check.py`, `tests/info_check.py` | New-option validation before stdin/output |
| `tests/integration_check.py` | Combined features through all routes |

Related: [Streaming I/O](streaming-io.md), [Resource limits](resource-limits.md), [Packaging](packaging.md).
