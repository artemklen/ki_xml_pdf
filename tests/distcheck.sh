#!/bin/bash
# Extract HEAD's archive into a private directory, install it, and run external clients.
# set -e and the EXIT trap keep a failed stage from looking successful.
set -euo pipefail
archive=${1:?archive}
cc=${CC:-cc}
cxx=${CXX:-c++}
pdftext=${PDFTOTEXT:-pdftotext}
command -v python3 >/dev/null
command -v "$cc" >/dev/null
command -v "$cxx" >/dev/null
command -v "$pdftext" >/dev/null
command -v tar >/dev/null
work=$(mktemp -d /tmp/ki-xml-pdf-dist.XXXXXX)
trap 'rm -rf "$work"' EXIT
tar -C "$work" -xzf "$archive"
root="$work/ki_xml_pdf-0.1.0"
if [[ ${DISTCHECK_BREAK:-} == license ]]; then
    rm -f "$root/LICENSE"
fi
python3 -c 'import pathlib,sys; root=pathlib.Path(sys.argv[1]); needle=b"/home/" + b"artem"; hits=[str(p) for p in root.rglob("*") if p.is_file() and needle in p.read_bytes()]; print("\n".join(hits)); sys.exit(1 if hits else 0)' "$root"
make -C "$root" CC="$cc" CXX="$cxx" PREFIX="$work/prefix" install
test -f "$work/prefix/share/ki_xml_pdf/LICENSE"
test -f "$work/prefix/share/ki_xml_pdf/DejaVuSans.copyright"
"$cc" -std=c99 -Wall -I"$work/prefix/include" -o "$work/host" \
    "$root/examples/consume.c" "$work/prefix/lib/libki_xml_pdf.a"
"$cc" -std=c99 -Wall -I"$work/prefix/include" -o "$work/host-so" \
    "$root/examples/consume.c" -L"$work/prefix/lib" -lki_xml_pdf -Wl,-rpath,"$work/prefix/lib"
"$cxx" -Wall -I"$work/prefix/include" -o "$work/hostcpp" \
    "$root/examples/consume.cpp" "$work/prefix/lib/libki_xml_pdf.a"
"$cxx" -Wall -I"$work/prefix/include" -o "$work/hostso" \
    "$root/examples/consume.cpp" -L"$work/prefix/lib" -lki_xml_pdf -Wl,-rpath,"$work/prefix/lib"
"$cc" -std=c99 -Wall -I"$work/prefix/include" -o "$work/report" \
    "$root/examples/report.c" "$work/prefix/lib/libki_xml_pdf.a"
( cd / && env -u KI_XML_PDF_FONT "$work/host" "$root/examples/hello.xml" "$work/hello.pdf" )
( cd / && env -u KI_XML_PDF_FONT "$work/host-so" "$root/examples/hello.xml" "$work/hello-c-so.pdf" )
( cd / && env -u KI_XML_PDF_FONT "$work/hostcpp" "$root/examples/hello.xml" "$work/hello-cpp.pdf" )
( cd / && env -u KI_XML_PDF_FONT "$work/hostso" "$root/examples/hello.xml" "$work/hello-so.pdf" )
( cd / && env -u KI_XML_PDF_FONT "$work/report" "$root/examples/report-v1.xml" "$work/report.pdf" )
oracle=${DISTCHECK_ORACLE:-$root/examples/report-v1.expected.txt}
"$pdftext" -layout "$work/report.pdf" - | python3 -c 'import pathlib,sys; got=[ln.rstrip() for ln in sys.stdin.read().splitlines() if ln.strip()]; exp=pathlib.Path(sys.argv[1]).read_text(encoding="utf-8").splitlines(); sys.exit(0 if got==exp else 1)' "$oracle"
test -s "$work/hello.pdf"
test -s "$work/hello-c-so.pdf"
test -s "$work/hello-cpp.pdf"
test -s "$work/hello-so.pdf"
echo "distcheck ok"
