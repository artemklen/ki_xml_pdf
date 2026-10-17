#!/usr/bin/env python3
"""OSR-23 resource runner: full-output and fixed-selection series.

    resource_baseline.py matrix  --binary B --font F --source-dir D \\
        --evidence-dir OUT --scratch DIR [--cases ID,...] [--keep-artifacts]
    resource_baseline.py compare BASE_DIR OTHER_DIR [--expect-identical-output]

`matrix` runs cases one at a time. For each case the fixture generator, the
converter, the PDF reader (`pdftotext -layout`) and the streaming oracle are
separate processes with separate `/usr/bin/time -v` records. The oracle
checks every rendered record against the fixture recipe. Large XML/PDF files
are written under --scratch (use a disk-backed directory, not tmpfs) and are
deleted after measurement unless --keep-artifacts is given; their sizes and
SHA-256 stay in the evidence. Raw rows go to runs.csv and runs.json.

Full output and selection are different modes and are labeled as such; a
selection row never stands for a full-output render.
"""

import argparse
import csv
import json
import subprocess
import sys
import time
from pathlib import Path

import resource_measure as rm
import resource_oracle

HERE = Path(__file__).resolve().parent
BASE_SHA = "075455cc930a1f5e79a9ae4811e82b5c95af06c4"

RSS_MAX_KIB = 4096
SEL_PAGES_MAX = 10000
SEL_PDF_MAX = 256 * rm.MIB
SPOOL_MAX = 64 * rm.MIB
FULL_OUTPUT_QUOTA = 2 * rm.GIB
FULL_PAGES_QUOTA = 1000000
DISK_RESERVE = 4 * rm.GIB
SELECT_LIMIT = 1000

# id: (mode, profile, target bytes, record limit)
CASES = {
    "full-records-1MiB": ("full", "records-v1", 1 * rm.MIB, 0),
    "full-records-16MiB": ("full", "records-v1", 16 * rm.MIB, 0),
    "full-records-64MiB": ("full", "records-v1", 64 * rm.MIB, 0),
    "full-report-1MiB": ("full", "report-v1", 1 * rm.MIB, 0),
    "full-report-16MiB": ("full", "report-v1", 16 * rm.MIB, 0),
    "full-report-64MiB": ("full", "report-v1", 64 * rm.MIB, 0),
    "select-report-16MiB-limit1000": ("selection", "report-v1", 16 * rm.MIB, SELECT_LIMIT),
    "select-report-256MiB-limit1000": ("selection", "report-v1", 256 * rm.MIB, SELECT_LIMIT),
    "select-report-1024MiB-limit1000": ("selection", "report-v1", 1024 * rm.MIB, SELECT_LIMIT),
}
SMALL_REF = "ref-small-fixture"

COLUMNS = [
    "run_id", "mode", "profile", "target_bytes", "input_bytes", "records_total",
    "record_offset", "record_limit", "records_expected_rendered", "records_checked", "oracle_ok",
    "oracle_error", "converter_exit", "converter_rss_kib", "converter_elapsed_s",
    "converter_user_s", "converter_sys_s", "pages_pdfinfo", "page_breaks_reader", "pdf_bytes",
    "pdf_disk_bytes", "spool_offset_bytes", "objects", "generator_rss_kib",
    "generator_elapsed_s", "reader_exit", "reader_rss_kib", "reader_elapsed_s", "oracle_rss_kib",
    "oracle_elapsed_s", "fixture_sha256", "pdf_sha256", "font_sha256", "binary_sha256",
    "gates_ok", "gate_failures", "started_utc", "converter_command",
]


def converter_argv(binary: Path, font: Path, profile: str, limit: int, spool_dir: Path,
                   src: Path, pdf: Path) -> list[str]:
    argv = [str(binary), "--font", str(font), "--profile", profile]
    if limit:
        argv += ["--record-limit", str(limit), "--max-pages", str(SEL_PAGES_MAX),
                 "--max-output-bytes", str(SEL_PDF_MAX)]
    else:
        argv += ["--max-pages", str(FULL_PAGES_QUOTA), "--max-output-bytes", str(FULL_OUTPUT_QUOTA)]
    argv += ["--max-spool-bytes", str(SPOOL_MAX), "--temp-dir", str(spool_dir), str(src), str(pdf)]
    return argv


def pdfinfo_pages(pdf: Path, log: Path) -> int | None:
    p = subprocess.run(["pdfinfo", str(pdf)], capture_output=True, text=True)
    log.write_text(p.stdout + p.stderr, encoding="utf-8")
    for line in p.stdout.splitlines():
        if line.startswith("Pages:"):
            return int(line.split(":", 1)[1])
    return None


def read_and_check(pdf: Path, run_dir: Path, profile: str, total: int, limit: int) -> dict:
    """pdftotext streams into the oracle process; each has its own time log."""
    with open(run_dir / "reader.stderr", "wb") as reader_err:
        reader = subprocess.Popen(
            rm.timed(run_dir / "reader.time", ["pdftotext", "-layout", str(pdf), "-"]),
            stdout=subprocess.PIPE, stderr=reader_err)
        oracle = subprocess.run(
            rm.timed(run_dir / "oracle.time",
                     [sys.executable, str(HERE / "resource_oracle.py"), profile, str(total), "0",
                      str(limit)]),
            stdin=reader.stdout, capture_output=True, text=True)
        reader.stdout.close()
        reader.wait()
    (run_dir / "oracle.json").write_text(oracle.stdout, encoding="utf-8")
    (run_dir / "oracle.stderr").write_text(oracle.stderr, encoding="utf-8")
    try:
        verdict = json.loads(oracle.stdout)
    except json.JSONDecodeError:
        verdict = {"ok": False, "error": f"oracle produced no verdict rc={oracle.returncode}",
                   "records_checked": 0, "page_breaks": None}
    verdict["exit"] = oracle.returncode
    return verdict


def gates(row: dict, small_rss_kib: int | None) -> list[str]:
    fail = []
    rss = row["converter_rss_kib"]
    if row["converter_exit"] != 0:
        fail.append(f"converter exit {row['converter_exit']}")
    if rss is None or rss > RSS_MAX_KIB:
        fail.append(f"converter rss {rss} KiB > {RSS_MAX_KIB}")
    if not row["oracle_ok"]:
        fail.append("oracle failed")
    if row["reader_exit"] != 0:
        fail.append(f"reader exit {row['reader_exit']}")
    if row["records_checked"] != row["records_expected_rendered"]:
        fail.append("checked record count differs from expected")
    if row["pages_pdfinfo"] is None or row["pages_pdfinfo"] != row["page_breaks_reader"]:
        fail.append("pdfinfo pages differ from reader page breaks")
    spool = row["spool_offset_bytes"]
    if spool is None or spool > SPOOL_MAX:
        fail.append(f"spool {spool} > {SPOOL_MAX}")
    if row["mode"] == "selection":
        if row["pages_pdfinfo"] is None or row["pages_pdfinfo"] > SEL_PAGES_MAX:
            fail.append(f"pages {row['pages_pdfinfo']} > {SEL_PAGES_MAX}")
        if row["pdf_bytes"] is None or row["pdf_bytes"] > SEL_PDF_MAX:
            fail.append(f"pdf {row['pdf_bytes']} > {SEL_PDF_MAX}")
    if row["run_id"] == "full-records-16MiB" and rss is not None:
        rss_b = rss * 1024
        if not rss_b < row["input_bytes"] // 2:
            fail.append(f"rss {rss_b} B not < half input {row['input_bytes'] // 2}")
        if small_rss_kib is None or rss_b > small_rss_kib * 1024 + 4 * rm.MIB:
            fail.append(f"rss {rss_b} B > small reference {small_rss_kib} KiB + 4 MiB")
    return fail


def run_small_reference(args, out: Path) -> dict:
    """Converter RSS on tests/fixture.xml: the reference of tests/verify.sh."""
    run_dir = out / SMALL_REF
    run_dir.mkdir()
    pdf = Path(args.scratch) / "ref-small.pdf"
    argv = [str(args.binary), "--font", str(args.font), str(HERE / "fixture.xml"), str(pdf)]
    p = subprocess.run(rm.timed(run_dir / "converter.time", argv), capture_output=True, text=True)
    (run_dir / "converter.stderr").write_text(p.stderr, encoding="utf-8")
    t = rm.parse_time(run_dir / "converter.time")
    ref = {"run_id": SMALL_REF, "converter_exit": p.returncode, "converter_rss_kib": t["rss_kib"],
           "converter_elapsed_s": t["elapsed_s"], "input_bytes": (HERE / "fixture.xml").stat().st_size,
           "pdf_bytes": pdf.stat().st_size if pdf.exists() else None, "converter_command": " ".join(argv)}
    pdf.unlink(missing_ok=True)
    (run_dir / "run.json").write_text(json.dumps(ref, indent=2) + "\n", encoding="utf-8")
    return ref


def run_case(args, out: Path, run_id: str, small_rss_kib: int | None) -> dict:
    mode, profile, target, limit = CASES[run_id]
    scratch = Path(args.scratch)
    run_dir = out / run_id
    run_dir.mkdir()
    budget = target + (SEL_PDF_MAX if limit else FULL_OUTPUT_QUOTA) + SPOOL_MAX + DISK_RESERVE
    free = rm.disk_free(scratch)
    preflight = {"scratch": str(scratch), "fs_type": rm.fs_type(scratch), "free_bytes": free,
                 "budget_bytes": budget}
    (run_dir / "preflight.json").write_text(json.dumps(preflight, indent=2) + "\n", encoding="utf-8")
    if free < budget:
        raise SystemExit(f"{run_id}: free {free} B on {scratch} is below budget {budget} B")
    src = scratch / f"{run_id}.xml"
    pdf = scratch / f"{run_id}.pdf"
    spool_dir = scratch / "spool"
    spool_dir.mkdir(exist_ok=True)
    started = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())

    gen_argv = [sys.executable, str(HERE / "resource_recipe.py"), "--profile", profile,
                "--target-bytes", str(target), "--out", str(src)]
    g = subprocess.run(rm.timed(run_dir / "generator.time", gen_argv), capture_output=True, text=True)
    (run_dir / "generator.stderr").write_text(g.stderr, encoding="utf-8")
    if g.returncode != 0:
        raise SystemExit(f"{run_id}: generator failed: {g.stderr}")
    fixture = json.loads(g.stdout)
    fixture["command"] = " ".join(gen_argv)
    (run_dir / "fixture.json").write_text(json.dumps(fixture, indent=2) + "\n", encoding="utf-8")

    conv_argv = converter_argv(args.binary, args.font, profile, limit, spool_dir, src, pdf)
    c = subprocess.run(rm.timed(run_dir / "converter.time", conv_argv), capture_output=True, text=True)
    (run_dir / "converter.stderr").write_text(c.stderr, encoding="utf-8")
    ct = rm.parse_time(run_dir / "converter.time")
    stats = {}
    for key in ("pages", "objects", "offset_bytes"):
        for tok in c.stderr.split():
            if tok.startswith(key + "="):
                stats[key] = int(tok.split("=", 1)[1])

    total = fixture["records"]
    expected = resource_oracle.expected_records(profile, total, 0, limit)
    row = {k: None for k in COLUMNS}
    row.update({
        "run_id": run_id, "mode": mode, "profile": profile, "target_bytes": target,
        "input_bytes": fixture["input_bytes"], "records_total": total, "record_offset": 0,
        "record_limit": limit, "records_expected_rendered": expected,
        "converter_exit": c.returncode, "converter_rss_kib": ct["rss_kib"],
        "converter_elapsed_s": ct["elapsed_s"], "converter_user_s": ct["user_s"],
        "converter_sys_s": ct["sys_s"], "spool_offset_bytes": stats.get("offset_bytes"),
        "objects": stats.get("objects"), "fixture_sha256": fixture["sha256"],
        "font_sha256": args.env["font_sha256"], "binary_sha256": args.env["binary_sha256"],
        "started_utc": started, "converter_command": " ".join(conv_argv),
        "records_checked": 0, "oracle_ok": False,
    })
    gt = rm.parse_time(run_dir / "generator.time")
    row["generator_rss_kib"], row["generator_elapsed_s"] = gt["rss_kib"], gt["elapsed_s"]
    if args.keep_artifacts:
        row["kept_xml"] = str(src)
    else:
        src.unlink()

    if c.returncode == 0 and pdf.is_file():
        st = pdf.stat()
        row["pdf_bytes"], row["pdf_disk_bytes"] = st.st_size, st.st_blocks * 512
        row["pages_pdfinfo"] = pdfinfo_pages(pdf, run_dir / "pdfinfo.txt")
        verdict = read_and_check(pdf, run_dir, profile, total, limit)
        rt = rm.parse_time(run_dir / "reader.time")
        ot = rm.parse_time(run_dir / "oracle.time")
        row.update({
            "oracle_ok": bool(verdict.get("ok")), "oracle_error": verdict.get("error", ""),
            "records_checked": verdict.get("records_checked", 0),
            "page_breaks_reader": verdict.get("page_breaks"), "reader_exit": rt["exit_status"],
            "reader_rss_kib": rt["rss_kib"], "reader_elapsed_s": rt["elapsed_s"],
            "oracle_rss_kib": ot["rss_kib"], "oracle_elapsed_s": ot["elapsed_s"],
            "pdf_sha256": rm.sha256_file(pdf),
        })
        if args.keep_artifacts:
            row["kept_pdf"] = str(pdf)
        else:
            pdf.unlink()
    failures = gates(row, small_rss_kib)
    row["gates_ok"] = not failures
    row["gate_failures"] = "; ".join(failures)
    (run_dir / "run.json").write_text(json.dumps(row, indent=2, ensure_ascii=False) + "\n",
                                      encoding="utf-8")
    return row


def write_manifest(out: Path) -> None:
    lines = []
    for path in sorted(p for p in out.rglob("*") if p.is_file() and p.name != "SHA256SUMS"):
        lines.append(f"{rm.sha256_file(path)}  {path.relative_to(out)}")
    (out / "SHA256SUMS").write_text("\n".join(lines) + "\n", encoding="utf-8")


def cmd_matrix(args) -> int:
    args.binary = Path(args.binary).resolve()
    args.font = Path(args.font).resolve()
    out = Path(args.evidence_dir)
    if out.exists() and any(out.iterdir()):
        raise SystemExit(f"evidence directory {out} is not empty; use a new one")
    out.mkdir(parents=True, exist_ok=True)
    Path(args.scratch).mkdir(parents=True, exist_ok=True)
    if rm.fs_type(Path(args.scratch)) == "tmpfs" and not args.allow_tmpfs:
        raise SystemExit("scratch is on tmpfs; use a disk-backed directory")
    ids = args.cases.split(",") if args.cases else list(CASES)
    unknown = [i for i in ids if i not in CASES]
    if unknown:
        raise SystemExit(f"unknown cases {unknown}; known: {list(CASES)}")
    args.env = rm.environment(args.binary, args.font, Path(args.source_dir).resolve(), BASE_SHA)
    args.env["runner_argv"] = sys.argv
    args.env["started_utc"] = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
    args.env["limits"] = {"converter_rss_max_kib": RSS_MAX_KIB, "selection_pages_max": SEL_PAGES_MAX,
                          "selection_pdf_max_bytes": SEL_PDF_MAX, "spool_max_bytes": SPOOL_MAX,
                          "full_output_quota_bytes": FULL_OUTPUT_QUOTA,
                          "full_pages_quota": FULL_PAGES_QUOTA, "disk_reserve_bytes": DISK_RESERVE}
    (out / "environment.json").write_text(json.dumps(args.env, indent=2) + "\n", encoding="utf-8")
    ref = run_small_reference(args, out)
    rows = []
    for run_id in ids:
        print(f"[{time.strftime('%H:%M:%S')}] {run_id} ...", flush=True)
        row = run_case(args, out, run_id, ref["converter_rss_kib"])
        rows.append(row)
        print(f"  exit={row['converter_exit']} rss={row['converter_rss_kib']}KiB "
              f"in={row['input_bytes']} pdf={row['pdf_bytes']} pages={row['pages_pdfinfo']} "
              f"checked={row['records_checked']}/{row['records_expected_rendered']} "
              f"gates={'ok' if row['gates_ok'] else row['gate_failures']}", flush=True)
    with (out / "runs.csv").open("w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=COLUMNS, extrasaction="ignore")
        w.writeheader()
        w.writerows(rows)
    summary = {"small_reference": ref, "runs": rows,
               "finished_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
               "all_gates_ok": all(r["gates_ok"] for r in rows)}
    (out / "runs.json").write_text(json.dumps(summary, indent=2, ensure_ascii=False) + "\n",
                                   encoding="utf-8")
    write_manifest(out)
    print(f"evidence {out} all_gates_ok={summary['all_gates_ok']}")
    return 0 if summary["all_gates_ok"] else 1


# Values a rerun with the same binary and recipe must reproduce exactly.
IDENTICAL = ["input_bytes", "records_total", "records_checked", "fixture_sha256", "converter_exit",
             "pages_pdfinfo", "pdf_bytes", "spool_offset_bytes", "objects", "pdf_sha256", "oracle_ok"]
SETTINGS = ["mode", "profile", "target_bytes", "record_offset", "record_limit", "font_sha256"]
MEASURED = ["converter_rss_kib", "converter_elapsed_s", "generator_rss_kib", "reader_rss_kib",
            "reader_elapsed_s", "oracle_rss_kib"]
ENV_KEYS = ["measured_source_sha", "binary_sha256", "font_sha256", "harness_sha", "uname", "glibc",
            "cc", "python", "pdfinfo", "pdftotext", "cpu", "mem_total"]


def load(dirpath: Path) -> tuple[dict, dict]:
    runs = json.loads((dirpath / "runs.json").read_text(encoding="utf-8"))
    env = json.loads((dirpath / "environment.json").read_text(encoding="utf-8"))
    return {r["run_id"]: r for r in runs["runs"]}, env


def cmd_compare(args) -> int:
    base, base_env = load(Path(args.base))
    other, other_env = load(Path(args.other))
    problems = []
    report = {"base": args.base, "other": args.other, "environment_differences": {}, "runs": {}}
    for key in ENV_KEYS:
        if base_env.get(key) != other_env.get(key):
            report["environment_differences"][key] = [base_env.get(key), other_env.get(key)]
    common = [r for r in base if r in other]
    if not common:
        problems.append("no common run ids")
    for run_id in common:
        a, b = base[run_id], other[run_id]
        entry = {"settings_match": True, "different_outputs": {}, "measured": {}}
        for key in SETTINGS:
            if a.get(key) != b.get(key):
                entry["settings_match"] = False
                problems.append(f"{run_id}: setting {key} differs {a.get(key)} vs {b.get(key)}")
        if a["fixture_sha256"] != b["fixture_sha256"]:
            problems.append(f"{run_id}: fixture hash differs")
        for key in IDENTICAL:
            if a.get(key) != b.get(key):
                entry["different_outputs"][key] = [a.get(key), b.get(key)]
        for key in MEASURED:
            entry["measured"][key] = [a.get(key), b.get(key)]
        if not (a.get("oracle_ok") and b.get("oracle_ok")):
            problems.append(f"{run_id}: oracle not ok in both")
        if args.expect_identical_output and entry["different_outputs"]:
            problems.append(f"{run_id}: outputs differ {sorted(entry['different_outputs'])}")
        report["runs"][run_id] = entry
    report["only_in_base"] = sorted(set(base) - set(other))
    report["only_in_other"] = sorted(set(other) - set(base))
    report["problems"] = problems
    report["ok"] = not problems
    text = json.dumps(report, indent=2, ensure_ascii=False)
    print(text)
    if args.output:
        Path(args.output).write_text(text + "\n", encoding="utf-8")
    return 0 if not problems else 1


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description="OSR-23 resource runner")
    sub = ap.add_subparsers(dest="cmd", required=True)
    m = sub.add_parser("matrix")
    m.add_argument("--binary", required=True)
    m.add_argument("--font", required=True)
    m.add_argument("--source-dir", required=True, help="git checkout the binary was built from")
    m.add_argument("--evidence-dir", required=True)
    m.add_argument("--scratch", required=True)
    m.add_argument("--cases", default="")
    m.add_argument("--keep-artifacts", action="store_true")
    m.add_argument("--allow-tmpfs", action="store_true")
    c = sub.add_parser("compare")
    c.add_argument("base")
    c.add_argument("other")
    c.add_argument("--expect-identical-output", action="store_true")
    c.add_argument("--output", default="")
    args = ap.parse_args(argv)
    return cmd_matrix(args) if args.cmd == "matrix" else cmd_compare(args)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
