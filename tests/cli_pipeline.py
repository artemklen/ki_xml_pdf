#!/usr/bin/env python3
"""Check CLI pipelines, publication, quotas and real signal cancellation."""

import argparse
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import time


ROOT = Path(__file__).resolve().parents[1]
OLD = b"OLD destination must survive\n"


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def check_status(result, expected, label):
    require(
        result.returncode == expected,
        f"{label}: exit {result.returncode}, expected {expected}; "
        f"stderr={result.stderr.decode('utf-8', errors='replace')}",
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=ROOT / "xml2pdf")
    parser.add_argument("--fixture", type=Path, default=ROOT / "examples/report-v1.xml")
    parser.add_argument("--oracle", type=Path, default=ROOT / "examples/report-v1.expected.txt")
    parser.add_argument("--pdftotext", default=os.environ.get("PDFTOTEXT", "pdftotext"))
    args = parser.parse_args()
    binary = args.binary.resolve()
    fixture = args.fixture.read_bytes()
    expected = args.oracle.read_text(encoding="utf-8").splitlines()
    env = os.environ.copy()
    env.pop("KI_XML_PDF_FONT", None)
    passed = 0

    def passed_case(label):
        nonlocal passed
        passed += 1
        print(f"PASS {label}", flush=True)

    with tempfile.TemporaryDirectory(prefix="ki-cli-pipeline.") as temporary:
        work = Path(temporary)
        input_file = work / "report.xml"
        input_file.write_bytes(fixture)

        def case_dir(name):
            directory = work / name
            directory.mkdir()
            spool = directory / "spool"
            spool.mkdir()
            return directory, spool, directory / "destination.pdf"

        def run(arguments, *, data=None, cwd=work, stdout=subprocess.PIPE):
            return subprocess.run(
                [str(binary), *map(str, arguments)],
                input=data,
                stdout=stdout,
                stderr=subprocess.PIPE,
                cwd=cwd,
                env=env,
                timeout=15,
            )

        def pdf_text(path):
            result = subprocess.run(
                [args.pdftotext, "-layout", "-nopgbrk", str(path), "-"],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                timeout=15,
            )
            check_status(result, 0, "Poppler")
            return [line.rstrip() for line in result.stdout.decode("utf-8").splitlines() if line.strip()]

        def check_pdf(path):
            require(path.read_bytes().startswith(b"%PDF-"), "output does not start with PDF header")
            require(pdf_text(path) == expected, "PDF text differs from report-v1 oracle")

        def check_preserved(directory, spool, destination):
            require(destination.read_bytes() == OLD, "failure replaced OLD destination")
            require(not list(spool.iterdir()), "failure leaked an offset spool")
            require(
                {path.name for path in directory.iterdir()} == {"spool", destination.name},
                "failure leaked a temporary PDF",
            )

        for stdin in (False, True):
            for stdout in (False, True):
                label = f"{'stdin' if stdin else 'file'}-to-{'stdout' if stdout else 'file'}"
                directory, spool, destination = case_dir(label)
                result = run(
                    ["--profile", "report-v1", "--temp-dir", spool,
                     "-" if stdin else input_file, "-" if stdout else destination],
                    data=fixture if stdin else None,
                )
                check_status(result, 0, label)
                require(result.stderr, f"{label}: missing conversion diagnostics on stderr")
                if stdout:
                    require(result.stdout.startswith(b"%PDF-"), f"{label}: stdout is not exclusively PDF")
                    require(result.stdout.rstrip().endswith(b"%%EOF"), f"{label}: incomplete PDF")
                    destination.write_bytes(result.stdout)
                else:
                    require(result.stdout == b"", f"{label}: diagnostics polluted stdout")
                check_pdf(destination)
                require(not list(spool.iterdir()), f"{label}: leaked spool")
                passed_case(label)

        for option in ("-h", "--help"):
            result = run([option])
            check_status(result, 0, option)
            require(result.stdout and not result.stderr, f"{option}: help must be on stdout")
            require(b"partial PDF" in result.stdout, "help does not explain partial stdout on failure")
            passed_case(option)

        directory, spool, destination = case_dir("dash-paths")
        (directory / "--input.xml").write_bytes(fixture)
        result = run(
            ["--profile", "report-v1", "--temp-dir", spool, "--", "--input.xml", "--output.pdf"],
            cwd=directory,
        )
        check_status(result, 0, "-- paths")
        check_pdf(directory / "--output.pdf")
        passed_case("-- accepts paths beginning with --")

        invalid = [
            [], ["--unknown"], ["--profile"], ["--profile", "absent", "-", "-"],
            ["--max-pages", "-1", "-", "-"], ["--max-pages", "2147483648", "-", "-"],
            ["--max-output-bytes", "not-a-number", "-", "-"],
            ["--record-limit", "9" * 100, "-", "-"],
            ["--record-offset", "-1", "-", "-"], ["-", "-", "extra"],
        ]
        for number, arguments in enumerate(invalid):
            result = run(arguments, data=fixture)
            check_status(result, 2, f"invalid arguments {number}")
            require(not result.stdout and result.stderr, "usage error polluted stdout or hid diagnostics")
        passed_case("invalid arguments return 2")

        records = b"".join(
            f'<record id="{number}"><field name="Value">Record {number}</field></record>'.encode()
            for number in range(100)
        )
        large = b'<report version="1" title="Pipeline">' + records + b"</report>"
        late = large[:-len(b"</report>")] + b"</repor"
        failures = [
            ("late-xml", [], late, 1),
            ("page-quota", ["--max-pages", "1"], large, 4),
            ("output-quota", ["--max-output-bytes", "64"], large, 4),
            ("spool-quota", ["--max-spool-bytes", "8"], large, 4),
        ]
        for label, options, data, status in failures:
            directory, spool, destination = case_dir(label)
            destination.write_bytes(OLD)
            result = run(
                ["--profile", "report-v1", "--temp-dir", spool, *options, "-", destination],
                data=data,
            )
            check_status(result, status, label)
            require(not result.stdout and result.stderr, f"{label}: failure diagnostics must use stderr")
            check_preserved(directory, spool, destination)
            passed_case(f"{label} preserves OLD and cleans temporary files")

        # Diagnostics name the consumed position: line 2 after CRLF, the
        # column counting "Ж" once, the raw byte offset counting the BOM.
        coordinates = [
            ("xml-coordinates", [], "\ufeff<r>\r\n Ж<a b='1' b".encode() + b"='2'/></r>",
             b"xml: duplicate attribute at line 2, column 13, byte 21\n"),
            ("profile-coordinates", ["--profile", "report-v1"],
             '<report version="1" title="T">\n<record><field name="n">a</field></record>\n<bogus/>'
             '</report>'.encode(), b"profile: unknown element at line 3, column 9, byte 82\n"),
            # Malformed UTF-8 inside a tag is reported after the tag's '>'.
            ("utf8-attribute-coordinates", [], b"<r a='\x80abc'/>",
             b"xml: illegal character at line 1, column 14, byte 13\n"),
            ("utf8-text-coordinates", [], b"<r>a\x80b</r>",
             b"xml: illegal character at line 1, column 6, byte 5\n"),
        ]
        for label, options, data, message in coordinates:
            directory, spool, destination = case_dir(label)
            destination.write_bytes(OLD)
            for stdin in (False, True):
                source = directory / "input.xml"
                source.write_bytes(data)
                result = run([*options, "--temp-dir", spool, "-" if stdin else source, destination],
                             data=data if stdin else None)
                check_status(result, 1, label)
                require(result.stderr == message, f"{label}: stderr {result.stderr!r}, expected {message!r}")
                source.unlink()
                check_preserved(directory, spool, destination)
            passed_case(f"{label} reports line, column and byte")

        directory, spool, destination = case_dir("signal-cancel")
        destination.write_bytes(OLD)
        process = subprocess.Popen(
            [str(binary), "--profile", "report-v1", "--temp-dir", str(spool), "-", str(destination)],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, cwd=work, env=env,
        )
        try:
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                require(process.poll() is None, "CLI exited before it could wait for stdin")
                wait_channel = Path(f"/proc/{process.pid}/wchan").read_text()
                temporary_pdf = any(path.name not in {"spool", destination.name}
                                    for path in directory.iterdir())
                if "pipe_read" in wait_channel and temporary_pdf:
                    break
                time.sleep(0.01)
            else:
                raise AssertionError("CLI did not wait for stdin after opening its temporary PDF")
            process.send_signal(signal.SIGTERM)
            # Keep stdin open: SIGTERM itself must interrupt the blocked reader.
            process.wait(timeout=5)
            out, error = process.communicate()
            require(process.returncode == 4, f"SIGTERM returned {process.returncode}: {error!r}")
            require(not out and error, "cancellation diagnostics must use stderr")
            check_preserved(directory, spool, destination)
        finally:
            if process.poll() is None:
                process.kill()
            process.communicate()
        passed_case("SIGTERM while stdin waits preserves OLD and cleans temporary files")

        with open("/dev/full", "wb", buffering=0) as full:
            result = run(["--profile", "report-v1", input_file, "-"], stdout=full)
        check_status(result, 3, "stdout /dev/full")
        require(result.stderr, "/dev/full failure must have diagnostics")
        passed_case("stdout /dev/full returns I/O failure")

        read_fd, write_fd = os.pipe()
        os.close(read_fd)
        with os.fdopen(write_fd, "wb", buffering=0) as closed_pipe:
            result = run(["--profile", "report-v1", input_file, "-"], stdout=closed_pipe)
        check_status(result, 3, "stdout closed pipe")
        require(result.stderr, "closed pipe failure must have diagnostics")
        passed_case("stdout closed pipe returns I/O failure instead of SIGPIPE death")

        result = run(["--profile", "report-v1", "-", "-"], data=late)
        check_status(result, 1, "late error on stdout")
        require(result.stdout.startswith(b"%PDF-"), "late stdout error did not exercise partial PDF output")
        require(not result.stdout.rstrip().endswith(b"%%EOF"), "failed stdout conversion produced a complete PDF")
        require(result.stderr, "late stdout error must have stderr diagnostics")
        passed_case("late XML error leaves a partial PDF on stdout")

        default_file = work / "records.xml"
        default_file.write_bytes(b"<doc>Hello</doc>")
        for stdin in (False, True):
            destination = work / f"default-{stdin}.pdf"
            result = run(["-" if stdin else default_file, "-" if stdin else destination],
                         data=default_file.read_bytes() if stdin else None)
            check_status(result, 0, "default records-v1")
            if stdin:
                require(result.stdout.startswith(b"%PDF-"), "default records-v1 stdout is not PDF")
                destination.write_bytes(result.stdout)
            else:
                require(not result.stdout, "default file conversion polluted stdout")
            require(pdf_text(destination) == ["doc", "Hello"], "default profile no longer renders records-v1")
            passed_case(f"default records-v1 {'pipeline' if stdin else 'files'}")

    print(f"cli_pipeline ok: {passed} scenarios; binary={binary}")


if __name__ == "__main__":
    main()
