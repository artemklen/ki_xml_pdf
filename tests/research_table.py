#!/usr/bin/env python3
"""OSR-18 research harness: bounded fixed-column tables on synthetic rows.

Research tool only; the library, CLI and installed examples do not use it
and no table API ships. It models a streaming fixed-column table with
explicit caps (BoundedTable), and drives the unchanged xml2pdf through an
external pre-layout that pads each physical line with spaces to the column
starts (records-v1, one CDATA element). Checks:

  fixture    expected physical lines per page == pdftotext of the PDF;
             every cell's text is recovered from its wrapped lines
  raster     pdftoppm: the gutter right of every column has no ink and every
             non-empty cell line has ink within one space of its column start
  caps       each cap/policy fails at the exact row, column and byte
  firstn     first-N auto widths: buffered bytes and the later wider row wraps
  stream     100,000 short rows piped into xml2pdf; converter RSS
  hugecell   an 8 MiB cell: the prototype refuses it at byte 4097; report-v1
             streams the same value as one field with flat RSS
  late       page quota and a late cap error keep an existing destination

usage: research_table.py --binary ./xml2pdf --scratch DIR [--json OUT]
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import unicodedata

sys.path.insert(0, str(Path(__file__).resolve().parent))
from font_metrics_check import BUNDLED, ROOT, Ttf  # noqa: E402

# Default geometry: Letter, 40 pt margins, 10 pt type, 50 lines per page.
FONT_PT = 10
TEXT_WIDTH_PT = 532
LEFT_PT = 40
PAGE_LINES = 50
LINE_BYTES = 1023

CAPS = {
    "columns": 16,
    "cell_bytes": 4096,
    "row_bytes": 16384,
    "header_cell_bytes": 255,
    "header_lines": 12,
    "first_n_rows": 32,
}
GUTTER_PT = 8


class TableError(Exception):
    def __init__(self, cause, rule, row=None, col=None, byte=None):
        super().__init__(f"{cause}: {rule} (row {row}, column {col}, byte {byte})")
        self.cause, self.rule, self.row, self.col, self.byte = cause, rule, row, col, byte

    def record(self):
        return {"cause": self.cause, "rule": self.rule, "row": self.row, "col": self.col, "byte": self.byte}


class Metrics:
    def __init__(self, font):
        self.font = font
        self.units = font.units
        self.space = self.adv(" ")

    def adv(self, ch):
        cp = ord(" " if ch == "\t" else ch)
        g = self.font.gid(cp) if 0x20 <= cp < 0x500 or 0x2010 <= cp < 0x2028 else 0
        if g == 0:
            raise TableError("font", f"U+{ord(ch):04X}")
        return self.font.advance(g)

    def pt(self, units):
        return units * FONT_PT / self.units

    def limit(self, width_pt):
        return int(width_pt * self.units // FONT_PT)

    def width(self, s):
        return sum(self.adv(c) for c in s)

    def widest(self):
        cps = [c for c in list(range(0x20, 0x500)) + list(range(0x2010, 0x2028)) if self.font.gid(c)]
        best = max(cps, key=lambda c: self.font.advance(self.font.gid(c)))
        return best, self.font.advance(self.font.gid(best))


def graphemes(word, m):
    """Base code point plus following zero-advance marks: the unit a cut keeps together."""
    out = []
    for ch in word:
        a = m.adv(ch)
        if a == 0 and out:
            out[-1] = (out[-1][0] + ch, out[-1][1])
        else:
            out.append((ch, a))
    return out


def wrap_cell(text, m, width_pt):
    """OSR-14 rule inside one column. Spaces at a break are not drawn; a word
    wider than the column is cut before the grapheme that would overflow."""
    limit = m.limit(width_pt)
    lines = []
    for para in text.replace("\t", " ").split("\n"):
        line, units = "", 0
        for tok in re.findall(r" +|[^ ]+", para):
            if tok[0] == " ":
                line, units = line + tok, units + m.width(tok)
                continue
            wu = m.width(tok)
            if line.strip() and units + wu > limit:
                lines.append(line.rstrip(" "))
                line, units = "", 0
            if units + wu <= limit:
                line, units = line + tok, units + wu
                continue
            if line.strip():
                lines.append(line.rstrip(" "))
                line, units = "", 0
            for g, a in graphemes(tok, m):
                if a > limit:
                    raise TableError("resource", "glyph wider than column")
                if units + a > limit:
                    lines.append(line)
                    line, units = "", 0
                line, units = line + g, units + a
        lines.append(line.rstrip(" "))
    return lines


class BoundedTable:
    """Streaming fixed-column table. Holds the header and one row of UTF-8
    bytes; everything else goes straight to the page sink."""

    def __init__(self, m, columns, sink, caps=CAPS, first_page_lines=PAGE_LINES - 1):
        self.m, self.caps, self.sink = m, caps, sink
        if not 1 <= len(columns) <= caps["columns"]:
            raise TableError("usage", "column count", col=len(columns))
        _, widest = m.widest()
        x = 0.0
        self.cols = []
        for j, (head, w) in enumerate(columns):
            if m.limit(w) < widest:
                raise TableError("usage", "column narrower than widest glyph", col=j)
            hb = len(head.encode())
            if hb > caps["header_cell_bytes"]:
                raise TableError("resource", "header cell bytes", col=j, byte=caps["header_cell_bytes"] + 1)
            self.cols.append((x, w))
            x += w + GUTTER_PT
        if x - GUTTER_PT + m.pt(m.space) > TEXT_WIDTH_PT:
            raise TableError("usage", "columns wider than text width")
        self.header = self.physical([wrap_cell(h, m, w) for h, w in columns])
        if len(self.header) > caps["header_lines"]:
            raise TableError("resource", "header lines", byte=len(self.header))
        self.page_left = first_page_lines
        self.page_lines = PAGE_LINES
        self.at_top = True
        self.pages = 0
        self.rows = 0
        self.row = None
        self.peak_buffer = sum(len(h.encode()) for h, _ in columns)
        self.header_bytes = self.peak_buffer
        self.max_row_pages = 0

    def physical(self, cells):
        """Pads each cell line to its column start. Pads from the absolute pen,
        so the error never accumulates and is below one space."""
        out = []
        for k in range(max(len(c) for c in cells)):
            s, pen = "", 0
            for j, c in enumerate(cells):
                t = c[k] if k < len(c) else ""
                if not t:
                    continue
                target = self.m.limit(self.cols[j][0])
                while pen < target:
                    s += " "
                    pen += self.m.space
                s += t
                pen += self.m.width(t)
            if len(s.encode()) > LINE_BYTES:
                raise TableError("resource", "physical line bytes", row=self.rows, byte=len(s.encode()))
            out.append(s)
        return out

    def _emit(self, line):
        if self.page_left == 0:
            self.page_left = self.page_lines
            self.at_top = True
        if self.at_top:
            self.pages += 1
            for h in self.header:
                self.sink(h)
            self.page_left -= len(self.header)
            self.at_top = False
        self.sink(line)
        self.page_left -= 1

    def start_row(self):
        self.row = []
        self.row_bytes = 0

    def cell(self, fragments):
        j = len(self.row)
        if j >= len(self.cols):
            raise TableError("profile", "more cells than columns", row=self.rows, col=j)
        buf = bytearray()
        for frag in fragments:
            b = frag.encode()
            if len(buf) + len(b) > self.caps["cell_bytes"]:
                raise TableError("resource", "cell bytes", row=self.rows, col=j, byte=self.caps["cell_bytes"] + 1)
            if self.row_bytes + len(buf) + len(b) > self.caps["row_bytes"]:
                raise TableError("resource", "row bytes", row=self.rows, col=j, byte=self.caps["row_bytes"] + 1)
            buf += b
            self.peak_buffer = max(self.peak_buffer, self.header_bytes + self.row_bytes + len(buf))
        self.row_bytes += len(buf)
        self.row.append(buf.decode())

    def end_row(self):
        if len(self.row) != len(self.cols):
            raise TableError("profile", "fewer cells than columns", row=self.rows, col=len(self.row))
        lines = self.physical([wrap_cell(t, self.m, w) for t, (_, w) in zip(self.row, self.cols)])
        body = self.page_lines - len(self.header)
        if not self.at_top and self.page_left < len(lines) <= body:
            while self.page_left > 0:
                self.sink("")
                self.page_left -= 1
        first = self.pages if not self.at_top and self.page_left else self.pages + 1
        for ln in lines:
            self._emit(ln)
        self.max_row_pages = max(self.max_row_pages, self.pages - first + 1)
        self.rows += 1
        self.row = None
        return lines


def sha(data):
    return hashlib.sha256(data).hexdigest()


def cdata(s):
    return s.replace("]]>", "]]]]><![CDATA[>")


class XmlSink:
    """records-v1 pre-layout output: one <t> element whose CDATA lines are
    the physical lines; the element name takes the first line of page 1."""

    def __init__(self, write):
        self.write = write
        self.lines = []
        write("<t><![CDATA[")
        self.first = True

    def __call__(self, line):
        self.write(("" if self.first else "\n") + cdata(line))
        self.first = False
        self.lines.append(line)

    def close(self):
        self.write("]]></t>\n")


COLUMNS = [("N", 30), ("Наименование товара или услуги", 170), ("Описание", 220), ("Сумма, руб.", 70)]
LOREM = ("Синтетическая строка отчёта с кириллицей, «кавычками», тире — и многоточием… "
         "Проверка переноса между словами внутри фиксированной колонки. ")


def fixture_rows():
    rows = []
    for i in range(1, 31):
        rows.append([str(i), f"Товар {i}", f"Описание позиции {i}: " + LOREM[: 40 + 7 * (i % 9)], f"{i * 137},50"])
    rows[2][2] = ""
    rows[3] = ["", "", "", ""]
    rows[5][1] = "Сверхдлинноенаименованиебезединогопробеладлякодовойточки"
    rows[6][1] = "Йод и\u0306 комбинированный знак, табуляция\tвнутри"
    rows[7][2] = "Текст с ]]> внутри CDATA и абзацем\nвторая строка ячейки"
    rows[9][2] = (LOREM * 30).encode()[:4000].decode(errors="ignore")
    rows[24][1] = "Поздняя широкая позиция: " + "очень длинное наименование " * 6
    return rows


def build_fixture(m, binary, scratch, name, widen=0):
    """Pre-layout of the fixture into records-v1 XML and the PDF. widen > 0 is
    the negative control: columns other than the last wrap that much wider
    than their declared width, so text runs into the gutter."""
    chunks = []
    sink = XmlSink(chunks.append)
    t = BoundedTable(m, COLUMNS, sink)
    declared = list(t.cols)
    if widen:
        t.cols = [(x, w + widen) if j + 1 < len(t.cols) else (x, w) for j, (x, w) in enumerate(t.cols)]
    rows = fixture_rows()
    for r in rows:
        t.start_row()
        for c in r:
            t.cell([c[i:i + 7] for i in range(0, len(c), 7)] or [""])
        t.end_row()
    sink.close()
    t.cols = declared
    xml = "".join(chunks)
    xml_path = scratch / f"{name}.xml"
    xml_path.write_text(xml, encoding="utf-8")
    pdf = scratch / f"{name}.pdf"
    r = subprocess.run([str(binary), "--font", str(BUNDLED), str(xml_path), str(pdf)], capture_output=True)
    return t, sink, rows, xml, pdf, r


def expected_pages(lines):
    expect, cur, left = [], ["t"], PAGE_LINES - 1
    for ln in lines:
        if left == 0:
            expect.append(cur)
            cur, left = [], PAGE_LINES
        if ln.strip():
            cur.append(ln)
        left -= 1
    expect.append(cur)
    return expect


def nfc(pages):
    return [[unicodedata.normalize("NFC", x) for x in p] for p in pages]


def run_fixture(m, binary, scratch, ev):
    t, sink, rows, xml, pdf, r = build_fixture(m, binary, scratch, "fixture")
    recovered = 0
    for row in rows:
        for c, (_, w) in zip(row, COLUMNS):
            if re.sub(r"\s", "", "".join(wrap_cell(c, m, w))) == re.sub(r"\s", "", c):
                recovered += 1
    text = subprocess.run(["pdftotext", str(pdf), "-"], capture_output=True, check=True).stdout.decode()
    pdf_pages = [[ln for ln in p.split("\n") if ln.strip()] for p in text.split("\f")[:-1]]
    expect = expected_pages(sink.lines)
    ok = r.returncode == 0 and nfc(pdf_pages) == nfc(expect) and recovered == 4 * len(rows)
    # Control: the text oracle must notice one dropped character.
    tampered = [list(p) for p in expect]
    tampered[1][5] = tampered[1][5][:-1]
    text_control = nfc(pdf_pages) != nfc(tampered)
    ev["fixture"] = {
        "ok": ok, "exit": r.returncode, "xml_sha256": sha(xml.encode()), "pdf_sha256": sha(pdf.read_bytes()),
        "rows": len(rows), "pages_model": t.pages, "pages_pdf": len(pdf_pages), "header_lines": len(t.header),
        "physical_lines": len(sink.lines), "cells_recovered": recovered, "peak_buffer_bytes": t.peak_buffer,
        "max_row_pages": t.max_row_pages, "column_x_pt": [x for x, _ in t.cols],
        "column_width_pt": [w for _, w in t.cols], "text_control_detects_drop": text_control,
        "expected_pages": expect,
    }
    ev["fixture"]["raster"] = raster_check(m, t, pdf, scratch, sink.lines, "fixture")
    tc, sc, _, _, pdfc, rc = build_fixture(m, binary, scratch, "control-widen", widen=40)
    ctrl = raster_check(m, tc, pdfc, scratch, sc.lines, "control-widen")
    ev["fixture"]["raster_control_widen_40pt"] = {"exit": rc.returncode, "gutter_ink_px": ctrl["gutter_ink_px"],
                                                  "detected": not ctrl["ok"]}
    return (ok and text_control and ev["fixture"]["raster"]["ok"]
            and ev["fixture"]["raster_control_widen_40pt"]["detected"])


def raster_check(m, t, pdf, scratch, lines, prefix):
    """Independent of the PDF writer: ink found in a 144 dpi render."""
    dpi = 144
    sc = dpi / 72
    res = {"ok": True, "pages": [], "gutter_ink_px": 0, "cell_starts_checked": 0, "cell_start_misses": 0}
    space_pt = m.pt(m.space)
    pages_lines = []
    cur, left = [None], PAGE_LINES - 1
    for ln in lines:
        if left == 0:
            pages_lines.append(cur)
            cur, left = [], PAGE_LINES
        cur.append(ln)
        left -= 1
    pages_lines.append(cur)
    for pno in (1, 2, 3):
        if pno > len(pages_lines):
            break
        base = scratch / f"{prefix}-p{pno}"
        subprocess.run(["pdftoppm", "-r", str(dpi), "-gray", "-f", str(pno), "-l", str(pno), str(pdf), str(base)],
                       check=True, capture_output=True)
        pgm = next(scratch.glob(f"{prefix}-p{pno}*.pgm"))
        w, h, px = read_pgm(pgm)
        bbox = subprocess.run(["pdftotext", "-bbox", "-f", str(pno), "-l", str(pno), str(pdf), "-"],
                              capture_output=True, check=True).stdout.decode()
        boxes = [(float(a), float(b)) for a, b in re.findall(r'yMin="([\d.]+)" xMax="[\d.]+" yMax="([\d.]+)"', bbox)]
        texts = [ln for ln in pages_lines[pno - 1] if ln is None or ln.strip()]
        if len(boxes) != len(texts):
            res["ok"] = False
            res["pages"].append({"page": pno, "line_count_mismatch": [len(boxes), len(texts)]})
            continue
        gutter_ink = 0
        for (y0, y1), ln in zip(boxes, texts):
            if ln is None:
                continue
            r0, r1 = int(y0 * sc), int(y1 * sc) + 1
            for j, (x, cw) in enumerate(t.cols):
                g0 = LEFT_PT + x + cw + space_pt + 0.5
                g1 = LEFT_PT + (t.cols[j + 1][0] if j + 1 < len(t.cols) else TEXT_WIDTH_PT) - 0.5
                gutter_ink += ink(px, w, r0, r1, int(g0 * sc) + 1, int(g1 * sc))
                cell_text = cell_at(m, t, ln, j)
                if cell_text:
                    res["cell_starts_checked"] += 1
                    s0, s1 = LEFT_PT + x, LEFT_PT + x + space_pt + 2
                    if ink(px, w, r0, r1, int(s0 * sc), int(s1 * sc)) == 0:
                        res["cell_start_misses"] += 1
        res["gutter_ink_px"] += gutter_ink
        res["pages"].append({"page": pno, "lines": len(boxes), "gutter_ink_px": gutter_ink})
    res["ok"] = (res["ok"] and res["gutter_ink_px"] == 0 and res["cell_start_misses"] == 0
                 and res["cell_starts_checked"] > 0)
    return res


def read_pgm(path):
    data = path.read_bytes()
    m = re.match(rb"P5\s+(\d+)\s+(\d+)\s+(\d+)\s", data)
    w, h = int(m.group(1)), int(m.group(2))
    return w, h, data[m.end():]


def ink(px, w, r0, r1, c0, c1):
    n = 0
    for y in range(r0, r1):
        row = px[y * w + c0: y * w + max(c0, c1)]
        n += sum(1 for v in row if v < 128)
    return n


def cell_at(m, t, line, j):
    """Text of column j in a padded physical line, by pen position."""
    pen, out = 0, ""
    lo = m.limit(t.cols[j][0])
    hi = m.limit(t.cols[j + 1][0]) if j + 1 < len(t.cols) else 1 << 60
    for ch in line:
        if lo <= pen < hi:
            out += ch
        pen += m.adv(ch)
    return out.strip()


def expect_error(fn, cause, rule, **where):
    try:
        fn()
    except TableError as e:
        rec = e.record()
        ok = rec["cause"] == cause and rec["rule"] == rule and all(rec[k] == v for k, v in where.items())
        return ok, rec
    return False, None


def run_caps(m, ev):
    out = {}
    sink = lambda line: None

    def table(cols=COLUMNS, **kw):
        return BoundedTable(m, cols, sink, **kw)

    def one_row(t, cells):
        t.start_row()
        for c in cells:
            t.cell(c)
        t.end_row()

    widest_cp, widest = m.widest()
    out["widest_glyph"] = {"cp": f"U+{widest_cp:04X}", "units": widest, "pt": m.pt(widest)}
    out["min_column_pt"] = m.pt(widest)
    out["max_columns_by_width"] = int((TEXT_WIDTH_PT - m.pt(m.space) + GUTTER_PT) // (m.pt(widest) + GUTTER_PT))
    cases = {
        "17 columns": (lambda: table([("c", 20)] * 17), "usage", "column count", {"col": 17}),
        "column 14 pt": (lambda: table([("c", 14)]), "usage", "column narrower than widest glyph", {"col": 0}),
        "columns too wide": (lambda: table([("c", 300), ("d", 300)]), "usage", "columns wider than text width", {}),
        "header cell 256 bytes": (lambda: table([("ж" * 128, 100)]), "resource", "header cell bytes",
                                  {"col": 0, "byte": 256}),
        "header 13 lines": (lambda: table([("ш " * 60, 30)]), "resource", "header lines", {}),
        "cell 4097 bytes": (lambda: one_row(table(), [["1"], ["x" * 4096, "y"], [""], [""]]), "resource",
                            "cell bytes", {"row": 0, "col": 1, "byte": 4097}),
        "row 16385 bytes": (lambda: one_row(table([("c", 30)] * 5), [["ж" * 2048]] * 4 + [["x"]]), "resource",
                            "row bytes", {"row": 0, "col": 4, "byte": 16385}),
        "5 cells for 4 columns": (lambda: one_row(table(), [["a"]] * 5), "profile", "more cells than columns",
                                  {"col": 4}),
        "3 cells for 4 columns": (lambda: one_row(table(), [["a"]] * 3), "profile", "fewer cells than columns",
                                  {"col": 3}),
        "mark bomb 1 base + 600 U+0301": (lambda: one_row(table(), [["1"], ["a" + "\u0301" * 600], [""], [""]]),
                                          "resource", "physical line bytes", {"row": 0}),
        "glyph outside repertoire": (lambda: one_row(table(), [["1"], ["\u4e2d"], [""], [""]]), "font",
                                     "U+4E2D", {}),
    }
    ok = True
    for name, (fn, cause, rule, where) in cases.items():
        good, rec = expect_error(fn, cause, rule, **where)
        out[name] = {"ok": good, "error": rec}
        ok = ok and good

    # Huge cell streamed in 64 KiB fragments: refused at byte 4097, holding
    # no more than the header and the row so far.
    t = table()
    frags = ("Ж" * 512 for _ in range(8192))  # 8 MiB in parser-sized 1 KiB fragments
    good, rec = expect_error(lambda: (t.start_row(), t.cell(["1"]), t.cell(frags)), "resource", "cell bytes",
                             byte=4097)
    out["huge cell 8 MiB"] = {"ok": good, "error": rec, "peak_buffer_bytes": t.peak_buffer,
                              "fragments_consumed": 8192 - sum(1 for _ in frags), "fragments_total": 8192}
    ok = ok and good and t.peak_buffer <= CAPS["row_bytes"] + 16 * CAPS["header_cell_bytes"]
    # Control: the same stream with the byte caps lifted is accepted and the
    # row buffer becomes the whole cell, so the caps are what bound memory.
    lifted = dict(CAPS, cell_bytes=1 << 40, row_bytes=1 << 40)
    t = BoundedTable(m, COLUMNS, sink, caps=lifted)
    t.start_row()
    t.cell(["1"])
    t.cell("Ж" * 512 for _ in range(8192))
    out["control: caps lifted, 8 MiB cell"] = {"accepted": True, "peak_buffer_bytes": t.peak_buffer}
    ok = ok and t.peak_buffer > 8 * 1024 * 1024

    # Worst tall row inside the caps: every cell at the cap of a wide 1-byte
    # glyph in minimum-width columns.
    wide_ascii = max((chr(c) for c in range(0x21, 0x7f)), key=m.adv)
    ncol = CAPS["row_bytes"] // CAPS["cell_bytes"]
    t = table([("c", 15)] * ncol)
    one_row(t, [[wide_ascii * CAPS["cell_bytes"]]] * ncol)
    out["tallest row within caps"] = {"glyph": wide_ascii, "columns": ncol, "pages": t.pages,
                                      "peak_buffer_bytes": t.peak_buffer}
    # Same row with 1-byte narrow glyphs: line count bound for any capped row.
    narrow = min((chr(c) for c in range(0x21, 0x7f)), key=m.adv)
    t2 = table([("c", 15)])
    one_row(t2, [[(narrow + " ") * (CAPS["cell_bytes"] // 2)]])
    out["one capped cell of narrow words, 15 pt"] = {"glyph": narrow, "pages": t2.pages}

    # Repeated header and page breaks: 2-line header, 50-line pages.
    t = table()
    for i in range(200):
        one_row(t, [[str(i)], ["x"], [""], [""]])
    out["repeated header"] = {"rows": 200, "header_lines": len(t.header), "pages": t.pages,
                              "expect_pages": -(-(200 + 1) // (PAGE_LINES - len(t.header)))}
    ok = ok and out["tallest row within caps"]["pages"] >= 1
    ev["caps"] = out
    return ok


def run_first_n(m, ev):
    """First-N sizing: widths from the natural width of the first N rows,
    scaled to the text width; later rows wrap in those widths."""
    n = CAPS["first_n_rows"]
    rows = fixture_rows()[:24] + [["99", "Поздняя очень широкая строка " * 4, "x", "1"]] + fixture_rows()[25:]
    rows = (rows * 2)[:n] + [["100", "Поздняя очень широкая строка " * 4, "коротко", "1"]]
    buffered, peak = [], 0
    nat = [0] * 4
    for r in rows[:n]:
        buffered.append(r)
        peak = max(peak, sum(len(c.encode()) for rr in buffered for c in rr))
        for j, c in enumerate(r):
            nat[j] = max([nat[j]] + [m.width(p) for p in c.split("\n")])
    avail = TEXT_WIDTH_PT - 3 * GUTTER_PT - m.pt(m.space)
    min_u = m.widest()[1]
    nat = [max(u, min_u) for u in nat]
    floor_pt = int(m.pt(min_u)) + 1
    spare = avail - floor_pt * len(nat)
    widths = [floor_pt + int(spare * u / sum(nat)) for u in nat]
    cols = [(h, w) for (h, _), w in zip(COLUMNS, widths)]
    t = BoundedTable(m, cols, lambda line: None)
    late = None
    for i, r in enumerate(rows):
        t.start_row()
        for c in r:
            t.cell([c])
        lines = t.end_row()
        if i == n:
            late = len(lines)
    ev["first_n"] = {"n": n, "widths_pt": widths, "buffered_bytes_peak": peak,
                     "worst_case_buffer_bytes": n * CAPS["row_bytes"], "late_row_lines": late}
    return late is not None and late > 1


def big_table_xml(write, rows, m, error_at=None):
    sink = XmlSink(write)
    t = BoundedTable(m, [("N", 60), ("Код", 80), ("Наименование", 250), ("Сумма", 80)], sink)
    for i in range(rows):
        t.start_row()
        t.cell([str(i + 1)])
        t.cell([f"К-{i % 977:04d}"])
        t.cell(["Ж" * 4097] if i == error_at else [f"Позиция {i + 1} синтетического отчёта"])
        t.cell([f"{(i * 37) % 100000},{i % 100:02d}"])
        t.end_row()
    sink.close()
    return t


def timed(cmd, **kw):
    full = ["/usr/bin/time", "-f", "RSS_KIB=%M WALL=%e"] + [str(c) for c in cmd]
    r = subprocess.run(full, capture_output=True, **kw)
    mm = re.search(rb"RSS_KIB=(\d+) WALL=([\d.]+)", r.stderr)
    return r, int(mm.group(1)), float(mm.group(2))


def run_stream(m, binary, scratch, ev):
    rows = 100000
    xml = scratch / "stream.xml"
    with open(xml, "w", encoding="utf-8") as f:
        t = big_table_xml(f.write, rows, m)
    pdf = scratch / "stream.pdf"
    r, rss, wall = timed([binary, "--font", BUNDLED, xml, pdf])
    small = scratch / "small.xml"
    small.write_text("<t><![CDATA[x]]></t>\n")
    r0, rss0, _ = timed([binary, "--font", BUNDLED, small, scratch / "small.pdf"])
    info = subprocess.run(["pdfinfo", str(pdf)], capture_output=True, text=True).stdout
    pages = int(re.search(r"Pages:\s+(\d+)", info).group(1))
    last = subprocess.run(["pdftotext", "-f", str(pages), "-l", str(pages), str(pdf), "-"],
                          capture_output=True, text=True).stdout
    ev["stream"] = {"rows": rows, "xml_bytes": xml.stat().st_size, "xml_sha256": sha(xml.read_bytes()),
                    "exit": r.returncode, "pages_model": t.pages, "pages_pdf": pages,
                    "pdf_bytes": pdf.stat().st_size, "converter_rss_kib": rss, "wall_s": wall,
                    "small_doc_rss_kib": rss0, "prelayout_peak_buffer_bytes": t.peak_buffer,
                    "last_row_on_last_page": f"Позиция {rows} синтетического" in last}
    ok = r.returncode == 0 and pages == t.pages and ev["stream"]["last_row_on_last_page"]
    xml.unlink()
    pdf.unlink()
    # The same rows as report-v1 sequential blocks, which need no table code.
    seq = scratch / "stream-report.xml"
    with open(seq, "w", encoding="utf-8") as f:
        f.write('<report version="1" title="Таблица как блоки">\n')
        for i in range(rows):
            f.write(f'<record id="{i + 1}"><field name="Код">К-{i % 977:04d}</field>'
                    f'<field name="Наименование">Позиция {i + 1} синтетического отчёта</field>'
                    f'<field name="Сумма">{(i * 37) % 100000},{i % 100:02d}</field></record>\n')
        f.write("</report>\n")
    spdf = scratch / "stream-report.pdf"
    r2, rss2, wall2 = timed([binary, "--font", BUNDLED, "--profile", "report-v1", seq, spdf])
    info = subprocess.run(["pdfinfo", str(spdf)], capture_output=True, text=True).stdout
    ev["stream"]["report_v1_blocks"] = {"exit": r2.returncode, "xml_bytes": seq.stat().st_size,
                                        "pages": int(re.search(r"Pages:\s+(\d+)", info).group(1)),
                                        "pdf_bytes": spdf.stat().st_size, "converter_rss_kib": rss2,
                                        "wall_s": wall2}
    seq.unlink()
    spdf.unlink()
    return ok and r2.returncode == 0


def run_hugecell(m, binary, scratch, ev):
    mib = 8
    word = "Огромнаяячейка "
    value = (word * (mib * 1024 * 1024 // len(word.encode()) + 1))
    value = value.encode()[: mib * 1024 * 1024].decode(errors="ignore")
    xml = scratch / "huge-report.xml"
    with open(xml, "w", encoding="utf-8") as f:
        f.write('<report version="1" title="Огромная ячейка"><record id="1">'
                '<field name="Начало">НАЧАЛО</field><field name="Значение">')
        f.write(value)
        f.write('</field><field name="Конец">КОНЕЦ</field></record></report>\n')
    pdf = scratch / "huge-report.pdf"
    r, rss, wall = timed([binary, "--font", BUNDLED, "--profile", "report-v1", xml, pdf])
    small = scratch / "small-report.xml"
    small.write_text('<report version="1" title="x"><record><field name="a">b</field></record></report>\n')
    _, rss0, _ = timed([binary, "--font", BUNDLED, "--profile", "report-v1", small, scratch / "small-report.pdf"])
    info = subprocess.run(["pdfinfo", str(pdf)], capture_output=True, text=True).stdout
    pages = int(re.search(r"Pages:\s+(\d+)", info).group(1))
    first = subprocess.run(["pdftotext", "-l", "1", str(pdf), "-"], capture_output=True, text=True).stdout
    last = subprocess.run(["pdftotext", "-f", str(pages), "-l", str(pages), str(pdf), "-"],
                          capture_output=True, text=True).stdout
    # The same value as one uncapped fixed-column cell: lines and pages.
    col_pt = 130
    lim = m.limit(col_pt)
    wu = m.width(word.strip())
    per_line = max(1, (lim + m.space) // (wu + m.space))
    words = value.count(" ") + 1
    cell_lines = -(-words // per_line)
    ev["hugecell"] = {
        "value_bytes": len(value.encode()), "xml_bytes": xml.stat().st_size, "exit": r.returncode,
        "report_v1_rss_kib": rss, "small_report_rss_kib": rss0, "wall_s": wall, "pages": pages,
        "pdf_bytes": pdf.stat().st_size, "first_page_has_start": "НАЧАЛО" in first,
        "last_page_has_end": "КОНЕЦ" in last,
        "uncapped_table_model": {"column_pt": col_pt, "words_per_line": per_line, "cell_lines": cell_lines,
                                 "pages_at_48_body_lines": -(-cell_lines // (PAGE_LINES - 2)),
                                 "row_buffer_bytes": len(value.encode())},
    }
    ok = r.returncode == 0 and ev["hugecell"]["first_page_has_start"] and ev["hugecell"]["last_page_has_end"]
    xml.unlink()
    pdf.unlink()
    return ok


def run_late(m, binary, scratch, ev):
    out = {}
    dest_dir = scratch / "dest"
    dest_dir.mkdir(exist_ok=True)
    dest = dest_dir / "out.pdf"
    old = b"%PDF previous destination\n"
    ok = True
    # Page quota at the end of a table that is otherwise valid.
    xml = scratch / "late.xml"
    with open(xml, "w", encoding="utf-8") as f:
        t = big_table_xml(f.write, 9000, m)
    dest.write_bytes(old)
    r = subprocess.run([str(binary), "--font", str(BUNDLED), "--max-pages", str(t.pages - 1),
                        "--temp-dir", str(scratch), str(xml), str(dest)], capture_output=True)
    out["page quota"] = {"pages_needed": t.pages, "max_pages": t.pages - 1, "exit": r.returncode,
                         "stderr": r.stderr.decode().strip(), "destination_kept": dest.read_bytes() == old,
                         "dir_after": sorted(p.name for p in dest_dir.iterdir())}
    ok = ok and r.returncode == 4 and out["page quota"]["destination_kept"] and out["page quota"]["dir_after"] == ["out.pdf"]
    # A cap error in the pre-layout at row 8999: the pipe ends mid-element.
    dest.write_bytes(old)
    p = subprocess.Popen([str(binary), "--font", str(BUNDLED), "-", str(dest)], stdin=subprocess.PIPE,
                         stderr=subprocess.PIPE)
    err = None
    try:
        big_table_xml(lambda s: p.stdin.write(s.encode()), 9000, m, error_at=8999)
    except TableError as e:
        err = e.record()
    p.stdin.close()
    stderr = p.stderr.read().decode().strip()
    rc = p.wait()
    out["late cap error, file destination"] = {"prelayout_error": err, "converter_exit": rc, "stderr": stderr,
                                               "destination_kept": dest.read_bytes() == old,
                                               "dir_after": sorted(x.name for x in dest_dir.iterdir())}
    ok = ok and err is not None and rc == 1 and dest.read_bytes() == old
    # Same on stdout: bytes already written stay.
    p = subprocess.Popen([str(binary), "--font", str(BUNDLED), "-", "-"], stdin=subprocess.PIPE,
                         stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    import threading
    got = []
    th = threading.Thread(target=lambda: got.append(p.stdout.read()))
    th.start()
    try:
        big_table_xml(lambda s: p.stdin.write(s.encode()), 9000, m, error_at=8999)
    except TableError:
        pass
    p.stdin.close()
    rc = p.wait()
    th.join()
    out["late cap error, stdout"] = {"converter_exit": rc, "bytes_left_on_stdout": len(got[0])}
    ok = ok and rc == 1 and len(got[0]) > 0
    xml.unlink()
    ev["late"] = out
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--binary", default=str(ROOT / "xml2pdf"))
    ap.add_argument("--scratch", required=True)
    ap.add_argument("--json")
    a = ap.parse_args()
    scratch = Path(a.scratch)
    scratch.mkdir(parents=True, exist_ok=True)
    m = Metrics(Ttf(BUNDLED))
    ev = {"caps": None, "binary_sha256": sha(Path(a.binary).read_bytes()), "font_sha256": sha(BUNDLED.read_bytes()),
          "cap_values": CAPS, "gutter_pt": GUTTER_PT, "space_pt": m.pt(m.space)}
    results = {}
    for name, fn in [("caps", lambda: run_caps(m, ev)), ("firstn", lambda: run_first_n(m, ev)),
                     ("fixture", lambda: run_fixture(m, a.binary, scratch, ev)),
                     ("stream", lambda: run_stream(m, a.binary, scratch, ev)),
                     ("hugecell", lambda: run_hugecell(m, a.binary, scratch, ev)),
                     ("late", lambda: run_late(m, a.binary, scratch, ev))]:
        results[name] = bool(fn())
        print(f"{name}: {'ok' if results[name] else 'FAIL'}", flush=True)
    ev["results"] = results
    if a.json:
        Path(a.json).write_text(json.dumps(ev, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
    return 0 if all(results.values()) else 1


if __name__ == "__main__":
    sys.exit(main())
