# Tooling
Active contributors: Artem

GNU Make drives the development/CI checks; CMake provides out-of-tree builds and installed package targets. Both build the same eight core C units and require zlib.

## Make targets

| Target | Scope in `Makefile` |
| --- | --- |
| `all` | Default: CLI linked with the static library and `-lz` |
| `libki_xml_pdf.a`, `libki_xml_pdf.so` | Explicit library builds; shared soname `libki_xml_pdf.so.0` |
| `test` | C groups, Python xref offsets and stream checks |
| `check` | Clean build, tests, synthetic RSS verification, examples and CLI/font/wrap/geometry/Info/integration/catalog checks |
| `test-san` | ASan/UBSan C groups with leak detection and fatal diagnostics |
| `example` | C/C++ consumers, report example and public catalog mapper |
| `install` | CLI, static/shared libraries, header, font/notices, CMake and pkg-config metadata |
| `dist`, `distcheck` | Exact-HEAD archive; clean extracted-source acceptance |
| `print-version`, `print-dist-name` | Working-tree header version; HEAD-derived archive name |
| `clean` | Generated binaries, objects, dependencies and font-prefix stamp |

Default flags are C99, `-Wall -Wextra -Wpedantic -O2 -g -fPIC` and `_POSIX_C_SOURCE=200809L`.
`-MMD -MP` generates header dependencies.
`src/.font_prefix` tracks `PREFIX` so a prefix switch rebuilds `src/font.o`.
Tests add pthreads and GNU wrappers for fsync and zlib fault injection.

## CMake and CTest

`CMakeLists.txt` requires CMake 3.16+, 64-bit Linux/glibc and `find_package(ZLIB REQUIRED)`.
It builds both library variants regardless of the default selection:

- Build targets: `ki_xml_pdf_static`, `ki_xml_pdf_shared`, `xml2pdf`, `catalog_example`.
- Consumer names: `ki_xml_pdf::static`, `ki_xml_pdf::shared`, `ki_xml_pdf::ki_xml_pdf`.
- `BUILD_SHARED_LIBS` defaults ON and selects the last target's variant.
- `BUILD_TESTING` defaults ON; it adds the internal C test executable, Threads and Python.
- CTest registers nine tests; see [Testing](testing.md) for exact names.

The test executable compiles the core sources directly because internal symbols are hidden from the shared ABI.
The build copies the font to the binary tree.
The installed CLI has an `$ORIGIN`-relative library rpath; do not confuse library lookup with the separately compiled font path.

## Installed discovery and ABI

`cmake/ki_xml_pdfConfig.cmake.in` finds zlib and imports package targets.
`ki_xml_pdf_USE_STATIC_LIBS` selects the default consumer target when explicitly set.
The static target carries zlib's link-only dependency.
`cmake/ki_xml_pdf.pc.in` records `Libs.private: -lz`; raw archive users must add `-lz`, while pkg-config users need `--static` for static dependency flags.

`libki_xml_pdf.map` has three inherited export nodes:

| Node | Additions |
| --- | --- |
| `KI_0_1` | Options initializer, file/callback conversion, document helpers and two legacy wrappers |
| `KI_0_2` | `ki_convert_to_file` |
| `KI_0_3` | `ki_config_init`, `ki_convert_cfg`, `ki_convert_file_cfg`, `ki_convert_to_file_cfg` |

Other symbols are local.
Concrete 0.2 binary compatibility is tested separately with old-header canaries.
The CMake version file deliberately matches only the same minor before 1.0; it does not advertise all 0.y releases as interchangeable.

## CI execution

`.github/workflows/ci.yml` runs on PRs, `main` pushes and manual dispatch.
It uses read-only repository permissions and cancels stale runs per ref.
PR checkout is pinned to the head SHA; other runs use the event SHA.

| Job | Checks |
| --- | --- |
| `gcc` | Functional/API/PDF suites, all external feature checks, installed consumers, documented examples, fuzz, header rebuild, distcheck and negative controls, prefix switch |
| `clang` | Functional tests/examples, installed consumers, ASan/UBSan |
| `asan` | GCC ASan/UBSan |
| `resources` | Ordinary `make check`; resource-judge self-test and 1 GiB selected-output proof |
| `source` | Needs all four jobs; archives HEAD, verifies provenance and uploads source/checksum/commit |

Actions are `actions/checkout@v4` and `actions/upload-artifact@v7.0.2`.
Version tags trigger a separate workflow, not these jobs.
The release workflow does not rerun CI or enforce the main-green precondition itself.
See [Distribution and releases](../deployment.md).

## Research is separate

`tests/research_font_subset.py`, `tests/research_utf16.py`, `tests/research_utf16_adapter.c` and `tests/research_table.py` are research harnesses, not linked runtime features.
They are not ordinary Make/CMake/CTest acceptance targets.
Additional research tools do not become product dependencies by appearing in evidence.
The extended resource-series runners are also explicit evidence tools, not hidden steps of `make check`.

## Key source files

| File | Role |
| --- | --- |
| `Makefile`, `CMakeLists.txt` | Targets, flags and test registration |
| `cmake/ki_xml_pdfConfig.cmake.in` | Installed targets and zlib discovery |
| `cmake/ki_xml_pdfConfigVersion.cmake.in` | Same-minor package matching |
| `cmake/ki_xml_pdf.pc.in` | pkg-config link flags |
| `libki_xml_pdf.map` | ELF exports |
| `.github/workflows/ci.yml`, `.github/workflows/release.yml` | CI versus tag publication |

Related: [Configuration](../reference/configuration.md), [Dependencies](../reference/dependencies.md).
