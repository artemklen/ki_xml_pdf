#!/bin/bash
# Exercise installed packages from a disposable source copy; no Git checkout is needed.
set -euo pipefail
root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
cc=${CC:-cc}
cxx=${CXX:-c++}
pdftext=${PDFTOTEXT:-pdftotext}
for tool in "$cc" "$cxx" "$pdftext" cmake pkg-config make python3 readelf; do
    command -v "$tool" >/dev/null
done
work=$(mktemp -d "${TMPDIR:-/tmp}/ki-build-integration.XXXXXX")
trap 'rm -rf "$work"' EXIT
source="$work/source"
consumer="$work/consumer"
foreign="$work/foreign"
mkdir -p "$source" "$consumer" "$foreign"
cp -R "$root/src" "$root/include" "$root/fonts" "$root/examples" "$root/tests" \
    "$root/cmake" "$source/"
cp "$root/Makefile" "$root/CMakeLists.txt" "$root/libki_xml_pdf.map" "$root/LICENSE" "$source/"
cp "$root/tests/consumer/CMakeLists.txt" "$consumer/"
cp "$root/tests/consumer/report.c" "$consumer/report.c"
cp "$root/tests/consumer/report.c" "$consumer/report.cpp"
cp "$root/examples/report-v1.xml" "$root/examples/report-v1.expected.txt" "$foreign/"
version=$(sed -n 's/^#define KI_XML_PDF_VERSION "\([^"]*\)"/\1/p' "$source/include/ki_xml_pdf.h")
test -n "$version"

fail() {
    echo "build_integration: $*" >&2
    exit 1
}

oracle() {
    "$pdftext" -layout -nopgbrk "$1" - | python3 -c '
import pathlib, sys
got = [line.rstrip() for line in sys.stdin.read().splitlines() if line.strip()]
expected = pathlib.Path(sys.argv[1]).read_text(encoding="utf-8").splitlines()
if got != expected:
    print("installed consumer PDF differs from report oracle", file=sys.stderr)
    raise SystemExit(1)
' "$foreign/report-v1.expected.txt"
}

check_stage() {
    local stage=$1 prefix=$2 staged="$1$2"
    for file in bin/xml2pdf lib/libki_xml_pdf.a lib/libki_xml_pdf.so lib/libki_xml_pdf.so.0 \
        include/ki_xml_pdf.h share/ki_xml_pdf/DejaVuSans.ttf \
        share/ki_xml_pdf/DejaVuSans.copyright share/ki_xml_pdf/LICENSE \
        lib/pkgconfig/ki_xml_pdf.pc lib/cmake/ki_xml_pdf/ki_xml_pdfConfig.cmake \
        lib/cmake/ki_xml_pdf/ki_xml_pdfConfigVersion.cmake; do
        test -s "$staged/$file" || fail "missing installed $file"
    done
    readelf -d "$staged/lib/libki_xml_pdf.so" | \
        grep -F 'Library soname: [libki_xml_pdf.so.0]' >/dev/null
    local installed_version installed_prefix
    installed_version=$(PKG_CONFIG_LIBDIR="$staged/lib/pkgconfig" pkg-config --modversion ki_xml_pdf)
    installed_prefix=$(PKG_CONFIG_LIBDIR="$staged/lib/pkgconfig" pkg-config --variable=prefix ki_xml_pdf)
    test "$installed_version" = "$version" || fail "pkg-config version differs from header"
    test "$installed_prefix" = "$prefix" || fail "DESTDIR leaked into pkg-config prefix"
    if grep -R -F "$stage" "$staged/lib/pkgconfig" "$staged/lib/cmake" >/dev/null; then
        fail "DESTDIR leaked into installed metadata"
    fi
    if grep -R -F "$source" "$staged/lib/pkgconfig" "$staged/lib/cmake" >/dev/null; then
        fail "source tree leaked into installed metadata"
    fi
    for library in libki_xml_pdf.a libki_xml_pdf.so; do
        grep -aFq "$prefix/share/ki_xml_pdf/DejaVuSans.ttf" "$staged/lib/$library" || \
            fail "runtime font path does not use install prefix"
        if grep -aFq "$stage" "$staged/lib/$library"; then
            fail "DESTDIR leaked into library runtime path"
        fi
    done
    local header_version
    header_version=$(sed -n 's/^#define KI_XML_PDF_VERSION "\([^"]*\)"/\1/p' "$staged/include/ki_xml_pdf.h")
    test "$header_version" = "$version" || fail "installed header version mismatch"
    mkdir -p "$(dirname "$prefix")"
    mv "$staged" "$prefix"
}

make_prefix="$work/make-prefix"
cmake_prefix="$work/cmake-prefix"
make_stage="$work/make-stage"
cmake_stage="$work/cmake-stage"
make -C "$source" clean
make -C "$source" CC="$cc" CXX="$cxx" PREFIX="$make_prefix" DESTDIR="$make_stage" install
check_stage "$make_stage" "$make_prefix"

cmake_build="$work/cmake-build"
cmake -S "$source" -B "$cmake_build" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER="$cc" -DCMAKE_INSTALL_PREFIX="$cmake_prefix" \
    -DCMAKE_INSTALL_LIBDIR=lib -DBUILD_TESTING=ON
cmake --build "$cmake_build" --parallel 2
( cd / && env -u KI_XML_PDF_FONT "$cmake_build/xml2pdf" --profile report-v1 \
    "$foreign/report-v1.xml" "$foreign/build-tree.pdf" )
oracle "$foreign/build-tree.pdf"
ctest --test-dir "$cmake_build" --output-on-failure
DESTDIR="$cmake_stage" cmake --install "$cmake_build"
check_stage "$cmake_stage" "$cmake_prefix"

# The clients below cannot find a font in the original source or build tree.
mv "$source" "$work/source-hidden"
mv "$cmake_build" "$work/build-hidden"
rows=0
for method in make cmake; do
    if [ "$method" = make ]; then prefix=$make_prefix; else prefix=$cmake_prefix; fi
    export PKG_CONFIG_LIBDIR="$prefix/lib/pkgconfig"
    unset PKG_CONFIG_PATH PKG_CONFIG_SYSROOT_DIR
    libdir=$(pkg-config --variable=libdir ki_xml_pdf)
    for language in c cpp; do
        if [ "$language" = c ]; then compiler=$cc; else compiler=$cxx; fi
        for linkage in static shared; do
            for discovery in cmake pkg-config; do
                name="$method-$language-$linkage-$discovery"
                binary="$foreign/$name"
                if [ "$discovery" = cmake ]; then
                    build="$work/consumer-$name"
                    cmake -S "$consumer" -B "$build" \
                        -DCMAKE_C_COMPILER="$cc" -DCMAKE_CXX_COMPILER="$cxx" \
                        -DCMAKE_PREFIX_PATH="$prefix" -DKI_EXPECTED_VERSION="$version" \
                        -DKI_SOURCE_SUFFIX="$language" -DKI_LINK_KIND="$linkage"
                    cmake --build "$build" --parallel 2
                    cp "$build/client" "$binary"
                else
                    read -r -a cflags <<< "$(pkg-config --cflags ki_xml_pdf)"
                    if [ "$linkage" = static ]; then
                        read -r -a libs <<< "$(pkg-config --static --libs ki_xml_pdf)"
                        linkflags=(-Wl,-Bstatic "${libs[@]}" -Wl,-Bdynamic)
                    else
                        read -r -a libs <<< "$(pkg-config --libs ki_xml_pdf)"
                        linkflags=("${libs[@]}" "-Wl,-rpath,$libdir")
                    fi
                    "$compiler" -Wall -Wextra "${cflags[@]}" \
                        "-DKI_EXPECTED_VERSION=\"$version\"" "$consumer/report.$language" \
                        -o "$binary" "${linkflags[@]}"
                fi
                dynamic=$(readelf -d "$binary")
                if [ "$linkage" = shared ]; then
                    grep -F 'Shared library: [libki_xml_pdf.so.0]' <<< "$dynamic" >/dev/null || \
                        fail "$name did not link the shared library"
                elif grep -F 'libki_xml_pdf.so' <<< "$dynamic" >/dev/null; then
                    fail "$name unexpectedly linked the shared library"
                fi
                ( cd "$foreign" && env -u KI_XML_PDF_FONT "$binary" \
                    "$foreign/report-v1.xml" "$foreign/$name.pdf" )
                oracle "$foreign/$name.pdf"
                ( cd "$foreign" && env -u KI_XML_PDF_FONT "$binary" \
                    "$foreign/report-v1.xml" "$foreign/$name-callback.pdf" callback )
                oracle "$foreign/$name-callback.pdf"
                echo "matrix PASS $name"
                rows=$((rows + 1))
            done
        done
    done
    ( cd / && env -u KI_XML_PDF_FONT "$prefix/bin/xml2pdf" --profile report-v1 \
        "$foreign/report-v1.xml" "$foreign/$method-cli.pdf" )
    oracle "$foreign/$method-cli.pdf"
    # Installed API font failures must not be masked by a checkout fallback.
    binary="$foreign/$method-c-static-pkg-config"
    mv "$prefix/share/ki_xml_pdf/DejaVuSans.ttf" "$prefix/share/ki_xml_pdf/font-hidden.ttf"
    if ( cd "$foreign" && env -u KI_XML_PDF_FONT "$binary" \
        "$foreign/report-v1.xml" "$foreign/missing-font.pdf" ) >"$work/negative.log" 2>&1; then
        fail "$method consumer accepted a missing installed font"
    fi
    mv "$prefix/share/ki_xml_pdf/font-hidden.ttf" "$prefix/share/ki_xml_pdf/DejaVuSans.ttf"
    if ( cd "$foreign" && env KI_XML_PDF_FONT="$work/no-such-font.ttf" "$binary" \
        "$foreign/report-v1.xml" "$foreign/explicit-font.pdf" ) >"$work/negative.log" 2>&1; then
        fail "$method consumer silently replaced an explicit missing font"
    fi
done
test "$rows" -eq 16
echo "build_integration ok: $rows installed C/C++ rows, staging, CTest and Poppler oracle; cc=$cc cxx=$cxx"
