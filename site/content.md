# ki_xml_pdf

## Hero

Convert XML into PDF with a streaming C library and command-line tool. [README.md:5-7@ec84339](https://github.com/artemklen/ki_xml_pdf/blob/ec84339db401685f60dae56a610bc4d77e831f2a/README.md#L5-L7)

- **C library and CLI:** embed the public C API or run `xml2pdf`. [README.md:5-7@ec84339](https://github.com/artemklen/ki_xml_pdf/blob/ec84339db401685f60dae56a610bc4d77e831f2a/README.md#L5-L7)
- **Streaming processing:** parser and built-in report state do not collect the whole document; PDF object offsets are spooled to disk. [README.md:271@ec84339](https://github.com/artemklen/ki_xml_pdf/blob/ec84339db401685f60dae56a610bc4d77e831f2a/README.md#L271) [README.md:292@ec84339](https://github.com/artemklen/ki_xml_pdf/blob/ec84339db401685f60dae56a610bc4d77e831f2a/README.md#L292)
- **Peak converter RSS ≤ 4 MiB on the documented synthetic workloads:** the CI resources job enforces this ceiling. This is a workload-specific acceptance gate, not a guarantee for every input, font, custom mapper or host. [README.md:296@ec84339](https://github.com/artemklen/ki_xml_pdf/blob/ec84339db401685f60dae56a610bc4d77e831f2a/README.md#L296) [.github/workflows/ci.yml:101-116@ec84339](https://github.com/artemklen/ki_xml_pdf/blob/ec84339db401685f60dae56a610bc4d77e831f2a/.github/workflows/ci.yml#L101-L116) [Resource limits wiki](https://github.com/artemklen/ki_xml_pdf/wiki/features--resource-limits)

## When to use / When not to use

### When to use

- You need a library or CLI on **64-bit Linux with glibc**, using GCC or Clang and a GNU-compatible linker. [README.md:13-25@ec84339](https://github.com/artemklen/ki_xml_pdf/blob/ec84339db401685f60dae56a610bc4d77e831f2a/README.md#L13-L25)
- You can supply the project's `report-v1` schema for sequential named fields, use `records-v1` for diagnostic element/attribute/text output, or write a mapper for your schema. [README.md:7@ec84339](https://github.com/artemklen/ki_xml_pdf/blob/ec84339db401685f60dae56a610bc4d77e831f2a/README.md#L7) [docs/mapper-guide.md:3-6@ec84339](https://github.com/artemklen/ki_xml_pdf/blob/ec84339db401685f60dae56a610bc4d77e831f2a/docs/mapper-guide.md#L3-L6)
- Your input is UTF-8 and your body text fits the documented DejaVu Sans repertoire, including Latin and Cyrillic. [README.md:275-277@ec84339](https://github.com/artemklen/ki_xml_pdf/blob/ec84339db401685f60dae56a610bc4d77e831f2a/README.md#L275-L277)
- You need file conversion or stdin/stdout pipelines, with atomic publication for a file destination. [README.md:35-54@ec84339](https://github.com/artemklen/ki_xml_pdf/blob/ec84339db401685f60dae56a610bc4d77e831f2a/README.md#L35-L54)

### When not to use

- **Other platforms:** macOS, musl/Alpine, BSD and Windows are unsupported. [README.md:13-22@ec84339](https://github.com/artemklen/ki_xml_pdf/blob/ec84339db401685f60dae56a610bc4d77e831f2a/README.md#L13-L22)
- **A full XML processor:** this is a documented XML subset. External DTDs/entities are never loaded and internal entity declarations are not expanded. [README.md:273-275@ec84339](https://github.com/artemklen/ki_xml_pdf/blob/ec84339db401685f60dae56a610bc4d77e831f2a/README.md#L273-L275)
- **UTF-16 input:** only UTF-8 is supported. [README.md:275@ec84339](https://github.com/artemklen/ki_xml_pdf/blob/ec84339db401685f60dae56a610bc4d77e831f2a/README.md#L275) [README.md:316@ec84339](https://github.com/artemklen/ki_xml_pdf/blob/ec84339db401685f60dae56a610bc4d77e831f2a/README.md#L316)
- **Namespaced report input:** `report-v1` rejects namespace prefixes and declarations except the predefined `xml:space` field attribute. [docs/report-v1.md:29-31@ec84339](https://github.com/artemklen/ki_xml_pdf/blob/ec84339db401685f60dae56a610bc4d77e831f2a/docs/report-v1.md#L29-L31)
- **Tables, global sorting or XSL-FO:** layout is sequential text blocks; these features are outside the contract. [README.md:316@ec84339](https://github.com/artemklen/ki_xml_pdf/blob/ec84339db401685f60dae56a610bc4d77e831f2a/README.md#L316)
- **Arbitrary scripts or text shaping:** body text is limited to the documented font repertoire, without shaping, composition or normalization. Characters outside it fail instead of receiving a fallback glyph. [README.md:277@ec84339](https://github.com/artemklen/ki_xml_pdf/blob/ec84339db401685f60dae56a610bc4d77e831f2a/README.md#L277)
- **Archival or accessible PDF conformance:** the project makes no PDF/A or PDF/UA claim. [README.md:90@ec84339](https://github.com/artemklen/ki_xml_pdf/blob/ec84339db401685f60dae56a610bc4d77e831f2a/README.md#L90)
- **Automatic conversion of an unknown XML schema:** schemas other than the built-in profiles need an adapter. [README.md:7@ec84339](https://github.com/artemklen/ki_xml_pdf/blob/ec84339db401685f60dae56a610bc4d77e831f2a/README.md#L7) [docs/mapper-guide.md:3-6@ec84339](https://github.com/artemklen/ki_xml_pdf/blob/ec84339db401685f60dae56a610bc4d77e831f2a/docs/mapper-guide.md#L3-L6)
- **Rollback of streamed output:** stdout and callback bytes already delivered cannot be recalled after a late error. Check success before using the PDF. [README.md:47@ec84339](https://github.com/artemklen/ki_xml_pdf/blob/ec84339db401685f60dae56a610bc4d77e831f2a/README.md#L47)

## Measured numbers

These are the OSR-82 historical measurements of **`de8ba0eeb39c9f35cb9dff2ecb576b689efd0fd4`**, before the later memory changes, **not measurements of the current source snapshot**. RSS is the median of five ordinary converter runs, measured by GNU time; Massif heap measurements are separate. [docs/evidence/osr-82-heap-profile/README.md:3-11@ec84339](https://github.com/artemklen/ki_xml_pdf/blob/ec84339db401685f60dae56a610bc4d77e831f2a/docs/evidence/osr-82-heap-profile/README.md#L3-L11) [docs/evidence/osr-82-heap-profile/README.md:47-55@ec84339](https://github.com/artemklen/ki_xml_pdf/blob/ec84339db401685f60dae56a610bc4d77e831f2a/docs/evidence/osr-82-heap-profile/README.md#L47-L55) [Revision scope in the wiki](https://github.com/artemklen/ki_xml_pdf/wiki/by-the-numbers#measurements-are-revision-scoped)

| Scenario | Actual XML bytes | Converter peak RSS, median (KiB) | Measured revision | Hardware | Command / evidence |
| --- | --- | --- | --- | --- | --- |
| `report-v1`, full output | 16,777,245 | 2,728 | `de8ba0e` | CPU/RAM not recorded | `report-full` below; [docs/evidence/osr-82-heap-profile/README.md:19-22@ec84339](https://github.com/artemklen/ki_xml_pdf/blob/ec84339db401685f60dae56a610bc4d77e831f2a/docs/evidence/osr-82-heap-profile/README.md#L19-L22) [docs/evidence/osr-82-heap-profile/README.md:51@ec84339](https://github.com/artemklen/ki_xml_pdf/blob/ec84339db401685f60dae56a610bc4d77e831f2a/docs/evidence/osr-82-heap-profile/README.md#L51) |
| `records-v1`, full output | 16,777,233 | 2,688 | `de8ba0e` | CPU/RAM not recorded | `records-full` below; [docs/evidence/osr-82-heap-profile/README.md:25-28@ec84339](https://github.com/artemklen/ki_xml_pdf/blob/ec84339db401685f60dae56a610bc4d77e831f2a/docs/evidence/osr-82-heap-profile/README.md#L25-L28) [docs/evidence/osr-82-heap-profile/README.md:52@ec84339](https://github.com/artemklen/ki_xml_pdf/blob/ec84339db401685f60dae56a610bc4d77e831f2a/docs/evidence/osr-82-heap-profile/README.md#L52) |
| `report-v1`, first 1,000 records from the same input | 16,777,245 | 2,728 | `de8ba0e` | CPU/RAM not recorded | `report-selection` below; [docs/evidence/osr-82-heap-profile/README.md:19-22@ec84339](https://github.com/artemklen/ki_xml_pdf/blob/ec84339db401685f60dae56a610bc4d77e831f2a/docs/evidence/osr-82-heap-profile/README.md#L19-L22) [docs/evidence/osr-82-heap-profile/README.md:53@ec84339](https://github.com/artemklen/ki_xml_pdf/blob/ec84339db401685f60dae56a610bc4d77e831f2a/docs/evidence/osr-82-heap-profile/README.md#L53) |

The recorded compiler is GCC (Debian 14.2.0-19) 14.2.0. The evidence's environment record does not identify CPU or RAM, so those details cannot be supplied for this run. The table is retained as a historical baseline with incomplete hardware provenance. [docs/evidence/osr-82-heap-profile/README.md:5-11@ec84339](https://github.com/artemklen/ki_xml_pdf/blob/ec84339db401685f60dae56a610bc4d77e831f2a/docs/evidence/osr-82-heap-profile/README.md#L5-L11) [docs/evidence/osr-82-heap-profile/environment.txt:1-4@ec84339](https://github.com/artemklen/ki_xml_pdf/blob/ec84339db401685f60dae56a610bc4d77e831f2a/docs/evidence/osr-82-heap-profile/environment.txt#L1-L4)

To reproduce those historical scenarios, check out the measured revision in a separate clone, build there and use its bundled font. The generation target is 16,777,216 bytes; the generator completes the last record, producing the actual sizes above. The following expands the evidence's `PROFILE`, `[extra]`, `INPUT` and `OUTPUT` placeholders for each scenario. [docs/evidence/osr-82-heap-profile/README.md:3-36@ec84339](https://github.com/artemklen/ki_xml_pdf/blob/ec84339db401685f60dae56a610bc4d77e831f2a/docs/evidence/osr-82-heap-profile/README.md#L3-L36) [docs/evidence/osr-82-heap-profile/README.md:47-53@ec84339](https://github.com/artemklen/ki_xml_pdf/blob/ec84339db401685f60dae56a610bc4d77e831f2a/docs/evidence/osr-82-heap-profile/README.md#L47-L53)

```sh
git checkout de8ba0eeb39c9f35cb9dff2ecb576b689efd0fd4
make xml2pdf libki_xml_pdf.so
python3 tests/resource_recipe.py --profile report-v1 --target-bytes 16777216 --out r16-report.xml
python3 tests/resource_recipe.py --profile records-v1 --target-bytes 16777216 --out r16-records.xml
# report-full
/usr/bin/time -f %M ./xml2pdf --font fonts/DejaVuSans.ttf --profile report-v1 --max-pages 0 --max-output-bytes 0 r16-report.xml report-full.pdf
# records-full
/usr/bin/time -f %M ./xml2pdf --font fonts/DejaVuSans.ttf --profile records-v1 --max-pages 0 --max-output-bytes 0 r16-records.xml records-full.pdf
# report-selection
/usr/bin/time -f %M ./xml2pdf --font fonts/DejaVuSans.ttf --profile report-v1 --max-pages 0 --max-output-bytes 0 --record-limit 1000 r16-report.xml report-selection.pdf
```

The current acceptance gate is separate from that historical table: **4,096 KiB converter RSS** on the documented synthetic workload. Output size, disk spool and elapsed work grow with rendered content; selection still reads and validates the entire input. No comparison with other converters has been measured. [README.md:296@ec84339](https://github.com/artemklen/ki_xml_pdf/blob/ec84339db401685f60dae56a610bc4d77e831f2a/README.md#L296) [README.md:305@ec84339](https://github.com/artemklen/ki_xml_pdf/blob/ec84339db401685f60dae56a610bc4d77e831f2a/README.md#L305) [README.md:316@ec84339](https://github.com/artemklen/ki_xml_pdf/blob/ec84339db401685f60dae56a610bc4d77e831f2a/README.md#L316) [README.md:57@ec84339](https://github.com/artemklen/ki_xml_pdf/blob/ec84339db401685f60dae56a610bc4d77e831f2a/README.md#L57)

## Install

Use 64-bit Linux/glibc with GCC or Clang, GNU Make, a GNU-compatible linker and zlib development files (`zlib1g-dev` on Debian/Ubuntu). CMake and pkg-config are required for the respective integrations below. [README.md:23@ec84339](https://github.com/artemklen/ki_xml_pdf/blob/ec84339db401685f60dae56a610bc4d77e831f2a/README.md#L23)

### Make

Start in a new working directory. The checkout is pinned to the source snapshot used for this page, `ec84339db401685f60dae56a610bc4d77e831f2a`. The Make workflow installs the CLI, public header, static/shared libraries, package metadata, font and license notices. Use the same prefix for build and install so the installed font can be found. [README.md:102-109@ec84339](https://github.com/artemklen/ki_xml_pdf/blob/ec84339db401685f60dae56a610bc4d77e831f2a/README.md#L102-L109) [Makefile:78-95@ec84339](https://github.com/artemklen/ki_xml_pdf/blob/ec84339db401685f60dae56a610bc4d77e831f2a/Makefile#L78-L95)

```sh
git clone --branch main https://github.com/artemklen/ki_xml_pdf.git
cd ki_xml_pdf
git checkout ec84339db401685f60dae56a610bc4d77e831f2a
prefix="$HOME/.local"
make PREFIX="$prefix"
make install PREFIX="$prefix"
export PATH="$prefix/bin:$PATH"
```

### pkg-config

Continue from the source directory in the same shell. Build and run the repository's C report example against the installed shared library in an external directory. `LD_LIBRARY_PATH` supplies its runtime library search path. [README.md:125-142@ec84339](https://github.com/artemklen/ki_xml_pdf/blob/ec84339db401685f60dae56a610bc4d77e831f2a/README.md#L125-L142)

```sh
export PKG_CONFIG_PATH="$prefix/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
export LD_LIBRARY_PATH="$(pkg-config --variable=libdir ki_xml_pdf)${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
consumer_dir="$(mktemp -d)"
cp examples/report.c examples/report-v1.xml "$consumer_dir/"
cd "$consumer_dir"
cc report.c $(pkg-config --cflags --libs ki_xml_pdf) -o report-shared
./report-shared report-v1.xml report-shared.pdf
```

### CMake find_package

Save this as `CMakeLists.txt` in that consumer directory. This is the README's installed CMake consumer reduced to its C report target. [README.md:156-166@ec84339](https://github.com/artemklen/ki_xml_pdf/blob/ec84339db401685f60dae56a610bc4d77e831f2a/README.md#L156-L166)

```cmake
cmake_minimum_required(VERSION 3.16)
project(ki_consumer LANGUAGES C)
find_package(ki_xml_pdf CONFIG REQUIRED)
add_executable(report-client report.c)
target_link_libraries(report-client PRIVATE ki_xml_pdf::ki_xml_pdf)
```

Build and run with the installed shared target, then select the installed static variant. [README.md:170-181@ec84339](https://github.com/artemklen/ki_xml_pdf/blob/ec84339db401685f60dae56a610bc4d77e831f2a/README.md#L170-L181)

```sh
cmake -S . -B shared -DCMAKE_PREFIX_PATH="$prefix"
cmake --build shared
./shared/report-client report-v1.xml cmake-shared.pdf
cmake -S . -B static -DCMAKE_PREFIX_PATH="$prefix" -Dki_xml_pdf_USE_STATIC_LIBS=ON
cmake --build static
./static/report-client report-v1.xml cmake-static.pdf
```

## Usage

### CLI: file and stdin/stdout

Continue in the consumer directory created above; it contains the repository's `report-v1.xml` fixture. `-` selects stdin or stdout. During conversion stdout contains PDF bytes and diagnostics go to stderr. [README.md:35-45@ec84339](https://github.com/artemklen/ki_xml_pdf/blob/ec84339db401685f60dae56a610bc4d77e831f2a/README.md#L35-L45)

```sh
xml2pdf --profile report-v1 report-v1.xml report.pdf
cat report-v1.xml | xml2pdf --profile report-v1 - from-stdin.pdf
xml2pdf --profile report-v1 report-v1.xml - > from-file.pdf
cat report-v1.xml | xml2pdf --profile report-v1 - - > pipeline.pdf
```

Check the converter exit status. A successful file conversion publishes the complete PDF atomically; a late error on stdout can leave a partial PDF. [README.md:47@ec84339](https://github.com/artemklen/ki_xml_pdf/blob/ec84339db401685f60dae56a610bc4d77e831f2a/README.md#L47)

### C API

The complete `report.c` example copied during Install is shown below unchanged. It initializes options, selects the report profile and reports conversion errors. [examples/report.c:1-22@ec84339](https://github.com/artemklen/ki_xml_pdf/blob/ec84339db401685f60dae56a610bc4d77e831f2a/examples/report.c#L1-L22)

```c
#include "ki_xml_pdf.h"

#include <stdio.h>

/* xml2pdf --profile report-v1 is the CLI. This file is the same choice through the API. */

int main(int argc, char **argv) {
    const char *in = argc > 1 ? argv[1] : "examples/report-v1.xml";
    const char *out = argc > 2 ? argv[2] : "report.pdf";
    KiOptions opt;
    ki_options_init(&opt);
    opt.profile = KI_PROFILE_REPORT;
    KiResult result;
    int rc = ki_convert_file(in, out, &opt, &result);
    if (rc != 0) {
        fprintf(stderr, "report: %s\n", result.message);
        return rc;
    }
    printf("pages=%d total=%llu selected=%llu skipped=%llu\n", result.pages, result.report_total,
           result.report_selected, result.report_skipped);
    return 0;
}
```

Compile that example and convert the fixture with the installed package. [README.md:139-142@ec84339](https://github.com/artemklen/ki_xml_pdf/blob/ec84339db401685f60dae56a610bc4d77e831f2a/README.md#L139-L142)

```sh
cc report.c $(pkg-config --cflags --libs ki_xml_pdf) -o report-api
./report-api report-v1.xml report-api.pdf
```

The built-in report profile uses a fixed Russian record label even when field names and values are English. A custom mapper controls its own labels. [docs/report-v1.md:39@ec84339](https://github.com/artemklen/ki_xml_pdf/blob/ec84339db401685f60dae56a610bc4d77e831f2a/docs/report-v1.md#L39) [docs/mapper-guide.md:3-6@ec84339](https://github.com/artemklen/ki_xml_pdf/blob/ec84339db401685f60dae56a610bc4d77e831f2a/docs/mapper-guide.md#L3-L6)

## Custom XML schema

Start with the [mapper guide](https://github.com/artemklen/ki_xml_pdf/blob/ec84339db401685f60dae56a610bc4d77e831f2a/docs/mapper-guide.md). The working catalog adapter demonstrates a project-owned schema through the public API without changing the parser, layout or PDF writer. It is an example to adapt, not automatic support for unknown schemas. [docs/mapper-guide.md:3-6@ec84339](https://github.com/artemklen/ki_xml_pdf/blob/ec84339db401685f60dae56a610bc4d77e831f2a/docs/mapper-guide.md#L3-L6)

Mapper events are borrowed for the duration of the callback. Keep bounded mapper state, emit text through `ki_doc_text` / `ki_doc_break`, and report schema failures with `ki_doc_fail`. [README.md:245@ec84339](https://github.com/artemklen/ki_xml_pdf/blob/ec84339db401685f60dae56a610bc4d77e831f2a/README.md#L245) [Custom mappers wiki](https://github.com/artemklen/ki_xml_pdf/wiki/features--custom-mappers)

## Comparison guidance

Use the documented limits to decide whether to evaluate another tool:

- For tables, global sorting or an XSL-FO workflow, look for a converter whose documented contract covers those requirements; they are outside ki_xml_pdf's scope. [README.md:316@ec84339](https://github.com/artemklen/ki_xml_pdf/blob/ec84339db401685f60dae56a610bc4d77e831f2a/README.md#L316)
- For a platform other than Linux/glibc, seek a library that explicitly supports your deployment target. [README.md:13-22@ec84339](https://github.com/artemklen/ki_xml_pdf/blob/ec84339db401685f60dae56a610bc4d77e831f2a/README.md#L13-L22)
- For PDF/A or PDF/UA conformance, choose a workflow with an explicit conformance claim and validation; ki_xml_pdf makes neither claim. [README.md:90@ec84339](https://github.com/artemklen/ki_xml_pdf/blob/ec84339db401685f60dae56a610bc4d77e831f2a/README.md#L90)
- For body text beyond the documented repertoire or for shaping, evaluate a text engine that documents those needs. [README.md:277@ec84339](https://github.com/artemklen/ki_xml_pdf/blob/ec84339db401685f60dae56a610bc4d77e831f2a/README.md#L277)
- For an existing XML vocabulary, budget for a mapper here or evaluate a converter that explicitly documents that vocabulary. [docs/mapper-guide.md:3-6@ec84339](https://github.com/artemklen/ki_xml_pdf/blob/ec84339db401685f60dae56a610bc4d77e831f2a/docs/mapper-guide.md#L3-L6)

These are selection criteria, not claims about any named competing library. The repository reports no measured adoption or comparisons with other converters. [README.md:316@ec84339](https://github.com/artemklen/ki_xml_pdf/blob/ec84339db401685f60dae56a610bc4d77e831f2a/README.md#L316)

## License, links, contributing, security

Project source and original examples use the **MIT license**. The bundled DejaVu Sans font has a separate license notice. [CONTRIBUTING.md:3@ec84339](https://github.com/artemklen/ki_xml_pdf/blob/ec84339db401685f60dae56a610bc4d77e831f2a/CONTRIBUTING.md#L3)

- [Source repository](https://github.com/artemklen/ki_xml_pdf)
- [English README](https://github.com/artemklen/ki_xml_pdf/blob/ec84339db401685f60dae56a610bc4d77e831f2a/README.md)
- [Russian README](https://github.com/artemklen/ki_xml_pdf/blob/ec84339db401685f60dae56a610bc4d77e831f2a/README.ru.md)
- [Repository wiki](https://github.com/artemklen/ki_xml_pdf/wiki)
- [Public C API](https://github.com/artemklen/ki_xml_pdf/blob/ec84339db401685f60dae56a610bc4d77e831f2a/include/ki_xml_pdf.h)
- [MIT license](https://github.com/artemklen/ki_xml_pdf/blob/ec84339db401685f60dae56a610bc4d77e831f2a/LICENSE)
- [Font license notice](https://github.com/artemklen/ki_xml_pdf/blob/ec84339db401685f60dae56a610bc4d77e831f2a/fonts/DejaVuSans.copyright)
- [Contributing](https://github.com/artemklen/ki_xml_pdf/blob/ec84339db401685f60dae56a610bc4d77e831f2a/CONTRIBUTING.md)
- [Code of conduct](https://github.com/artemklen/ki_xml_pdf/blob/ec84339db401685f60dae56a610bc4d77e831f2a/CODE_OF_CONDUCT.md)
- [Security policy](https://github.com/artemklen/ki_xml_pdf/blob/ec84339db401685f60dae56a610bc4d77e831f2a/SECURITY.md)
- [CI checks](https://github.com/artemklen/ki_xml_pdf/actions/workflows/ci.yml)

For ordinary bugs, use [GitHub Issues](https://github.com/artemklen/ki_xml_pdf/issues) with the source revision, environment, command and a small shareable reproducer. Send vulnerability reports privately to **artem.klen@icloud.com**, with the affected revision, impact and reproduction steps; do not publish security details in an issue. [CONTRIBUTING.md:7-18@ec84339](https://github.com/artemklen/ki_xml_pdf/blob/ec84339db401685f60dae56a610bc4d77e831f2a/CONTRIBUTING.md#L7-L18)

Version **0.3.0** is read from the public header. The security policy supports only the latest minor release before the stable API; older minor releases are unsupported. [include/ki_xml_pdf.h:43@ec84339](https://github.com/artemklen/ki_xml_pdf/blob/ec84339db401685f60dae56a610bc4d77e831f2a/include/ki_xml_pdf.h#L43) [SECURITY.md:3-14@ec84339](https://github.com/artemklen/ki_xml_pdf/blob/ec84339db401685f60dae56a610bc4d77e831f2a/SECURITY.md#L3-L14)
