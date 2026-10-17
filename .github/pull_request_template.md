## Summary

<!-- What changed, and which observable it affects. -->

## Checklist

- [ ] A regression test fails before the fix and checks an observable result, or this change has no converter behavior to regress.
- [ ] The pull request reports the commands, results, and source revision of the checks that apply (`make check`, `make test-san`, `make distcheck`, installed C/C++ consumers).
- [ ] A memory change reports converter RSS from an ordinary binary (`/usr/bin/time -f %M`), measured separately from ASan/UBSan, the input generator, and the PDF reader.
- [ ] Intentional schema or public API changes are documented, including an entry in `CHANGELOG.md`.
