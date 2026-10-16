#!/usr/bin/env python3
"""Bounded mutation smoke with publication checks and independent PDF reading."""

import hashlib
import os
import random
import re
import shutil
import subprocess
import sys
import tempfile
import time
import xml.etree.ElementTree as ET
from pathlib import Path

SEED = 20261007
ITERS = 24
LIMIT = 4096
TIMEOUT = 5
AS_KIB = 262144


def run_bounded(binary: Path, args: list[str]) -> subprocess.CompletedProcess[bytes]:
    cmd = ["bash", "-c", f"ulimit -v {AS_KIB} && exec \"$0\" \"$@\"", str(binary), *args]
    env = os.environ.copy()
    env.pop("KI_XML_PDF_FONT", None)
    return subprocess.run(cmd, timeout=TIMEOUT, capture_output=True, env=env)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def normalized(text: str) -> str:
    return re.sub(r"[ \t\r\n]+", " ", text).strip()


def check_report_projection(data: bytes, lines: list[str]) -> bool:
    # ElementTree only projects a successfully converted, small report. It is
    # never an acceptance oracle for the library's intentionally limited XML.
    try:
        report = ET.fromstring(data)
    except (ET.ParseError, LookupError, UnicodeError):
        return False
    if report.tag != "report" or report.attrib.get("version") != "1":
        return False
    records = report.findall("record")
    require(lines[-3:] == [f"Всего записей: {len(records)}", f"Выбрано записей: {len(records)}",
                          "Пропущено записей: 0"], "successful mutant has incorrect report counters")
    text = normalized("\n".join(lines))
    # Check short source payloads without reproducing schema validation or layout.
    title = normalized(report.attrib.get("title", ""))
    if title and len(title) <= 40:
        require(title in text, "successful mutant lost its report title")
    for record in records:
        record_id = normalized(record.attrib.get("id", ""))
        if record_id and len(record_id) <= 20:
            require(f"Запись {record_id}" in text, "successful mutant lost a record id")
        for field in record.findall("field"):
            value = normalized("".join(field.itertext()))
            if value and len(value) <= 40:
                require(value in text, "successful mutant lost a short field value")
    return True


def main() -> int:
    started = time.monotonic()
    root = Path(__file__).resolve().parents[1]
    binary = root / "xml2pdf"
    if not binary.is_file():
        print("missing xml2pdf", file=sys.stderr)
        return 2
    rng = random.Random(SEED)
    crash_dir = root / "fuzz-crash"
    base = (root / "examples" / "report-v1.xml").read_bytes()
    golden = (root / "examples" / "report-v1.expected.txt").read_text(encoding="utf-8").splitlines()
    reader = shutil.which(os.environ.get("PDFTOTEXT", "pdftotext"))
    if reader is None:
        print("missing pdftotext", file=sys.stderr)
        return 2
    old = b"OLD output must survive failed conversion\n"
    successful_mutants = 0
    projected_mutants = 0

    def bounded(args: list[str], data: bytes, suffix: str, label: str) -> subprocess.CompletedProcess[bytes]:
        digest = hashlib.sha256(data).hexdigest()[:16]
        try:
            proc = run_bounded(binary, args)
        except subprocess.TimeoutExpired:
            crash_dir.mkdir(exist_ok=True)
            (crash_dir / f"{digest}{suffix}").write_bytes(data)
            raise AssertionError(f"timeout {label} seed={SEED} sha={digest}")
        if proc.returncode < 0:
            crash_dir.mkdir(exist_ok=True)
            (crash_dir / f"{digest}{suffix}").write_bytes(data)
            raise AssertionError(f"crash {label} signal {-proc.returncode} sha={digest}")
        require(proc.returncode in range(5), f"undocumented exit code {proc.returncode}: {label}")
        return proc

    def read_pdf(dst: Path) -> list[str]:
        data = dst.read_bytes()
        require(data.startswith(b"%PDF-") and data.rstrip().endswith(b"%%EOF"), "success produced incomplete PDF")
        proc = run_bounded(Path(reader), ["-layout", "-nopgbrk", str(dst), "-"])
        require(proc.returncode == 0, f"Poppler rejected successful PDF: {proc.stderr!r}")
        return [line.rstrip() for line in proc.stdout.decode("utf-8").splitlines() if line.strip()]

    def check_cleanup(directory: Path, spool: Path, filenames: set[str]) -> None:
        require(not list(spool.iterdir()), "conversion leaked an offset spool")
        require({path.name for path in directory.iterdir()} == filenames | {"spool"}, "conversion leaked a temporary PDF")

    def check_xml(data: bytes, label: str, status: int | None = None,
                  expected: list[str] | None = None) -> tuple[bool, bool]:
        with tempfile.TemporaryDirectory(prefix="ki-fuzz.") as tmp:
            directory = Path(tmp)
            src, dst, spool = directory / "in.xml", directory / "out.pdf", directory / "spool"
            src.write_bytes(data)
            dst.write_bytes(old)
            spool.mkdir()
            proc = bounded(["--profile", "report-v1", "--temp-dir", str(spool), str(src), str(dst)],
                           data, ".xml", label)
            if status is not None:
                require(proc.returncode == status, f"{label}: got {proc.returncode}, expected {status}; {proc.stderr!r}")
            if proc.returncode == 0:
                lines = read_pdf(dst)
                if expected is not None:
                    require(lines == expected, "golden report PDF differs from fixed full-text oracle")
                projected = check_report_projection(data, lines)
            else:
                require(dst.read_bytes() == old, f"{label}: failure replaced OLD output")
                projected = False
            check_cleanup(directory, spool, {src.name, dst.name})
            return proc.returncode == 0, projected

    try:
        check_xml(base, "golden report", status=0, expected=golden)
        for i in range(ITERS):
            blob = bytearray(base)
            for _ in range(rng.randrange(1, 8)):
                at = rng.randrange(len(blob))
                blob[at] = rng.randrange(256)
            data = bytes(blob[:LIMIT])
            succeeded, projected = check_xml(data, f"mutation {i}")
            successful_mutants += int(succeeded)
            projected_mutants += int(projected)

        record = b'<record id="1"><field name="n">pad</field></record>'
        wide = b'<report version="1" title="wide">' + record * 200 + b"</report>"
        require(len(wide) > 4097, "stream fixture is shorter than the parser window")
        cuts = (0, 1, 4095, 4096, 4097, len(wide) - 1)
        for cut in cuts:
            check_xml(wide[:cut], f"truncation {cut}", status=1)

        font = (root / "fonts" / "DejaVuSans.ttf").read_bytes()[:LIMIT]
        broken = bytearray(font)
        for _ in range(8):
            broken[rng.randrange(len(broken))] = rng.randrange(256)
        samples = [font[:206], font[:1024], font, bytes(broken)]
        samples.append((root / "tests" / "corpus" / "truncated-hmtx.ttf").read_bytes()[:LIMIT])
        samples.append((root / "tests" / "corpus" / "truncated-cmap-184.ttf").read_bytes()[:LIMIT])
        hello = root / "examples" / "hello.xml"
        for number, data in enumerate(samples):
            with tempfile.TemporaryDirectory(prefix="ki-fuzz.") as tmp:
                directory = Path(tmp)
                face, dst, spool = directory / "face.ttf", directory / "out.pdf", directory / "spool"
                face.write_bytes(data)
                dst.write_bytes(old)
                spool.mkdir()
                proc = bounded(["--font", str(face), "--temp-dir", str(spool), str(hello), str(dst)],
                               data, ".ttf", f"font {number}")
                require(proc.returncode == 1, f"truncated font {number}: got {proc.returncode}, expected 1")
                require(dst.read_bytes() == old, "font failure replaced OLD output")
                check_cleanup(directory, spool, {face.name, dst.name})
    except (AssertionError, subprocess.TimeoutExpired, UnicodeError) as exc:
        print(f"fuzz_smoke failed: {exc}", file=sys.stderr)
        return 1

    print(f"fuzz_smoke seed={SEED} iters={ITERS} as_kib={AS_KIB} timeout={TIMEOUT}s "
          f"golden=1 truncations={len(cuts)} fonts={len(samples)} "
          f"successful_mutants={successful_mutants} projected_mutants={projected_mutants} "
          f"elapsed={time.monotonic() - started:.2f}s ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
