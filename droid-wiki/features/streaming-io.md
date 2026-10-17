# Streaming I/O
Active contributors: Artem

Callback I/O lets applications supply XML incrementally and receive PDF bytes without a seekable destination. The library owns its adapters, not caller resources; callback output is never rolled back, and `output_bytes` counts bytes the callback actually accepted.

## Choose a route

| API, with optional `_cfg` variant | Input | Output |
| --- | --- | --- |
| `ki_convert_file` | Paths, ignores both I/O callbacks | Atomic file |
| `ki_convert_to_file` | Required read callback | Atomic file, ignores write callback |
| `ki_convert` | Required read callback | Required write callback |

`include/ki_xml_pdf.h` defines all contracts. Configured variants accept separate geometry/Info state; original calls use the defaults.

## Callback rules

```c
int read_bytes(void *user, char *dst, int max);
int write_bytes(void *user, const char *src, int n);
```

Both receive `KiOptions.io_user`.

| Callback result | Interpretation |
| --- | --- |
| Read `1..max` | Data; short positive reads are not EOF |
| Read `0` | EOF only |
| Read negative or greater than `max` | I/O failure |
| Write exactly `n` | Entire chunk accepted |
| Write negative, zero, short or greater than `n` | I/O failure, conversion cannot succeed |

Buffers are borrowed for the callback. Copy/write bytes before returning if they are needed later. The library neither closes nor frees user state, FILE handles, sockets or other resources hidden behind the callbacks.

## Reader flow

`conv_read` in `src/convert.c` checks cancellation before/after reading, validates callback counts and records I/O/cancel flags. The [XML parser](../systems/xml-parser.md) repeatedly fills its 4096-byte window, so one-byte readers work across tags, references and UTF-8 sequences.

The library validates all input through EOF. Reaching a report record limit is not a reason to stop supplying data, and returning zero early means a truncated document rather than successful selection.

## Writer flow and failure latch

`ki_convert_cfg` wraps the callback in `ByteSink` behind GNU `fopencookie`. The [PDF writer](../systems/pdf-writer.md) adds its own counted/quota cookie above it; stdio buffering means writer-side byte counts alone do not prove callback delivery.

`sink_write` in `src/convert.c` maintains:

- `accepted`: add a valid positive result no larger than the requested count, including a short accepted prefix.
- `failed`: latch the first invalid/error write. The callback is never invoked again for that conversion, even if later writes would succeed.

After `convert_streams` flushes and closes its adapters, `ki_convert_cfg` sets `KiResult.output_bytes = accepted`, including bytes delivered during cleanup flushes. A callback claiming `n+1` is invalid and not counted; callers must report honestly what they accepted. A negative result cannot communicate an accepted prefix.

`sink_close` does not free caller state or the `ByteSink`; the entry wrapper keeps the sink alive through all closing flushes, reads its counter, then frees its own allocation. A subsequent conversion with reset caller state can reuse the same options/result.

The first failure returns status 3 / `KI_CAUSE_IO`, not success with broken xref offsets. `tests/test_callback_io.c` covers one-shot and persistent negative/short/zero/over-count failures at early, middle and final writes, with and without a result, and exact accepted-prefix/reuse checks.

## Partial-output boundary

A later XML/schema/quota/cancellation error can leave an incomplete PDF, including previously emitted font/page bytes. A byte prefix is not a usable PDF merely because Poppler can recover text from it. Check the return status and arrange application-level staging if the consumer needs all-or-nothing delivery.

Use `ki_convert_to_file` when the desired boundary is an [atomic file destination](atomic-output.md). Only the metadata offset spool still needs temporary disk storage for a pure callback conversion.

## CLI streams

`src/main.c` maps `-` input to stdin and `-` output to stdout:

```sh
cat examples/report-v1.xml | ./xml2pdf --profile report-v1 - report.pdf
./xml2pdf --profile report-v1 examples/report-v1.xml - > streamed.pdf
```

The CLI opens named callback input lazily, makes stdout unbuffered, ignores SIGPIPE for stdout output, and reports conversion diagnostics on stderr. A closed pipe or `/dev/full` becomes exit 3; SIGINT/SIGTERM drive cancellation. Redirection is done by the shell and does not gain file rollback.

## Modification and tests

This feature is implemented inside `src/convert.c`, with one dedicated C fault suite (`tests/test_callback_io.c`) plus ownership, short-read and accepted-byte groups in `tests/test_stream.c`. Change `conv_read`, `sink_write` and wrapper cleanup together; keep the header contract aligned.

`tests/cli_pipeline.py` checks all four CLI routes, real signals, failed pipes and partial stdout. `tests/integration_check.py` requires byte-identical successful output across file/stdin and file/stdout for combined features. See [Testing](../how-to-contribute/testing.md).

## Key source files

| File | Role |
| --- | --- |
| `include/ki_xml_pdf.h` | Callback signatures, ownership and failure contract |
| `src/convert.c` | Pull reader, ByteSink latch, accepted-byte result and routes |
| `src/pdf_write.c` | Counted/quota stream above the callback sink |
| `src/main.c` | stdin/stdout adapters, lazy input and SIGPIPE behavior |
| `tests/test_callback_io.c`, `tests/test_stream.c` | Fault, ownership and callback result regressions |
| `tests/cli_pipeline.py`, `tests/integration_check.py` | CLI failure and route-equivalence checks |

Related: [C API](../api/c-api.md), [Conversion pipeline](../systems/conversion-pipeline.md), [Resource limits](resource-limits.md).
