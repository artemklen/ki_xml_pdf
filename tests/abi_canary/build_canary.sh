#!/bin/bash
# Compile the ABI canaries once against an installed prefix and link them
# dynamically. Run this against the old 0.2 prefix and keep DEST; do not
# rebuild the canaries against a newer header for compatibility checks.
# usage: build_canary.sh PREFIX DEST
set -euo pipefail
prefix=$1
dest=$2
here=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$dest"
CC=${CC:-cc}
CXX=${CXX:-c++}
"$CC" -std=c99 -Wall -Wextra -O2 -I"$prefix/include" -o "$dest/abi_canary_c" "$here/canary.c" \
    -L"$prefix/lib" -lki_xml_pdf
"$CXX" -std=c++11 -Wall -Wextra -O2 -I"$prefix/include" -o "$dest/abi_canary_cpp" \
    "$here/canary.cpp" -L"$prefix/lib" -lki_xml_pdf
for bin in abi_canary_c abi_canary_cpp; do
    if ! readelf -d "$dest/$bin" | grep -q 'NEEDED.*\[libki_xml_pdf\.so\.0\]'; then
        echo "$bin is not dynamically linked to libki_xml_pdf.so.0" >&2
        exit 1
    fi
done
cp "$here/canary.c" "$here/canary.cpp" "$dest/"
{
    echo "prefix $prefix"
    echo "cc $("$CC" --version | head -n 1)"
    echo "cxx $("$CXX" --version | head -n 1)"
    echo "c_cmd $CC -std=c99 -Wall -Wextra -O2 -I$prefix/include -o $dest/abi_canary_c canary.c -L$prefix/lib -lki_xml_pdf"
    echo "cpp_cmd $CXX -std=c++11 -Wall -Wextra -O2 -I$prefix/include -o $dest/abi_canary_cpp canary.cpp -L$prefix/lib -lki_xml_pdf"
} >"$dest/BUILD.txt"
(cd "$dest" && sha256sum abi_canary_c abi_canary_cpp canary.c canary.cpp >SHA256SUMS)
echo "canaries built in $dest"
