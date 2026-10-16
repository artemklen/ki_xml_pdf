#!/bin/bash
# Build with the default PREFIX, then install somewhere else without clean.
set -euo pipefail
root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
a=$(mktemp -d /tmp/ki-prefix-a.XXXXXX)
b=$(mktemp -d /tmp/ki-prefix-b.XXXXXX)
trap 'rm -rf "$a" "$b"' EXIT
make -C "$root" clean
make -C "$root" xml2pdf
make -C "$root" install PREFIX="$b"
cc -std=c99 -Wall -I"$b/include" -o "$b/host" "$root/examples/consume.c" "$b/lib/libki_xml_pdf.a" -lz
cc -std=c99 -Wall -I"$b/include" -o "$b/hostso" "$root/examples/consume.c" \
    -L"$b/lib" -lki_xml_pdf -Wl,-rpath,"$b/lib"
c++ -Wall -I"$b/include" -o "$b/hostcpp" "$root/examples/consume.cpp" "$b/lib/libki_xml_pdf.a" -lz
( cd / && env -u KI_XML_PDF_FONT "$b/host" "$root/examples/hello.xml" "$b/out.pdf" )
( cd / && env -u KI_XML_PDF_FONT "$b/hostso" "$root/examples/hello.xml" "$b/out-so.pdf" )
( cd / && env -u KI_XML_PDF_FONT "$b/hostcpp" "$root/examples/hello.xml" "$b/out-cpp.pdf" )
if env KI_XML_PDF_FONT=/no/such.ttf "$b/host" "$root/examples/hello.xml" "$b/bad.pdf"; then
    echo "explicit missing font was accepted" >&2
    exit 1
fi
make -C "$root" install PREFIX="$a"
cc -std=c99 -Wall -I"$a/include" -o "$a/host" "$root/examples/consume.c" "$a/lib/libki_xml_pdf.a" -lz
( cd / && env -u KI_XML_PDF_FONT "$a/host" "$root/examples/hello.xml" "$a/out.pdf" )
make -C "$root" install PREFIX="$b"
( cd / && env -u KI_XML_PDF_FONT "$b/host" "$root/examples/hello.xml" "$b/again.pdf" )
echo "prefix_switch ok"
