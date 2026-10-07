#!/usr/bin/env python3
"""Check a bounded report-v1 selection against budgets and an independent oracle.

Converter RSS is /usr/bin/time on xml2pdf only. Poppler runs afterwards under
its own 1 GiB address-space limit. --self-test checks the gates without a
large file. Inputs are removed on success and on failure.
"""

import re
import subprocess
import sys
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
RSS_MAX_KIB = 65536
PAGES_MAX = 10000
PDF_MAX = 256 * 1024 * 1024
SPOOL_MAX = 64 * 1024 * 1024
READER_AS_KIB = 1048576


def expected_lines(total: int) -> list[str]:
    lines = ["large", "Запись 1", "n: FIRST_RECORD"]
    for _ in range(998):
        lines.append("Запись m")
        lines.append("n: mid")
    lines.append("Запись 1000")
    lines.append("n: LAST_SELECTED")
    skipped = total - 1000
    lines.append(f"Всего записей: {total}")
    lines.append("Выбрано записей: 1000")
    lines.append(f"Пропущено записей: {skipped}")
    return lines


def logical_lines(text: str) -> list[str]:
    return [ln.rstrip() for ln in text.replace("\f", "\n").splitlines() if ln.strip()]


def judge(rss_kib, pages, pdf_bytes, spool, lines, total) -> str | None:
    if rss_kib is None or pages is None or pdf_bytes is None or spool is None or lines is None:
        return "missing metric"
    if rss_kib > RSS_MAX_KIB:
        return f"rss {rss_kib} KiB over {RSS_MAX_KIB}"
    if pages > PAGES_MAX:
        return f"pages {pages} over {PAGES_MAX}"
    if pdf_bytes > PDF_MAX:
        return f"pdf {pdf_bytes} over {PDF_MAX}"
    if spool > SPOOL_MAX:
        return f"spool {spool} over {SPOOL_MAX}"
    want = expected_lines(total)
    if lines != want:
        return "selected text does not match the generator oracle"
    return None


def self_test() -> int:
    good = expected_lines(1000)
    cases = [
        ("over-rss", judge(106912, 41, 1000, 720, good, 1000)),
        ("over-pages", judge(2000, 10001, 1000, 720, good, 1000)),
        ("over-pdf", judge(2000, 41, PDF_MAX + 1, 720, good, 1000)),
        ("over-spool", judge(2000, 41, 1000, SPOOL_MAX + 1, good, 1000)),
        ("missing-metric", judge(None, 41, 1000, 720, good, 1000)),
        ("changed-value", judge(2000, 41, 1000, 720, ["large", "Запись 1", "n: MISSING_VALUE"], 1000)),
        ("ok", judge(2416, 41, 1009241, 720, good, 1000)),
    ]
    failed = False
    for name, err in cases:
        if name == "ok":
            if err is not None:
                print(f"self-test {name} failed: {err}", file=sys.stderr)
                failed = True
        elif err is None:
            print(f"self-test {name} was accepted", file=sys.stderr)
            failed = True
    if failed:
        return 1
    print("large_proof self-test ok")
    return 0


def read_page(pdf: Path, page: int) -> str:
    proc = subprocess.run(
        ["bash", "-c", "ulimit -v \"$1\" && exec pdftotext -layout -f \"$2\" -l \"$2\" \"$3\" -",
         "pdftotext", str(READER_AS_KIB), str(page), str(pdf)],
        capture_output=True, text=True,
    )
    if proc.returncode != 0:
        raise RuntimeError(proc.stderr.strip() or f"reader rc={proc.returncode}")
    return proc.stdout


def extra_cases(binary: Path) -> None:
    work = Path(tempfile.mkdtemp(prefix="ki-large-extra."))
    try:
        src = work / "in.xml"
        pdf = work / "out.pdf"
        src.write_text(
            '<report version="1" title="off">'
            '<record id="1"><field name="n">UNIQUE1</field></record>'
            '<record id="2"><field name="n">UNIQUE2</field></record>'
            '<record id="3"><field name="n">UNIQUE3</field></record>'
            "</report>",
            encoding="utf-8",
        )
        proc = subprocess.run(
            [str(binary), "--profile", "report-v1", "--record-offset", "1", "--record-limit", "1", str(src), str(pdf)],
            capture_output=True, text=True,
        )
        if proc.returncode != 0:
            raise RuntimeError(proc.stderr)
        text = subprocess.run(["pdftotext", "-layout", str(pdf), "-"], capture_output=True, text=True, check=True).stdout
        if "UNIQUE2" not in text or "UNIQUE1" in text or "UNIQUE3" in text:
            raise RuntimeError("offset selection did not keep only the second record")
        pdf.unlink()
        kept = work / "kept.pdf"
        kept.write_bytes(b"PREVIOUS")
        src.write_text(
            '<report version="1" title="late">'
            '<record id="1"><field name="n">ok</field></record>'
            "<nope/>"
            "</report>",
            encoding="utf-8",
        )
        proc = subprocess.run(
            [str(binary), "--profile", "report-v1", "--record-limit", "1", str(src), str(kept)],
            capture_output=True, text=True,
        )
        if proc.returncode == 0 or kept.read_bytes() != b"PREVIOUS":
            raise RuntimeError("late invalid record replaced the previous PDF")
    finally:
        for child in work.iterdir():
            child.unlink()
        work.rmdir()


def main() -> int:
    if len(sys.argv) > 1 and sys.argv[1] == "--self-test":
        return self_test()
    target = int(sys.argv[1]) if len(sys.argv) > 1 else 134217728
    binary = root / "xml2pdf"
    commit = subprocess.run(["git", "-C", str(root), "rev-parse", "HEAD"], capture_output=True, text=True).stdout.strip()
    print(f"commit={commit}")
    record = b'<record id="pad"><field name="n">12345</field></record>'
    chunk = record * 4096
    head = b'<report version="1" title="large">'
    first = b'<record id="1"><field name="n">FIRST_RECORD</field></record>'
    mid = b'<record id="m"><field name="n">mid</field></record>'
    last_sel = b'<record id="1000"><field name="n">LAST_SELECTED</field></record>'
    tail = b"</report>"
    work = Path(tempfile.mkdtemp(prefix="ki-large."))
    path = work / "in.xml"
    pdf = work / "out.pdf"
    try:
        extra_cases(binary)
        nrec = 0
        with path.open("wb") as out:
            out.write(head)
            out.write(first)
            nrec += 1
            for _ in range(998):
                out.write(mid)
                nrec += 1
            out.write(last_sel)
            nrec += 1
            while out.tell() + len(chunk) + len(tail) < target:
                out.write(chunk)
                nrec += 4096
            while out.tell() + len(record) + len(tail) < target:
                out.write(record)
                nrec += 1
            out.write(tail)
        cmd = [
            "/usr/bin/time", "-f", "rss_kib=%M elapsed=%e",
            str(binary), "--profile", "report-v1", "--record-limit", "1000",
            "--max-pages", str(PAGES_MAX),
            "--max-output-bytes", str(PDF_MAX),
            "--max-spool-bytes", str(SPOOL_MAX),
            str(path), str(pdf),
        ]
        proc = subprocess.run(cmd, stderr=subprocess.PIPE, text=True)
        print(proc.stderr, end="")
        print(f"xml_bytes={path.stat().st_size}")
        if proc.returncode != 0:
            return proc.returncode
        rss_m = re.search(r"rss_kib=(\d+)", proc.stderr)
        spool_m = re.search(r"offset_bytes=(\d+)", proc.stderr)
        info = subprocess.run(["pdfinfo", str(pdf)], capture_output=True, text=True)
        pages = None
        if info.returncode == 0:
            for line in info.stdout.splitlines():
                if line.startswith("Pages:"):
                    pages = int(line.split(":", 1)[1])
        pdf_bytes = pdf.stat().st_size if pdf.is_file() else None
        rss = int(rss_m.group(1)) if rss_m else None
        spool = int(spool_m.group(1)) if spool_m else None
        full = subprocess.run(
            ["bash", "-c", "ulimit -v \"$1\" && exec pdftotext -layout -nopgbrk \"$2\" -",
             "pdftotext", str(READER_AS_KIB), str(pdf)],
            capture_output=True, text=True,
        )
        lines = logical_lines(full.stdout) if full.returncode == 0 else None
        err = judge(rss, pages, pdf_bytes, spool, lines, nrec)
        print(f"pdf_bytes={pdf_bytes} pdf_pages={pages} records_total={nrec} rss_kib={rss} spool={spool}")
        if pages is not None and pages >= 1:
            for page, needle in ((1, "FIRST_RECORD"), (pages, "LAST_SELECTED")):
                page_text = read_page(pdf, page)
                if needle not in page_text:
                    err = err or f"reader page {page} missing {needle}"
                else:
                    print(f"reader_page={page} ok")
            if pages >= 3:
                mid_page = (pages + 1) // 2
                read_page(pdf, mid_page)
                print(f"reader_page={mid_page} ok")
        if err:
            print(err, file=sys.stderr)
            return 1
        print("large_proof ok")
        return 0
    finally:
        for child in work.iterdir():
            child.unlink(missing_ok=True)
        work.rmdir()


if __name__ == "__main__":
    sys.exit(main())
