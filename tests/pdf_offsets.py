#!/usr/bin/env python3
"""Independently check classic xref offsets and exact CLI output quotas.

Without --binary, each PDF argument is checked: startxref points at the xref
section, every entry is a 20-byte line with a ten-digit offset no larger than
9,999,999,999, and every in-use entry points at its "N 0 obj" header.

With --binary, the CLI converts --fixture with no quota to learn its size N,
then checks that quota N publishes the same N bytes and that N-1 and a quota
ending inside the xref section exit RESOURCE (4) and keep the old file.
"""

import argparse
from pathlib import Path
import re
import subprocess
import sys
import tempfile

XREF_MAX = 9_999_999_999
RESOURCE_EXIT = 4
ROOT = Path(__file__).resolve().parents[1]


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def check_pdf(data):
    require(data.endswith(b"%%EOF\n"), "missing final %%EOF")
    at = data.rfind(b"startxref\n")
    require(at >= 0, "missing startxref")
    m = re.match(rb"startxref\n(\d+)\n%%EOF\n\Z", data[at:])
    require(m is not None, "malformed startxref tail")
    xref = int(m.group(1))
    require(xref <= XREF_MAX, f"startxref {xref} exceeds {XREF_MAX}")
    require(data[xref:xref + 5] == b"xref\n", f"startxref {xref} does not point at xref")
    m = re.match(rb"xref\n0 (\d+)\n", data[xref:])
    require(m is not None, "malformed xref subsection header")
    count = int(m.group(1))
    pos = xref + m.end()
    for num in range(count):
        line = data[pos:pos + 20]
        e = re.fullmatch(rb"(\d{10}) (\d{5}) ([nf]) \n", line)
        require(e is not None, f"entry {num} is not a 20-byte classic xref line: {line!r}")
        off = int(e.group(1))
        require(off <= XREF_MAX, f"entry {num} offset {off} exceeds {XREF_MAX}")
        if num == 0:
            require(e.group(3) == b"f" and e.group(2) == b"65535", "entry 0 must be free 65535")
        else:
            require(e.group(3) == b"n" and e.group(2) == b"00000", f"entry {num} must be in use")
            header = b"%d 0 obj\n" % num
            require(data[off:off + len(header)] == header, f"entry {num} offset {off} misses its object")
        pos += 20
    trailer = re.match(rb"trailer\n<< /Size (\d+) ", data[pos:])
    require(trailer is not None and int(trailer.group(1)) == count, "trailer /Size differs from xref count")
    return count, xref


def run_cli(binary, fixture, dest, quota):
    args = [str(binary)]
    if quota is not None:
        args += ["--max-output-bytes", str(quota)]
    args += [str(fixture), str(dest)]
    return subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE).returncode


def check_cli(binary, fixture):
    old = b"OLD destination must survive\n"
    with tempfile.TemporaryDirectory(prefix="ki-pdf-offsets-") as tmp:
        dest = Path(tmp) / "out.pdf"
        require(run_cli(binary, fixture, dest, 0) == 0, "unlimited conversion failed")
        ref = dest.read_bytes()
        total = len(ref)
        _, xref = check_pdf(ref)
        print(f"quota 0: exit 0, size {total}, startxref {xref}")
        dest.unlink()
        require(run_cli(binary, fixture, dest, total) == 0, f"quota {total} failed")
        require(dest.stat().st_size == total and dest.read_bytes() == ref, "quota N output differs")
        print(f"quota {total}: exit 0, size {dest.stat().st_size}")
        for quota in (total - 1, xref + 1, xref):
            dest.write_bytes(old)
            rc = run_cli(binary, fixture, dest, quota)
            require(rc == RESOURCE_EXIT, f"quota {quota}: exit {rc}, expected {RESOURCE_EXIT}")
            require(dest.read_bytes() == old, f"quota {quota} replaced the destination")
            leftovers = sorted(p.name for p in Path(tmp).iterdir())
            require(leftovers == ["out.pdf"], f"quota {quota} left {leftovers}")
            print(f"quota {quota}: exit {rc}, destination kept")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("pdf", nargs="*", type=Path)
    parser.add_argument("--binary", type=Path)
    parser.add_argument("--fixture", type=Path, default=ROOT / "tests/fixture.xml")
    args = parser.parse_args()
    require(args.pdf or args.binary, "nothing to check")
    for path in args.pdf:
        count, xref = check_pdf(path.read_bytes())
        print(f"{path}: {count} xref entries, startxref {xref}")
    if args.binary is not None:
        check_cli(args.binary.resolve(), args.fixture)
    print("pdf offsets ok")


if __name__ == "__main__":
    try:
        main()
    except AssertionError as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        sys.exit(1)
