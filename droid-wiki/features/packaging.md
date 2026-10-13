# Packaging

Active contributors: Artem Klen

ki_xml_pdf ships through two build systems that produce the same installed layout: GNU Make and CMake. The installed package is a CLI, static and shared libraries, one public header, the DejaVu Sans font, CMake and pkg-config discovery metadata; the default font path is compiled into the library from the install prefix.

## Purpose

Make the library consumable outside the source tree: `find_package(ki_xml_pdf)` or `pkg-config` for compilation and linking, an installed `xml2pdf` that finds its own shared library through an rpath, and a font that is found without environment setup, while `DESTDIR` staging stays a packaging-only concept.

## Build systems

| System | Entry | Notes |
| --- | --- | --- |
| Make | `Makefile` | `PREFIX` (default `/usr/local`), `DESTDIR`, `make install`, `make dist` |
| CMake | `CMakeLists.txt` | Requires CMake 3.16, `CMAKE_INSTALL_PREFIX`, `BUILD_SHARED_LIBS` (default ON), `BUILD_TESTING` |

`CMakeLists.txt` refuses to configure anywhere except 64-bit Linux with glibc: the shared build needs glibc's `fopencookie` for `ki_convert`. Both systems build the same seven library sources, the CLI and the catalog example.

## Installed layout

| Path under prefix | Contents |
| --- | --- |
| `bin/xml2pdf` | CLI |
| `lib/libki_xml_pdf.a` | Static library |
| `lib/libki_xml_pdf.so` and `lib/libki_xml_pdf.so.0` | Shared library, soname `libki_xml_pdf.so.0` (CMake also writes `libki_xml_pdf.so.0.2.0`) |
| `include/ki_xml_pdf.h` | The only public header |
| `share/ki_xml_pdf/DejaVuSans.ttf`, `share/ki_xml_pdf/DejaVuSans.copyright`, `share/ki_xml_pdf/LICENSE` | Font and license notices |
| `lib/pkgconfig/ki_xml_pdf.pc` | pkg-config metadata |
| `lib/cmake/ki_xml_pdf/ki_xml_pdfConfig.cmake`, `ki_xml_pdfConfigVersion.cmake`, `ki_xml_pdfTargets.cmake` | CMake package |

The Makefile creates the `.so.0` name with `ln -sfn`; CMake sets `VERSION 0.2.0` and `SOVERSION 0`. `tests/build_integration.sh` checks the soname with `readelf`.

## CMake package

Consumers use:

```cmake
find_package(ki_xml_pdf CONFIG REQUIRED)
target_link_libraries(client PRIVATE ki_xml_pdf::ki_xml_pdf)
```

`ki_xml_pdf::ki_xml_pdf` is an interface target defined in `cmake/ki_xml_pdfConfig.cmake.in` that links to `ki_xml_pdf::shared` or `ki_xml_pdf::static`. The choice is explicit through `ki_xml_pdf_USE_STATIC_LIBS` (`ON` or `OFF`); without it the default recorded at install time applies (`@KI_DEFAULT_SHARED@`, which is `ON` for the Makefile install and equals `BUILD_SHARED_LIBS` for CMake). Both variant targets always exist, so `ki_xml_pdf::static` and `ki_xml_pdf::shared` can be linked directly.

`cmake/ki_xml_pdfConfigVersion.cmake.in` marks a requested version compatible only when it equals the installed version, or when it is older with the same major and minor number (before 1.0 a minor bump may change the ABI). A non-64-bit consumer is unsuitable. The Makefile install writes the same three files by `sed` substitution from `cmake/ki_xml_pdfMakeTargets.cmake.in` and the `.in` templates, so both installations expose identical target names; the Makefile's targets file derives the prefix from its own location rather than hard-coded paths.

## pkg-config

`cmake/ki_xml_pdf.pc.in` yields `prefix`, `libdir`, `includedir`, `Libs: -L${libdir} -lki_xml_pdf` and `Cflags: -I${includedir}`. The Makefile substitutes `$(PREFIX)/lib` and `$(PREFIX)/include`; CMake uses the full `GNUInstallDirs` paths. For static linking against an installation that also contains the shared library, the README's pattern is `pkg-config --static --libs` wrapped in `-Wl,-Bstatic ... -Wl,-Bdynamic`.

## Symbol versioning

Exports of the shared library are fixed by `libki_xml_pdf.map` with versioned nodes:

| Node | Symbols |
| --- | --- |
| `KI_0_1` | `ki_convert`, `ki_convert_file`, `ki_doc_text`, `ki_doc_break`, `ki_doc_fail`, `ki_options_init`, `xml2pdf_convert`, `xml2pdf_convert_cap` |
| `KI_0_2` | `ki_convert_to_file` |

Everything else is `local: *`, so internal symbols never enter the ABI. A new public function needs a new version node. The legacy `xml2pdf_convert` and `xml2pdf_convert_cap` are declared in `src/convert.h`, exported for compatibility, and not part of `include/ki_xml_pdf.h`.

## Font path compiled from the prefix

The default font path is a compile-time constant. The Makefile passes `-DXML2PDF_FONT_PATH="$(PREFIX)/share/ki_xml_pdf/DejaVuSans.ttf"`; CMake sets `KI_FONT_PATH` to `${CMAKE_INSTALL_FULL_DATADIR}/ki_xml_pdf/DejaVuSans.ttf` and defines the same macro. At runtime the PDF writer in `src/pdf_write.c` first loads `KiOptions.font_path` directly when it is set (no fallback if it fails). When it is not set, `font_open_default` in `src/font.c` tries, in order:

1. the `KI_XML_PDF_FONT` environment variable,
2. the compiled `XML2PDF_FONT_PATH`,
3. paths beside the executable (`fonts/DejaVuSans.ttf`, `../fonts/DejaVuSans.ttf`, `../share/ki_xml_pdf/DejaVuSans.ttf`), which lets the build-tree binary work from any directory,
4. `fonts/DejaVuSans.ttf` relative to the working directory.

Use the same prefix when building and installing. A relocated installation must set `KiOptions.font_path` or `KI_XML_PDF_FONT`; an explicitly supplied font that fails is an error and is never silently replaced.

## The src/.font_prefix rebuild trick

Because the prefix reaches the library through a preprocessor macro, rebuilding with a different prefix has to recompile `src/font.o`. The `Makefile` implements this with a stamp: the target `src/.font_prefix` writes `$(PREFIX)` to the stamp file only when it differs, and `src/font.o` depends on the stamp. So `make install PREFIX=elsewhere` without `make clean` rewrites the stamp, recompiles `src/font.o` with the new path, and relinks. `tests/prefix_switch.sh` exercises exactly this: it builds with the default prefix, installs to a second prefix without cleaning, verifies the consumer finds the font, installs to a third prefix, then back to the second.

## DESTDIR and rpath

`DESTDIR` stages files under a different root without changing the compiled font path or the prefix recorded in discovery metadata. `tests/build_integration.sh` proves both claims: it installs with `DESTDIR`, moves the staged tree to the real prefix, and asserts that `pkg-config --variable=prefix` reports the real prefix, that neither `lib/pkgconfig` nor `lib/cmake` contains the staging path, and that the binary libraries contain `<prefix>/share/ki_xml_pdf/DejaVuSans.ttf` but no staging path.

The installed CLI carries `INSTALL_RPATH "$ORIGIN/<path from bin to lib>"` (CMake computes the relative path), so `bin/xml2pdf` resolves `libki_xml_pdf.so.0` without `LD_LIBRARY_PATH`. The Makefile-installed CLI links the static library directly. Shared-library consumers that do not use CMake need the library directory in the runtime linker path (an rpath like `-Wl,-rpath,$libdir`, as used by the test scripts).

## Source distribution

`make dist` builds `ki_xml_pdf-<version>.tar.gz` from `git archive HEAD`, so the archive always describes HEAD even in a dirty tree (`DIST_VERSION` is read from `git show HEAD:include/ki_xml_pdf.h`), and prints its SHA-256. `make distcheck` unpacks the archive, builds and installs it, runs external C and C++ consumers and checks the report oracle (`tests/distcheck.sh`). CI preserves the archive, `SHA256SUMS` and `SOURCE_COMMIT` as artifacts.

## Packaging checks

| Script | What it proves |
| --- | --- |
| `tests/install_smoke.sh` | Installs into a private prefix, builds four consumers (C and C++, static and shared), runs them from `/` with `KI_XML_PDF_FONT` unset, verifies the PDFs |
| `tests/prefix_switch.sh` | Reinstall to different prefixes without `make clean`, relying on `src/.font_prefix`; rejects an explicit missing font |
| `tests/build_integration.sh` | The full matrix: Make and CMake installs, `DESTDIR` staging, `ctest`, 16 installed consumer rows (two build systems by two languages by static/shared by CMake/pkg-config discovery), readelf soname checks, the catalog suite against installed libraries, and font-failure negatives |

The build-integration matrix also verifies that the installed CMake package rejects the wrong variant (`ki_xml_pdf_USE_STATIC_LIBS` selecting a `STATIC_LIBRARY` versus `SHARED_LIBRARY` target) and that installed clients fail when the installed font is missing rather than falling back to a source tree.

## Key source files

| File | Contents |
| --- | --- |
| `CMakeLists.txt` | CMake build, install rules, exported targets, rpath, tests |
| `Makefile` | Default build, install, dist, distcheck, `src/.font_prefix` stamp |
| `cmake/ki_xml_pdfConfig.cmake.in` | `ki_xml_pdf::ki_xml_pdf` interface and variant selection |
| `cmake/ki_xml_pdfMakeTargets.cmake.in` | Imported targets for the Makefile-installed package |
| `cmake/ki_xml_pdfConfigVersion.cmake.in` | Version compatibility policy |
| `cmake/ki_xml_pdf.pc.in` | pkg-config template |
| `libki_xml_pdf.map` | Symbol versioning script |
| `src/font.c` | Default font lookup order |
| `tests/install_smoke.sh`, `tests/prefix_switch.sh`, `tests/build_integration.sh` | Install verification suites |

## Related pages

- [CLI](cli.md) for the installed `xml2pdf` behavior.
- [C API](../api/c-api.md) for what the installed header exposes.
- [Getting started](../overview/getting-started.md) for the first build and convert.
