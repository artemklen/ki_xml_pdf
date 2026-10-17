# Packaging
Active contributors: Artem

ki_xml_pdf 0.3.0 installs the CLI, static/shared libraries, one public header, font/licenses and CMake/pkg-config discovery metadata. Both build systems target 64-bit Linux with glibc, GCC/Clang and a GNU-compatible linker; zlib is a required dependency.

## Build and install

```sh
make PREFIX="$HOME/.local"
make install PREFIX="$HOME/.local"

cmake -S . -B build-install -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$HOME/.local" -DBUILD_SHARED_LIBS=ON
cmake --build build-install
cmake --install build-install
```

Use the same prefix at build/install because it sets the compiled runtime font path. Both libraries install regardless of `BUILD_SHARED_LIBS`; that CMake option selects the default library target, not which variant exists.

`CMakeLists.txt` requires CMake 3.16 and rejects non-Linux, non-64-bit and non-glibc builds. `src/platform.h` rejects unsupported platforms during compilation. macOS, musl/Alpine, BSD and Windows are not supported. CMake tests default on and require Python 3; `-DBUILD_TESTING=OFF` omits test targets.

Packaging consists of two top-level build files and four discovery templates in `cmake/`. Three dedicated install/prefix/integration scripts test that surface (3:6 test-to-build/template-file ratio), with source-distribution and ABI suites alongside them.

## Installed layout

Make uses `lib`/`include`/`share` below `PREFIX`; CMake uses `GNUInstallDirs`, so the configured libdir can differ.

| Path under prefix | Contents |
| --- | --- |
| `bin/xml2pdf` | CLI |
| `lib/libki_xml_pdf.a` | Static library |
| `lib/libki_xml_pdf.so`, `lib/libki_xml_pdf.so.0` | Shared-library name and SONAME alias |
| `include/ki_xml_pdf.h` | Only public header |
| `share/ki_xml_pdf/DejaVuSans.ttf` | Bundled font |
| `share/ki_xml_pdf/DejaVuSans.copyright`, `share/ki_xml_pdf/LICENSE` | Separate font notice and project MIT license |
| `lib/pkgconfig/ki_xml_pdf.pc` | pkg-config discovery |
| `lib/cmake/ki_xml_pdf/` | Config, version and imported targets |

CMake also installs the versioned `libki_xml_pdf.so.0.3.0`; SONAME remains `libki_xml_pdf.so.0`. The Make-built CLI links the archive, while the CMake CLI links the selected variant and has an installed `$ORIGIN`-relative rpath.

## Installed C/C++ consumers

```cmake
find_package(ki_xml_pdf CONFIG REQUIRED)
target_link_libraries(client PRIVATE ki_xml_pdf::ki_xml_pdf)
```

Installed targets are `ki_xml_pdf::static`, `ki_xml_pdf::shared` and the interface selector `ki_xml_pdf::ki_xml_pdf`. Set `ki_xml_pdf_USE_STATIC_LIBS=ON|OFF` before discovery to override the install-time default.

`cmake/ki_xml_pdfConfig.cmake.in` uses `find_dependency(ZLIB)`. Static targets propagate their zlib link requirement; the shared library records `libz.so.1` itself.

```sh
export PKG_CONFIG_PATH="$HOME/.local/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
cc client.c $(pkg-config --cflags --libs ki_xml_pdf) -o client
cc client.c $(pkg-config --cflags ki_xml_pdf) \
  -Wl,-Bstatic $(pkg-config --static --libs ki_xml_pdf) -Wl,-Bdynamic -o client-static
```

`pkg-config --static` adds `Libs.private: -lz` but does not itself force archive selection. Linking `libki_xml_pdf.a` by path also requires `-lz`. Shared consumers must arrange runtime library discovery, for example an rpath or `LD_LIBRARY_PATH`.

## ABI and version matching

| ELF node in `libki_xml_pdf.map` | Export group |
| --- | --- |
| `KI_0_1` | Original conversion/document/options calls and two legacy path exports |
| `KI_0_2` | `ki_convert_to_file` |
| `KI_0_3` | `ki_config_init`, `ki_convert_cfg`, `ki_convert_file_cfg`, `ki_convert_to_file_cfg` |

All other symbols are local. The new config preserves the 0.2 options/result/event layouts; precompiled C/C++ ABI canaries demonstrate old binaries running against 0.3.0. See [C API](../api/c-api.md).

This does not widen CMake package matching: `cmake/ki_xml_pdfConfigVersion.cmake.in` accepts exact versions or older patch versions with the same major/minor, and rejects non-64-bit consumers. A request for 0.2 is not advertised as compatible by a 0.3 package even though the tested old binary ABI survives.

## Font location and staging

The default path is `<prefix>/share/ki_xml_pdf/DejaVuSans.ttf`. Make tracks prefix changes through `src/.font_prefix`, recompiling `src/font.o` and relinking without requiring a manual clean.

`DESTDIR` stages installation only; it does not enter the compiled font path or discovery prefix:

```sh
make install PREFIX=/usr/local DESTDIR="$PWD/stage"
DESTDIR="$PWD/stage-cmake" cmake --install build-install
```

For relocation, explicitly set `KiOptions.font_path` or nonempty `KI_XML_PDF_FONT`. Invalid explicit fonts are errors, not silently replaced. Build-tree executable-relative fallback uses `/proc/self/exe`; see [Layout and fonts](../systems/layout-and-fonts.md).

## Source artifacts and checks

`make dist` archives Git HEAD as `ki_xml_pdf-0.3.0.tar.gz` when HEAD has this version, even in a dirty working tree, and prints SHA-256. It does not package uncommitted edits. `make distcheck` builds/installs the archive and runs external C/C++ clients and report-oracle checks. CI/source artifacts and releases are covered in [Deployment](../deployment.md).

| Check | Purpose |
| --- | --- |
| `tests/install_smoke.sh` | C/C++ static/shared consumers from outside the source tree |
| `tests/prefix_switch.sh` | Repeated prefix changes without clean and explicit-font failure |
| `tests/build_integration.sh` | Make/CMake, DESTDIR, discovery, ELF/zlib metadata, installed consumers and catalog/Poppler oracles |
| `tests/abi_canary/run_canary.sh` | Hash-checked precompiled old binaries against the selected installed library |
| `tests/distcheck.sh` | Source artifact, consumers, required licenses and failure propagation |

Change install/discovery behavior in both build systems and the relevant template; then test from an unrelated working directory without source-tree fallback. Do not put internal headers or fault hooks into the public install.

## Key source files

| File | Role |
| --- | --- |
| `Makefile`, `CMakeLists.txt` | Build, install, font path, artifacts and tests |
| `cmake/ki_xml_pdfConfig.cmake.in`, `cmake/ki_xml_pdfMakeTargets.cmake.in` | Installed target selection/imports |
| `cmake/ki_xml_pdfConfigVersion.cmake.in`, `cmake/ki_xml_pdf.pc.in` | Version policy and pkg-config/zlib flags |
| `libki_xml_pdf.map`, `include/ki_xml_pdf.h` | Versioned exports and public version/API |
| `tests/build_integration.sh`, `tests/install_smoke.sh`, `tests/prefix_switch.sh` | Installed integration coverage |

Related: [Getting started](../overview/getting-started.md), [Dependencies](../reference/dependencies.md), [CLI](cli.md).
