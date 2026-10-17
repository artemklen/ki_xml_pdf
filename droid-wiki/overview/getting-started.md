# Getting started

Build on 64-bit Linux with glibc using GCC or Clang. `README.md` is the full onboarding reference; these commands cover a source build, a report and the main validation paths.

## Prerequisites

The build requires a C99 compiler, GNU Make, a GNU-compatible linker and zlib development files. Checks also use C++, Python 3, GNU time, binutils and Poppler. CMake integration requires CMake 3.16 or newer and pkg-config (`Makefile`, `CMakeLists.txt`, `.github/workflows/ci.yml`).

On Debian/Ubuntu, the documented package set is:

```sh
sudo apt-get install gcc g++ clang make cmake pkg-config zlib1g-dev \
  binutils python3 time poppler-utils
```

## Build and convert

Run from the repository root:

```sh
make
./xml2pdf --version
./xml2pdf --profile report-v1 examples/report-v1.xml report.pdf
pdfinfo report.pdf
pdftotext report.pdf -
```

The output is a sequential report, not a table layout. The default profile is the diagnostic `records-v1`; select `report-v1` explicitly (`src/main.c`, `docs/report-v1.md`).

```sh
./xml2pdf --profile report-v1 --page-size a4 --orientation landscape \
  --margin 36 --font-size 12 --title "Quarterly report" \
  examples/report-v1.xml a4.pdf
./xml2pdf --profile report-v1 examples/report-v1.xml - > streamed.pdf
```

Check the exit status before consuming stdout output: late failure can leave a partial PDF. A named output is replaced atomically on success (`src/convert.c`).

## CMake

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Tests are enabled by default and require Python at configure time. `-DBUILD_TESTING=OFF` disables test targets. Both static and shared libraries are built; `BUILD_SHARED_LIBS` selects the default target (`CMakeLists.txt`).

## Install and use

```sh
make PREFIX="$HOME/.local"
make install PREFIX="$HOME/.local"
```

Use the same prefix for build and installation because it sets the runtime font path. `DESTDIR` stages files without changing that path. Installed clients use `find_package(ki_xml_pdf CONFIG REQUIRED)` and `ki_xml_pdf::ki_xml_pdf`, or `pkg-config --cflags --libs ki_xml_pdf`. Static manual linking needs zlib too (`cmake/ki_xml_pdfConfig.cmake.in`, `cmake/ki_xml_pdf.pc.in`).

See [Packaging](../features/packaging.md) for external C/C++ consumers and relocation.

## Validate a change

```sh
make test
make check
make test-san
```

`make check` cleans and rebuilds before functional, resource and reader checks; do not run it concurrently with other Make builds in the same tree. Sanitizers and ordinary converter RSS are separate checks (`Makefile`, `tests/verify.sh`). For documentation changes, `tests/docs_smoke.sh` exercises documented clients and commands. Source archive checks use committed HEAD rather than uncommitted edits. See [Testing](../how-to-contribute/testing.md).
