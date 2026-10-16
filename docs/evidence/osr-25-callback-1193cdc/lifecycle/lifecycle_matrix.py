#!/usr/bin/env python3
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess

ROOT = Path(__file__).resolve().parent.parent
work = ROOT / "scratch" / "lifecycle-complete"
work.mkdir(exist_ok=True)
prefix = ROOT / "scratch" / "prefix"
binary = work / "client"
env = os.environ.copy()
env.pop("KI_XML_PDF_FONT", None)
env.pop("LD_LIBRARY_PATH", None)
env["PKG_CONFIG_LIBDIR"] = str(prefix / "lib/pkgconfig")
cflags = shlex.split(subprocess.check_output(["pkg-config", "--cflags", "ki_xml_pdf"], env=env).decode())
libs = shlex.split(subprocess.check_output(["pkg-config", "--libs", "ki_xml_pdf"], env=env).decode())
build = ["cc", "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror", *cflags,
         str(ROOT / "runs/lifecycle_client.c"), "-o", str(binary), *libs,
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
        row = {"case": name, "argv": list(map(str, argv)), "processExitStatus": p.returncode,
               "expectedStatus": status, "expectedCause": cause, "observed": metrics,
               "fixtureSha256": hashlib.sha256(source.read_bytes()).hexdigest(),
               "fontSha256": hashlib.sha256(font.read_bytes()).hexdigest(),
               "destinationExisted": existing, "oldDestinationSha256": hashlib.sha256(sentinel).hexdigest() if existing else None,
               "newDestinationSha256": hashlib.sha256(dest.read_bytes()).hexdigest() if existing else None,
               "inventoryBefore": before, "inventoryAfter": after, "spoolAfter": [],
               "statusMatches": status_matches, "stderr": p.stderr.decode()}
        rows.append(row)
        (ROOT / "runs/lifecycle-results.json").write_text(json.dumps(rows, indent=2) + "\n")
bad_rows = [r["case"] for r in rows if not r["statusMatches"]]
print(f"RESULT {len(rows)} installed lifecycle checks / {len(specs)} finite failure specifications; mismatched statuses: {bad_rows}")
raise SystemExit(bool(bad_rows))
