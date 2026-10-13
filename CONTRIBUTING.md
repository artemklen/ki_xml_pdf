# Contributing

Project source and original examples use the [MIT license](LICENSE). The font has a separate notice at [fonts/DejaVuSans.copyright](fonts/DejaVuSans.copyright). Contributions are welcome through the [public repository](https://github.com/artemklen/ki_xml_pdf); [GitHub Actions](https://github.com/artemklen/ki_xml_pdf/actions/workflows/ci.yml) records checks for a specific commit.

## Report a bug

Use [GitHub Issues](https://github.com/artemklen/ki_xml_pdf/issues) for ordinary bugs. Include:

- The version from `xml2pdf --version`, plus a commit ID or source archive SHA-256.
- Your OS, architecture, compiler, and whether the library is static or shared.
- The command or minimal public API call, its return code, and the relevant stderr or `KiResult` diagnostic.
- A small reproducible XML input that you have permission to share, and the expected result.

Do not attach someone else's documents. A synthetic reproducer is preferable when the original contains private or customer data. Keep a partial stdout/callback PDF separate from a successful result; include the failure status when reporting it.

## Report a security issue privately

Send security reports directly to Artem Klen at **artem.klen@icloud.com**. Include the affected version/commit, impact, reproduction steps, and the smallest shareable input. Use this private route for security details rather than opening a public issue with them. Do not send private source documents or secrets.

## Build and check a change

The tested platform is 64-bit Linux with glibc, GCC or Clang, GNU Make, and a GNU-compatible linker. Checks require C and C++ compilers, `python3`, `/usr/bin/time`, and `readelf` (binutils) and Poppler's `pdfinfo`/`pdftotext`/`pdftohtml`; integration checks also require CMake and pkg-config. See [README.md](README.md) for copyable Make/CMake builds, installed consumers, and pipelines. [README.ru.md](README.ru.md) preserves the Russian onboarding.

For a behavior change, add a meaningful regression that fails before the fix and checks an observable result. Run the checks appropriate to the change and report their commands, results, and source revision. Streaming and report behavior can be checked with `make check`; sanitizer coverage uses `make test-san`. Installed-package changes need the external C/C++ consumer checks, and source-distribution changes need `make distcheck`. PDF contents should be checked through an independent reader, not just by matching writer output bytes.

Measure converter RSS on an ordinary binary, separately from ASan/UBSan, the input generator, and the PDF reader. Historical performance numbers remain tied to the revision that produced them. Test success alone does not establish adoption, demand, or performance against another converter.

## Keep the public contract reviewable

Preserve the accepted [report-v1 grammar, selection, and whitespace rules](docs/report-v1.md), parser/font limits, resource quotas, cancellation, and the [public API contract](include/ki_xml_pdf.h). Selection must validate skipped records and the entire input. Maintain bounded parser/mapper state and preserve significant text across short reads and TEXT/CDATA/reference boundaries.

File output must remain atomic: late XML/profile failures, I/O errors, quotas, and cancellation preserve an existing destination and clean temporary files. Callback/stdout output has a partial-output contract. The library must keep ownership of caller resources with the caller and leave application signal handlers alone. Changes to callbacks should cover short reads, EOF, failures, and reused `KiResult` values where relevant.

Custom-schema examples should use only the public API and explain borrowed event pointers and bounded mapper state; see the [mapper guide](docs/mapper-guide.md). Do not imply support for untested third-party XML schemas or operating systems.

Document intentional schema or public API changes in the relevant reference and [CHANGELOG.md](CHANGELOG.md). Keep header, CLI, CMake metadata, pkg-config metadata, and archive versions consistent. Include applicable regression/check evidence in a pull request, together with any checks that could not run and the reason. A passing CI run does not automatically create a version tag or GitHub Release.
