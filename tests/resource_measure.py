#!/usr/bin/env python3
"""Shared process measurement and provenance helpers for the OSR-23 runner.

Every measured process runs under its own `/usr/bin/time -v`, so converter,
generator, reader and oracle peaks are separate records.
"""

import hashlib
import os
import platform
import re
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
TIME = "/usr/bin/time"
MIB = 1024 * 1024
GIB = 1024 * MIB


def timed(log: Path, argv: list[str]) -> list[str]:
    return [TIME, "-v", "-o", str(log), *argv]


def parse_time(log: Path) -> dict:
    """Fields of a GNU time -v record; a missing log yields no values."""
    if not log.is_file():
        return {"rss_kib": None, "elapsed_s": None, "user_s": None, "sys_s": None, "exit_status": None}
    text = log.read_text(encoding="utf-8", errors="replace")

    def num(pattern: str):
        m = re.search(pattern, text)
        return m.group(1) if m else None

    rss = num(r"Maximum resident set size \(kbytes\):\s*(\d+)")
    wall = num(r"Elapsed \(wall clock\) time \(h:mm:ss or m:ss\):\s*([0-9:.]+)")
    elapsed = None
    if wall is not None:
        parts = [float(p) for p in wall.split(":")]
        elapsed = 0.0
        for p in parts:
            elapsed = elapsed * 60 + p
        elapsed = round(elapsed, 2)
    user = num(r"User time \(seconds\):\s*([0-9.]+)")
    sys_t = num(r"System time \(seconds\):\s*([0-9.]+)")
    status = num(r"Exit status:\s*(\d+)")
    signal = num(r"Command terminated by signal\s*(\d+)")
    return {
        "rss_kib": int(rss) if rss else None,
        "elapsed_s": elapsed,
        "user_s": float(user) if user else None,
        "sys_s": float(sys_t) if sys_t else None,
        "exit_status": int(status) if status else (128 + int(signal) if signal else None),
    }


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        while True:
            chunk = f.read(4 * MIB)
            if not chunk:
                break
            h.update(chunk)
    return h.hexdigest()


def first_line(argv: list[str]) -> str:
    try:
        p = subprocess.run(argv, capture_output=True, text=True)
    except OSError as exc:
        return f"unavailable: {exc}"
    out = (p.stdout or p.stderr).strip().splitlines()
    return out[0] if out else ""


def git(dirpath: Path, *args: str) -> str:
    p = subprocess.run(["git", "-C", str(dirpath), *args], capture_output=True, text=True)
    return p.stdout.strip() if p.returncode == 0 else ""


def disk_free(path: Path) -> int:
    return shutil.disk_usage(path).free


def fs_type(path: Path) -> str:
    p = subprocess.run(["stat", "-f", "-c", "%T", str(path)], capture_output=True, text=True)
    return p.stdout.strip()


def environment(binary: Path, font: Path, source_dir: Path, base: str) -> dict:
    """Toolchain, OS and source provenance for one evidence directory."""
    harness = HERE.parent
    mem_total = ""
    for line in Path("/proc/meminfo").read_text().splitlines():
        if line.startswith("MemTotal:"):
            mem_total = line.split(":", 1)[1].strip()
    cpu = ""
    for line in Path("/proc/cpuinfo").read_text().splitlines():
        if line.startswith("model name"):
            cpu = line.split(":", 1)[1].strip()
            break
    product_paths = ["src", "include", "Makefile", "CMakeLists.txt", "libki_xml_pdf.map", "fonts"]
    return {
        "measured_source_dir": str(source_dir),
        "measured_source_sha": git(source_dir, "rev-parse", "HEAD"),
        "measured_source_tracked_changes": git(source_dir, "status", "--porcelain", "--untracked-files=no"),
        "measured_product_diff_vs_base": git(source_dir, "diff", "--stat", base, "--", *product_paths),
        "accepted_base_sha": base,
        "binary": str(binary),
        "binary_sha256": sha256_file(binary),
        "font": str(font),
        "font_sha256": sha256_file(font),
        "font_bytes": font.stat().st_size,
        "harness_dir": str(harness),
        "harness_sha": git(harness, "rev-parse", "HEAD"),
        "harness_branch": git(harness, "rev-parse", "--abbrev-ref", "HEAD"),
        "harness_uncommitted": git(harness, "status", "--porcelain", "--untracked-files=all", "--", "tests"),
        "harness_product_diff_vs_base": git(harness, "diff", "--stat", base, "--", *product_paths),
        "harness_files_sha256": {
            name: sha256_file(HERE / name)
            for name in ("resource_recipe.py", "resource_oracle.py", "resource_measure.py",
                         "resource_baseline.py", "resource_controls.py")
            if (HERE / name).is_file()
        },
        "uname": " ".join(platform.uname()),
        "glibc": first_line(["ldd", "--version"]),
        "cc": first_line(["cc", "--version"]),
        "python": sys.version.split()[0],
        "pdfinfo": first_line(["pdfinfo", "-v"]),
        "pdftotext": first_line(["pdftotext", "-v"]),
        "time": first_line([TIME, "--version"]),
        "cpu": cpu,
        "cpus": os.cpu_count(),
        "mem_total": mem_total,
    }
