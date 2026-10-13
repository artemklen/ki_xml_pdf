#!/bin/bash
# Unit groups, two CLI runs, a 16 MiB RSS check, and a Poppler read of the PDF.
# RSS is /usr/bin/time on ./xml2pdf only. The XML generator and pdftotext are not included.
set -euo pipefail

require() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "missing required tool: $1" >&2
        exit 1
    fi
}
require python3
require pdfinfo
require pdftotext
require pdftohtml
if [[ ! -x /usr/bin/time ]]; then
    echo "missing required tool: /usr/bin/time" >&2
    exit 1
fi

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
if [[ -n "${SCRATCH:-}" ]]; then
    mkdir -p "$SCRATCH"
else
    SCRATCH=$(mktemp -d "${TMPDIR:-/tmp}/ki-xml-pdf-verify.XXXXXX")
fi

make xml2pdf test_stream

set +e
./xml2pdf --help >"$SCRATCH/help.txt"
help_rc=$?
./xml2pdf --version >"$SCRATCH/version.txt"
ver_rc=$?
./xml2pdf >"$SCRATCH/usage.txt" 2>&1
use_rc=$?
set -e
if [[ "$help_rc" -ne 0 || "$ver_rc" -ne 0 || "$use_rc" -ne 2 ]]; then
    echo "cli help/version/usage codes: $help_rc $ver_rc $use_rc" >&2
    exit 1
fi
grep -q records-v1 "$SCRATCH/help.txt"
grep -q UTF-8 "$SCRATCH/help.txt"
version=$(sed -n 's/^#define KI_XML_PDF_VERSION "\([^"]*\)"/\1/p' include/ki_xml_pdf.h)
if [[ -z "$version" ]]; then
    echo "could not read CLI version from include/ki_xml_pdf.h" >&2
    exit 1
fi
grep -Fxq "xml2pdf $version records-v1" "$SCRATCH/version.txt"

./test_stream >"$SCRATCH/unit.log" 2>&1
groups=$(grep -c '^ok ' "$SCRATCH/unit.log" || true)
if [[ "$groups" -ne 16 ]]; then
    echo "expected 16 unit groups, got $groups" >&2
    exit 1
fi

recover_py() {
    python3 - "$1" "$2" << 'PY'
import sys
path, report = sys.argv[1], sys.argv[2]
data = open(path, "rb").read()

def streams(buf):
    out = []
    i = 0
    while True:
        j = buf.find(b"stream\n", i)
        if j < 0:
            break
        start = j + 7
        k = buf.find(b"endstream", start)
        if k < 0:
            break
        out.append(buf[start:k])
        i = k + 9
    return out

def gid_map(buf):
    mapping = {}
    i = 0
    while True:
        j = buf.find(b"beginbfchar", i)
        if j < 0:
            break
        k = buf.find(b"endbfchar", j)
        if k < 0:
            break
        for line in buf[j:k].splitlines():
            if b"<" not in line:
                continue
            parts = line.replace(b">", b" ").replace(b"<", b" ").split()
            if len(parts) >= 2:
                mapping[int(parts[0], 16)] = int(parts[1], 16)
        i = k + 9
    if not mapping:
        raise SystemExit("no ToUnicode map")
    return mapping

def decode_page(stream, mapping):
    out = bytearray()
    i = 0
    any_str = False
    while True:
        j = stream.find(b"<", i)
        if j < 0:
            break
        k = stream.find(b">", j)
        if k < 0:
            break
        nxt = stream[k + 1:k + 8].lstrip()
        if not nxt.startswith(b"Tj"):
            i = k + 1
            continue
        hexes = bytes(c for c in stream[j + 1:k] if c not in b" \t\r\n")
        if hexes and len(hexes) % 4 == 0:
            any_str = True
            for p in range(0, len(hexes), 4):
                cp = mapping.get(int(hexes[p:p + 4], 16), 0)
                if cp:
                    out += chr(cp).encode("utf-8")
        i = k + 1
    if not any_str:
        raise SystemExit("no text operator in stream")
    return bytes(out)

mapping = gid_map(data)
pages = [s for s in streams(data) if b"BT\n" in s]
texts = [decode_page(s, mapping) for s in pages]
all_text = b"".join(texts)
needle = b"/Type /Page"
count = 0
start = 0
while True:
    i = data.find(needle, start)
    if i < 0:
        break
    after = i + len(needle)
    if after >= len(data) or data[after:after + 1] != b"s":
        count += 1
    start = after

need = [
    b"section",
    b"child",
    b"alpha&beta",
    b"nested<child-text",
    b"START_MARKER",
    b"END_MARKER",
]
bad = [b"alpha&amp;beta", b"nested&lt;child-text", b"skip-me"]
lines = []
lines.append(f"pdf_bytes {len(data)}")
lines.append(f"page_objects {count}")
lines.append(f"streams {len(texts)}")
for item in need:
    lines.append(f"has {item.decode()} {1 if item in all_text else 0}")
for item in bad:
    lines.append(f"absent {item.decode()} {1 if item not in all_text else 0}")
first = texts[0] if texts else b""
last = texts[-1] if texts else b""
lines.append(f"first_has_START {1 if b'START_MARKER' in first else 0}")
lines.append(f"first_has_END {1 if b'END_MARKER' in first else 0}")
later_end = 1 if any(b"END_MARKER" in t for t in texts[1:]) else 0
lines.append(f"later_has_END {later_end}")
tail = 1 if (b"END_MARKER" in last or b"</root>" in last or b"</tail>" in last) else 0
lines.append(f"last_is_tail {tail}")
lines.append(f"header {1 if data.startswith(b'%PDF-') else 0}")
lines.append(f"xref {1 if b'\nxref\n' in data or data.find(b'xref\n') >= 0 else 0}")
lines.append(f"trailer {1 if b'trailer' in data else 0}")
lines.append(f"eof {1 if data.rstrip().endswith(b'%%EOF') else 0}")
# Recovered text with the long padding collapsed so the log stays readable,
# while the checks above ran on the full unescaped page text.
shown = all_text.replace(b"A" * 40, b"A...A")
lines.append("--- recovered ---")
lines.append(shown.decode("utf-8", "replace"))
open(report, "w", encoding="utf-8").write("\n".join(lines) + "\n")
print(f"pages={count} streams={len(texts)} bytes={len(data)}")
PY
}

run_one() {
    local pdf="$1"
    local report="$2"
    local out="$3"
    local err="$4"
    set +e
    ./xml2pdf tests/fixture.xml "$pdf" >"$out" 2>"$err"
    local rc=$?
    set -e
    {
        echo "exit $rc"
        echo "--- stdout ---"
        cat "$out"
        echo "--- stderr ---"
        cat "$err"
    } >"$report"
    recover_py "$pdf" "$report.body"
    cat "$report.body" >>"$report"
    rm -f "$report.body"
    if [[ "$rc" -ne 0 ]]; then
        echo "CLI failed (exit $rc)" >&2
        exit 1
    fi
}

run_one "$SCRATCH/run1.pdf" "$SCRATCH/cli-run1.txt" "$SCRATCH/run1.stdout" "$SCRATCH/run1.stderr"
run_one "$SCRATCH/run2.pdf" "$SCRATCH/cli-run2.txt" "$SCRATCH/run2.stdout" "$SCRATCH/run2.stderr"

python3 - << PY
from pathlib import Path
scratch = Path("$SCRATCH")
a = (scratch / "cli-run1.txt").read_text(encoding="utf-8")
b = (scratch / "cli-run2.txt").read_text(encoding="utf-8")

def field(text, key):
    prefix = key + " "
    for line in text.splitlines():
        if line.startswith(prefix):
            return line[len(prefix):]
    raise SystemExit(f"missing {key}")

for key in [
    "has section",
    "has child",
    "has alpha&beta",
    "has nested<child-text",
    "has START_MARKER",
    "has END_MARKER",
    "absent alpha&amp;beta",
    "absent nested&lt;child-text",
    "first_has_START",
    "later_has_END",
    "last_is_tail",
    "header",
    "xref",
    "trailer",
    "eof",
]:
    va, vb = field(a, key), field(b, key)
    if va != "1" or vb != "1":
        raise SystemExit(f"{key} run1={va} run2={vb}")
pa, pb = int(field(a, "page_objects")), int(field(b, "page_objects"))
if pa < 2 or pa != pb:
    raise SystemExit(f"page count {pa} vs {pb}")
if field(a, "first_has_END") != "0" or field(b, "first_has_END") != "0":
    raise SystemExit("END_MARKER leaked onto page 1")
if field(a, "exit") != "0" or field(b, "exit") != "0":
    raise SystemExit("non-zero CLI exit")
# Same recoverable text on both runs.
ra = a.split("--- recovered ---", 1)[1]
rb = b.split("--- recovered ---", 1)[1]
if ra != rb:
    raise SystemExit("recovered text differs between runs")
print("cli runs match")
PY

python3 - << PY
from pathlib import Path
scratch = Path("$SCRATCH")
target = 16 * 1024 * 1024
path = scratch / "large.xml"
chunk = b"<item>pad</item>" * 4096
tail = b'<item id="last">LAST_MARKER_UNIQUE</item></root>'
with path.open("wb") as f:
    f.write(b'<root><item id="first">FIRST_MARKER_UNIQUE</item>')
    while f.tell() + len(tail) < target:
        f.write(chunk)
    f.write(tail)
size = path.stat().st_size
if size < target or size >= 17 * 1024 * 1024:
    raise SystemExit(f"large xml is {size} bytes")
print("large_xml", size)
PY

set +e
/usr/bin/time -v -o "$SCRATCH/time_large.txt" ./xml2pdf "$SCRATCH/large.xml" "$SCRATCH/large.pdf"
large_rc=$?
/usr/bin/time -v -o "$SCRATCH/time_small.txt" ./xml2pdf tests/fixture.xml "$SCRATCH/small.pdf"
small_rc=$?
set -e

python3 - << PY
import re
from pathlib import Path
scratch = Path("$SCRATCH")
large_rc = int("$large_rc")
small_rc = int("$small_rc")

def rss_kb(path):
    text = Path(path).read_text(encoding="utf-8", errors="replace")
    m = re.search(r"Maximum resident set size \(kbytes\):\s*(\d+)", text)
    if not m:
        raise SystemExit(f"no Max RSS in {path}\\n" + text)
    return int(m.group(1)), text

large_kb, large_time = rss_kb(scratch / "time_large.txt")
small_kb, small_time = rss_kb(scratch / "time_small.txt")
large_rss = large_kb * 1024
small_rss = small_kb * 1024
large_in = (scratch / "large.xml").stat().st_size
small_in = Path("tests/fixture.xml").stat().st_size
large_pdf = (scratch / "large.pdf").stat().st_size
data = (scratch / "large.pdf").read_bytes()

def page_count(buf):
    needle = b"/Type /Page"
    count = 0
    start = 0
    while True:
        i = buf.find(needle, start)
        if i < 0:
            break
        after = i + len(needle)
        if after >= len(buf) or buf[after:after + 1] != b"s":
            count += 1
        start = after
    return count

def streams(buf):
    out = []
    i = 0
    while True:
        j = buf.find(b"stream\n", i)
        if j < 0:
            break
        start = j + 7
        k = buf.find(b"endstream", start)
        if k < 0:
            break
        out.append(buf[start:k])
        i = k + 9
    return out

def gid_map(buf):
    mapping = {}
    i = 0
    while True:
        j = buf.find(b"beginbfchar", i)
        if j < 0:
            break
        k = buf.find(b"endbfchar", j)
        if k < 0:
            break
        for line in buf[j:k].splitlines():
            if b"<" not in line:
                continue
            parts = line.replace(b">", b" ").replace(b"<", b" ").split()
            if len(parts) >= 2:
                mapping[int(parts[0], 16)] = int(parts[1], 16)
        i = k + 9
    return mapping

def decode_page(stream, mapping):
    out = bytearray()
    i = 0
    while True:
        j = stream.find(b"<", i)
        if j < 0:
            break
        k = stream.find(b">", j)
        if k < 0:
            break
        nxt = stream[k + 1:k + 8].lstrip()
        if not nxt.startswith(b"Tj"):
            i = k + 1
            continue
        hexes = bytes(c for c in stream[j + 1:k] if c not in b" \t\r\n")
        if hexes and len(hexes) % 4 == 0:
            for p in range(0, len(hexes), 4):
                cp = mapping.get(int(hexes[p:p + 4], 16), 0)
                if cp:
                    out += chr(cp).encode("utf-8")
        i = k + 1
    return bytes(out)

pages = page_count(data)
mp = gid_map(data)
first = b""
last_raw = b""
seen_page = 0
i = 0
while True:
    j = data.find(b"stream\n", i)
    if j < 0:
        break
    start = j + 7
    k = data.find(b"endstream", start)
    if k < 0:
        break
    chunk = data[start:k]
    if b"BT\n" in chunk:
        if seen_page == 0:
            first = decode_page(chunk, mp)
        last_raw = chunk
        seen_page += 1
    i = k + 9
last = decode_page(last_raw, mp) if last_raw else b""
has_first = b"FIRST_MARKER_UNIQUE" in first
has_last = b"LAST_MARKER_UNIQUE" in last
first_on_first = has_first
last_not_on_first = b"LAST_MARKER_UNIQUE" not in first
last_on_later = seen_page > 1 and has_last
small_pages = page_count(Path(scratch / "small.pdf").read_bytes())

half = large_in // 2
cap = small_rss + 4 * 1024 * 1024
ok = (
    large_rc == 0
    and small_rc == 0
    and large_in >= 16 * 1024 * 1024
    and small_in < 1024 * 1024
    and small_pages >= 2
    and pages >= 2
    and has_first
    and has_last
    and first_on_first
    and last_not_on_first
    and last_on_later
    and large_rss < half
    and large_rss <= cap
    and large_pdf > large_rss
)
lines = []
lines.append(f"large_exit {large_rc}")
lines.append(f"small_exit {small_rc}")
lines.append(f"large_input_bytes {large_in}")
lines.append(f"small_input_bytes {small_in}")
lines.append(f"large_pdf_bytes {large_pdf}")
lines.append(f"large_rss_kbytes {large_kb}")
lines.append(f"small_rss_kbytes {small_kb}")
lines.append(f"large_rss_bytes {large_rss}")
lines.append(f"small_rss_bytes {small_rss}")
lines.append(f"half_input_bytes {half}")
lines.append(f"rss_cap_bytes {cap}")
lines.append(f"large_pages {pages}")
lines.append(f"small_pages {small_pages}")
lines.append(f"has_FIRST_MARKER_UNIQUE {int(has_first)}")
lines.append(f"has_LAST_MARKER_UNIQUE {int(has_last)}")
lines.append(f"first_page_has_FIRST {int(first_on_first)}")
lines.append(f"first_page_lacks_LAST {int(last_not_on_first)}")
lines.append(f"later_page_has_LAST {int(last_on_later)}")
lines.append(f"last_page_tail {int(b'LAST_MARKER_UNIQUE' in last or b'</root>' in last)}")
lines.append(f"rss_under_half_input {int(large_rss < half)}")
lines.append(f"rss_within_small_plus_4mib {int(large_rss <= cap)}")
lines.append(f"pdf_larger_than_rss {int(large_pdf > large_rss)}")
lines.append(f"pass {int(ok)}")
lines.append("--- time large ---")
lines.append(large_time.rstrip())
lines.append("--- time small ---")
lines.append(small_time.rstrip())
(scratch / "mem.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")
print("\\n".join(lines[:20]))
if not ok:
    raise SystemExit("memory gate failed")
PY

pdfinfo "$SCRATCH/run1.pdf" >"$SCRATCH/pdfinfo.txt"
pages=$(awk '/^Pages:/ {print $2}' "$SCRATCH/pdfinfo.txt")
if [[ -z "$pages" || "$pages" -lt 2 ]]; then
    echo "pdfinfo did not report at least 2 pages" >&2
    exit 1
fi
pdftotext -f 1 -l 1 -layout "$SCRATCH/run1.pdf" "$SCRATCH/page1.txt"
pdftotext -f "$pages" -l "$pages" -layout "$SCRATCH/run1.pdf" "$SCRATCH/pagelast.txt"
python3 - "$SCRATCH" << 'PY'
import pathlib, subprocess, sys
scratch = pathlib.Path(sys.argv[1])
xml = scratch / "wide.xml"
xml.write_text("<doc><body>" + ("W" * 90) + "</body></doc>", encoding="utf-8")
pdf = scratch / "wide.pdf"
subprocess.check_call(["./xml2pdf", str(xml), str(pdf)], cwd=".")
text = subprocess.check_output(["pdftotext", "-layout", str(pdf), "-"], text=True)
body = text.replace("\n", "").replace(" ", "")
if body.count("W") != 90:
    raise SystemExit(f"wide W count {body.count('W')}")
bbox = subprocess.check_output(["pdftotext", "-bbox", str(pdf), "-"], text=True)
import re
xs = [float(v) for v in re.findall(r'xMax="([0-9.]+)"', bbox)]
if not xs or max(xs) > 572:
    raise SystemExit(f"glyph box outside margin: {max(xs) if xs else 'none'}")
print("wide line stays inside the margin")
cyr = scratch / "cyr.xml"
cyr.write_text('<?xml version="1.0" encoding="UTF-8"?><doc><t>Привет, мир — ёж</t></doc>', encoding="utf-8")
cyr_pdf = scratch / "cyr.pdf"
subprocess.check_call(["./xml2pdf", str(cyr), str(cyr_pdf)])
plain = subprocess.check_output(["pdftotext", "-layout", str(cyr_pdf), "-"], text=True)
html = subprocess.check_output(["pdftohtml", "-stdout", "-i", str(cyr_pdf)], text=True, stderr=subprocess.DEVNULL)
if "Привет, мир — ёж" not in plain or "Привет, мир — ёж" not in html:
    raise SystemExit("cyrillic missing from pdftotext or pdftohtml")
bad = scratch / "bad.xml"
bad.write_text("<doc><t>&#x1F600;</t></doc>", encoding="utf-8")
bad_pdf = scratch / "bad.pdf"
proc = subprocess.run(["./xml2pdf", str(bad), str(bad_pdf)], capture_output=True)
if proc.returncode == 0 or bad_pdf.exists():
    raise SystemExit("unsupported character was published")
print("cyrillic readers agree; unsupported character rejected")
PY

python3 - "$SCRATCH/page1.txt" "$SCRATCH/pagelast.txt" << 'PY'
import sys
first = open(sys.argv[1], encoding="utf-8", errors="replace").read()
last = open(sys.argv[2], encoding="utf-8", errors="replace").read()
need = ["START_MARKER", "section", "child", "alpha&beta", "nested<child-text"]
bad = ["END_MARKER", "skip-me", "alpha&amp;beta", "nested&lt;child-text"]
for item in need:
    if item not in first:
        raise SystemExit(f"page 1 missing {item}")
for item in bad:
    if item in first:
        raise SystemExit(f"page 1 contains {item}")
if "END_MARKER" not in last:
    raise SystemExit("last page missing END_MARKER")
if "START_MARKER" in last:
    raise SystemExit("last page repeats START_MARKER")
print("poppler page1 and last page match")
PY
{
    echo "--- pdfinfo ---"
    cat "$SCRATCH/pdfinfo.txt"
    echo "--- pdftotext page 1 ---"
    cat "$SCRATCH/page1.txt"
    echo "--- pdftotext last page ---"
    cat "$SCRATCH/pagelast.txt"
} >"$SCRATCH/pdf-reader.txt"

large_bytes=$(wc -c <"$SCRATCH/large.xml")
pdf_bytes=$(wc -c <"$SCRATCH/large.pdf")
rm -f "$SCRATCH/large.xml" "$SCRATCH/large.pdf"
{
    echo "rss_scope converter_process_only"
    echo "removed_large_xml_bytes $large_bytes"
    echo "removed_large_pdf_bytes $pdf_bytes"
    echo "scratch $SCRATCH"
} >>"$SCRATCH/mem.txt"
big_left=$(find "$SCRATCH" -type f -size +8M | wc -l)
if [[ "$big_left" -ne 0 ]]; then
    echo "scratch still has files larger than 8 MiB" >&2
    exit 1
fi

{
    echo "cc $(cc --version | head -n 1)"
    echo "python $(python3 --version 2>&1)"
    echo "pdfinfo $(pdfinfo -v 2>&1 | head -n 1 || true)"
    echo "pdftotext $(pdftotext -v 2>&1 | head -n 1 || true)"
    if git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
        echo "commit $(git rev-parse HEAD)"
    else
        echo "commit none"
    fi
    uname -srm
} >"$SCRATCH/versions.txt"

echo "verify ok scratch=$SCRATCH"
