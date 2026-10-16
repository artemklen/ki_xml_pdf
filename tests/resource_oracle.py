#!/usr/bin/env python3
"""Independent streaming record oracle for OSR-23 resource runs.

It compares the logical lines of `pdftotext -layout` output, read as a
stream, with the lines the fixture recipe expects. Nothing is buffered
beyond the current line, so it scales to any PDF the reader can stream.
Blank lines and page breaks are layout, not content, and are skipped.
"""

import json
import sys
from typing import Iterable, Iterator, TextIO

import resource_recipe


def logical(stream: Iterable[str]) -> Iterator[str]:
    for raw in stream:
        for part in raw.split("\f"):
            line = part.strip()
            if line:
                yield line


class Verdict:
    def __init__(self) -> None:
        self.ok = False
        self.lines_checked = 0
        self.records_checked = 0
        self.page_breaks = 0
        self.error = ""

    def as_dict(self) -> dict:
        return {
            "ok": self.ok,
            "lines_checked": self.lines_checked,
            "records_checked": self.records_checked,
            "page_breaks": self.page_breaks,
            "error": self.error,
        }


def check(stream: Iterable[str], profile: str, total: int, offset: int = 0, limit: int = 0) -> Verdict:
    """Return a Verdict; ok only if every expected line appears, in order,
    with no extra or missing content."""
    v = Verdict()
    per = resource_recipe.lines_per_record(profile)
    want = resource_recipe.expected_lines(profile, total, offset, limit)
    # Header line(s) precede the first record; one for both profiles.
    header = 1

    def counted(it: Iterable[str]) -> Iterator[str]:
        for raw in it:
            v.page_breaks += raw.count("\f")
            yield raw

    got = logical(counted(stream))
    for index, expected in enumerate(want):
        line = next(got, None)
        if line is None:
            v.error = f"output ended at logical line {index + 1}; expected {expected!r}"
            return v
        if line != expected:
            v.error = f"logical line {index + 1}: expected {expected!r}, got {line!r}"
            return v
        v.lines_checked += 1
        if index >= header and (index - header + 1) % per == 0:
            v.records_checked += 1
    extra = next(got, None)
    if extra is not None:
        v.error = f"unexpected extra line after {v.lines_checked} lines: {extra!r}"
        return v
    if profile == "report-v1":
        # The three counter lines are not records.
        v.records_checked = expected_records(profile, total, offset, limit)
    v.ok = True
    return v


def expected_records(profile: str, total: int, offset: int = 0, limit: int = 0) -> int:
    if profile == "records-v1":
        return total
    first = min(offset, total)
    remaining = total - first
    return remaining if limit == 0 else min(limit, remaining)


def main(argv: list[str]) -> int:
    """usage: resource_oracle.py PROFILE TOTAL [OFFSET LIMIT] < pdftotext-output"""
    if len(argv) not in (2, 4):
        print(main.__doc__, file=sys.stderr)
        return 2
    profile, total = argv[0], int(argv[1])
    offset = int(argv[2]) if len(argv) == 4 else 0
    limit = int(argv[3]) if len(argv) == 4 else 0
    stdin: TextIO = open(sys.stdin.fileno(), encoding="utf-8", errors="strict", closefd=False)
    verdict = check(stdin, profile, total, offset, limit)
    json.dump(verdict.as_dict(), sys.stdout, ensure_ascii=False)
    sys.stdout.write("\n")
    return 0 if verdict.ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
