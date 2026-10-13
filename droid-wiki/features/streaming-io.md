# Streaming I/O

Active contributors: Artem Klen

`ki_convert` and `ki_convert_to_file` read and write through caller callbacks instead of paths, and the CLI uses the same callbacks for stdin and stdout. This page covers the callback contract, the pull reader, the `fopencookie` sink, the CLI modes and the partial-output rule.

## Purpose

Callback I/O lets a caller stream XML from a socket, a decompressor or an in-memory buffer, and deliver the PDF to a socket or another consumer without a file. It also lets the CLI read stdin and write stdout. The library never owns the caller's resources, and it never closes or frees callback state.

## Key abstractions

| Abstraction | Path | Role |
| --- | --- | --- |
| `read_bytes`, `write_bytes`, `io_user` | `include/ki_xml_pdf.h` | Callback fields in `KiOptions` |
| `conv_read` | `src/convert.c` | Pull adapter the parser reads through |
| `ReadState` | `src/convert.c` | File or callback reader plus failure flags |
| `ByteSink`, `sink_write`, `sink_close` | `src/convert.c` | Write callback behind `fopencookie` |
| `ki_convert_to_file` | `src/convert.c` | Callback input to an atomic file |
| `ki_convert` | `src/convert.c` | Callback input to callback output |
| `CliIo`, `cli_read`, `cli_write` | `src/main.c` | stdin/stdout callbacks for the CLI |
| `test_callback_to_file` | `tests/test_stream.c` | Short reads, write failure, ownership checks |
| CLI stream cases | `tests/cli_pipeline.py` | stdin/stdout, closed pipe, partial stdout |

## How it works

### Callback contract

`read_bytes(user, dst, max)` returns 0 only at end of input, returns 1 through `max` for data, and returns a negative value for an error. A short read is not EOF: the parser asks again, so a caller may return one byte at a time. `write_bytes(user, src, n)` must return `n` after accepting the entire count, or a negative value for an error. A short write also fails. The `io_user` pointer is passed to both. Accepted bytes are never rolled back.

`ki_convert_file` ignores both callbacks. `ki_convert_to_file` uses `read_bytes` and ignores `write_bytes`. `ki_convert` needs both.

### The pull reader

`conv_read` in `src/convert.c` is the `XmlReadFn` the parser pulls through. It checks `opt->cancelled` first and returns -1 with `cancelled` set. The `xml2pdf_debug_fail_after_bytes` hook fails the read after a byte budget. A `max` of zero or less returns 0.

- With a `FILE` (file input), it uses `fread`. A zero-byte result is EOF unless `ferror` is set or `errno` is `EINTR`, in which case it is an I/O failure.
- With `read_bytes`, it calls the callback and treats a return below 0 or above `max` as an I/O failure. A return of 0 is EOF.

It adds each served count to `ReadState.bytes`. The driver reads `reader.cancelled` and `reader.io_failed` when the parser reports an error, which is how a cancellation or read failure becomes cause `KI_CAUSE_CANCEL` or `KI_CAUSE_IO`.

### The write sink

`ki_convert` in `src/convert.c` allocates a `ByteSink {write_bytes, user}` and calls `fopencookie` with `sink_write` and `sink_close`. `sink_write` rejects a count above `INT_MAX`, calls the callback, and returns `-1` with `errno = EIO` when the callback returns a negative value or a count other than the requested one. `sink_close` frees the sink; it does not touch caller state. The driver then runs `convert_streams(NULL, out, NULL, NULL, opt, result)`: there is no destination path, so nothing is renamed and the temp-file logic does not run.

### Callback input to a file

`ki_convert_to_file` in `src/convert.c` requires `opt` and `opt->read_bytes`, rejects a NULL path, and calls `convert_file_stream(NULL, ...)`. The read side is the callback, the write side is the same temp-file and rename path as a file conversion. The previous destination is preserved on failure and the temp is removed. See [Atomic output](atomic-output.md).

### CLI stdin and stdout modes

`src/main.c` chooses the entry point by whether each positional argument is `-`:

| Input | Output | Callbacks set | Entry point |
| --- | --- | --- | --- |
| file | file | no | `ki_convert_file` |
| `-` (stdin) | file | `cli_read` (and `cli_write`, ignored) | `ki_convert_to_file` |
| file or `-` | `-` (stdout) | `cli_read`, `cli_write` | `ki_convert` |

When either argument is `-`, the CLI sets `opt.read_bytes`, `opt.write_bytes` and `opt.io_user`. `io.input` is stdin for a stdin argument, otherwise the opened input file. `cli_read` in `src/main.c` uses `fread` and returns -1 on `ferror` or on a zero-byte read with `errno == EINTR`. `cli_write` uses `fwrite` to stdout and returns -1 on a short write.

For stdout output the CLI makes stdout unbuffered with `setvbuf(stdout, NULL, _IONBF, 0)` so errors surface during the conversion, and ignores `SIGPIPE` for the duration. Without that, a closed pipe would kill the process; with it, `cli_write` returns -1 and the run ends with status 3. After `ki_convert` returns success the CLI flushes stdout and turns a flush failure into status 3.

The CLI installs `SIGINT` and `SIGTERM` handlers that set a `sig_atomic_t` stop flag read by `cli_cancelled`, and restores the previous handlers before returning. Conversion diagnostics go to stderr through `cli_log`, so stdout carries only PDF bytes.

### Caller ownership and partial output

The library never closes or frees callback state, and it does not close a caller-owned `FILE` passed as a file path. `tests/test_stream.c` in `test_callback_to_file` checks that a caller's input `FILE` and output `FILE` remain open and usable after both a success and a late XML error.

`ki_convert` has no destination to protect, so bytes already accepted by `write_bytes` stay when the run fails. The CLI can therefore leave an incomplete PDF on stdout after a late XML error, and `tests/cli_pipeline.py` asserts exactly that: the output starts with `%PDF-`, does not end with `%%EOF`, and diagnostics appear on stderr. `tests/cli_pipeline.py` also checks that writing to `/dev/full` and to a closed pipe both return status 3 instead of dying.

## Integration points

- The read callback is consumed by `conv_read`, which the parser calls; see [XML parser](../systems/xml-parser.md).
- The write callback receives the writer's bytes; see [PDF writer](../systems/pdf-writer.md).
- File destinations use the same driver; see [Conversion pipeline](../systems/conversion-pipeline.md).
- Cancellation and quota causes are on [Resource limits](../features/resource-limits.md).
- CLI argument and signal handling are on [CLI](../features/cli.md).

## Entry points for modification

- Change read validation in `conv_read` in `src/convert.c`.
- Change write validation in `sink_write` in `src/convert.c`.
- Change stdin/stdout selection, buffering or signal setup in `src/main.c`.
- Change the documented contract in the `read_bytes` / `write_bytes` comment in `include/ki_xml_pdf.h` and in `docs/contract.md`.

## Key source files

| File | Role |
| --- | --- |
| `include/ki_xml_pdf.h` | Callback fields and the written contract |
| `src/convert.c` | `conv_read`, `ByteSink`, `ki_convert`, `ki_convert_to_file` |
| `src/main.c` | `cli_read`, `cli_write`, stdin/stdout modes, signals |
| `tests/test_stream.c` | Short reads, write modes, caller-owned `FILE`s |
| `tests/cli_pipeline.py` | stdin/stdout, closed pipe, partial stdout |
| `docs/contract.md` | Stated callback and partial-output behavior |

## Related pages

- [Conversion pipeline](../systems/conversion-pipeline.md), [XML parser](../systems/xml-parser.md), [PDF writer](../systems/pdf-writer.md).
- [Atomic output](atomic-output.md), [Resource limits](resource-limits.md), [CLI](cli.md).
- [C API](../api/c-api.md), [Glossary](../overview/glossary.md), [Testing](../how-to-contribute/testing.md).
