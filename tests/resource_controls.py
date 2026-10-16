#!/usr/bin/env python3
"""OSR-23 negative and quota controls against a real xml2pdf binary.

    resource_controls.py --binary B --font F --evidence-dir OUT --scratch DIR

Each control states its expectation and records what happened. Oracle
controls mutate the reader stream or the fixture and must be rejected; tail
controls put a bad record far beyond a 1000-record selection; quota controls
use reduced page/output/spool quotas and an oversize font. Exit 0 only if
every control behaved as expected. Fixtures are small and bounded.
"""

import argparse
import json
import subprocess
import sys
from pathlib import Path

import resource_measure as rm
import resource_recipe

HERE = Path(__file__).resolve().parent
FONT_CAP = 8 * rm.MIB


class Controls:
    def __init__(self, args) -> None:
        self.binary = Path(args.binary).resolve()
        self.font = Path(args.font).resolve()
        self.out = Path(args.evidence_dir)
        self.work = Path(args.scratch)
        self.results = []

    def record(self, name: str, expectation: str, passed: bool, **observed) -> None:
        self.results.append({"control": name, "expectation": expectation, "passed": passed,
                             "observed": observed})
        print(f"{'ok  ' if passed else 'FAIL'} {name}: {observed}", flush=True)

    def fixture(self, name: str, profile: str, target: int, **kw) -> tuple[Path, dict]:
        path = self.work / f"{name}.xml"
        meta = resource_recipe.write_fixture(path, profile, target, **kw)
        return path, meta

    def convert(self, src: Path, pdf: Path, *opts: str) -> subprocess.CompletedProcess:
        argv = [str(self.binary), "--font", str(self.font), *opts, str(src), str(pdf)]
        p = subprocess.run(argv, capture_output=True, text=True)
        p.argv = argv
        return p

    def oracle(self, pdf: Path, profile: str, total: int, limit: int = 0, mutate=None) -> dict:
        """pdftotext -> optional line mutation -> oracle subprocess."""
        reader = subprocess.Popen(["pdftotext", "-layout", str(pdf), "-"], stdout=subprocess.PIPE)
        oracle = subprocess.Popen(
            [sys.executable, str(HERE / "resource_oracle.py"), profile, str(total), "0", str(limit)],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE)
        mutated = 0
        try:
            for raw in reader.stdout:
                line = raw.decode("utf-8")
                if mutate is not None:
                    new = mutate(line)
                    if new != line:
                        mutated += 1
                    line = new
                oracle.stdin.write(line.encode("utf-8"))
        except BrokenPipeError:
            pass
        finally:
            try:
                oracle.stdin.close()
            except BrokenPipeError:
                pass
        verdict_text = oracle.stdout.read().decode("utf-8")
        oracle.wait()
        reader.stdout.close()
        reader.wait()
        verdict = json.loads(verdict_text)
        verdict["oracle_exit"] = oracle.returncode
        verdict["mutated_lines"] = mutated
        return verdict

    def run_oracle_controls(self) -> None:
        total_target = 300000
        src, meta = self.fixture("oracle-report", "report-v1", total_target)
        pdf = self.work / "oracle-report.pdf"
        p = self.convert(src, pdf, "--profile", "report-v1")
        total = meta["records"]
        good = self.oracle(pdf, "report-v1", total)
        self.record("oracle-positive", "unmodified reader stream passes with every record checked",
                    p.returncode == 0 and good["ok"] and good["records_checked"] == total,
                    converter_exit=p.returncode, records=total, verdict=good,
                    fixture_sha256=meta["sha256"])
        k = total // 2
        drop = {f"Запись {k}", f"v: {resource_recipe.value(k)}"}
        missing = self.oracle(pdf, "report-v1", total,
                              mutate=lambda ln: "" if ln.strip() in drop else ln)
        self.record("oracle-missing-interior-record", f"reader stream without record {k} fails",
                    not missing["ok"] and missing["oracle_exit"] == 1 and missing["mutated_lines"] == 2,
                    verdict=missing)
        target = f"v: {resource_recipe.value(k + 1)}"
        bad = target[:-1] + ("0" if target[-1] != "0" else "1")
        corrupt = self.oracle(pdf, "report-v1", total,
                              mutate=lambda ln: ln.replace(target, bad) if ln.strip() == target else ln)
        self.record("oracle-corrupt-interior-record",
                    f"reader stream with one changed digit in record {k + 1} fails",
                    not corrupt["ok"] and corrupt["oracle_exit"] == 1 and corrupt["mutated_lines"] == 1,
                    verdict=corrupt)
        pdf.unlink(missing_ok=True)
        src.unlink()

        src, meta = self.fixture("oracle-omit", "records-v1", 200000, omit=1234)
        pdf = self.work / "oracle-omit.pdf"
        p = self.convert(src, pdf)
        omitted = self.oracle(pdf, "records-v1", meta["records"])
        self.record("oracle-fixture-omits-record",
                    "a fixture lacking record 1234 converts, and the oracle (recipe still expects it) fails",
                    p.returncode == 0 and not omitted["ok"] and "1234" in omitted["error"],
                    converter_exit=p.returncode, verdict=omitted, fixture_sha256=meta["sha256"])
        pdf.unlink(missing_ok=True)
        src.unlink()

    def run_tail_controls(self) -> None:
        target = 1 * rm.MIB
        for tail in resource_recipe.TAILS:
            src, meta = self.fixture(f"tail-{tail}", "report-v1", target, tail=tail)
            pdf = self.work / f"tail-{tail}.pdf"
            pdf.write_bytes(b"PREVIOUS")
            p = self.convert(src, pdf, "--profile", "report-v1", "--record-limit", "1000")
            kept = pdf.read_bytes() == b"PREVIOUS"
            obs = {"converter_exit": p.returncode, "stderr": p.stderr.strip(), "records": meta["records"],
                   "input_bytes": meta["input_bytes"], "fixture_sha256": meta["sha256"],
                   "destination_kept": kept, "command": " ".join(p.argv)}
            if tail == "valid":
                verdict = self.oracle(pdf, "report-v1", meta["records"], 1000)
                obs["verdict"] = verdict
                self.record("tail-valid", "valid tail succeeds; counters total/1000/total-1000 match",
                            p.returncode == 0 and verdict["ok"] and verdict["records_checked"] == 1000,
                            **obs)
            elif tail == "malformed":
                self.record("tail-malformed-xml",
                            "malformed XML after the selected records fails as XML; destination kept",
                            p.returncode == 1 and p.stderr.startswith("xml:") and kept, **obs)
            else:
                self.record("tail-profile-invalid",
                            "well-formed but profile-invalid record after the selection fails; destination kept",
                            p.returncode == 1 and p.stderr.startswith("profile:") and kept, **obs)
            pdf.unlink(missing_ok=True)
            src.unlink()

    def run_quota_controls(self) -> None:
        src, meta = self.fixture("quota", "records-v1", 200000)
        cases = [
            ("quota-pages", ["--max-pages", "1"], 4, "resource"),
            ("quota-output-bytes", ["--max-output-bytes", "4096"], 4, "resource"),
            ("quota-spool-bytes", ["--max-spool-bytes", "16"], 4, "resource"),
        ]
        for name, opts, want_rc, want_msg in cases:
            pdf = self.work / f"{name}.pdf"
            pdf.write_bytes(b"PREVIOUS")
            p = self.convert(src, pdf, *opts)
            kept = pdf.read_bytes() == b"PREVIOUS"
            self.record(name, f"{' '.join(opts)} fails with exit {want_rc} ({want_msg}); destination kept",
                        p.returncode == want_rc and want_msg in p.stderr and kept,
                        converter_exit=p.returncode, stderr=p.stderr.strip(), destination_kept=kept,
                        command=" ".join(p.argv), fixture_sha256=meta["sha256"])
            pdf.unlink(missing_ok=True)
        base = self.font.read_bytes()
        for name, size, want_rc in (("font-at-8MiB-cap", FONT_CAP, 0), ("font-over-8MiB-cap", FONT_CAP + 1, 1)):
            padded = self.work / f"{name}.ttf"
            padded.write_bytes(base + b"\0" * (size - len(base)))
            pdf = self.work / f"{name}.pdf"
            argv = [str(self.binary), "--font", str(padded), str(src), str(pdf)]
            p = subprocess.run(argv, capture_output=True, text=True)
            ok = p.returncode == want_rc and (want_rc == 0 or p.stderr.startswith("font:"))
            self.record(name, f"{size}-byte font (DejaVu + zero padding) gives exit {want_rc}", ok,
                        converter_exit=p.returncode, stderr=p.stderr.strip(), font_bytes=size,
                        command=" ".join(argv))
            padded.unlink()
            pdf.unlink(missing_ok=True)
        src.unlink()

    def run(self) -> int:
        self.out.mkdir(parents=True, exist_ok=True)
        self.work.mkdir(parents=True, exist_ok=True)
        self.run_oracle_controls()
        self.run_tail_controls()
        self.run_quota_controls()
        summary = {"binary": str(self.binary), "binary_sha256": rm.sha256_file(self.binary),
                   "font": str(self.font), "font_sha256": rm.sha256_file(self.font),
                   "controls": self.results, "all_passed": all(r["passed"] for r in self.results)}
        (self.out / "controls.json").write_text(json.dumps(summary, indent=2, ensure_ascii=False) + "\n",
                                                encoding="utf-8")
        print(f"controls all_passed={summary['all_passed']} -> {self.out / 'controls.json'}")
        return 0 if summary["all_passed"] else 1


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description="OSR-23 negative and quota controls")
    ap.add_argument("--binary", required=True)
    ap.add_argument("--font", required=True)
    ap.add_argument("--evidence-dir", required=True)
    ap.add_argument("--scratch", required=True)
    return Controls(ap.parse_args(argv)).run()


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
