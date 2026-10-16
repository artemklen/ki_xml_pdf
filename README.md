# ki_xml_pdf

[Русский README](README.ru.md)

A C library and command-line tool for converting large XML inputs to PDF with bounded converter memory. Version **0.2.0** adds installed CMake and pkg-config integration, stdin/stdout pipelines, and an example adapter for a project-owned XML schema.

The built-in [report-v1](docs/report-v1.md) profile renders a sequential report with named fields and explicit record selection. The default `records-v1` profile prints element names, attributes, and text for diagnostic use. Other schemas require a mapper; start with the [mapper guide](docs/mapper-guide.md).

Project source and original examples use [MIT](LICENSE). DejaVu Sans has a [separate license notice](fonts/DejaVuSans.copyright). See the [repository](https://github.com/artemklen/ki_xml_pdf) and [GitHub Actions](https://github.com/artemklen/ki_xml_pdf/actions/workflows/ci.yml) for source and checks tied to a specific commit.

## Build and convert

The supported environment is **64-bit Linux with glibc**, GCC or Clang, GNU Make, and a GNU-compatible linker. The implementation uses GNU/POSIX APIs. Other operating systems have not been validated. CMake and pkg-config are needed for their respective integration workflows.

Run these commands from the source directory:

```sh
make
./xml2pdf --profile report-v1 examples/report-v1.xml report.pdf
```

The same profile works with all combinations of files and streams:

```sh
cat examples/report-v1.xml | ./xml2pdf --profile report-v1 - from-stdin.pdf
./xml2pdf --profile report-v1 examples/report-v1.xml - > from-file.pdf
cat examples/report-v1.xml | ./xml2pdf --profile report-v1 - - > pipeline.pdf
```

`-` means stdin for the input argument and stdout for the output argument. During conversion, stdout contains only PDF bytes; diagnostics go to stderr. `-h` and `--help` print help and return 0; `--version` prints the version. `--` ends option parsing and allows paths beginning with a hyphen:

```sh
cp examples/report-v1.xml ./--input.xml
./xml2pdf --profile report-v1 -- --input.xml --output.pdf
```

A file destination is published atomically after the entire input has been validated and the PDF completed. A failure preserves an existing destination. Output sent to stdout or a write callback cannot be rolled back: a late error can leave an incomplete PDF. Check the exit status before using streamed output.

For large inputs, selection and quotas are explicit:

```sh
./xml2pdf --profile report-v1 --record-offset 0 --record-limit 1000 \
  --max-pages 10000 --max-output-bytes 268435456 \
  --max-spool-bytes 67108864 input.xml report.pdf
```

Without a record limit, all records after the offset are selected. Selection still reads and validates the entire XML, including skipped records; reaching the limit does not hide a later error.

An out-of-tree CMake build is also supported. CMake enables tests by default and requires Python 3 during configuration. Use `-DBUILD_TESTING=OFF` for a library/CLI build without test targets:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON
cmake --build build
./build/xml2pdf --profile report-v1 examples/report-v1.xml cmake-report.pdf
```

## Install

Make installs the CLI, public header, static and shared libraries, CMake package, pkg-config metadata, font, and license notices:

```sh
make PREFIX="$HOME/.local"
make install PREFIX="$HOME/.local"
```

Use the same prefix when building and installing because it sets the installed runtime font path. CMake installation uses its own configured prefix:

```sh
cmake -S . -B build-install -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$HOME/.local" -DBUILD_SHARED_LIBS=ON
cmake --build build-install
cmake --install build-install
```

Both libraries are installed. `BUILD_SHARED_LIBS` selects the default CMake target variant. `DESTDIR` stages files without changing the compiled runtime font path or the prefix in discovery metadata:

```sh
make install PREFIX=/usr/local DESTDIR="$PWD/stage"
DESTDIR="$PWD/stage-cmake" cmake --install build-install
```

The installed font path is `<prefix>/share/ki_xml_pdf/DejaVuSans.ttf`. For a relocated installation, set `KiOptions.font_path` or `KI_XML_PDF_FONT` explicitly. An invalid explicit font is an error; it is not silently replaced. Shared consumers also need the library directory to be discoverable by the runtime linker.

## Installed C and C++ consumers

From the source directory, copy the examples and input fixtures into a separate working directory. The programs below then build against the installation and run outside the source tree:

```sh
prefix="$HOME/.local"
export PKG_CONFIG_PATH="$prefix/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
export LD_LIBRARY_PATH="$(pkg-config --variable=libdir ki_xml_pdf)${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
consumer_dir="$(mktemp -d)"
cp examples/report.c examples/consume.cpp examples/report-v1.xml examples/hello.xml "$consumer_dir/"
cd "$consumer_dir"

cc report.c $(pkg-config --cflags --libs ki_xml_pdf) -o report-shared
c++ consume.cpp $(pkg-config --cflags --libs ki_xml_pdf) -o consume-shared
./report-shared report-v1.xml report-shared.pdf
./consume-shared hello.xml consume-shared.pdf

cc report.c $(pkg-config --cflags ki_xml_pdf) \
  -Wl,-Bstatic $(pkg-config --static --libs ki_xml_pdf) -Wl,-Bdynamic -o report-static
c++ consume.cpp $(pkg-config --cflags ki_xml_pdf) \
  -Wl,-Bstatic $(pkg-config --static --libs ki_xml_pdf) -Wl,-Bdynamic -o consume-static
./report-static report-v1.xml report-static.pdf
./consume-static hello.xml consume-static.pdf
```

`pkg-config --static` includes private dependency flags; the GNU linker flags above select the static library even when a shared library is installed alongside it.

In that consumer directory, save this as `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.16)
project(ki_consumer LANGUAGES C CXX)
find_package(ki_xml_pdf CONFIG REQUIRED)
add_executable(report-client report.c)
add_executable(consume-client consume.cpp)
target_link_libraries(report-client PRIVATE ki_xml_pdf::ki_xml_pdf)
target_link_libraries(consume-client PRIVATE ki_xml_pdf::ki_xml_pdf)
```

Build and run both languages with the default shared target, then choose the installed static variant:

```sh
cmake -S . -B shared -DCMAKE_PREFIX_PATH="$prefix"
cmake --build shared
./shared/report-client report-v1.xml cmake-shared.pdf
./shared/consume-client hello.xml cmake-shared-cpp.pdf

cmake -S . -B static -DCMAKE_PREFIX_PATH="$prefix" -Dki_xml_pdf_USE_STATIC_LIBS=ON
cmake --build static
./static/report-client report-v1.xml cmake-static.pdf
./static/consume-client hello.xml cmake-static-cpp.pdf
```

## Public API

```c
#include "ki_xml_pdf.h"
#include <stdio.h>

int main(void) {
    KiOptions options;
    ki_options_init(&options);
    options.profile = KI_PROFILE_REPORT;
    options.record_limit = 1000;
    options.max_pages = 10000;

    KiResult result;
    int status = ki_convert_file("input.xml", "report.pdf", &options, &result);
    if (status != 0) {
        fprintf(stderr, "%d: %s\n", result.cause, result.message);
    }
    return status;
}
```

| Function | Input | Output and failure behavior |
| --- | --- | --- |
| `ki_convert_file(xml_path, pdf_path, options, result)` | File path; ignores read/write callbacks | Atomic file publication; preserves the previous destination on failure |
| `ki_convert(options, result)` | `read_bytes` callback | `write_bytes` callback; previously accepted bytes remain on failure |
| `ki_convert_to_file(pdf_path, options, result)` | `read_bytes` callback; ignores `write_bytes` | Atomic file publication, including input from a pipe or application stream |

A read callback returns 0 only at EOF, a positive byte count for data, and a negative value for an error. A positive short read is not EOF. A write callback must accept the entire requested count or return an error. The caller owns callback resources and closes them; the library does not close caller streams or free caller state.

Call `ki_options_init` before setting options and leave `options.size` equal to `sizeof(KiOptions)`. The 0.x API remains unstable until 1.0; this version preserves the existing `KiOptions` layout and adds `ki_convert_to_file`. A non-null `KiResult` is initialized for the current call, including early argument errors, rather than retaining previous conversion counters.

Use separate per-call options/result and mutable I/O/mapper state for concurrent conversions. Independent conversion calls can run simultaneously. The library does not install signal handlers and does not print to a terminal unless the caller supplies a log callback. `KiEvent` pointers are borrowed for the duration of the mapper callback; do not retain them. Mapping emits text with `ki_doc_text`/`ki_doc_break` and reports schema failures with `ki_doc_fail`. See the [public header](include/ki_xml_pdf.h) and [mapper guide](docs/mapper-guide.md) for the complete callback contract and working catalog adapter.

Return codes are 0 for success, 1 for XML/profile/font errors, 2 for invalid usage, 3 for I/O errors, and 4 for resource limits or cancellation. `KiResult.cause` distinguishes XML, usage, I/O, resource, font, cancellation, and profile failures. Profile errors include the rule and input byte offset; unsupported characters are font errors with a `U+` code point.

## XML, font, and resource limits

Parser state does not grow with document size. Input is read through a 4096-byte window. The limits are 64 simultaneously open elements, 63 bytes per name, 8 attributes per element, 255 UTF-8 bytes per decoded attribute value, and 1023 bytes per text event fragment. A field value can span many fragments; it is not collected into a complete field, record, or document buffer.

The parser supports XML declarations, comments, processing instructions, one DOCTYPE, CDATA inside an element, the five predefined entities, and decimal/hexadecimal (`&#x...;`, lowercase `x`) character references. It never loads external DTDs or entities, and entities declared in the internal subset are not expanded. Unknown entities, CDATA outside an element, unsupported declarations such as `<!FOO>`, `]]>` in ordinary text, invalid declarations, prohibited control bytes, NUL, and invalid UTF-8 are rejected. Literal CR and CRLF are normalized to LF.

Every consumed byte must be well-formed UTF-8 that encodes an XML `Char`, including comments, processing instructions, the skipped DTD, records skipped by selection, and content after the root. For example, a literal U+FFFE or U+FFFF is an XML error, not a font error. The XML declaration may appear only at the start (after an optional UTF-8 BOM). Its `version="1.0"`, optional `encoding="UTF-8"` and optional `standalone` pseudo-attributes must appear in that order, once each, and be separated by white space. A processing instruction target must be a name of at most 63 bytes followed by white space or `?>`; the target `xml` in any letter case is reserved, while targets such as `xml-stylesheet` are ordinary PIs that are not rendered. The DOCTYPE must precede the root. Its internal subset is skipped with awareness of comments, PIs and quoted values, so `]` or `>` inside them does not end it. This is a documented XML subset, rather than a full XML 1.0 implementation. `report-v1` requires UTF-8 and rejects namespaces; see its [grammar and whitespace contract](docs/report-v1.md).

Text uses DejaVu Sans. The repertoire is U+0009/U+000A/U+000D, U+0020–U+04FF, and U+2010–U+2027; a character outside it produces a font error. A character counts as present when the font maps it to an existing glyph (nonzero GID below `maxp` numGlyphs), even with zero advance: combining marks such as U+0301 and U+0306 are drawn over the previous glyph and kept in extracted text, without shaping, composition or normalization. One output line holds at most 1023 UTF-8 bytes; a longer run of zero-advance marks that cannot wrap is a resource error (exit 4, `KI_CAUSE_RESOURCE`). The PDF `BaseFont`/`FontName` is the embedded font's own PostScript name (name ID 6), escaped as a PDF name; a font without a usable printable-ASCII name of 1–63 characters gets `KiFont-XXXXXXXX` from a hash of its bytes, and a font whose `name` table records lie outside the table is rejected as a font error. Pages are Letter with 40 pt margins, 10 pt type, 14 pt line spacing, and at most 50 complete lines per page. Text wraps using glyph widths, not a byte-count threshold.

PDF object offsets are spooled to disk rather than retained in a growing array. `--max-pages` and `--max-output-bytes` are optional quotas; 0 disables their additional limit. The metadata spool defaults to 64 MiB when `--max-spool-bytes` is 0. There are at most 8,000,000 PDF objects regardless of those settings. The spool uses `--temp-dir`/`KiOptions.temp_dir` or `/tmp`.

For atomic file output, the temporary PDF is created beside the destination and published by `rename` after successful parsing, PDF completion, `fflush`, `fsync`, and close. Errors and cancellation before publication remove the temporary PDF and preserve the destination. `ki_convert_file` rejects an input and destination referring to the same inode, including hard or symbolic links. A destination symlink to another file is replaced by the new PDF rather than followed for writing. `SIGKILL` can leave an unfinished temporary file beside the destination.

The resource acceptance ceiling is **64 MiB converter RSS on the documented synthetic workload and Linux test environment**. Historical measurements on revision [`7c62fe4`](https://github.com/artemklen/ki_xml_pdf/commit/7c62fe4af72b097885d76a7f9f897492f5c4c2d5) recorded 2372 KiB converter RSS for a 1,073,741,815-byte synthetic report, selecting the first 1000 of 19,522,650 records: 41 PDF pages, 1,009,241 output bytes, and 720 spool bytes. These are measurements of that revision and workload, not a promise for arbitrary inputs. The [report reference](docs/report-v1.md) and [historical core contract](docs/contract.md) retain commands, budgets, and measurement boundaries.

### Resource baseline on 075455c

The [OSR-23 baseline](docs/evidence/osr-23-baseline-075455c/README.md) measured the unmodified 0.2.0 source at `075455c` on one Linux host with the bundled DejaVu Sans font. It has two separate series:

- **Full output**, every record rendered: `records-v1` and `report-v1` at 1, 16 and 64 MiB. Converter peak RSS was 2340–2524 KiB. The 64 MiB inputs produced 115,552 pages / 713,436,448 bytes (`records-v1`) and 39,033 pages / 347,124,499 bytes (`report-v1`).
- **Selection**, `report-v1 --record-limit 1000` from 16, 256 and 1024 MiB inputs: the whole input is read and validated, 1000 records are rendered. Converter peak RSS was 2340–2536 KiB, 41 pages and about 1.1 MB of PDF each time.

An independent streaming oracle checked every rendered record against the fixture recipe. Raw CSV/JSON, per-process time records for the generator, converter and reader, hashes and negative controls are in that directory. PDF size, spool size and time grow with the rendered content, and RSS includes the loaded font, so a different font changes it. Full output of a 1 GiB input was not measured. The selection rows do not describe full output, and no comparison with other converters is made.

Layout consists of sequential text blocks. Tables, global sorting, XSL-FO, UTF-16, additional operating systems, and automatic support for unknown schemas are outside the current contract. Adoption and comparisons with other converters have not been measured.

## Checks

Checks require C and C++ compilers, GNU Make, `python3`, `/usr/bin/time`, `readelf` (binutils), and Poppler’s `pdfinfo`, `pdftotext`, and `pdftohtml`. Integration checks additionally require CMake and pkg-config. On Debian/Ubuntu, the packages are `gcc g++ clang make cmake pkg-config python3 time poppler-utils`. Sanitizer jobs need these dependencies too; missing PDF readers cause a check failure.

From the source directory:

```sh
make check
make test-san
make distcheck
python3 tests/large_proof.py --self-test
python3 tests/large_proof.py 1073741824
```

`make check` cleans and rebuilds the CLI and test binary, runs the streaming API tests, and checks PDFs through Poppler. CMake users can run the registered checks with `ctest --test-dir build --output-on-failure`. RSS is measured on the ordinary converter, separately from the XML generator, PDF reader, and ASan/UBSan.

To repeat the resource series, use a disk-backed scratch directory with several GiB free (the full 64 MiB `records-v1` PDF alone is about 713 MB) and a new evidence directory:

```sh
python3 tests/resource_baseline_test.py
python3 tests/resource_baseline.py matrix --binary ./xml2pdf --font fonts/DejaVuSans.ttf \
  --source-dir . --evidence-dir "$HOME/ki-evidence/run1" --scratch "$HOME/ki-scratch"
python3 tests/resource_controls.py --binary ./xml2pdf --font fonts/DejaVuSans.ttf \
  --evidence-dir "$HOME/ki-evidence/controls1" --scratch "$HOME/ki-scratch"
python3 tests/resource_baseline.py compare docs/evidence/osr-23-baseline-075455c/baseline \
  "$HOME/ki-evidence/run1"
```

`--cases full-records-1MiB,full-report-1MiB` limits `matrix` to a subset. The runner deletes its large XML/PDF files after each case and keeps their sizes and hashes.

`tests/verify.sh` uses a temporary scratch directory or the directory named by `SCRATCH`. Its large synthetic XML/PDF files are removed after the RSS measurement; `mem.txt` retains the converter's maximum resident set size. See [CONTRIBUTING.md](CONTRIBUTING.md) for regression evidence and private security reporting.

## Source distribution

`make dist` creates `ki_xml_pdf-0.2.0.tar.gz` from the current Git HEAD and prints its SHA-256. `make distcheck` unpacks the same source archive, builds and installs it, runs external C/C++ consumers, and checks the report oracle. New build files, documentation, examples, tests, and licensing are included in the source archive.

Successful [CI](https://github.com/artemklen/ki_xml_pdf/actions/workflows/ci.yml) preserves the source archive, `SHA256SUMS`, and `SOURCE_COMMIT` as artifacts tied to the checked revision. Downloading CI artifacts requires GitHub login. A version tag, GitHub Release, or package-registry publication is a separate action. See the [release checklist](docs/release-checklist.md).
