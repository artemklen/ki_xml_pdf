#!/bin/bash
# Verify a versioned source archive without a Git checkout.
# Fail fast: the EXIT trap cleans every unpack/build/install directory.
set -euo pipefail
archive=${1:?archive}
cc=${CC:-cc}
cxx=${CXX:-c++}
pdftext=${PDFTOTEXT:-pdftotext}
for tool in python3 "$cc" "$cxx" "$pdftext" tar make cmake pkg-config readelf tee; do
    command -v "$tool" >/dev/null
done
work=$(mktemp -d /tmp/ki-xml-pdf-dist.XXXXXX)
trap 'rm -rf "$work"' EXIT

fail() {
    echo "distcheck: $*" >&2
    exit 1
}

# Derive the actual single archive root before extracting or assuming a version.
archive_root=$(python3 - "$archive" <<'PY'
import pathlib
import sys
import tarfile

with tarfile.open(sys.argv[1], "r:gz") as source:
    roots = set()
    for entry in source:
        path = pathlib.PurePosixPath(entry.name)
        if path.is_absolute() or ".." in path.parts or not path.parts:
            raise SystemExit("distcheck: invalid archive member path")
        roots.add(path.parts[0])
if len(roots) != 1:
    raise SystemExit("distcheck: source archive must have one top directory")
print(roots.pop())
PY
)
tar -C "$work" -xzf "$archive"
root="$work/$archive_root"
test -d "$root" || fail "archive root is not a directory"

version=$(python3 - "$root/include/ki_xml_pdf.h" <<'PY'
import pathlib
import re
import sys

header = pathlib.Path(sys.argv[1]).read_text(encoding="utf-8")
versions = re.findall(r'^#define KI_XML_PDF_VERSION "([^"]+)"$', header, re.MULTILINE)
if len(versions) != 1:
    raise SystemExit("distcheck: public header must define exactly one version")
print(versions[0])
PY
)
test "$archive_root" = "ki_xml_pdf-$version" || fail "archive root differs from public header version"

if [[ ${DISTCHECK_BREAK:-} == license ]]; then
    rm -f "$root/LICENSE"
fi

# These sources also make all package, mapper and CLI checks runnable unpacked.
required=(
    Makefile CMakeLists.txt libki_xml_pdf.map include/ki_xml_pdf.h
    cmake/ki_xml_pdf.pc.in cmake/ki_xml_pdfConfig.cmake.in
    cmake/ki_xml_pdfConfigVersion.cmake.in cmake/ki_xml_pdfMakeTargets.cmake.in
    README.md README.ru.md CONTRIBUTING.md CHANGELOG.md LICENSE baseline-manifest.json
    .github/workflows/ci.yml .gitignore
    docs/contract.md docs/license-decision.md docs/profile-decision.md
    docs/release-checklist.md docs/report-v1.md docs/mapper-guide.md
    src/convert.c src/convert.h src/font.c src/font.h src/geometry.c src/geometry.h src/info.c src/info.h
    src/layout.c src/layout.h
    src/main.c src/page_sink.c src/page_sink.h src/pdf_write.c src/pdf_write.h
    src/report.c src/report.h src/xml_parse.c src/xml_parse.h
    examples/consume.c examples/consume.cpp examples/hello.xml
    examples/report.c examples/report-v1.xml examples/report-v1.expected.txt
    examples/catalog_driver.c examples/catalog_map.c examples/catalog_map.h
    examples/catalog.xml examples/catalog.expected.txt
    tests/build_integration.sh tests/catalog_verify.py tests/cli_pipeline.py
    tests/consumer/CMakeLists.txt tests/consumer/report.c tests/docs_smoke.sh
    tests/distcheck.sh tests/install_smoke.sh tests/prefix_switch.sh tests/test_stream.c
    tests/test_xml_subset.c tests/test_xml_coords.c tests/test_font_metrics.c tests/test_pdf_bounds.c
    tests/pdf_offsets.py tests/font_metrics_check.py tests/pdf_streams.py
    tests/test_flate.c tests/pdf_inflate.c tests/test_layout_wrap.c tests/test_geometry.c tests/test_info.c tests/wrap_check.py tests/geometry_check.py tests/info_check.py
    tests/test_integration.c tests/test_callback_io.c tests/integration_check.py
    tests/verify.sh tests/fixture.xml tests/fuzz_smoke.py tests/large_proof.py
    tests/corpus/split_whitespace.xml tests/corpus/utf8_512.xml
    tests/corpus/truncated-hmtx.ttf tests/corpus/truncated-cmap-184.ttf tests/font_fixtures.py
    fonts/DejaVuSans.ttf fonts/DejaVuSans.copyright
    tests/resource_recipe.py tests/resource_oracle.py tests/resource_measure.py
    tests/resource_baseline.py tests/resource_controls.py tests/resource_baseline_test.py
    tests/abi_canary/canary.c tests/abi_canary/canary.cpp tests/abi_canary/build_canary.sh
    tests/abi_canary/run_canary.sh docs/evidence/osr-23-baseline-075455c/README.md
    docs/evidence/osr-23-baseline-075455c/baseline/runs.csv
)
for file in "${required[@]}"; do
    test -s "$root/$file" || fail "missing source file $file"
done

python3 - "$root" <<'PY'
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
if any(root.rglob(".git")):
    raise SystemExit("distcheck: unpacked source contains Git checkout metadata")
needle = b"/home/" + b"artem"
hits = [str(path.relative_to(root)) for path in root.rglob("*")
        if path.is_file() and needle in path.read_bytes()]
if hits:
    print("\n".join(hits), file=sys.stderr)
    raise SystemExit("distcheck: source archive contains a private workspace path")
PY

# Preserve the original standalone Make install and old public C/C++ clients.
make -C "$root" CC="$cc" CXX="$cxx" PREFIX="$work/prefix" install
test -f "$work/prefix/share/ki_xml_pdf/LICENSE"
test -f "$work/prefix/share/ki_xml_pdf/DejaVuSans.copyright"
"$cc" -std=c99 -Wall -I"$work/prefix/include" -o "$work/host" \
    "$root/examples/consume.c" "$work/prefix/lib/libki_xml_pdf.a" -lz
"$cc" -std=c99 -Wall -I"$work/prefix/include" -o "$work/host-so" \
    "$root/examples/consume.c" -L"$work/prefix/lib" -lki_xml_pdf -Wl,-rpath,"$work/prefix/lib"
"$cxx" -Wall -I"$work/prefix/include" -o "$work/hostcpp" \
    "$root/examples/consume.cpp" "$work/prefix/lib/libki_xml_pdf.a" -lz
"$cxx" -Wall -I"$work/prefix/include" -o "$work/hostso" \
    "$root/examples/consume.cpp" -L"$work/prefix/lib" -lki_xml_pdf -Wl,-rpath,"$work/prefix/lib"
"$cc" -std=c99 -Wall -I"$work/prefix/include" -o "$work/report" \
    "$root/examples/report.c" "$work/prefix/lib/libki_xml_pdf.a" -lz
( cd / && env -u KI_XML_PDF_FONT "$work/host" "$root/examples/hello.xml" "$work/hello.pdf" )
( cd / && env -u KI_XML_PDF_FONT "$work/host-so" "$root/examples/hello.xml" "$work/hello-c-so.pdf" )
( cd / && env -u KI_XML_PDF_FONT "$work/hostcpp" "$root/examples/hello.xml" "$work/hello-cpp.pdf" )
( cd / && env -u KI_XML_PDF_FONT "$work/hostso" "$root/examples/hello.xml" "$work/hello-so.pdf" )
( cd / && env -u KI_XML_PDF_FONT "$work/report" "$root/examples/report-v1.xml" "$work/report.pdf" )
oracle=${DISTCHECK_ORACLE:-$root/examples/report-v1.expected.txt}
oracle=$(python3 -c 'import pathlib,sys; print(pathlib.Path(sys.argv[1]).resolve())' "$oracle")
"$pdftext" -layout "$work/report.pdf" - | python3 -c '
import pathlib, sys
got = [line.rstrip() for line in sys.stdin.read().splitlines() if line.strip()]
expected = pathlib.Path(sys.argv[1]).read_text(encoding="utf-8").splitlines()
if got != expected:
    raise SystemExit("distcheck: standalone report PDF differs from oracle")
' "$oracle"
test -s "$work/hello.pdf"
test -s "$work/hello-c-so.pdf"
test -s "$work/hello-cpp.pdf"
test -s "$work/hello-so.pdf"

# Exercise installed CLI pipelines using the unpacked source's test and oracle.
( cd / && env -u KI_XML_PDF_FONT python3 "$root/tests/cli_pipeline.py" \
    --binary "$work/prefix/bin/xml2pdf" --fixture "$root/examples/report-v1.xml" \
    --oracle "$oracle" --pdftotext "$pdftext" )

# The common suite owns Make/CMake staging, 16 external C/C++ discovery rows,
# and all four installed catalog variants against the fixed catalog oracle.
# Its CTest already runs the build-tree tests; don't repeat full unit/RSS suites.
CC="$cc" CXX="$cxx" PDFTOTEXT="$pdftext" bash "$root/tests/build_integration.sh" \
    2>&1 | tee "$work/build-integration.log"
matrix_rows=$(grep -c '^matrix PASS ' "$work/build-integration.log" || true)
catalog_variants=$(grep -c '^catalog installed PASS ' "$work/build-integration.log" || true)
test "$matrix_rows" -eq 16 || fail "expected 16 installed C/C++ rows, got $matrix_rows"
test "$catalog_variants" -eq 4 || fail "expected 4 installed catalog variants, got $catalog_variants"

echo "distcheck ok: version=$version clean archive; 16 installed C/C++ rows; 4 catalog variants; installed CLI pipelines"
