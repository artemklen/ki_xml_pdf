# Dependencies
Active contributors: Artem

The shipping library depends on Linux/glibc and zlib, plus a compatible TrueType font. Build/test/research tools are separate from runtime dependencies.

## Runtime and platform

| Dependency | Why | Source |
| --- | --- | --- |
| 64-bit Linux with glibc | Only supported platform; GNU/POSIX APIs and ELF packaging | `README.md`, `CMakeLists.txt`, `src/platform.h` |
| glibc cookie streams | Callback adapter and counted output share `FILE *` writing | `src/convert.c`, `src/pdf_write.c` |
| zlib | Flate-compressed page content and complete embedded font | `src/pdf_write.c`, `Makefile`, `CMakeLists.txt` |
| TrueType font | Fixed-repertoire metrics and embedded glyph outlines | `src/font.c`, `fonts/DejaVuSans.ttf` |

CMake rejects non-Linux, non-64-bit and non-glibc targets.
Make rejects non-Linux; `src/platform.h` also requires Linux/glibc during compilation.
musl, macOS and Windows are not supported or advertised as merely untested alternatives.

The XML parser and PDF writer are project code, not libxml2 or an external PDF engine.
There is no network resolver, Java/FOP runtime, image library, shaping engine or linked subsetter.
A caller can supply another compatible font, subject to the 8 MiB cap and unchanged body repertoire.

## Link dependencies reach consumers

| Consumption route | zlib handling |
| --- | --- |
| Raw static archive | Add `-lz` after the archive/library |
| pkg-config shared | `pkg-config --libs ki_xml_pdf` gives public library flags |
| pkg-config static | Use `pkg-config --static --libs ki_xml_pdf` to include `Libs.private: -lz` |
| CMake static target | `ki_xml_pdf::static` carries the zlib link-only dependency |
| CMake shared target | Shared library records libz as an ELF NEEDED dependency |

`cmake/ki_xml_pdfConfig.cmake.in` calls `find_dependency(ZLIB)`.
zlib development files are required when building and for installed CMake discovery.
Do not copy obsolete “glibc and font only” or “no dependencies” claims.

## Build tools

| Tool | Use |
| --- | --- |
| GCC or Clang, C99 | Core library and CLI |
| GNU Make | Development/install/source targets in `Makefile` |
| GNU-compatible linker and `ar` | ELF version scripts/soname and static archive |
| zlib development package | Headers and link library; Debian/Ubuntu `zlib1g-dev` |
| CMake 3.16+ | Out-of-tree build, CTest, install and consumers |
| pkg-config | Installed consumer discovery |
| Git, tar/gzip, SHA-256 tools | Exact-HEAD source archives and provenance |

C++ is not a core runtime requirement.
A C++ compiler is required to check public-header and installed-client compatibility.
The Make defaults include C99 warnings, PIC and POSIX feature macros; see [Tooling](../how-to-contribute/tooling.md).

## Check tools

| Tool | Evidence |
| --- | --- |
| Python 3 | CLI/feature/PDF/resource oracles and fixture generators; regular suites use the standard library |
| Bash and standard Unix tools | Integration, docs, installation and archive scripts |
| Poppler `pdfinfo`, `pdftotext`, `pdftohtml` | Page structure/text extraction and independent reading |
| Poppler `pdffonts`, `pdftoppm` | Embedded-font identity and actual raster ink for font/geometry/integration checks |
| binutils `readelf`, loader `ldd` | Soname, symbol/linkage and old-binary checks |
| `/usr/bin/time` | Separate converter/generator/reader/oracle RSS records |
| pthreads | Independent-context C regressions |
| GNU linker `--wrap` | fsync and zlib failure injection |
| DejaVu system fonts | Non-default bold-font checks in `tests/font_metrics_check.py` |

For a Debian/Ubuntu check environment:

```sh
sudo apt-get install -y gcc g++ clang make cmake pkg-config zlib1g-dev \
  binutils python3 time poppler-utils fonts-dejavu-core
```

The repository also bundles its default font.
A development system font is not a reason for installed consumers to depend on the source checkout.
See [Testing](../how-to-contribute/testing.md) for exact suite membership.

## CI and release tooling

`.github/workflows/ci.yml` uses GitHub-hosted `ubuntu-latest` and installs job-specific tools.
Actions are `actions/checkout@v4` and `actions/upload-artifact@v7.0.2`.
Those are tag/version references, not immutable commit-SHA pins.
`.github/workflows/release.yml` additionally uses GitHub CLI to create tag-triggered releases.
The tag job does not rerun functional/resource CI.

## Licensing and research boundary

Project source, public API and original examples use MIT under `LICENSE`.
`fonts/DejaVuSans.copyright` carries the separate Bitstream Vera/DejaVu terms.
Both build systems install the font and its notice alongside the project license.
Users supplying other fonts are responsible for their embedding/distribution rights.

Massif/Valgrind in the OSR-82 evidence is historical profiling tooling, not a converter dependency.
UTF-16, table and subsetting research scripts are not runtime or ordinary Make/CMake test dependencies.
No product support for those experiments follows from their presence in the source tree.

## Key source files

| File | Role |
| --- | --- |
| `Makefile`, `CMakeLists.txt` | Required compiler/link/build dependencies |
| `cmake/ki_xml_pdf.pc.in`, `cmake/ki_xml_pdfConfig.cmake.in` | Consumer dependency propagation |
| `src/platform.h` | Compile-time Linux/glibc guard |
| `.github/workflows/ci.yml` | Job packages and action references |
| `LICENSE`, `fonts/DejaVuSans.copyright` | Distribution notices |

Related: [Packaging](../features/packaging.md), [Distribution and releases](../deployment.md).
