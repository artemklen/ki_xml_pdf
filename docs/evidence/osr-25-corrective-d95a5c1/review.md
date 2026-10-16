# Independent review of d95a5c1

Reviewed commit: `d95a5c12c3b0e3b0a2f281556c87d9fba30c617a` (parent `c54356f47d2390ada73139dca13e6063f1a9d711`). The reviewer was a separate executor that did not write the change and used only read-only git commands.

**Verdict: approve, no blocking findings.** No production file changed.

Checked and found correct:

- The counter update, the test-and-decrement of `deflate_fail_in` and the read of `deflate_fail_rc` happen under `deflate_lock`; `__real_deflate` runs outside the lock. Serial fault injection in `tests/test_flate.c` is unchanged.
- `deflate_calls = 0` happens before `pthread_create`; the count is read only after every started thread is joined; references run on the main thread with no conversion threads alive.
- No other shared test state is written by the conversion threads: `__wrap_fsync` only reads `arm_fsync_cancel` while it is 0, `__wrap_deflateInit2_` and `test_cancelled` only read, `it_gate` is mutex and condition-variable protected, mapper and read counters are per job.
- The deflate-count equality is deterministic: the number of `deflate` calls depends only on the input, `FLATE_CHUNK` and zlib, and both totals include the retry conversions.
- The expected path counts 8, 14 and 28 of `integration_cleanup` match the stop stages.
- `it_discard` unlinks only paths it owns and clears the flags, so it is safe to repeat; `it_release` nulls freed pointers; every return after thread creation follows the join loop.

Non-blocking observations, not changed:

1. Pre-existing, outside this diff: two other threaded tests in `tests/test_stream.c` can return from a failing `CHECK` without joining all threads. This only happens after a test has already failed.
2. Pre-existing: small buffers in `tests/test_integration.c` leak on failing `CHECK` paths; they are not fixtures.
3. The `access()` check could in theory be confused by another process creating the same random `mkstemp` name in `/tmp` between unlink and check; negligible.
4. `integration_cleanup` adds three partial integration runs, which lengthens the suite, mostly under sanitizers.
