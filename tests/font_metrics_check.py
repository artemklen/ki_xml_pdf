#!/usr/bin/env python3
"""Independent check of embedded font names, zero-advance marks and mark runs.

Font ground truth comes from this script's own TTF reader (cmap format 4,
hmtx, maxp, name), not from the converter. Poppler (pdffonts, pdftotext,
pdftoppm) reads the PDFs.

    python3 tests/font_metrics_check.py [--binary xml2pdf] [--evidence out.json]
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[1]
BUNDLED = ROOT / "fonts/DejaVuSans.ttf"
SYSTEM_BOLD = Path("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf")


def require(cond, msg):
    if not cond:
        raise AssertionError(msg)


class Ttf:
    def __init__(self, path):
        self.data = Path(path).read_bytes()
        d = self.data
        self.tables = {}
        for i in range(struct.unpack(">H", d[4:6])[0]):
            tag, _, off, ln = struct.unpack(">4sIII", d[12 + 16 * i:28 + 16 * i])
            self.tables[tag.decode("latin-1")] = d[off:off + ln]
        self.units = struct.unpack(">H", self.tables["head"][18:20])[0]
        self.num_h = struct.unpack(">H", self.tables["hhea"][34:36])[0]
        maxp = self.tables.get("maxp")
        self.num_glyphs = struct.unpack(">H", maxp[4:6])[0] if maxp else 0x10000
        cmap = self.tables["cmap"]
        self.sub = None
        for r in range(struct.unpack(">H", cmap[2:4])[0]):
            plat, enc, rel = struct.unpack(">HHI", cmap[4 + 8 * r:12 + 8 * r])
            if plat == 3 and enc == 1 and struct.unpack(">H", cmap[rel:rel + 2])[0] == 4:
                self.sub = cmap[rel:]
        require(self.sub is not None, "no (3,1) format 4 cmap")

    def gid(self, cp):
        s = self.sub
        seg = struct.unpack(">H", s[6:8])[0] // 2
        u16 = lambda at: struct.unpack(">H", s[at:at + 2])[0]
        for i in range(seg):
            if cp > u16(14 + 2 * i):
                continue
            start = u16(16 + 2 * seg + 2 * i)
            if cp < start:
                return 0
            delta = u16(16 + 4 * seg + 2 * i)
            roff = u16(16 + 6 * seg + 2 * i)
            if roff == 0:
                g = (cp + delta) & 0xFFFF
            else:
                g = u16(16 + 6 * seg + 2 * i + roff + 2 * (cp - start))
                g = 0 if g == 0 else (g + delta) & 0xFFFF
            return g if g < self.num_glyphs else 0
        return 0

    def advance(self, gid):
        h = self.tables["hmtx"]
        at = 4 * min(gid, self.num_h - 1)
        return struct.unpack(">H", h[at:at + 2])[0]

    def ps_name(self):
        t = self.tables["name"]
        count, store = struct.unpack(">HH", t[2:6])
        for i in range(count):
            plat, enc, lang, nid, ln, off = struct.unpack(">6H", t[6 + 12 * i:18 + 12 * i])
            if nid == 6 and plat == 3 and enc == 1 and lang == 0x409:
                return t[store + off:store + off + ln].decode("utf-16-be")
        return None


def pdf_objects(pdf):
    objs = {}
    for m in re.finditer(rb"(\d+) 0 obj\n(.*?)endobj\n", pdf, re.S):
        body = m.group(2)
        sm = re.search(rb"stream\n(.*)\nendstream", body, re.S)
        if sm:
            data = sm.group(1)
            if b"/FlateDecode" in body[:sm.start()]:
                data = zlib.decompress(data)
            body = body[:sm.start()] + b"stream\n" + data
        objs[int(m.group(1))] = body
    return objs


def run(binary, xml, font, out, expect=0):
    r = subprocess.run([str(binary), "--font", str(font), "-", str(out)], input=xml,
                       capture_output=True, timeout=60)
    require(r.returncode == expect,
            f"exit {r.returncode}, expected {expect}: {r.stderr.decode(errors='replace')}")
    return r


def text_of(pdf):
    r = subprocess.run(["pdftotext", "-enc", "UTF-8", str(pdf), "-"], capture_output=True,
                       check=True, timeout=60)
    return r.stdout.decode("utf-8")


def gray(pdf, page=1):
    """Grayscale page raster at 100 dpi as (width, height, bytes)."""
    with tempfile.TemporaryDirectory() as tmp:
        subprocess.run(["pdftoppm", "-gray", "-r", "100", "-f", str(page), "-l", str(page),
                        str(pdf), f"{tmp}/p"], check=True, timeout=60)
        img = next(Path(tmp).glob("p*.pgm")).read_bytes()
    m = re.match(rb"P5\s+(\d+)\s+(\d+)\s+255\s", img)
    w, h = int(m.group(1)), int(m.group(2))
    return w, h, img[m.end():]


def ink_rows(raster):
    w, h, px = raster
    return [y for y in range(h) if any(px[y * w + x] < 128 for x in range(w))]


def check_names(binary, tmp, ev):
    fonts = [BUNDLED] + ([SYSTEM_BOLD] if SYSTEM_BOLD.exists() else [])
    for path in fonts:
        ttf = Ttf(path)
        name = ttf.ps_name()
        out = tmp / f"name-{name}.pdf"
        run(binary, "<doc>Hello é</doc>".encode(), path, out)
        pdf = out.read_bytes()
        bases = re.findall(rb"/BaseFont /([^\s/<>\[\]()]+)", pdf)
        fname = re.findall(rb"/FontName /([^\s/<>\[\]()]+)", pdf)
        require(bases == [name.encode()] * 2 and fname == [name.encode()], f"{path}: {bases} {fname}")
        pf = subprocess.run(["pdffonts", str(out)], capture_output=True, check=True,
                            timeout=60).stdout.decode()
        require(re.search(rf"^{re.escape(name)}\s+CID TrueType\s+Identity-H\s+yes", pf, re.M),
                f"pdffonts: {pf}")
        require(text_of(out).strip() == "doc\nHello é", "name sample text")
        ev["fonts"].append({"path": str(path.name), "sha256": hashlib.sha256(ttf.data).hexdigest(),
                            "intrinsic_ps_name": name, "pdf_base_fonts": [b.decode() for b in bases],
                            "pdffonts": pf.splitlines()[2].split()[0]})
    require(len({f["intrinsic_ps_name"] for f in ev["fonts"]}) == len(ev["fonts"]), "names not distinct")


def check_marks(binary, tmp, ev):
    ttf = Ttf(BUNDLED)
    sample = "e\u0301 \u0438\u0306 \u0301x\u0483"
    out = tmp / "marks.pdf"
    run(binary, f"<doc>{sample}</doc>".encode(), BUNDLED, out)
    objs = pdf_objects(out.read_bytes())
    cid = next(b for b in objs.values() if b"/CIDFontType2" in b)
    widths = {int(g): int(w) for g, w in re.findall(rb"(\d+) \[(-?\d+)\]", cid)}
    cmap = next(b for b in objs.values() if b"begincmap" in b)
    tounicode = {}
    for g, u in re.findall(rb"<([0-9A-F]{4})> <([0-9A-F]{4})>", cmap):
        require(int(g, 16) not in tounicode, f"duplicate ToUnicode GID {g}")
        tounicode[int(g, 16)] = int(u, 16)
    truth = []
    for cp in sorted(set(map(ord, sample))):
        g = ttf.gid(cp)
        require(g != 0, f"bundled font lacks U+{cp:04X}")
        want = ttf.advance(g) * 1000 // ttf.units
        require(widths.get(g) == want, f"U+{cp:04X} GID {g}: /W {widths.get(g)} != {want}")
        require(tounicode.get(g) == cp, f"U+{cp:04X} GID {g}: ToUnicode {tounicode.get(g)}")
        truth.append({"cp": f"U+{cp:04X}", "gid": g, "advance": ttf.advance(g), "pdf_width": want})
    for cp in (0x301, 0x306, 0x483):
        require(ttf.advance(ttf.gid(cp)) == 0, f"U+{cp:04X} is not zero advance in the font")
    got = [ord(c) for c in text_of(out).split("\n", 1)[1].rstrip("\n\f")]
    want = [ord(c) for c in sample]
    require(got == want, f"extracted {got} != {want}")
    ev["glyph_truth"] = truth
    ev["extracted_codepoints"] = [f"U+{c:04X}" for c in got]

    base = tmp / "e.pdf"
    marked = tmp / "e-acute.pdf"
    run(binary, b"<doc>e</doc>", BUNDLED, base)
    run(binary, "<doc>e\u0301</doc>".encode(), BUNDLED, marked)
    rb_, rm = ink_rows(gray(base)), ink_rows(gray(marked))
    # Rows 0..~50 hold the "doc" line; compare the second line only.
    split = (min(rb_) + max(rb_)) // 2
    e_rows = [y for y in rb_ if y > split]
    m_rows = [y for y in rm if y > split]
    require(min(m_rows) < min(e_rows), "no mark ink above the e")
    require(max(m_rows) == max(e_rows), "mark changed the baseline ink")
    wb, hb, pb = gray(base)
    wm, hm, pm = gray(marked)
    require((wb, hb) == (wm, hm), "page size differs")
    width_of = lambda w, px, rows: {x for y in rows for x in range(w) if px[y * w + x] < 128}
    e_cols = width_of(wb, pb, e_rows)
    mark_cols = width_of(wm, pm, [y for y in m_rows if y < min(e_rows)])
    require(mark_cols and max(mark_cols) <= max(e_cols) + 2, "mark not placed over its base")
    ev["raster"] = {"e_top_row": min(e_rows), "marked_top_row": min(m_rows),
                    "mark_columns": [min(mark_cols), max(mark_cols)],
                    "e_columns": [min(e_cols), max(e_cols)]}

    for bad, cp in (("e\u1e00", "U+1E00"), ("\u0500", "U+0500")):
        r = run(binary, f"<doc>{bad}</doc>".encode(), BUNDLED, tmp / "bad.pdf", expect=1)
        require(cp in r.stderr.decode(), f"{bad!r}: {r.stderr}")
        require(not (tmp / "bad.pdf").exists(), "failed conversion published a PDF")
    ev["font_controls"] = ["U+1E00 exit 1", "U+0500 exit 1"]


def check_runs(binary, tmp, ev):
    rows = []
    for marks, expect in ((511, 0), (512, 4)):
        xml = ("<doc>a" + "\u0301" * marks + "</doc>").encode()
        out = tmp / f"run-{marks}.pdf"
        r = run(binary, xml, BUNDLED, out, expect=expect)
        if expect == 0:
            line = text_of(out).split("\n")[1]
            require(line == "a" + "\u0301" * marks, f"{marks} marks not intact")
        else:
            require("1023" in r.stderr.decode(), r.stderr)
            require(not out.exists(), "over-capacity run published a PDF")
        rows.append({"marks": marks, "line_bytes": 1 + 2 * marks, "exit": r.returncode})
    unit = "<r>\u0438\u0306\u0301</r>"
    xml = ("<doc>" + unit * 120 + "</doc>").encode()
    out = tmp / "pages.pdf"
    run(binary, xml, BUNDLED, out)
    text = text_of(out)
    require(text.count("\u0438\u0306\u0301") == 120 and text.count("\u0306") == 120, "mark count")
    pages = text.count("\f")
    require(pages == 5, f"pages {pages}")
    wrap = ("<doc>" + "e\u0301" * 200 + "</doc>").encode()
    run(binary, wrap, BUNDLED, tmp / "wrap.pdf")
    lines = [l for l in text_of(tmp / "wrap.pdf").split("\n")[1:] if l and l != "\f"]
    require("".join(lines) == "e\u0301" * 200, "wrapped text changed")
    require(all(l[0] == "e" for l in lines) and len(lines) >= 3, f"wrap split a mark: {lines}")
    ev["runs"] = {"capacity_bytes": 1023, "cases": rows, "pages_case": {"records": 120, "pages": pages},
                  "wrap_lines": len(lines)}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--binary", type=Path, default=ROOT / "xml2pdf")
    ap.add_argument("--evidence", type=Path)
    args = ap.parse_args()
    ev = {"bundled_sha256": hashlib.sha256(BUNDLED.read_bytes()).hexdigest(), "fonts": []}
    with tempfile.TemporaryDirectory() as t:
        tmp = Path(t)
        check_names(args.binary.resolve(), tmp, ev)
        check_marks(args.binary.resolve(), tmp, ev)
        check_runs(args.binary.resolve(), tmp, ev)
    if args.evidence:
        args.evidence.write_text(json.dumps(ev, indent=2, ensure_ascii=False) + "\n")
    print(f"font metrics: {len(ev['fonts'])} fonts, {len(ev['glyph_truth'])} glyphs, runs ok")


if __name__ == "__main__":
    main()
