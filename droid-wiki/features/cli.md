# CLI

Active contributors: Artem Klen

`xml2pdf` is the command-line front end of the library: a thin argument parser in `src/main.c` over the three public conversion entry points. It supports the same two profiles, quotas and atomic publication as the library, plus stdin/stdout pipelines, `-` placeholders and signal-driven cancellation.

## Purpose

Give operators and scripts direct access to the converter: convert files, pipe XML through stdin, write PDF bytes to stdout, select records, cap resources, and get distinguishable exit codes. The CLI adds no conversion logic of its own; every behavior comes from `KiOptions` and the functions declared in `include/ki_xml_pdf.h`.

## Key abstractions

| Abstraction | Path | Role |
| --- | --- | --- |
| `main` | `src/main.c` | Argument loop, mode selection, exit codes |
| `usage` | `src/main.c` | Help text; prints profile names, bounds and exit codes |
| `take_long` | `src/main.c` | Decimal option parsing; rejects negatives, overflow and trailing characters |
| `cli_read` / `cli_write` | `src/main.c` | `KiOptions.read_bytes` / `write_bytes` adapters over stdio |
| `cli_cancelled` | `src/main.c` | `KiOptions.cancelled` polling a `sig_atomic_t` flag set by `on_stop` |
| `cli_log` | `src/main.c` | `KiOptions.log` adapter; every library message goes to stderr |

## Command line

```
xml2pdf [options] [--] input.xml output.pdf
xml2pdf -h | --help | --version
```

Options are parsed only until the first argument that does not begin with `-`, or that is exactly `-` (the stream placeholder). `--` ends option parsing so paths may begin with a hyphen. Options after the first path argument are treated as paths, and `-h`, `--help` and `--version` are recognized only while options are being parsed. After options, exactly two positional arguments are required: input and output.

| Option | Meaning |
| --- | --- |
| `--profile NAME` | `records-v1` (default) or `report-v1`; any other name is a usage error |
| `--record-offset N` | Skip the first N `report-v1` records |
| `--record-limit N` | Select at most N records after the offset (0: all) |
| `--font PATH` | Use only this font, with no fallback |
| `--max-pages N` | Page quota (0: no separate quota) |
| `--max-output-bytes N` | PDF byte quota |
| `--max-spool-bytes N` | Offset-spool quota (0: 64 MiB) |
| `--temp-dir DIR` | Directory for the offset spool file |

`take_long` accepts decimal numbers only: an empty string, trailing characters, a negative value, or a value that does not fit in `long` (for example a 100-digit number) is a usage error with exit 2. `--max-pages` must also fit in an `int`, so 2147483648 is rejected. `--record-offset` and `--record-limit` are meaningful only for `report-v1`; `records-v1` ignores them.

## How it works

1. Parse options as above. Any problem prints the usage text to stderr and returns exit 2, with nothing on stdout.
2. Fill a `KiOptions` through `ki_options_init`, then set `font_path`, `record_offset`, `record_limit`, `profile`, `temp_dir`, `max_pages`, `max_output_bytes`, `max_spool_bytes`, the `cancelled` and `log` callbacks.
3. Choose the entry point from the `-` placeholders:

| Arguments | API called | Publication |
| --- | --- | --- |
| file to file | `ki_convert_file` | Temporary PDF beside the destination, then `rename` |
| `-` to file | `ki_convert_to_file` with `read_bytes` reading stdin | Same atomic file lifecycle |
| file to `-` | `ki_convert` with `read_bytes` over a `FILE*` opened for the input path and `write_bytes` writing stdout | PDF bytes on stdout; a failure may leave a partial PDF |
| `-` to `-` | `ki_convert` with both callbacks on stdin/stdout | Same as above |

When either side is a stream, the input is opened as a `FILE*` (stdin itself for `-`); if that open fails the CLI prints `cannot open input <path>` and returns exit 3. For stdout output, `setvbuf(stdout, NULL, _IONBF, 0)` makes the stream unbuffered so write errors surface during the conversion, and a final `fflush(stdout)` is checked; on failure the CLI prints `stdout write failed` and returns exit 3. The `cli_write` callback fails on any short write.

4. Install signal handlers, convert, restore the previous handlers, and return the library status as the exit code.

## Signals and cancellation

`SIGINT` and `SIGTERM` handlers set a flag; the library polls it through the `cancelled` callback at its documented checkpoints, so a conversion stops as status 4 with `KI_CAUSE_CANCEL` and, for file destinations, the previous file survives. A `SIGTERM` that interrupts a blocked `fread` on stdin is caught the same way: the read returns, the cancel check runs, and the process exits 4 instead of dying by signal. `SIGPIPE` is ignored when the output is stdout, so a closed or full pipe becomes an I/O error (exit 3) rather than process death. Handlers are installed only after argument validation and are restored before exit.

## Diagnostics and exit codes

All conversion diagnostics, including the success summary `xml2pdf: pages=... objects=... offset_bytes=...`, go to stderr through `cli_log`. stdout carries only PDF bytes. The usage text states the contract plainly: `Exit codes: 0 success, 1 XML/profile/font, 2 usage, 3 I/O, 4 quota/cancel.` Files are replaced atomically; stdout may contain a partial PDF on failure, so check the exit status before using streamed output.

`-h` and `--help` print the help text to stdout and return 0. `--version` prints one line, for example `xml2pdf 0.2.0 records-v1` (version from `KI_XML_PDF_VERSION`, default profile from `KI_XML_PDF_PROFILE`).

## Examples

```sh
./xml2pdf --profile report-v1 examples/report-v1.xml report.pdf
cat examples/report-v1.xml | ./xml2pdf --profile report-v1 - from-stdin.pdf
./xml2pdf --profile report-v1 examples/report-v1.xml - > from-file.pdf
cat examples/report-v1.xml | ./xml2pdf --profile report-v1 - - > pipeline.pdf

# Paths that begin with hyphens
./xml2pdf --profile report-v1 -- --input.xml --output.pdf

# Selection with quotas for a large input
./xml2pdf --profile report-v1 --record-offset 0 --record-limit 1000 \
  --max-pages 10000 --max-output-bytes 268435456 \
  --max-spool-bytes 67108864 input.xml report.pdf
```

## Tested behaviors

`tests/cli_pipeline.py` drives the binary through Poppler and checks:

- All four stream combinations (file or stdin to file or stdout) match the `examples/report-v1.expected.txt` oracle, diagnostics stay on stderr, and the spool directory is left empty.
- Help goes to stdout and explains the partial-stdout caveat; invalid arguments (missing or unknown options, bad profile names, negative, huge or non-numeric numbers, wrong argument counts) return 2 with diagnostics on stderr only.
- A late XML error returns 1, and page, output-byte and spool quotas return 4; each failure preserves an existing destination byte-for-byte, removes the temporary PDF and leaves no spool behind.
- `SIGTERM` while the process waits on stdin returns 4, keeps the old destination and cleans up temporary files.
- stdout to `/dev/full` or a closed pipe returns 3 with diagnostics; the closed pipe returns an error rather than killing the process with `SIGPIPE`.
- A late XML error with stdout output returns 1 and leaves a partial PDF on stdout that lacks `%%EOF`.
- The default profile converts `<doc>Hello</doc>` to the lines `doc` and `Hello`.

## Integration points

- The CLI links `ki_xml_pdf::ki_xml_pdf` and inherits every conversion behavior from the pipeline in [Conversion pipeline](../systems/conversion-pipeline.md); profile behavior lives in [records-v1](records-v1.md), [report-v1](report-v1.md) and [Custom mappers](custom-mappers.md).
- Quotas and the spool directory are enforced by the PDF writer; see [Resource limits](resource-limits.md). Stream behavior and atomic publication are on [Streaming I/O](streaming-io.md) and [Atomic output](atomic-output.md).
- The installed CLI and its rpath are part of [Packaging](packaging.md).

## Entry points for modification

- New options: add the argument in `main`, map it to a `KiOptions` field, extend `usage`, and keep validation in `take_long` or a sibling helper. Extend the invalid-argument list in `tests/cli_pipeline.py`.
- New profiles: extend the profile name check and `usage`; the library side change belongs in `src/convert.c` and `include/ki_xml_pdf.h`.
- Help text and version output: `usage` and the `--version` branch in `main`.

## Key source files

| File | Contents |
| --- | --- |
| `src/main.c` | The entire CLI: parsing, callbacks, signals, exit codes |
| `include/ki_xml_pdf.h` | Option and result structs the CLI fills |
| `tests/cli_pipeline.py` | CLI scenario suite used by `make check` and CTest |
| `README.md` | User-facing CLI documentation |
