#!/usr/bin/env python3
"""Independent stream reader for xml2pdf output.

Objects are located through the classic xref table. Every stream must end
exactly at its declared /Length (direct or an indirect "N 0 R" length
object) followed by "\\nendstream"; /FlateDecode bodies must inflate to the
end of the deflate data with no trailing bytes; /Length1 must equal the
decoded size. Any violation raises StreamError.

As a script with --binary, converts a multipage fixture and checks the
stream inventory: every page contents and the embedded font use
FlateDecode, the decoded font hashes the same as the font file, Poppler
reads every page, and damaged, truncated or mislabelled streams are
rejected.
"""

import argparse
import hashlib
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[1]


class StreamError(ValueError):
    pass


def xref_offsets(data):
    at = data.rfind(b"startxref\n")
    if at < 0:
        raise StreamError("missing startxref")
    m = re.match(rb"startxref\n(\d+)\n", data[at:])
    if m is None:
        raise StreamError("malformed startxref")
    xref = int(m.group(1))
    h = re.match(rb"xref\n0 (\d+)\n", data[xref:])
    if h is None:
        raise StreamError("startxref does not point at xref")
    pos = xref + h.end()
    offsets = {}
    for num in range(int(h.group(1))):
        e = re.fullmatch(rb"(\d{10}) (\d{5}) ([nf]) \n", data[pos:pos + 20])
        if e is None:
            raise StreamError(f"bad xref entry {num}")
        if e.group(3) == b"n":
            off = int(e.group(1))
            if not data.startswith(b"%d 0 obj\n" % num, off):
                raise StreamError(f"xref entry {num} misses its object")
            offsets[num] = off
        pos += 20
    return offsets


def inflate(body):
    d = zlib.decompressobj()
    try:
        out = d.decompress(body) + d.flush()
    except zlib.error as exc:
        raise StreamError(f"damaged Flate data: {exc}") from None
    if not d.eof:
        raise StreamError("truncated Flate data")
    if d.unused_data:
        raise StreamError("bytes after the end of Flate data")
    return out


def streams(data):
    """Yields (object number, dictionary bytes, declared length, decoded bytes)."""
    offsets = xref_offsets(data)
    for num, off in sorted(offsets.items(), key=lambda kv: kv[1]):
        head = b"%d 0 obj\n" % num
        start = off + len(head)
        if not data.startswith(b"<<", start):
            continue
        s = data.find(b">>\nstream\n", start)
        e = data.find(b"endobj\n", start)
        if s < 0 or (0 <= e < s):
            continue
        d = data[start:s + 2]
        m = re.search(rb"/Length (\d+)( 0 R)?[ >]", d)
        if m is None:
            raise StreamError(f"object {num}: stream without /Length")
        if m.group(2):
            ref = int(m.group(1))
            if ref not in offsets:
                raise StreamError(f"object {num}: length object {ref} missing")
            lm = re.match(rb"%d 0 obj\n(\d+)\nendobj\n" % ref, data[offsets[ref]:])
            if lm is None:
                raise StreamError(f"object {num}: malformed length object {ref}")
            length = int(lm.group(1))
        else:
            length = int(m.group(1))
        body_at = s + len(b">>\nstream\n")
        if not data.startswith(b"\nendstream\nendobj\n", body_at + length):
            raise StreamError(f"object {num}: /Length {length} does not end at endstream")
        body = data[body_at:body_at + length]
        decoded = inflate(body) if b"/Filter /FlateDecode" in d else body
        l1 = re.search(rb"/Length1 (\d+)", d)
        if l1 is not None and int(l1.group(1)) != len(decoded):
            raise StreamError(f"object {num}: /Length1 {int(l1.group(1))} != decoded {len(decoded)}")
        yield num, d, length, decoded


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def inventory(data, font_bytes):
    rows = list(streams(data))
    pages = int(subprocess.run(["pdfinfo", "-"], input=data, stdout=subprocess.PIPE, check=True)
                .stdout.decode().split("Pages:")[1].split()[0])
    contents = [r for r in rows if r[3].startswith(b"BT\n")]
    fonts = [r for r in rows if b"/Length1 " in r[1]]
    plain = [r for r in rows if b"/FlateDecode" not in r[1]]
    require(len(contents) == pages, f"{len(contents)} content streams for {pages} pages")
    require(all(b"/Filter /FlateDecode" in r[1] for r in contents), "uncompressed page contents")
    require(len(fonts) == 1 and b"/Filter /FlateDecode" in fonts[0][1], "font stream is not Flate")
    require(hashlib.sha256(fonts[0][3]).digest() == hashlib.sha256(font_bytes).digest(),
            "decoded font differs from the font file")
    require(len(plain) == 1 and plain[0][3].startswith(b"/CIDInit"), "unexpected uncompressed stream")
    return rows, pages


def rejects(data, label):
    try:
        list(streams(data))
    except StreamError as exc:
        print(f"control {label}: rejected ({exc})")
        return
    raise AssertionError(f"control {label} was accepted")


def controls(data):
    rows = list(streams(data))
    num, d, length, _ = next(r for r in rows if r[3].startswith(b"BT\n"))
    offsets = xref_offsets(data)
    body_at = data.index(b">>\nstream\n", offsets[num]) + 10
    bad = bytearray(data)
    bad[body_at + length // 2] ^= 0x55
    rejects(bytes(bad), "damaged page body")
    bad = bytearray(data)
    bad[body_at + length - 4:body_at + length] = b"    "
    rejects(bytes(bad), "page body with lost deflate tail")
    rejects(data[:body_at + length // 2], "output truncated inside a page stream")
    try:
        inflate(data[body_at:body_at + length // 2])
    except StreamError as exc:
        print(f"control page body cut in half: rejected ({exc})")
    else:
        raise AssertionError("control page body cut in half was accepted")
    ref = int(re.search(rb"/Length (\d+) 0 R", d).group(1))
    lo = offsets[ref] + len(b"%d 0 obj\n" % ref)
    bad = bytearray(data)
    bad[lo] = ord("1") if bad[lo] == ord("9") else bad[lo] + 1
    rejects(bytes(bad), "wrong length object value")
    i = data.index(b"/Length1 ") + 9
    bad = bytearray(data)
    bad[i] = ord("1") if bad[i] == ord("9") else bad[i] + 1
    rejects(bytes(bad), "wrong font /Length1")


def check_binary(binary, font):
    font_bytes = font.read_bytes()
    lines = [f"Flate oracle line {i:04d}" for i in range(150)]
    xml = "<doc>" + "".join(f"<p>{t}</p>" for t in lines) + "</doc>"
    with tempfile.TemporaryDirectory(prefix="ki-pdf-streams-") as tmp:
        src = Path(tmp) / "in.xml"
        src.write_text(xml, encoding="utf-8")
        # stdout to a pipe: the writer may not seek its destination.
        proc = subprocess.run([str(binary), "--font", str(font), str(src), "-"],
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        require(proc.returncode == 0, f"conversion failed: {proc.stderr!r}")
        data = proc.stdout
        rows, pages = inventory(data, font_bytes)
        require(pages >= 3, f"expected a multipage PDF, got {pages}")
        text = subprocess.run(["pdftotext", "-layout", "-", "-"], input=data, stdout=subprocess.PIPE,
                              check=True).stdout.decode("utf-8")
        found = [ln for ln in text.splitlines() if "Flate oracle line" in ln]
        got = [re.search(r"Flate oracle line \d{4}", ln).group(0) for ln in found]
        require(got == lines, "pdftotext lines differ from the fixture")
        for num, d, length, decoded in rows:
            kind = "font" if b"/Length1" in d else ("page" if decoded.startswith(b"BT\n") else "cmap")
            flt = "Flate" if b"/FlateDecode" in d else "none"
            print(f"obj {num} {kind} filter={flt} length={length} decoded={len(decoded)}")
        print(f"pages {pages}, font sha256 {hashlib.sha256(font_bytes).hexdigest()} matches")
        controls(data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("pdf", nargs="*", type=Path)
    parser.add_argument("--binary", type=Path)
    parser.add_argument("--font", type=Path, default=ROOT / "fonts/DejaVuSans.ttf")
    args = parser.parse_args()
    require(args.pdf or args.binary, "nothing to check")
    for path in args.pdf:
        rows = list(streams(path.read_bytes()))
        print(f"{path}: {len(rows)} streams decode")
    if args.binary is not None:
        check_binary(args.binary.resolve(), args.font.resolve())
    print("pdf streams ok")


if __name__ == "__main__":
    try:
        main()
    except (AssertionError, StreamError) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        sys.exit(1)
