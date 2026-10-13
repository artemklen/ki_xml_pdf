# Dependencies

The library has no third-party code: at runtime it needs glibc and the bundled font, nothing else. The tool list grows only for building and checking. Versions below come from `CMakeLists.txt`, `Makefile`, the test scripts and `.github/workflows/ci.yml`.

## Runtime

| Dependency | Why | Where enforced |
| --- | --- | --- |
| glibc | `ki_convert` wraps `write_bytes` with `fopencookie` | `CMakeLists.txt` compiles a `__GLIBC__` probe and fails configuration without it |
| 64-bit Linux | Validated platform; `CMakeLists.txt` rejects others | `CMakeLists.txt` checks `CMAKE_SYSTEM_NAME` and pointer size |
| POSIX 200809 | `mkstemp`, `fsync`, `rename`, `sigaction`, `readlink` | `_POSIX_C_SOURCE=200809L` in `Makefile`; `_GNU_SOURCE` in `src/convert.c` and `src/pdf_write.c` for `fopencookie` |
| DejaVu Sans | The one embedded font, installed with the package | `fonts/DejaVuSans.ttf`, notice in `fonts/DejaVuSans.copyright` |

There is no network access, no XML library, no PDF library and no image library. Project source and examples are MIT (`LICENSE`); the font keeps its separate notice.

## Build

| Tool | Used for |
| --- | --- |
| GCC or Clang (C99) | Compiling `src/` and `examples/` |
| GNU Make | The `Makefile` targets (`all`, `test`, `check`, `test-san`, `example`, `install`, `dist`, `distcheck`, `clean`) |
| GNU-compatible linker and `ar` | `--version-script=libki_xml_pdf.map`, soname `libki_xml_pdf.so.0`, `libki_xml_pdf.a` |
| CMake 3.16+ | Out-of-tree builds, CTest, installation, consumer discovery (optional but exercised by the checks) |
| pkg-config | Consumer discovery against installed packages |
| binutils (`readelf`) | Linkage and soname verification in the check scripts |

## Test tools

| Tool | Used by |
| --- | --- |
| C++ compiler (GCC or Clang) | `examples/consume.cpp` and the installed C++ consumers in `tests/build_integration.sh`, `tests/distcheck.sh`, `tests/docs_smoke.sh` |
| Python 3 | `tests/verify.sh`, `tests/cli_pipeline.py`, `tests/catalog_verify.py`, `tests/fuzz_smoke.py`, `tests/large_proof.py`, and CMake configure when `BUILD_TESTING` is on. Standard library only: no pip packages |
| `/usr/bin/time` | RSS measurement in `tests/verify.sh` and `tests/large_proof.py` (the `time` package on Debian/Ubuntu) |
| Poppler: `pdfinfo`, `pdftotext`, `pdftohtml` | The independent PDF readers used as oracles |
| bash, `tar`, `mktemp` | `tests/verify.sh`, `tests/distcheck.sh`, `tests/build_integration.sh`, `tests/docs_smoke.sh` |

On Debian or Ubuntu the full set is:

```sh
sudo apt-get install -y gcc g++ clang make cmake pkg-config binutils python3 time poppler-utils
```

The Python scripts import only the standard library (`argparse`, `pathlib`, `subprocess`, `tempfile`, `random`, `hashlib`, `re`, `xml.etree.ElementTree`), so no virtual environment is needed.

## CI

GitHub Actions on `ubuntu-latest` runners. Two actions, pinned as recorded in `.github/workflows/ci.yml`:

| Action | Version | Used for |
| --- | --- | --- |
| `actions/checkout` | `v4` | Every job, with the PR head commit as the ref |
| `actions/upload-artifact` | `v7.0.2` | The `source` job: archive, `SHA256SUMS`, `SOURCE_COMMIT` |

Each job installs only what its steps need: the `gcc` job adds `gcc g++ make cmake pkg-config binutils poppler-utils python3 time`; `clang` swaps `gcc` for `clang`; `asan` installs `gcc make poppler-utils python3`; `resources` installs `gcc g++ make poppler-utils python3 time`. The job graph is on [Tooling](../how-to-contribute/tooling.md).

## Related pages

- [Getting started](../overview/getting-started.md) for the install commands in context.
- [Testing](../how-to-contribute/testing.md) for how each tool is used.
- [Packaging](../features/packaging.md) for what lands in an installed prefix.
- [Release and distribution](../deployment.md) for artifact provenance.
