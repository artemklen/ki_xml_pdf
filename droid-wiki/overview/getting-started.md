# Getting started

This page covers the prerequisites, the two build systems, a first conversion and the main checks. The authoritative copy of these commands is `README.md`; `tests/docs_smoke.sh` runs the README blocks in CI so they stay correct.

## Prerequisites

- 64-bit Linux with glibc. `CMakeLists.txt` fails configuration on anything else.
- GCC or Clang, GNU Make, a GNU-compatible linker.
- For checks: a C++ compiler, `python3`, `/usr/bin/time`, `readelf` (binutils), and Poppler's `pdfinfo`, `pdftotext`, `pdftohtml`.
- For integration checks: CMake 3.16+ and pkg-config.

On Debian or Ubuntu:

```sh
sudo apt-get install -y gcc g++ clang make cmake pkg-config binutils python3 time poppler-utils
```

## Build with Make

```sh
make
./xml2pdf --profile report-v1 examples/report-v1.xml report.pdf
```

`make` builds `libki_xml_pdf.a` and the `xml2pdf` binary. The default font path is compiled in from `PREFIX` (`-DXML2PDF_FONT_PATH` in `Makefile`), so use the same `PREFIX` for build and install:

```sh
make PREFIX="$HOME/.local"
make install PREFIX="$HOME/.local"
```

When the font is not installed yet, `font_open_default` in `src/font.c` also looks for `fonts/DejaVuSans.ttf` relative to the executable, which is why `./xml2pdf` works straight from the source tree.

## Build with CMake

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON
cmake --build build
./build/xml2pdf --profile report-v1 examples/report-v1.xml cmake-report.pdf
ctest --test-dir build --output-on-failure
```

CMake enables tests by default and needs Python 3 at configure time. Pass `-DBUILD_TESTING=OFF` to skip test targets.

## Try the CLI

```sh
# records-v1 (default): element names, attributes, text
./xml2pdf examples/hello.xml hello.pdf

# report-v1 with selection and quotas
./xml2pdf --profile report-v1 --record-offset 0 --record-limit 1000 \
  --max-pages 10000 --max-output-bytes 268435456 input.xml report.pdf

# pipelines: '-' is stdin for input, stdout for output
cat examples/report-v1.xml | ./xml2pdf --profile report-v1 - - > pipeline.pdf
```

Exit codes: 0 success, 1 XML/profile/font, 2 usage, 3 I/O, 4 quota or cancellation. See [CLI](../features/cli.md).

## Run the checks

| Command | What it covers |
| --- | --- |
| `make test` | Builds and runs `tests/test_stream.c` (unit and API tests) |
| `make check` | Clean rebuild, `make test`, `tests/verify.sh` (16 MiB RSS gate, Poppler), examples, CLI pipeline and catalog oracles |
| `make test-san` | Same unit tests under ASan and UBSan |
| `make distcheck` | Builds and installs the `git archive` of HEAD, runs external consumers |
| `python3 tests/large_proof.py 1073741824` | 1 GiB synthetic report, RSS and page budgets |

More detail is in [Testing](../how-to-contribute/testing.md).

## Next steps

- Read the [Architecture](architecture.md) page.
- Embed the library through the [C API](../api/c-api.md).
- Write a mapper for your own schema with [Custom mappers](../features/custom-mappers.md).
