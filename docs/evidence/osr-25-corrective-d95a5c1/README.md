# OSR-25 corrective test-harness evidence (d95a5c1)

Checked source: `d95a5c12c3b0e3b0a2f281556c87d9fba30c617a`, one commit on top of merged `main` `c54356f47d2390ada73139dca13e6063f1a9d711` (PR #2). It changes only `tests/test_stream.c` and `tests/test_integration.c`. The commit that adds this directory changes only documentation.

This record supplements [osr-25-integration-48bdef2](../osr-25-integration-48bdef2/README.md). Those gates, the PR #2 GitHub CI runs and the post-merge run of `c54356f` checked the earlier test sources. They are historical and are not evidence for the corrected tests.

## What changed

- `__wrap_deflate` in `tests/test_stream.c` incremented `deflate_calls` and decremented `deflate_fail_in` without synchronization while `integration_contexts` ran conversions on several threads. A mutex now guards that state. Serial fault injection is unchanged: the nth following call still returns `deflate_fail_rc` without compressing.
- The threaded rounds of `integration_contexts` now also require the same total number of `deflate()` calls as the serial references, so a lost counter update fails the test.
- `integration_contexts` now always joins its started threads and then removes every `/tmp/ki-it-*` fixture and buffer it created, including after an early failure. The new `integration_cleanup` group forces failures at three stages (during the references, after them, after the first threaded round) and checks that 8, 14 and 28 created paths respectively are all gone.

ASan and UBSan do not detect data races, and no thread sanitizer run is claimed. Race freedom rests on the reviewed locking and ordering (`pthread_create`/`pthread_join`) plus the real concurrent reruns below.

## Gates on d95a5c1

`gates/run_gates.sh` is the same 20-command script as for 48bdef2, run sequentially in a fresh clone checked out at `d95a5c1`. Every command exited 0 (`gates/summary.tsv`), and the second line of every log is `### HEAD d95a5c12c3b0e3b0a2f281556c87d9fba30c617a`. `<corrective-root>` is the external directory of this run on the measuring host.

| Gate | Result | Log |
| --- | --- | --- |
| `make test`, `make check` | all groups ok, including `integration_contexts` and `integration_cleanup` | `01-make-test.txt`, `02-make-check.txt` |
| `make CC=gcc test-san`, `make CC=clang test-san` | all groups ok, including both integration groups; no sanitizer report | `03-san-gcc.txt`, `04-san-clang.txt` |
| Clang and GCC clean rebuilds with functional tests, `integration_check.py`, catalog | ok | `05-…`, `06-…` |
| Fuzz smoke, font fixtures, CLI/catalog/integration/`--help`/`--version` | ok, `xml2pdf 0.3.0 records-v1` | `07-…`, `08-…`, `09-…` |
| `tests/build_integration.sh` with GCC and Clang | installed C/C++ static/shared Make, CMake and pkg-config consumers ok | `10-…`, `11-…` |
| Docs smoke, header dependency, prefix switch | ok | `12-…`, `13-…`, `16-…` |
| `make distcheck` and five negative controls | archive ok; each control failed as required | `14-…`, `15-…` |
| CMake build and CTest, resource unit tests, 1 GiB selection proof, install/pkg-config/ELF | ok | `17-…` to `20-…` |

The distcheck archive `ki_xml_pdf-0.3.0.tar.gz` built from `d95a5c1` was 886,958 bytes with SHA-256 `f3c9b5e6a22696d49bce9cbe911d009f769cfdea3eeb520bc9ebb2e0b619280e`. After the run, `ls /tmp/ki-it-*` on the host found no file (an observation, not a committed log).

## Negative controls

Both new checks were shown to fail against a deliberately broken copy of the tests (`negative/`, built by the correction work in a separate clone):

- `race-mutation.diff` moves the counter update out of the lock and adds `sched_yield()` between its read and write. `race.txt`: `round 0: 250 deflate calls, serial references made 251`, `FAILED integration_contexts`.
- `cleanup-mutation.diff` removes the cleanup after `it_contexts()`. `cleanup.txt`: `FAIL … !job->owns_in && !job->owns_out`, `FAILED integration_cleanup`.

## Reused evidence

The production tree is unchanged since the measured revision. The git tree objects of `src`, `include`, `Makefile`, `CMakeLists.txt`, `cmake`, `examples`, `fonts` and `LICENSE` are identical in `48bdef2`, `0f76a20`, `c54356f` and `d95a5c1`; between `48bdef2` and `d95a5c1` only `tests/`, `docs/evidence/`, the README files and `.github/workflows/ci.yml` differ. The nine-case resource series, the resource controls and the old 0.2 ABI canary and probe results therefore remain those recorded for `48bdef2` and are reused with that SHA, not rerun. The converter binary hash differs between clones (`3ebb6b17…` there), because the binary embeds the absolute checkout path; the identical source tree is the basis for reuse.

## Review

An independent reviewer, separate from the author of the correction, reviewed the exact commit `d95a5c1` read-only and approved it with no blocking findings (`review.md`).
