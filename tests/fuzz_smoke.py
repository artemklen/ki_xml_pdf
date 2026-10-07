#!/usr/bin/env python3
"""Bounded crash smoke. Seeds are fixed. A signal writes the input next to this script."""

import hashlib
import random
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from pathlib import Path

SEED = 20261007
ITERS = 24
LIMIT = 4096
TIMEOUT = 5
AS_KIB = 262144


def oracle_wellformed(blob: bytes) -> bool:
    try:
        ET.fromstring(blob)
        return True
    except (ET.ParseError, LookupError, UnicodeError):
        return False


def run_bounded(binary: Path, args: list[str]) -> subprocess.CompletedProcess[bytes]:
    cmd = ["bash", "-c", f"ulimit -v {AS_KIB} && exec \"$0\" \"$@\"", str(binary), *args]
    return subprocess.run(cmd, timeout=TIMEOUT, capture_output=True)


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    binary = root / "xml2pdf"
    if not binary.is_file():
        print("missing xml2pdf", file=sys.stderr)
        return 2
    rng = random.Random(SEED)
    crash_dir = root / "fuzz-crash"
    base = (root / "examples" / "report-v1.xml").read_bytes()
    if not oracle_wellformed(base):
        print("oracle rejected examples/report-v1.xml", file=sys.stderr)
        return 1
    with tempfile.TemporaryDirectory(prefix="ki-fuzz.") as tmp:
        dst = Path(tmp) / "out.pdf"
        proc = run_bounded(binary, ["--profile", "report-v1", str(root / "examples" / "report-v1.xml"), str(dst)])
        if proc.returncode != 0:
            print(f"golden report-v1 returned {proc.returncode}", file=sys.stderr)
            return 1
    for i in range(ITERS):
        blob = bytearray(base)
        for _ in range(rng.randrange(1, 8)):
            at = rng.randrange(len(blob))
            blob[at] = rng.randrange(256)
        if len(blob) > LIMIT:
            del blob[LIMIT:]
        data = bytes(blob)
        digest = hashlib.sha256(data).hexdigest()[:16]
        with tempfile.TemporaryDirectory(prefix="ki-fuzz.") as tmp:
            src = Path(tmp) / "in.xml"
            dst = Path(tmp) / "out.pdf"
            src.write_bytes(data)
            try:
                proc = run_bounded(binary, ["--profile", "report-v1", str(src), str(dst)])
            except subprocess.TimeoutExpired:
                print(f"timeout seed={SEED} iter={i} sha={digest}", file=sys.stderr)
                return 1
            if proc.returncode < 0:
                crash_dir.mkdir(exist_ok=True)
                (crash_dir / f"{digest}.xml").write_bytes(data)
                print(f"crash signal {-proc.returncode} sha={digest}", file=sys.stderr)
                return 1
            if oracle_wellformed(data) and data == base and proc.returncode != 0:
                print(f"oracle accepted the unchanged sample, converter returned {proc.returncode}", file=sys.stderr)
                return 1
    record = b'<record id="1"><field name="n">pad</field></record>'
    wide = b'<report version="1" title="wide">' + record * 200 + b"</report>"
    if len(wide) <= 4097:
        print("stream fixture is shorter than the parser window", file=sys.stderr)
        return 1
    for cut in (0, 1, 4095, 4096, 4097, len(wide) - 1):
        data = wide[:cut]
        digest = hashlib.sha256(data).hexdigest()[:16]
        with tempfile.TemporaryDirectory(prefix="ki-fuzz.") as tmp:
            src = Path(tmp) / "in.xml"
            dst = Path(tmp) / "out.pdf"
            src.write_bytes(data)
            try:
                proc = run_bounded(binary, ["--profile", "report-v1", str(src), str(dst)])
            except subprocess.TimeoutExpired:
                print(f"stream timeout cut={cut}", file=sys.stderr)
                return 1
            if proc.returncode < 0:
                crash_dir.mkdir(exist_ok=True)
                (crash_dir / f"{digest}.xml").write_bytes(data)
                print(f"stream crash cut={cut} signal {-proc.returncode}", file=sys.stderr)
                return 1
    font = (root / "fonts" / "DejaVuSans.ttf").read_bytes()[:LIMIT]
    broken = bytearray(font)
    for _ in range(8):
        broken[rng.randrange(len(broken))] = rng.randrange(256)
    samples = [font[:206], font[:1024], bytes(font), bytes(broken)]
    corpus = root / "tests" / "corpus" / "truncated-hmtx.ttf"
    if corpus.is_file():
        samples.append(corpus.read_bytes()[:LIMIT])
    hello = root / "examples" / "hello.xml"
    for sample in samples:
        data = bytes(sample)
        digest = hashlib.sha256(data).hexdigest()[:16]
        with tempfile.TemporaryDirectory(prefix="ki-fuzz.") as tmp:
            face = Path(tmp) / "face.ttf"
            dst = Path(tmp) / "out.pdf"
            face.write_bytes(data)
            try:
                proc = run_bounded(binary, ["--font", str(face), str(hello), str(dst)])
            except subprocess.TimeoutExpired:
                print(f"font timeout sha={digest}", file=sys.stderr)
                return 1
            if proc.returncode < 0:
                crash_dir.mkdir(exist_ok=True)
                (crash_dir / f"{digest}.ttf").write_bytes(data)
                print(f"font crash signal {-proc.returncode} sha={digest}", file=sys.stderr)
                return 1
    print(f"fuzz_smoke seed={SEED} iters={ITERS} as_kib={AS_KIB} ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
