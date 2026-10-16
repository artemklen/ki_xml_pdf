#!/bin/bash
# Sequential OSR-25 gates in the expendable checkout. Each gate's output and
# exit status are kept in logs/; a failing gate does not stop later ones.
set -u
E=<corrective-root>
C=$E/checkout
L=$E/logs/${1:-gates}
mkdir -p "$L" "$E/scratch"
export SCRATCH_ROOT=$E/scratch
cd "$C" || exit 1
summary=$L/summary.tsv
: > "$summary"
gate() {
    local name=$1
    shift
    local start=$(date +%s)
    echo "### $name: $*" > "$L/$name.txt"
    echo "### HEAD $(git rev-parse HEAD)" >> "$L/$name.txt"
    bash -c "$*" >> "$L/$name.txt" 2>&1
    local rc=$?
    printf '%s\t%s\t%ss\t%s\n' "$name" "$rc" "$(( $(date +%s) - start ))" "$*" >> "$summary"
    echo "$name rc=$rc"
}
gate 01-make-test "make -j2 xml2pdf test_stream && make test"
gate 02-make-check "SCRATCH=$E/scratch/verify make check"
gate 03-san-gcc "make CC=gcc test-san"
gate 04-san-clang "make CC=clang test-san"
gate 05-clang-functional "make clean && make -j2 CC=clang xml2pdf test_stream && make CC=clang test xml2pdf example && python3 tests/integration_check.py && python3 tests/catalog_verify.py --binary examples/catalog"
gate 06-gcc-rebuild "make clean && make -j2 xml2pdf test_stream && make test xml2pdf example"
gate 07-fuzz "python3 tests/fuzz_smoke.py"
gate 08-font-fixtures "python3 tests/font_fixtures.py --check"
gate 09-cli-catalog "python3 tests/cli_pipeline.py && python3 tests/catalog_verify.py --binary examples/catalog && python3 tests/integration_check.py && ./xml2pdf --help && ./xml2pdf --version"
gate 10-build-integration-gcc "bash tests/build_integration.sh"
gate 11-build-integration-clang "CC=clang CXX=clang++ bash tests/build_integration.sh"
gate 12-docs-smoke "bash tests/docs_smoke.sh"
gate 13-header-dependency "sleep 1 && touch src/layout.h && make xml2pdf && test xml2pdf -nt src/layout.h"
gate 14-distcheck "make distcheck"
gate 15-distcheck-negative "if make distcheck CC=false; then echo 'failing compiler accepted'; exit 1; fi; if make distcheck CXX=false; then echo 'failing C++ compiler accepted'; exit 1; fi; if make distcheck PDFTOTEXT=/bin/false; then echo 'failing reader accepted'; exit 1; fi; printf 'not the oracle\n' > $E/scratch/bad-oracle.txt; if make distcheck DISTCHECK_ORACLE=$E/scratch/bad-oracle.txt; then echo 'bad oracle accepted'; exit 1; fi; if make distcheck DISTCHECK_BREAK=license; then echo 'missing license accepted'; exit 1; fi; echo 'all five negative controls failed as required'"
gate 16-prefix-switch "bash tests/prefix_switch.sh"
gate 17-cmake-ctest "rm -rf $E/scratch/cmake-build && cmake -S . -B $E/scratch/cmake-build -DCMAKE_BUILD_TYPE=Release && cmake --build $E/scratch/cmake-build -j2 && ctest --test-dir $E/scratch/cmake-build --output-on-failure"
gate 18-resource-units "python3 tests/resource_baseline_test.py && python3 tests/large_proof.py --self-test"
gate 19-large-proof "TMPDIR=$E/scratch python3 tests/large_proof.py 1073741824"
gate 20-pkgconfig-static "rm -rf $E/scratch/prefix && make -j2 install PREFIX=$E/scratch/prefix && PKG_CONFIG_LIBDIR=$E/scratch/prefix/lib/pkgconfig pkg-config --static --libs ki_xml_pdf && PKG_CONFIG_LIBDIR=$E/scratch/prefix/lib/pkgconfig pkg-config --modversion ki_xml_pdf && sed -n 1,3p $E/scratch/prefix/lib/cmake/ki_xml_pdf/ki_xml_pdfConfigVersion.cmake && grep -n VERSION $E/scratch/prefix/include/ki_xml_pdf.h | head -3 && $E/scratch/prefix/bin/xml2pdf --version && readelf -d $E/scratch/prefix/lib/libki_xml_pdf.so | grep -E 'SONAME|NEEDED' && readelf --dyn-syms -W $E/scratch/prefix/lib/libki_xml_pdf.so | grep -E 'KI_0_[23]' | awk '{print \$8}' | sort && ls -la $E/scratch/prefix/bin $E/scratch/prefix/include $E/scratch/prefix/lib $E/scratch/prefix/share/ki_xml_pdf $E/scratch/prefix/lib/cmake/ki_xml_pdf $E/scratch/prefix/lib/pkgconfig"
echo "done; summary in $summary"
