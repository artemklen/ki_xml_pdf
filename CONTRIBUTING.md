# Contributing

Project source and original examples use the [MIT license](LICENSE). The font has a separate notice at [fonts/DejaVuSans.copyright](fonts/DejaVuSans.copyright). Contributions are welcome through the [public repository](https://github.com/artemklen/ki_xml_pdf); [GitHub Actions](https://github.com/artemklen/ki_xml_pdf/actions/workflows/ci.yml) builds a specific commit and preserves its source archive. Checks run locally before sending.

## Report a bug

Use [GitHub Issues](https://github.com/artemklen/ki_xml_pdf/issues) for ordinary bugs. Include:

- The version from `xml2pdf --version`, plus a commit ID or source archive SHA-256.
- Your OS, architecture, compiler, and whether the library is static or shared.
- The command or minimal public API call, its return code, and the relevant stderr or `KiResult` diagnostic.
- A small reproducible XML input that you have permission to share, and the expected result.

Do not attach someone else's documents. A synthetic reproducer is preferable when the original contains private or customer data. Keep a partial stdout/callback PDF separate from a successful result; include the failure status when reporting it.

## Report a security issue privately

Send security reports directly to Artem Klen at **artem.klen@icloud.com**. Include the affected version/commit, impact, reproduction steps, and the smallest shareable input. Use this private route for security details rather than opening a public issue with them. Do not send private source documents or secrets. Supported versions and the same contact are in [SECURITY.md](SECURITY.md).

## Build and check a change

The tested platform is 64-bit Linux with glibc, GCC or Clang, GNU Make, and a GNU-compatible linker. A pull request that ports the library is not accepted without a prior maintainer decision; see [Supported platforms](README.md#supported-platforms). Checks require C and C++ compilers, `python3`, `/usr/bin/time`, and `readelf` (binutils) and Poppler's `pdfinfo`/`pdftotext`/`pdftohtml`; integration checks also require CMake and pkg-config. See [README.md](README.md) for copyable Make/CMake builds, installed consumers, and pipelines. [README.ru.md](README.ru.md) preserves the Russian onboarding.

For a behavior change, add a meaningful regression that fails before the fix and checks an observable result. Run the checks appropriate to the change and report their commands, results, and source revision. Streaming and report behavior can be checked with `make check`; sanitizer coverage uses `make test-san`. Installed-package changes need the external C/C++ consumer checks, and source-distribution changes need `make distcheck`. PDF contents should be checked through an independent reader, not just by matching writer output bytes.

### Local pre-send gate (OPS-625)

The agent must run the applicable checks on the exact revision being sent and record commands, results, environment and `git rev-parse HEAD`. CI (`.github/workflows/ci.yml`) only compiles the CLI/library/example and creates the source archive with SHA-256; it does not run tests.

```sh
make clean && make xml2pdf example libki_xml_pdf.so
make test xml2pdf example
tests/build_integration.sh
tests/docs_smoke.sh
python3 tests/fuzz_smoke.py
make distcheck
make test-san
make check
```

Required environment: 64-bit Linux with glibc, GCC/Clang, GNU Make, Python 3, `/usr/bin/time`, binutils `readelf`, Poppler `pdfinfo`/`pdftotext`/`pdftohtml`, CMake and pkg-config for installed consumers. A missing platform tool is a blocker in Linear, not a skip and not a reason to restore tests in CI. A meaningful code change invalidates the matching evidence; an unchanged SHA may reuse it.

Measure converter RSS on an ordinary binary, separately from ASan/UBSan, the input generator, and the PDF reader. Historical performance numbers remain tied to the revision that produced them. Test success alone does not establish adoption, demand, or performance against another converter.

## Keep the public contract reviewable

Preserve the accepted [report-v1 grammar, selection, and whitespace rules](docs/report-v1.md), parser/font limits, resource quotas, cancellation, and the [public API contract](include/ki_xml_pdf.h). Selection must validate skipped records and the entire input. Maintain bounded parser/mapper state and preserve significant text across short reads and TEXT/CDATA/reference boundaries.

File output must remain atomic: late XML/profile failures, I/O errors, quotas, and cancellation preserve an existing destination and clean temporary files. Callback/stdout output has a partial-output contract. The library must keep ownership of caller resources with the caller and leave application signal handlers alone. Changes to callbacks should cover short reads, EOF, failures, and reused `KiResult` values where relevant.

Custom-schema examples should use only the public API and explain borrowed event pointers and bounded mapper state; see the [mapper guide](docs/mapper-guide.md). Do not imply support for untested third-party XML schemas or operating systems.

Document intentional schema or public API changes in the relevant reference and [CHANGELOG.md](CHANGELOG.md). Keep header, CLI, CMake metadata, pkg-config metadata, and archive versions consistent. Include applicable regression/check evidence in a pull request, together with any checks that could not run and the reason. A passing CI run does not automatically create a version tag or GitHub Release.

## Code of conduct

Participation follows the [Contributor Covenant 2.1](CODE_OF_CONDUCT.md). Reports of unacceptable behavior go to Artem Klen at **artem.klen@icloud.com**, the same contact as [SECURITY.md](SECURITY.md). An open question for the maintainer: whether conduct reports should use a different address. This file does not invent one.
