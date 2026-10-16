#!/usr/bin/env python3
"""Combined CLI matrix: geometry, wrapping, a non-default TTF with
zero-advance marks, Flate streams and /Info together, through all four
file/stdin -> file/stdout routes and both built-in profiles.

Expected words, counters, page boxes and metadata are computed here from
the inputs and the documented rules, not read from the converter. Poppler
(pdftotext, pdfinfo, pdftoppm) reads the PDFs; pdf_streams.py walks the
xref and inflates every stream. Every route of one case must give the same
bytes, stdout must carry only the PDF, and diagnostics go to stderr. The
combined failure cases must keep an existing destination and create none.

    python3 tests/integration_check.py [--binary xml2pdf]
"""

import argparse
import hashlib
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from font_metrics_check import BUNDLED, ROOT, SYSTEM_BOLD, require  # noqa: E402
from geometry_check import NAMED, PX, boxes, ink_box  # noqa: E402
from info_check import info_dict, pdfinfo, field, xref  # noqa: E402
from pdf_streams import streams, xref_offsets  # noqa: E402

MARKS = "e\u0301t\u00e9 \u0430\u0306 "
WRAP_TEXT = ("ALPHA wrap jumpy Wgyq " + MARKS + "\u0416\u0429 quick brown fox ") * 120
SYNTAX_TITLE = "Integration (a) \\ b ) /JS (x) >> endobj"


def records_xml():
    return f"<doc><p>{WRAP_TEXT}</p><note kind=\"k\">tail END</note></doc>"


def records_words():
    # records-v1 prints element names, each attribute as "name: value", then text.
    return ["doc", "p", *WRAP_TEXT.split(), "note", "kind:", "k", "tail", "END"]


REPORT_RECORDS = [(f"r{i}", [("Name", f"Record {i} value"),
                             ("Note", f"BETA {i} & more words that wrap across the narrow custom page")])
                  for i in range(9)]
OFFSET, LIMIT = 2, 6


def report_xml():
    out = ['<report version="1" title="BETA Report">']
    for rid, fields in REPORT_RECORDS:
        out.append(f'<record id="{rid}">')
        for name, value in fields:
            out.append(f'<field name="{name}">{value.replace("&", "&amp;")}</field>')
        out.append("</record>")
    out.append("</report>")
    return "".join(out)


def report_words():
    words = ["BETA", "Report"]
    for rid, fields in REPORT_RECORDS[OFFSET:OFFSET + LIMIT]:
        words += ["Запись", rid]
        for name, value in fields:
            words += [f"{name}:", *value.split()]
    total, sel = len(REPORT_RECORDS), len(REPORT_RECORDS[OFFSET:OFFSET + LIMIT])
    words += ["Всего", "записей:", str(total), "Выбрано", "записей:", str(sel),
              "Пропущено", "записей:", str(total - sel)]
    return words


def cases(font):
    return [
        {
            "name": "records-a4-landscape-font-info",
            "args": ["--font", font, "--page-size", "a4", "--orientation", "landscape", "--margin", "36",
                     "--font-size", "12", "--title", SYNTAX_TITLE, "--creation-date", "2026-10-16T12:00:00+03:00"],
            "xml": records_xml(), "words": records_words(), "font": font,
            "page": (NAMED["a4"][1], NAMED["a4"][0]), "margins": (36, 36, 36, 36), "min_pages": 2,
            "info": {"Title": SYNTAX_TITLE, "CreationDate": "D:20261016120000+03'00'"},
        },
        {
            "name": "report-custom-page-producer",
            "args": ["--font", font, "--profile", "report-v1", "--record-offset", str(OFFSET), "--record-limit",
                     str(LIMIT), "--page-width", "400", "--page-height", "300", "--margin", "10,30,50,70",
                     "--font-size", "10.5", "--producer", "ki_xml_pdf integration"],
            "xml": report_xml(), "words": report_words(), "font": font,
            "page": (400, 300), "margins": (10, 30, 50, 70), "min_pages": 2,
            "info": {"Producer": "ki_xml_pdf integration"},
        },
        {
            "name": "records-legacy-defaults",
            "args": ["--font", str(BUNDLED)],
            "xml": records_xml(), "words": records_words(), "font": str(BUNDLED),
            "page": NAMED["letter"], "margins": (40, 40, 40, 40), "min_pages": 2, "info": None,
        },
    ]


def run(binary, args, src, dest, data, cwd):
    return subprocess.run([str(binary), *args, src, dest], input=data, capture_output=True, cwd=cwd,
                          timeout=120)


def check_pdf(case, pdf):
    data = pdf.read_bytes()
    require(data.startswith(b"%PDF-") and data.endswith(b"%%EOF\n"), f"{case['name']}: not a complete PDF")
    xref_offsets(data)
    rows = list(streams(data))
    media = boxes(pdf)
    pages = len(media)
    require(pages >= case["min_pages"], f"{case['name']}: {pages} pages, expected wrapping over more")
    w, h = case["page"]
    for box in media:
        require(all(abs(a - b) < 0.006 for a, b in zip(box, (0, 0, w, h))), f"{case['name']}: MediaBox {box}")
    contents = [r for r in rows if r[3].startswith(b"BT\n")]
    require(len(contents) == pages and all(b"/FlateDecode" in r[1] for r in contents),
            f"{case['name']}: page contents are not one Flate stream per page")
    fonts = [r for r in rows if b"/Length1 " in r[1]]
    font_bytes = Path(case["font"]).read_bytes()
    require(len(fonts) == 1 and b"/FlateDecode" in fonts[0][1] and
            hashlib.sha256(fonts[0][3]).digest() == hashlib.sha256(font_bytes).digest(),
            f"{case['name']}: embedded font differs from {case['font']}")
    out = subprocess.run(["pdftotext", "-enc", "UTF-8", str(pdf), "-"], capture_output=True, check=True,
                         timeout=120).stdout.decode("utf-8")
    require(out.split() == case["words"], f"{case['name']}: extracted words differ from the input oracle")
    top, right, bottom, left = case["margins"]
    for page in (1, pages):
        x0, x1, y0, y1 = ink_box(pdf, page)
        require(x0 >= left - PX and x1 <= w - right + PX and y0 >= top - PX and y1 <= h - bottom + PX,
                f"{case['name']} p{page}: ink {x0:.1f}..{x1:.1f} x {y0:.1f}..{y1:.1f} outside the margins")
    _, offs, trailer = xref(data)
    got = info_dict(data, offs, trailer)
    require(got == case["info"], f"{case['name']}: Info {got!r} != {case['info']!r}")
    info = pdfinfo(pdf)
    if case["info"] and "Title" in case["info"]:
        require(field(info, "Title") == case["info"]["Title"], f"{case['name']}: pdfinfo Title")
    if not case["info"] or "CreationDate" not in case["info"]:
        require(b"/CreationDate" not in data, f"{case['name']}: implicit CreationDate")
    return pages


def check_routes(binary, tmp, case):
    xml = case["xml"].encode()
    src_file = tmp / f"{case['name']}.xml"
    src_file.write_bytes(xml)
    outputs = []
    for stdin in (False, True):
        for stdout in (False, True):
            label = f"{case['name']} {'stdin' if stdin else 'file'}->{'stdout' if stdout else 'file'}"
            dest = tmp / f"{case['name']}-{int(stdin)}{int(stdout)}.pdf"
            r = run(binary, case["args"], "-" if stdin else str(src_file), "-" if stdout else str(dest),
                    xml if stdin else None, tmp)
            require(r.returncode == 0, f"{label}: exit {r.returncode}: {r.stderr.decode(errors='replace')}")
            if stdout:
                require(r.stdout.startswith(b"%PDF-") and r.stdout.endswith(b"%%EOF\n"),
                        f"{label}: stdout is not exclusively one PDF")
                dest.write_bytes(r.stdout)
            else:
                require(r.stdout == b"", f"{label}: stdout polluted")
            require(b"pages=" in r.stderr, f"{label}: no diagnostics on stderr")
            outputs.append(dest.read_bytes())
    require(all(o == outputs[0] for o in outputs), f"{case['name']}: routes give different PDFs")
    return check_pdf(case, tmp / f"{case['name']}-00.pdf")


def check_failures(binary, tmp, case):
    """Late XML error and an output quota with the full combined options."""
    sentinel = b"previous destination\n"
    late = case["xml"].encode()[:-len(b"</doc>")] + b"</dox>"
    bad = [("late-xml", [], late, 1), ("output-quota", ["--max-output-bytes", "4096"], case["xml"].encode(), 4)]
    n = 0
    for label, extra, data, status in bad:
        for stdin in (False, True):
            spool = tmp / f"spool-{label}-{int(stdin)}"
            spool.mkdir()
            src = tmp / f"fail-{label}.xml"
            src.write_bytes(data)
            kept = tmp / f"kept-{label}-{int(stdin)}.pdf"
            kept.write_bytes(sentinel)
            fresh = tmp / f"fresh-{label}-{int(stdin)}.pdf"
            for dest in (kept, fresh):
                args = [*case["args"], *extra, "--temp-dir", str(spool)]
                r = run(binary, args, "-" if stdin else str(src), str(dest), data if stdin else None, tmp)
                require(r.returncode == status, f"{label}: exit {r.returncode}, expected {status}")
                require(r.stdout == b"" and r.stderr, f"{label}: diagnostics missing or on stdout")
                n += 1
            require(kept.read_bytes() == sentinel, f"{label}: destination changed")
            require(not fresh.exists(), f"{label}: destination created")
            require(not list(spool.iterdir()), f"{label}: spool or temporary file left")
    return n


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--binary", type=Path, default=ROOT / "xml2pdf")
    args = ap.parse_args()
    binary = args.binary.resolve()
    with tempfile.TemporaryDirectory(prefix="ki-integration.") as t:
        tmp = Path(t)
        # A font other than the bundled file, at a path of its own.
        font = tmp / "custom-font.ttf"
        shutil.copyfile(SYSTEM_BOLD if SYSTEM_BOLD.exists() else BUNDLED, font)
        total_pages = routes = 0
        all_cases = cases(str(font))
        for case in all_cases:
            total_pages += check_routes(binary, tmp, case)
            routes += 4
        failures = check_failures(binary, tmp, all_cases[0])
    print(f"integration: {len(all_cases)} combined cases over {routes} routes ({total_pages} pages), "
          f"{failures} failure runs kept or did not create the destination; font {font.name} from "
          f"{'system DejaVu Sans Bold' if SYSTEM_BOLD.exists() else 'bundled copy'}")


if __name__ == "__main__":
    main()
