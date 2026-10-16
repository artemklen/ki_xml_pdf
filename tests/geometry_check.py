#!/usr/bin/env python3
"""Independent page geometry check against Poppler.

Every page's MediaBox comes from pdfinfo and every page's actual ink from a
pdftoppm raster; expected boxes are computed here from the documented sizes
(Letter 612 x 792, A4 595.276 x 841.89, landscape swaps them), not read
from the converter. Rejected geometry must exit 2 without reading stdin and
without creating or changing the destination.

    python3 tests/geometry_check.py [--binary xml2pdf]
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

DPI = 144
PX = 72 / DPI
NAMED = {"letter": (612, 792), "a4": (595.276, 841.89)}

# Wide, tall and deep glyphs, a negative left bearing (j) and accents, so
# the ink reaches the edges of the line box.
TEXT = ("jW\u00c5\u00c9 jumpy Wgyq \u017d\u00e9 quick brown fox \u00c5W "
        "\u0416\u0429\u0444 \u00fdj ") * 150


def convert(binary, args, out, xml):
    r = subprocess.run([str(binary), "--font", str(BUNDLED), *args, "-", str(out)], input=xml.encode(),
                       capture_output=True, timeout=120)
    require(r.returncode == 0, f"{args}: exit {r.returncode}: {r.stderr.decode(errors='replace')}")


def boxes(pdf):
    r = subprocess.run(["pdfinfo", "-box", "-f", "1", "-l", "1000", str(pdf)], capture_output=True,
                       check=True, timeout=60)
    out = r.stdout.decode()
    pages = int(re.search(r"^Pages:\s+(\d+)", out, re.M).group(1))
    media = [tuple(float(v) for v in m.groups())
             for m in re.finditer(r"^Page\s+\d+ MediaBox:\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)", out, re.M)]
    require(len(media) == pages, f"{pdf}: {len(media)} MediaBox lines for {pages} pages")
    return media


def ink_box(pdf, page):
    """(x0, x1, y0_from_top, y1_from_top) of dark pixels in points."""
    with tempfile.TemporaryDirectory() as tmp:
        subprocess.run(["pdftoppm", "-gray", "-r", str(DPI), "-f", str(page), "-l", str(page), str(pdf),
                        f"{tmp}/p"], check=True, timeout=120)
        img = next(Path(tmp).glob("p*.pgm")).read_bytes()
    m = re.match(rb"P5\s+(\d+)\s+(\d+)\s+255\s", img)
    w, h = int(m.group(1)), int(m.group(2))
    px = img[m.end():]
    rows = [y for y in range(h) if min(px[y * w:(y + 1) * w]) < 250]
    require(rows, f"{pdf} page {page}: no ink")
    cols = [x for x in range(w) if min(px[x::w]) < 250]
    return cols[0] * PX, (cols[-1] + 1) * PX, rows[0] * PX, (rows[-1] + 1) * PX


def words(pdf):
    """[(page, xMin, xMax, text)] from pdftotext -bbox-layout. Each line's
    ActualText span comes back as one word."""
    r = subprocess.run(["pdftotext", "-bbox-layout", "-enc", "UTF-8", str(pdf), "-"], capture_output=True,
                       check=True, timeout=120)
    out, page = [], 0
    for line in r.stdout.decode().split("\n"):
        if "<page " in line:
            page += 1
        m = re.search(r'<word xMin="([\d.]+)" yMin="[\d.]+" xMax="([\d.]+)" yMax="[\d.]+">(.*)</word>', line)
        if m:
            out.append((page, float(m.group(1)), float(m.group(2)), m.group(3)))
    return out


def check_pages(binary, tmp):
    cases = [
        (["--page-size", "letter"], "letter", False, (40,) * 4, 10),
        (["--page-size", "letter", "--orientation", "landscape"], "letter", True, (40,) * 4, 10),
        (["--page-size", "a4"], "a4", False, (40,) * 4, 10),
        (["--page-size", "a4", "--orientation", "landscape", "--font-size", "12"], "a4", True, (40,) * 4, 12),
        (["--page-width", "360", "--page-height", "504", "--margin", "24"], (360, 504), False, (24,) * 4, 10),
        (["--page-width", "504", "--page-height", "360", "--margin", "24", "--font-size", "12"], (504, 360),
         False, (24,) * 4, 12),
        (["--page-width", "360", "--page-height", "504", "--margin", "0"], (360, 504), False, (0,) * 4, 10),
        (["--page-width", "400", "--page-height", "300", "--margin", "10,30,50,70", "--font-size", "10.5"],
         (400, 300), False, (10, 30, 50, 70), 10.5),
        ([], "letter", False, (40,) * 4, 10),
    ]
    xml = "<doc><p>" + TEXT + "</p></doc>"
    for n, (args, size, land, (top, right, bottom, left), _font) in enumerate(cases):
        w, h = NAMED[size] if isinstance(size, str) else size
        if land:
            w, h = h, w
        pdf = tmp / f"page{n}.pdf"
        convert(binary, args, pdf, xml)
        media = boxes(pdf)
        require(len(media) >= 2, f"{args}: expected wrapped text over several pages, got {len(media)}")
        for box in media:
            # pdfinfo prints two decimals.
            require(all(abs(a - b) < 0.006 for a, b in zip(box, (0, 0, w, h))),
                    f"{args}: MediaBox {box}, expected 0 0 {w} {h}")
        found = words(pdf)
        got = " ".join(t for _, _, _, t in found).split()
        require(got == ["doc", "p"] + TEXT.split(), f"{args}: extracted text differs from the input")
        for page, x0, x1, t in found:
            # Trailing spaces may hang past the right margin, so only the
            # left edge of these advance boxes is bounded; the raster bounds ink.
            require(x0 >= left - 0.01, f"{args} p{page}: line {t[:20]!r} starts at {x0}")
        require(min(x0 for _, x0, _, _ in found) < left + 0.5, f"{args}: no line starts at the left margin")
        for page in (1, len(media)):
            x0, x1, y0, y1 = ink_box(pdf, page)
            # A pixel touched by antialiasing may extend one pixel past the ink.
            require(x0 >= left - PX and x1 <= w - right + PX, f"{args} p{page}: ink x {x0}..{x1}")
            require(y0 >= top - PX and y1 <= h - bottom + PX, f"{args} p{page}: ink y {y0}..{y1}")
            if page == 1:
                require(x0 <= left + 1.5, f"{args}: line starts {x0 - left:.2f} pt right of the margin")
    return len(cases)


def check_rejections(binary, tmp):
    bad = [
        ["--page-size", "A4"], ["--page-size", "legal"], ["--orientation", "sideways"],
        ["--page-size", "a4", "--page-size", "a4"], ["--orientation", "portrait", "--orientation", "portrait"],
        ["--page-width", "300"], ["--page-height", "300"],
        ["--page-width", "300", "--page-height", "400", "--page-size", "a4"],
        ["--page-width", "300", "--page-height", "400", "--orientation", "landscape"],
        ["--page-width", "300", "--page-width", "300", "--page-height", "400"],
        ["--margin", "10", "--margin", "10"], ["--font-size", "10", "--font-size", "10"],
        ["--page-width", "2.999", "--page-height", "400"], ["--page-width", "300", "--page-height", "14400.001"],
        ["--page-width", "300", "--page-height", "400", "--margin", "0,150,0,150"],
        ["--page-width", "300", "--page-height", "400", "--margin", "200,0,200,0"],
        ["--margin", "14400.5"], ["--font-size", "0"], ["--font-size", "0.5"], ["--font-size", "144.5"],
        ["--page-width", "100", "--page-height", "100", "--font-size", "144"],
        ["--page-width", "50", "--page-height", "400", "--margin", "0,0,0,0", "--font-size", "144"],
    ]
    for v in ["1e3", "-5", "+5", "0x10", " 5", "5 ", "5.", ".5", "inf", "nan", "5pt", "", "1,2,3",
              "1,2,3,4,5", "1,,2,3", "1,2,3,4,", "5\u00a0", "1" * 40]:
        bad.append(["--margin", v])
        if "," not in v:
            bad.append(["--font-size", v])
    sentinel = b"previous destination\n"
    for n, args in enumerate(bad):
        kept = tmp / f"kept{n}.pdf"
        kept.write_bytes(sentinel)
        fresh = tmp / f"fresh{n}.pdf"
        for dest in (kept, fresh):
            # stdin stays open: reading it would block until the timeout.
            p = subprocess.Popen([str(binary), "--font", str(BUNDLED), *args, "-", str(dest)],
                                 stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            try:
                rc = p.wait(timeout=20)
            except subprocess.TimeoutExpired:
                p.kill()
                p.wait()
                raise AssertionError(f"{args}: read stdin instead of rejecting the geometry")
            err = p.stderr.read().decode(errors="replace")
            p.stdin.close()
            p.stdout.close()
            p.stderr.close()
            require(rc == 2, f"{args}: exit {rc}, expected 2: {err}")
        require(kept.read_bytes() == sentinel, f"{args}: destination changed")
        require(not fresh.exists(), f"{args}: destination created")
        missing = tmp / "missing.xml"
        r = subprocess.run([str(binary), "--font", str(BUNDLED), *args, str(missing), "-"],
                           capture_output=True, timeout=20)
        require(r.returncode == 2 and r.stdout == b"", f"{args}: missing input gave exit {r.returncode}")
    leftovers = [f for f in os.listdir(tmp) if not f.startswith("kept")]
    require(leftovers == [], f"temporary files left: {leftovers}")
    return len(bad)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--binary", type=Path, default=ROOT / "xml2pdf")
    args = ap.parse_args()
    binary = args.binary.resolve()
    with tempfile.TemporaryDirectory() as t:
        pages = check_pages(binary, Path(t))
    with tempfile.TemporaryDirectory() as t:
        bad = check_rejections(binary, Path(t))
    print(f"geometry: {pages} page layouts inside their margins, {bad} rejected option sets")


if __name__ == "__main__":
    main()
