#!/usr/bin/env python3
"""Independent check of the optional PDF /Info dictionary.

Expected values come from the command line given here, not from the
converter. The trailer, xref and Info object are parsed by this script, its
UTF-16BE strings are decoded here, and Poppler pdfinfo/pdftotext read the
same files. Omitted fields must be absent, no date may appear unless given,
the body/page count/MediaBox must match the run without metadata, and
invalid metadata must exit 2 without reading stdin or touching the output.

    python3 tests/info_check.py [--binary xml2pdf]
"""

import argparse
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from font_metrics_check import BUNDLED, ROOT, require  # noqa: E402

MAX = 1024
XML = ("<doc><p>Info leaves the body alone</p><p>\u0417\u0430\u043f\u0438\u0441\u044c "
       "two</p></doc>").encode()

TITLE_RU = "\u041e\u0442\u0447\u0451\u0442 \u2116 7"
SYNTAX = "a(b)\\c\nline 2\r\t) /JS (app.alert(1)) >> endobj trailer << /Root 9 0 R >>"
CASES = [
    {},
    {"title": "Plain title"},
    {"producer": "ki_xml_pdf test"},
    {"creation_date": ("2026-10-16T12:34:56Z", "D:20261016123456Z")},
    {"title": TITLE_RU, "producer": "\u0421\u0431\u043e\u0440\u043a\u0430 \U0001F600",
     "creation_date": ("2024-02-29T23:59:59-05:30", "D:20240229235959-05'30'")},
    {"title": SYNTAX, "producer": SYNTAX},
    {"title": "\u0416" * (MAX // 2)},
    {"title": "x" * MAX, "creation_date": ("2000-01-01T00:00:00+14:00", "D:20000101000000+14'00'")},
]
FLAG = {"title": "--title", "producer": "--producer", "creation_date": "--creation-date"}


def args_for(case):
    out = []
    for key, val in case.items():
        out += [FLAG[key], val[0] if key == "creation_date" else val]
    return out


def run(binary, extra, dest):
    r = subprocess.run([str(binary), "--font", str(BUNDLED), *extra, "-", str(dest)], input=XML,
                       capture_output=True, timeout=120)
    require(r.returncode == 0, f"{extra}: exit {r.returncode}: {r.stderr.decode(errors='replace')}")
    return dest.read_bytes()


def xref(data):
    start = int(re.search(rb"startxref\n(\d+)\n%%EOF\n$", data).group(1))
    m = re.match(rb"xref\n0 (\d+)\n", data[start:])
    n = int(m.group(1))
    body = data[start + m.end():start + m.end() + 20 * n]
    offs = [int(body[20 * i:20 * i + 10]) for i in range(n)]
    trailer = data[start + m.end() + 20 * n:]
    require(trailer.startswith(b"trailer\n<<"), "trailer missing")
    return start, offs, trailer[:trailer.index(b">>") + 2]


def decode_text(tok):
    if tok.startswith(b"<"):
        raw = bytes.fromhex(tok[1:-1].decode())
        require(raw[:2] == b"\xfe\xff", f"text string without BOM: {tok[:20]!r}")
        return raw[2:].decode("utf-16-be")
    require(tok.startswith(b"(") and tok.endswith(b")"), f"bad string {tok!r}")
    require(re.fullmatch(rb"\([0-9A-Za-z:+\-']*\)", tok) is not None, f"unexpected literal {tok!r}")
    return tok[1:-1].decode("ascii")


def info_dict(data, offs, trailer):
    m = re.search(rb"/Info (\d+) 0 R", trailer)
    if m is None:
        return None
    num = int(m.group(1))
    obj = data[offs[num]:]
    head = f"{num} 0 obj\n<<\n".encode()
    require(obj.startswith(head), f"Info object {num} not at its xref offset")
    end = obj.index(b">>\nendobj\n")
    body = obj[len(head):end]
    out = {}
    for line in body.split(b"\n"):
        if not line:
            continue
        mm = re.fullmatch(rb"/(Title|Producer|CreationDate) (<[0-9A-F]*>|\([^()\\]*\))", line)
        require(mm is not None, f"unexpected Info entry {line[:60]!r}")
        key = mm.group(1).decode()
        require(key not in out, f"duplicate {key}")
        out[key] = decode_text(mm.group(2))
    return out


def pdfinfo(pdf, *flags):
    r = subprocess.run(["pdfinfo", "-enc", "UTF-8", *flags, str(pdf)], capture_output=True, check=True,
                       timeout=60)
    return r.stdout.decode("utf-8")


def text(pdf):
    r = subprocess.run(["pdftotext", "-enc", "UTF-8", str(pdf), "-"], capture_output=True, check=True, timeout=60)
    return r.stdout


def field(info, key):
    m = re.search(rf"^{key}:\s+(.*?)\n(?=[A-Z][A-Za-z ]+:\s)", info, re.M | re.S)
    return None if m is None else m.group(1)


def check_values(binary, tmp):
    base_pdf = tmp / "base.pdf"
    base = run(binary, [], base_pdf)
    base_xref, base_offs, base_trailer = xref(base)
    require(b"/Info" not in base and b"/CreationDate" not in base and b"/ModDate" not in base,
            "no-metadata PDF has Info entries")
    base_text = text(base_pdf)
    base_info = pdfinfo(base_pdf)
    shape = re.findall(r"^(Pages|Page size):.*$", base_info, re.M)
    for n, case in enumerate(CASES):
        pdf = tmp / f"case{n}.pdf"
        data = run(binary, args_for(case), pdf)
        start, offs, trailer = xref(data)
        got = info_dict(data, offs, trailer)
        want = {}
        if "title" in case:
            want["Title"] = case["title"]
        if "producer" in case:
            want["Producer"] = case["producer"]
        if "creation_date" in case:
            want["CreationDate"] = case["creation_date"][1]
        if not case:
            require(data == base and got is None, "empty case differs from the plain run")
            continue
        require(got == want, f"case {n}: Info {got!r} != {want!r}")
        require(len(offs) == len(base_offs) + 1, f"case {n}: {len(offs)} xref entries, base {len(base_offs)}")
        require(data[:base_xref] == base[:base_xref], f"case {n}: body bytes changed")
        require(b"/ModDate" not in data and b"/Metadata" not in data and b"/ID" not in trailer,
                f"case {n}: unrequested ModDate/XMP/ID")
        if "creation_date" not in case:
            require(b"/CreationDate" not in data, f"case {n}: implicit CreationDate")
        info = pdfinfo(pdf)
        for key in ("Title", "Producer"):
            require(field(info, key) == want.get(key), f"case {n}: pdfinfo {key} {field(info, key)!r}")
        raw = pdfinfo(pdf, "-rawdates")
        require(field(raw, "CreationDate") == want.get("CreationDate"), f"case {n}: pdfinfo date")
        require(re.findall(r"^(Pages|Page size):.*$", info, re.M) == shape, f"case {n}: pages/size changed")
        require(text(pdf) == base_text, f"case {n}: extracted text changed")
    again = run(binary, args_for(CASES[5]), tmp / "again.pdf")
    require(again == (tmp / "case5.pdf").read_bytes(), "repeated run without a date is not identical")
    return len(CASES)


def check_rejections(binary, tmp):
    bad = [
        ["--title", ""], ["--producer", ""], ["--title", "x" * (MAX + 1)],
        ["--producer", "\u0416" * (MAX // 2) + "x"], ["--title", "bell\x07"], ["--title", "esc\x1b[0m"],
        ["--producer", "del\x7f"], ["--title", "c1\u0085"],
        ["--title", b"\xc0\xaf"], ["--title", b"\xed\xa0\x80"], ["--producer", b"\xf4\x90\x80\x80"],
        ["--title", b"cut\xe2\x82"], ["--title", b"\xff"],
        ["--title", "a", "--title", "b"], ["--producer", "a", "--producer", "a"],
        ["--creation-date", "2026-10-16T12:00:00Z", "--creation-date", "2026-10-16T12:00:00Z"],
    ]
    for d in ["", "2026-02-29T00:00:00Z", "2100-02-29T00:00:00Z", "2026-13-01T00:00:00Z", "2026-04-31T00:00:00Z",
              "2026-10-16T24:00:00Z", "2026-10-16T12:60:00Z", "2026-10-16T12:00:60Z", "2026-10-16T12:00:00",
              "2026-10-16T12:00:00z", "2026-10-16 12:00:00Z", "2026-10-16T12:00:00+24:00",
              "2026-10-16T12:00:00+03:60", "2026-10-16T12:00:00+0300", "D:20261016120000Z", "20261016T120000Z",
              "2026-10-16T12:00:00.5Z", "2026-10-16T12:00:00Z ", "now", "2026-10-16"]:
        bad.append(["--creation-date", d])
    sentinel = b"previous destination\n"
    for n, args in enumerate(bad):
        argv = [os.fsencode(a) if isinstance(a, str) else a for a in args]
        kept = tmp / f"kept{n}.pdf"
        kept.write_bytes(sentinel)
        fresh = tmp / f"fresh{n}.pdf"
        for dest in (kept, fresh):
            # stdin stays open: reading it would block until the timeout.
            p = subprocess.Popen([os.fsencode(binary), b"--font", os.fsencode(BUNDLED), *argv, b"-",
                                  os.fsencode(dest)], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                 stderr=subprocess.PIPE)
            try:
                rc = p.wait(timeout=20)
            except subprocess.TimeoutExpired:
                p.kill()
                p.wait()
                raise AssertionError(f"{args}: read stdin instead of rejecting the metadata")
            err = p.stderr.read().decode(errors="replace")
            for f in (p.stdin, p.stdout, p.stderr):
                f.close()
            require(rc == 2, f"{args}: exit {rc}, expected 2: {err}")
        require(kept.read_bytes() == sentinel, f"{args}: destination changed")
        require(not fresh.exists(), f"{args}: destination created")
        # A missing input or font does not mask the metadata error.
        for inp, font in ((tmp / "missing.xml", BUNDLED), (tmp / "missing.xml", tmp / "missing.ttf")):
            r = subprocess.run([os.fsencode(binary), b"--font", os.fsencode(font), *argv, os.fsencode(inp),
                                os.fsencode(kept)], capture_output=True, timeout=20)
            require(r.returncode == 2, f"{args}: missing input/font gave exit {r.returncode}")
            require(kept.read_bytes() == sentinel, f"{args}: destination changed")
    leftovers = [f for f in os.listdir(tmp) if not f.startswith("kept")]
    require(leftovers == [], f"temporary files left: {leftovers}")
    return len(bad)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--binary", type=Path, default=ROOT / "xml2pdf")
    args = ap.parse_args()
    binary = args.binary.resolve()
    with tempfile.TemporaryDirectory() as t:
        good = check_values(binary, Path(t))
    with tempfile.TemporaryDirectory() as t:
        bad = check_rejections(binary, Path(t))
    print(f"info: {good} metadata cases decoded, {bad} rejected option sets")


if __name__ == "__main__":
    main()
