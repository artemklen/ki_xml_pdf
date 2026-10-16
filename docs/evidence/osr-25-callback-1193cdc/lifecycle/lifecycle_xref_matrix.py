#!/usr/bin/env python3
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
work = ROOT / "scratch" / "lifecycle-xref"
work.mkdir(exist_ok=True)
prefix = ROOT / "scratch" / "prefix"
binary = work / "client-xref"
retry_dir = work / "retry-outputs"
retry_dir.mkdir(exist_ok=True)
sys.path.insert(0, str(ROOT / "checkout/tests"))
from pdf_offsets import check_pdf
expected_lines = [f"line {n:03d} lifecycle words" for n in range(160)]

def structural(data):
    try:
        count, xref = check_pdf(data)
        return {"ok": True, "objects": count, "startxref": xref}
    except AssertionError as e:
        return {"ok": False, "error": str(e)}

def text_lines(path):
    out = subprocess.run(["pdftotext", "-layout", str(path), "-"], capture_output=True, check=True).stdout.decode()
    return [l.strip() for l in out.splitlines() if l.strip().startswith("line ")]

env = os.environ.copy()
env.pop("KI_XML_PDF_FONT", None)
env.pop("LD_LIBRARY_PATH", None)
env["PKG_CONFIG_LIBDIR"] = str(prefix / "lib/pkgconfig")
cflags = shlex.split(subprocess.check_output(["pkg-config", "--cflags", "ki_xml_pdf"], env=env).decode())
libs = shlex.split(subprocess.check_output(["pkg-config", "--libs", "ki_xml_pdf"], env=env).decode())
build = ["cc", "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror", *cflags,
         str(ROOT / "runs/lifecycle_client_xref.c"), "-o", str(binary), *libs,
         "-Wl,-rpath," + str(prefix / "lib")]
print("$", shlex.join(build), flush=True)
subprocess.run(build, check=True, cwd=work, env=env)
good = ("<doc>" + "".join(f"<p>line {n:03d} lifecycle words</p>" for n in range(160)) + "</doc>").encode()
bad = good[:-6] + b"</wrong>"
retry = work / "retry.xml"
retry.write_bytes(good)
font = ROOT / "checkout/tests/corpus/truncated-cmap-184.ttf"
sentinel = b"OLD lifecycle destination bytes\n"
specs = [("xml", "file", 1, 1), ("font", "file", 1, 5), ("profile", "file", 1, 7),
         ("input-open", "file", 3, 3), ("input-read", "file", 3, 3),
         ("negative-read", "tofile", 3, 3), ("page-quota", "tofile", 4, 4),
         ("output-quota", "tofile", 4, 4), ("spool-quota", "tofile", 4, 4),
         ("cancel-at-eof", "tofile", 4, 6), ("negative-write", "callback", 3, 3),
         ("short-write", "callback", 3, 3), ("xml", "callback", 1, 1),
         ("invalid-option", "callback", 2, 2), ("invalid-geometry", "file_cfg", 2, 2),
         ("invalid-metadata", "tofile_cfg", 2, 2), ("invalid-metadata", "callback_cfg", 2, 2)]
rows = []
for fault, route, status, cause in specs:
    for existing in ([False] if route.startswith("callback") else [True, False]):
        name = f"{route}-{fault}-{'existing' if existing else 'absent'}"
        case = work / name
        case.mkdir(exist_ok=True)
        spool = case / "spool"
        spool.mkdir(exist_ok=True)
        source = case / "input.xml"
        source.write_bytes(bad if fault == "xml" else good)
        dest = case / "out.pdf"
        if existing:
            dest.write_bytes(sentinel)
        before = sorted(p.name for p in case.iterdir())
        argv = [binary, route, fault, source, dest, font, spool, retry]
        retry_pdf = retry_dir / f"{name}.pdf"
        env["KI_RETRY_OUT"] = str(retry_pdf)
        p = subprocess.run(list(map(str, argv)), capture_output=True, timeout=30, cwd=work, env=env)
        (case / "stdout.txt").write_bytes(p.stdout)
        (case / "stderr.txt").write_bytes(p.stderr)
        print("$", shlex.join(list(map(str, argv))), "->", p.returncode, flush=True)
        assert p.returncode == 0, p.stderr
        metrics = json.loads(p.stdout)
        status_matches = (metrics["status"], metrics["result_status"], metrics["cause"]) == (status, status, cause)
        assert metrics["input_open"] == metrics["output_open"] == metrics["caller_reusable"] == metrics["guard_ok"] == 1
        assert metrics["retry_status"] == metrics["retry_cause"] == 0 and metrics["retry_output_bytes"] > 0
        if route.startswith("file"):
            assert metrics["read_calls"] == metrics["write_calls"] == 0
        elif route.startswith("tofile"):
            assert metrics["write_calls"] == 0
        else:
            assert metrics["output_bytes"] == metrics["accepted_bytes"]
            if fault in ("negative-write", "short-write", "xml"):
                assert metrics["accepted_bytes"] > 0
        if fault.startswith("invalid-"):
            assert metrics["read_calls"] == metrics["write_calls"] == metrics["pages"] == metrics["objects"] == metrics["output_bytes"] == 0
        if existing:
            assert dest.read_bytes() == sentinel
        else:
            assert not dest.exists()
        assert not list(spool.iterdir())
        after = sorted(p.name for p in case.iterdir() if p.name not in ("stdout.txt", "stderr.txt", "out.pdf.callback.pdf"))
        assert before == after, (before, after)
        retry_bytes = retry_pdf.read_bytes()
        assert len(retry_bytes) == metrics["retry_output_bytes"], (len(retry_bytes), metrics)
        retry_xref = structural(retry_bytes)
        retry_text_ok = text_lines(retry_pdf) == expected_lines
        captured_xref = None
        captured_prefix = None
        if route.startswith("callback"):
            captured = (case / "out.pdf.callback.pdf").read_bytes()
            assert len(captured) == metrics["accepted_bytes"] == metrics["output_bytes"]
            captured_xref = structural(captured) if captured else {"ok": False, "error": "empty"}
            if fault in ("negative-write", "short-write"):
                captured_prefix = len(captured) < len(retry_bytes) and retry_bytes.startswith(captured)
        structural_ok = retry_xref["ok"] and retry_text_ok and (captured_xref is None or not captured_xref["ok"]) and captured_prefix is not False
        row = {"structuralOk": structural_ok, "retryXref": retry_xref, "retryTextLinesMatch": retry_text_ok,
               "retryPdfBytes": len(retry_bytes), "retryPdfSha256": hashlib.sha256(retry_bytes).hexdigest(),
               "capturedXref": captured_xref, "capturedIsStrictPrefixOfReference": captured_prefix,
               "case": name, "argv": list(map(str, argv)), "processExitStatus": p.returncode,
               "expectedStatus": status, "expectedCause": cause, "observed": metrics,
               "fixtureSha256": hashlib.sha256(source.read_bytes()).hexdigest(),
               "fontSha256": hashlib.sha256(font.read_bytes()).hexdigest(),
               "destinationExisted": existing, "oldDestinationSha256": hashlib.sha256(sentinel).hexdigest() if existing else None,
               "newDestinationSha256": hashlib.sha256(dest.read_bytes()).hexdigest() if existing else None,
               "inventoryBefore": before, "inventoryAfter": after, "spoolAfter": [],
               "statusMatches": status_matches, "stderr": p.stderr.decode()}
        rows.append(row)
        (ROOT / "runs/lifecycle-xref-results.json").write_text(json.dumps(rows, indent=2) + "\n")
bad_rows = [r["case"] for r in rows if not r["statusMatches"]]
bad_struct = [r["case"] for r in rows if not r["structuralOk"]]
print(f"RESULT {len(rows)} installed lifecycle checks / {len(specs)} finite failure specifications; mismatched statuses: {bad_rows}; structural failures: {bad_struct}")
print("retry PDFs passing independent xref + 160-line text oracle:", sum(1 for r in rows if r["retryXref"]["ok"] and r["retryTextLinesMatch"]))
raise SystemExit(bool(bad_rows or bad_struct))
