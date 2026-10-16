#!/usr/bin/env python3
"""Deterministic synthetic fixtures for the OSR-23 resource series.

The recipe defines both the XML bytes and the logical text lines a correct
PDF must contain. Expected lines come from the recipe only, never from a PDF
produced by the converter.

records-v1 record i (1-based):  <r n="i">VALUE(i)</r>\\n
    renders the lines  "r", "n: i", "VALUE(i)"
report-v1 record i:  <record id="i"><field name="v">VALUE(i)</field></record>\\n
    renders the lines  "Запись i", "v: VALUE(i)"
    after the title line and before the three counter lines.

Records are appended until the file is at least --target-bytes long, so the
actual length exceeds the target by less than one record.

Run as a program it writes one fixture and prints JSON metadata (record
count, bytes, SHA-256) on stdout. It is measured as its own process.
"""

import argparse
import hashlib
import json
import sys
from pathlib import Path

RECIPE = "osr23-v1"
REPORT_TITLE = "OSR-23 synthetic report"
ROOT_RECORDS = "records"
TAILS = ("valid", "malformed", "profile-invalid")


def value(i: int) -> str:
    """Per-record value with a check part so any changed digit is caught."""
    return f"v{i}.{(i * 2654435761) % 1000003:07d}"


def record_bytes(profile: str, i: int) -> bytes:
    if profile == "records-v1":
        return f'<r n="{i}">{value(i)}</r>\n'.encode("ascii")
    if profile == "report-v1":
        return (f'<record id="{i}"><field name="v">{value(i)}</field></record>\n').encode("ascii")
    raise ValueError(f"unknown profile {profile}")


def head_bytes(profile: str) -> bytes:
    if profile == "records-v1":
        return f"<{ROOT_RECORDS}>\n".encode("ascii")
    return f'<report version="1" title="{REPORT_TITLE}">\n'.encode("utf-8")


def tail_bytes(profile: str) -> bytes:
    if profile == "records-v1":
        return f"</{ROOT_RECORDS}>\n".encode("ascii")
    return b"</report>\n"


# A bad record placed right before the closing tag, far beyond any selection.
BAD_TAILS = {
    "malformed": b'<record id="bad"><field name="v">x</fieldx></record>\n',
    "profile-invalid": b'<record id="bad"><nope/></record>\n',
}


def record_lines(profile: str, i: int) -> list[str]:
    if profile == "records-v1":
        return ["r", f"n: {i}", value(i)]
    return [f"Запись {i}", f"v: {value(i)}"]


def expected_lines(profile: str, total: int, offset: int = 0, limit: int = 0):
    """Yield every logical line a correct PDF holds, in order.

    For report-v1 the selection is source order: skip `offset` records, then
    take `limit` records (0 means all remaining).
    """
    if profile == "records-v1":
        yield ROOT_RECORDS
        for i in range(1, total + 1):
            yield from record_lines(profile, i)
        return
    yield REPORT_TITLE
    first = min(offset, total) + 1
    last = total if limit == 0 else min(total, offset + limit)
    for i in range(first, last + 1):
        yield from record_lines(profile, i)
    selected = max(0, last - first + 1)
    yield f"Всего записей: {total}"
    yield f"Выбрано записей: {selected}"
    yield f"Пропущено записей: {total - selected}"


def lines_per_record(profile: str) -> int:
    return 3 if profile == "records-v1" else 2


def write_fixture(path: Path, profile: str, target: int, omit: int = 0, tail: str = "valid") -> dict:
    """Write one fixture. `omit` drops record number `omit` from the XML (a
    negative control: the recipe still expects it). Returns metadata."""
    if tail not in TAILS:
        raise ValueError(f"unknown tail {tail}")
    if tail != "valid" and profile != "report-v1":
        raise ValueError("bad tails are defined for report-v1 only")
    digest = hashlib.sha256()
    end = tail_bytes(profile) + BAD_TAILS.get(tail, b"")
    size = 0
    count = 0
    batch = []
    batch_len = 0
    with path.open("wb") as out:
        def put(chunk: bytes) -> None:
            nonlocal size
            out.write(chunk)
            digest.update(chunk)
            size += len(chunk)

        put(head_bytes(profile))
        while size + batch_len + len(end) < target:
            count += 1
            rec = record_bytes(profile, count)
            if count != omit:
                batch.append(rec)
                batch_len += len(rec)
            if batch_len >= 1 << 20:
                put(b"".join(batch))
                batch.clear()
                batch_len = 0
        put(b"".join(batch))
        put(BAD_TAILS.get(tail, b""))
        put(tail_bytes(profile))
    return {
        "recipe": RECIPE,
        "profile": profile,
        "target_bytes": target,
        "input_bytes": size,
        "records": count,
        "omitted_record": omit,
        "tail": tail,
        "sha256": digest.hexdigest(),
        "path": str(path),
    }


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--profile", required=True, choices=["records-v1", "report-v1"])
    ap.add_argument("--target-bytes", type=int, required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--omit-record", type=int, default=0)
    ap.add_argument("--tail", choices=TAILS, default="valid")
    args = ap.parse_args(argv)
    meta = write_fixture(Path(args.out), args.profile, args.target_bytes, args.omit_record, args.tail)
    json.dump(meta, sys.stdout)
    sys.stdout.write("\n")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
