#!/bin/bash
# Run the documented small examples in a disposable source copy and install prefix.
set -euo pipefail
root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
export CC=${CC:-cc} CXX=${CXX:-c++}
pdftext=${PDFTOTEXT:-pdftotext}
for tool in "$CC" "$CXX" "$pdftext" pdfinfo bash cmake pkg-config make python3 readelf; do
    command -v "$tool" >/dev/null || { echo "docs_smoke: missing tool: $tool" >&2; exit 1; }
done
"$pdftext" -v >/dev/null 2>&1 || { echo "docs_smoke: PDFTOTEXT cannot run: $pdftext" >&2; exit 1; }
work=$(mktemp -d "${TMPDIR:-/tmp}/ki-docs-smoke.XXXXXX")
trap 'rm -rf "$work"' EXIT
source_dir="$work/source"
blocks="$work/blocks"
mkdir -p "$blocks"
unset KI_XML_PDF_FONT PKG_CONFIG_PATH PKG_CONFIG_LIBDIR PKG_CONFIG_SYSROOT_DIR LD_LIBRARY_PATH

python3 - "$root" "$source_dir" "$blocks" <<'PY'
from pathlib import Path
import re
import shutil
import subprocess
import sys
from urllib.parse import unquote, urlsplit

root, source, blocks = map(Path, sys.argv[1:])
source.mkdir()
ignore = shutil.ignore_patterns("*.o", "*.d", "*.a", "*.so*", "*.pdf", "*.pyc",
    "__pycache__", ".font_prefix*", "xml2pdf", "test_stream", "test_stream_san",
    "consume", "consume_cpp", "report", "catalog")
for name in ("src", "include", "fonts", "examples", "tests", "cmake", "docs", ".github"):
    shutil.copytree(root / name, source / name, ignore=ignore)
for path in [root / name for name in ("Makefile", "CMakeLists.txt", "libki_xml_pdf.map", "LICENSE")] + list(root.glob("*.md")):
    shutil.copy2(path, source / path.name)

def fenced(text):
    return re.findall(r"^```([^\n]*)\n(.*?)^```\s*$", text, re.M | re.S)

def anchors(path):
    text = re.sub(r"^```[^\n]*\n.*?^```\s*$", "", path.read_text(), flags=re.M | re.S)
    found, counts = set(), {}
    for heading in re.findall(r"^#{1,6}\s+(.+?)\s*#*\s*$", text, re.M):
        heading = re.sub(r"\[([^\]]+)\]\([^)]*\)", r"\1", heading)
        slug = re.sub(r"[^\w\-\s]", "", heading.lower()).replace(" ", "-")
        duplicate = counts.get(slug, 0)
        counts[slug] = duplicate + 1
        found.add(f"{slug}-{duplicate}" if duplicate else slug)
    return found

shell_count = link_count = 0
for name in ("README.md", "README.ru.md", "CONTRIBUTING.md", "docs/report-v1.md", "docs/mapper-guide.md"):
    path = source / name
    text = path.read_text()
    assert text.count("```") % 2 == 0, f"{name}: unclosed code fence"
    for language, body in fenced(text):
        if language == "sh":
            subprocess.run(["bash", "-n"], input=body, text=True, check=True)
            shell_count += 1
    prose = re.sub(r"^```[^\n]*\n.*?^```\s*$", "", text, flags=re.M | re.S)
    for target in re.findall(r"\[[^\]]+\]\(([^)]+)\)", prose):
        url = urlsplit(target)
        if url.scheme or url.netloc:
            continue
        destination = (path.parent / unquote(url.path)).resolve() if url.path else path
        assert destination.is_file(), f"{name}: missing link {target}"
        if url.fragment:
            assert destination.suffix == ".md" and unquote(url.fragment) in anchors(destination), f"{name}: missing anchor {target}"
        link_count += 1

readme = fenced((source / "README.md").read_text())
guide = fenced((source / "docs/mapper-guide.md").read_text())
commands = {
    "quickstart": (readme, "make\n./xml2pdf"),
    "pipes": (readme, "cat examples/report-v1.xml"),
    "dashpaths": (readme, "cp examples/report-v1.xml"),
    "selection": (readme, "./xml2pdf --profile report-v1 --record-offset"),
    "cmake-build": (readme, "cmake -S . -B build -"),
    "make-install": (readme, "make PREFIX="),
    "cmake-install": (readme, "cmake -S . -B build-install"),
    "staging": (readme, "make install PREFIX=/usr/local"),
    "pkg-consumers": (readme, "prefix="),
    "cmake-consumers": (readme, "cmake -S . -B shared"),
    "catalog": (guide, "make example"),
    "catalog-install": (guide, "catalog_prefix="),
}
for name, (document, first_line) in commands.items():
    candidates = [body for language, body in document if language == "sh" and body.startswith(first_line)]
    assert len(candidates) == 1, f"documented block missing or ambiguous: {name}"
    body = candidates[0].replace("$HOME/.local", "${docs_prefix}")
    body = body.replace("$(mktemp -d)", '$(mktemp -d "$work/consumer.XXXXXX")')
    body = re.sub(r"^cc ", '"$CC" ', body, flags=re.M)
    body = re.sub(r"^c\+\+ ", '"$CXX" ', body, flags=re.M)
    (blocks / f"{name}.sh").write_text(body)
snippets = (("cmake", "CMakeLists.txt", lambda body: True), ("c", "api.c", lambda body: "KiConfig" not in body),
            ("c", "geometry.c", lambda body: "KiConfig" in body))
for language, filename, wanted in snippets:
    candidates = [body for kind, body in readme if kind == language and wanted(body)]
    assert len(candidates) == 1, f"documented {language} snippet {filename} missing or ambiguous"
    (blocks / filename).write_text(candidates[0])
print(f"docs_smoke: syntax PASS ({shell_count} shell blocks); local links/anchors PASS ({link_count})")
PY

run_block() {
    echo "docs_smoke: documented $1"
    # The extracted commands change directory and set variables as in the README.
    source "$blocks/$1.sh"
}

pdf_count=0
linkage() {
    local dynamic
    dynamic=$(readelf -d "$1")
    if { [ "$2" = shared ] && [[ "$dynamic" != *'Shared library: [libki_xml_pdf.so.0]'* ]]; } ||
       { [ "$2" = static ] && [[ "$dynamic" == *libki_xml_pdf.so* ]]; }; then
        echo "docs_smoke: $1 did not use the documented $2 linkage" >&2
        exit 1
    fi
}

oracle() {
    python3 - "$1" "$2" "$pdftext" <<'PY'
from pathlib import Path
import subprocess
import sys

pdf, expected, reader = sys.argv[1:]
with open(pdf, "rb") as stream:
    assert stream.read(5) == b"%PDF-", f"{pdf}: stdout is not a PDF"
result = subprocess.run([reader, "-layout", "-nopgbrk", pdf, "-"], capture_output=True, text=True)
if result.returncode:
    raise SystemExit(f"PDF reader failed for {pdf}: {result.stderr}")
actual = [line.rstrip() for line in result.stdout.splitlines() if line.strip()]
wanted = Path(expected).read_text().splitlines()
assert actual == wanted, f"{pdf}: text differs from independent oracle\nactual={actual!r}\nexpected={wanted!r}"
PY
    pdf_count=$((pdf_count + 1))
}

report_oracle="$source_dir/examples/report-v1.expected.txt"
catalog_oracle="$source_dir/examples/catalog.expected.txt"
hello_oracle="$work/hello.expected.txt"
printf 'doc\nHello\n' > "$hello_oracle"
cd "$source_dir"
run_block quickstart
run_block pipes
run_block dashpaths
for pdf in report.pdf from-stdin.pdf from-file.pdf pipeline.pdf --output.pdf; do
    oracle "$source_dir/$pdf" "$report_oracle"
done
cp examples/report-v1.xml input.xml
run_block selection
oracle "$source_dir/report.pdf" "$report_oracle"
./xml2pdf -h > "$work/help.txt"
./xml2pdf --help > "$work/long-help.txt"
test -s "$work/help.txt" && test -s "$work/long-help.txt"
./xml2pdf --version > "$work/version.txt"
python3 - "$source_dir/include/ki_xml_pdf.h" "$work/version.txt" <<'PY'
from pathlib import Path
import re
import sys
version = re.search(r'^#define KI_XML_PDF_VERSION "([^"]+)"', Path(sys.argv[1]).read_text(), re.M).group(1)
assert version in Path(sys.argv[2]).read_text(), "CLI version differs from header"
print(f"docs_smoke: help/version PASS (header {version}; final distribution checks own literal release version)")
PY
run_block cmake-build
oracle "$source_dir/cmake-report.pdf" "$report_oracle"
docs_prefix="$work/make-prefix"
run_block make-install
docs_prefix="$work/cmake-prefix"
run_block cmake-install
run_block staging
docs_prefix="$work/make-prefix"
run_block pkg-consumers
for kind in static shared; do
    linkage "$consumer_dir/report-$kind" "$kind"
    linkage "$consumer_dir/consume-$kind" "$kind"
done
for pdf in report-shared.pdf report-static.pdf; do oracle "$consumer_dir/$pdf" "$report_oracle"; done
for pdf in consume-shared.pdf consume-static.pdf; do oracle "$consumer_dir/$pdf" "$hello_oracle"; done
cp "$blocks/CMakeLists.txt" "$consumer_dir/"
run_block cmake-consumers
for kind in static shared; do
    linkage "$consumer_dir/$kind/report-client" "$kind"
    linkage "$consumer_dir/$kind/consume-client" "$kind"
done
for pdf in cmake-shared.pdf cmake-static.pdf; do oracle "$consumer_dir/$pdf" "$report_oracle"; done
for pdf in cmake-shared-cpp.pdf cmake-static-cpp.pdf; do oracle "$consumer_dir/$pdf" "$hello_oracle"; done
cp "$blocks/api.c" .
cp report-v1.xml input.xml
"$CC" api.c $(pkg-config --cflags --libs ki_xml_pdf) -o api-client
./api-client
oracle "$consumer_dir/report.pdf" "$report_oracle"
cp "$blocks/geometry.c" .
"$CC" geometry.c $(pkg-config --cflags --libs ki_xml_pdf) -o geometry-client
./geometry-client
oracle "$consumer_dir/a4.pdf" "$report_oracle"
pdfinfo -box "$consumer_dir/a4.pdf" | grep -E '^MediaBox: +0\.00 +0\.00 +841\.89 +595\.28$' >/dev/null || {
    echo "documented geometry client: A4 landscape MediaBox differs" >&2
    exit 1
}
"$work/cmake-prefix/bin/xml2pdf" --profile report-v1 report-v1.xml installed-cmake.pdf
oracle "$consumer_dir/installed-cmake.pdf" "$report_oracle"
cd "$source_dir"
run_block catalog
for pdf in catalog.pdf catalog-one-byte.pdf catalog-callback.pdf; do oracle "$source_dir/$pdf" "$catalog_oracle"; done
run_block catalog-install
oracle "$source_dir/catalog.pdf" "$catalog_oracle"
echo "docs_smoke PASS: $pdf_count PDFs checked; documented Make/CMake, C/C++ static/shared consumers, pipelines and catalog modes; CC=$CC CXX=$CXX PDFTOTEXT=$pdftext"
