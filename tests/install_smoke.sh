#!/bin/sh
# Install into a private prefix and run external clients from /.
# CC and CXX select the toolchain. An optional argument is the prefix;
# otherwise the script creates one and removes it.
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
cc=${CC:-cc}
cxx=${CXX:-c++}
if [ $# -ge 1 ]; then
    prefix=$1
    owned=0
    mkdir -p "$prefix"
else
    prefix=$(mktemp -d /tmp/ki-install-smoke.XXXXXX)
    owned=1
fi
make -C "$root" clean
make -C "$root" CC="$cc" CXX="$cxx" PREFIX="$prefix" install
"$cc" -std=c99 -Wall -I"$prefix/include" -o "$prefix/host" \
    "$root/examples/consume.c" "$prefix/lib/libki_xml_pdf.a" -lz
"$cc" -std=c99 -Wall -I"$prefix/include" -o "$prefix/host-so" \
    "$root/examples/consume.c" -L"$prefix/lib" -lki_xml_pdf -Wl,-rpath,"$prefix/lib"
"$cxx" -Wall -I"$prefix/include" -o "$prefix/hostcpp" \
    "$root/examples/consume.cpp" "$prefix/lib/libki_xml_pdf.a" -lz
"$cxx" -Wall -I"$prefix/include" -o "$prefix/hostso" \
    "$root/examples/consume.cpp" -L"$prefix/lib" -lki_xml_pdf -Wl,-rpath,"$prefix/lib"
cd /
env -u KI_XML_PDF_FONT "$prefix/host" "$root/examples/hello.xml" "$prefix/hello.pdf"
env -u KI_XML_PDF_FONT "$prefix/host-so" "$root/examples/hello.xml" "$prefix/hello-c-so.pdf"
env -u KI_XML_PDF_FONT "$prefix/hostcpp" "$root/examples/hello.xml" "$prefix/hello-cpp.pdf"
env -u KI_XML_PDF_FONT "$prefix/hostso" "$root/examples/hello.xml" "$prefix/hello-so.pdf"
test -s "$prefix/hello.pdf"
test -s "$prefix/hello-c-so.pdf"
test -s "$prefix/hello-cpp.pdf"
test -s "$prefix/hello-so.pdf"
make -C "$root" clean
if [ "$owned" -eq 1 ]; then
    rm -rf "$prefix"
fi
echo "install_smoke ok cc=$cc cxx=$cxx"
