# Atomic output

Active contributors: Artem Klen

File destinations are written to a temporary file in the destination directory and renamed over the destination only after a complete, flushed and `fsync`ed PDF exists. A failure leaves the previous destination in place and removes the temporary file. This page covers the temp-file lifecycle, the same-file check and what each failure path leaves behind.

## Purpose

A converter that writes a destination in place can leave a half-written PDF when the input turns out to be malformed, when a quota stops the run or when the process is cancelled. The library avoids that for file output by writing to `<dest>.XXXXXX`, publishing with `rename`, and refusing an output that resolves to the input.

## Key abstractions

| Abstraction | Path | Role |
| --- | --- | --- |
| `make_temp` | `src/convert.c` | Creates `<dest>.XXXXXX` with `mkstemp` |
| `output_identity`, `same_file` | `src/convert.c` | Resolve and compare input and output identity |
| `convert_file_stream` | `src/convert.c` | Temp open, error cleanup, driver handoff |
| Publish block in `convert_streams` | `src/convert.c` | `fflush`, `fsync`, `fclose`, `rename` |
| `ki_convert_to_file` | `src/convert.c` | Callback input with the same file lifecycle |
| `test_output_keeps_previous` | `tests/test_stream.c` | Temp cleanup and preserved-destination evidence |
| `test_callback_to_file` | `tests/test_stream.c` | Temp counts around callback publication |
| `check_preserved` | `tests/cli_pipeline.py` | Destination, spool and temp checks after failures |

## How it works

### Temp-file lifecycle

`make_temp` in `src/convert.c` appends `.XXXXXX` to the destination path and calls `mkstemp`. The temp file therefore sits in the same directory as the destination, which is what makes the final `rename` atomic on the same filesystem. The destination directory must be writable; if it is not, `mkstemp` fails before any conversion starts and the destination is never touched. The offset spool file is separate: it is created in `--temp-dir` or `/tmp` by `pdf_begin_cfg` in `src/pdf_write.c` and removed by `pdf_free`.

`convert_file_stream` in `src/convert.c` opens the returned descriptor with `fdopen("wb")` and a 4096-byte buffer, then calls `convert_streams`. The publish block of `convert_streams` runs this sequence for file output:

1. `fflush(out)` on the temp stream.
2. `fsync(fileno(out))`.
3. `fclose(out)`.
4. `rename(tmp_path, pdf_path)`.

The writer's own `pdf_end` flushes its stream first, so the offsets and xref are complete before the driver flushes the temp. If any of `fflush`, `fsync` or `fclose` fails, the run fails with status 3 and `KI_CAUSE_IO` and the temp is removed. If `rename` fails, the run fails with status 3 and the message `cannot replace output <path>`, and the temp is removed.

### Same-file detection

`same_file` in `src/convert.c` compares the input and output in two steps. It first `stat`s both paths and compares `st_dev` and `st_ino`, which catches the same path and a hard link. If either path does not exist, it compares `realpath(input)` with `output_identity(output)`.

`output_identity` in `src/convert.c` returns `realpath(path, NULL)` when the output path exists, following symlinks. When it does not exist, it resolves the parent directory with `realpath` and joins the base name, so a path like `dir/./out.pdf` and a missing output in a symlinked directory still compare correctly. A symlink or hard link at the output that points to the input is therefore refused with status 3 and `input and output are the same file`, and both names keep their bytes.

A destination symlink that points somewhere else is not followed for writing. The temp file is created at the symlink's path plus `.XXXXXX`, and `rename` replaces the destination directory entry, so the new regular PDF takes the place of the symlink rather than being written through it.

### SIGKILL caveat

`SIGKILL` cannot be handled. If the process is killed between `mkstemp` and `rename`, the temp file `<dest>.XXXXXX` stays on disk and the destination is untouched. `tests/test_stream.c` in `test_output_keeps_previous` forks a child on a FIFO input, kills it with `SIGKILL`, checks the destination still holds the old bytes, and then removes the leftover `out.pdf.*` files itself. `SIGTERM` is different: the CLI installs a handler that sets the cancel flag, so the run returns status 4 and the temp is cleaned up (see [CLI](../features/cli.md) and [Streaming I/O](../features/streaming-io.md)).

### What each failure leaves behind

| Failure | Temp file | Destination |
| --- | --- | --- |
| Input and output are the same file | not created | untouched |
| Input cannot be opened, or is a directory | not created | untouched |
| Temp cannot be created (missing or read-only directory, `ENOSPC`, `EDQUOT`) | none | untouched |
| XML, profile or font error during the run | unlinked | untouched |
| I/O error, quota or cancellation during the run | unlinked | untouched |
| `fflush`, `fsync` or `fclose` on the temp fails | unlinked | untouched |
| `rename` fails | unlinked | untouched |
| Success | renamed to the destination | replaced |
| `SIGKILL` during the run | left behind | untouched |

The cleanup is the `done` block of `convert_streams` in `src/convert.c`: it closes the output and unlinks `tmp_path` whenever `published` is not set. The tests check this with `count_temps(dir, "out.pdf") == 0` in `tests/test_stream.c` and with `check_preserved` in `tests/cli_pipeline.py`, which asserts the destination still holds the old bytes, the spool directory is empty and no extra file sits next to the destination.

### Callback input to a file

`ki_convert_to_file` in `src/convert.c` uses the same lifecycle as `ki_convert_file`, only the input differs. It requires `read_bytes` and ignores `write_bytes`, then calls `convert_file_stream(NULL, ...)`. `tests/test_stream.c` in `test_callback_to_file` runs it with one-byte, three-byte and 4096-byte reads and confirms that the destination is replaced, its size equals `result.output_bytes`, and no temp or spool file is left. It also runs late XML, read, page-quota, output-quota and spool-quota failures and confirms the previous destination survives each one.

## Integration points

- The publish block is part of the driver; see [Conversion pipeline](../systems/conversion-pipeline.md).
- The offset spool file and its quota are on [Resource limits](../features/resource-limits.md).
- `ki_convert` has no temp file or rename; its partial-output contract is on [Streaming I/O](../features/streaming-io.md).
- The CLI's `-` handling and signal behavior are on [CLI](../features/cli.md).

## Entry points for modification

- Change temp naming or creation in `make_temp` in `src/convert.c`.
- Change identity rules in `output_identity` and `same_file` in `src/convert.c`.
- Change the publish order or add a durability step in the publish block of `convert_streams` in `src/convert.c`.
- Change the offset spool location in `pdf_begin_cfg` in `src/pdf_write.c`.

## Key source files

| File | Role |
| --- | --- |
| `src/convert.c` | `make_temp`, `same_file`, `output_identity`, publish and cleanup |
| `src/pdf_write.c` | Offset spool temp file, `pdf_end` flush |
| `tests/test_stream.c` | Temp counts, same-file, `SIGKILL` and preserved-destination checks |
| `tests/cli_pipeline.py` | `check_preserved` across failure modes and signals |
| `docs/contract.md` | Stated publication and `SIGKILL` behavior |

## Related pages

- [Conversion pipeline](../systems/conversion-pipeline.md), [PDF writer](../systems/pdf-writer.md).
- [Streaming I/O](../features/streaming-io.md), [Resource limits](../features/resource-limits.md), [CLI](../features/cli.md).
- [Architecture](../overview/architecture.md), [Glossary](../overview/glossary.md), [Security](../security.md).
