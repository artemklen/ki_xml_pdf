#!/usr/bin/env python3
"""Deterministic generator for synthetic malformed TTF fixtures.

The fixtures are hand-built byte layouts for loader bounds tests. They are not
derived from DejaVu or any other third-party font.

    python3 tests/font_fixtures.py            # write tests/corpus fixtures
    python3 tests/font_fixtures.py --check    # verify committed bytes
"""

import hashlib
import struct
import sys
from pathlib import Path

def table_directory(tables):
    """tables: list of (tag, payload) in file order; directory keeps that order."""
    header = struct.pack(">IHHHH", 0x00010000, len(tables), 0, 0, 0)
    offset = len(header) + 16 * len(tables)
    entries = b""
    body = b""
    for tag, payload in tables:
        entries += struct.pack(">4sIII", tag, 0, offset + len(body), len(payload))
        body += payload
    return header + entries + body


def truncated_cmap_184():
    """head 54, hhea 36, hmtx 4, then a 14-byte cmap whose single (3,1)
    record has rel=12: only the format=4 field of the subtable header exists.
    cmap is the final table, so reading its length field runs past the file."""
    head = bytearray(54)
    struct.pack_into(">H", head, 18, 2048)
    hhea = bytearray(36)
    struct.pack_into(">H", hhea, 34, 1)
    hmtx = struct.pack(">Hh", 500, 0)
    cmap = struct.pack(">HHHHI", 0, 1, 3, 1, 12) + struct.pack(">H", 4)
    data = table_directory([(b"head", bytes(head)), (b"hhea", bytes(hhea)),
                            (b"hmtx", hmtx), (b"cmap", cmap)])
    assert len(data) == 184, len(data)
    return data


FIXTURES = {"truncated-cmap-184.ttf": truncated_cmap_184}


def main(argv):
    corpus = Path(__file__).resolve().parent / "corpus"
    check = "--check" in argv
    status = 0
    for name, build in FIXTURES.items():
        data = build()
        path = corpus / name
        digest = hashlib.sha256(data).hexdigest()
        if check:
            if not path.exists() or path.read_bytes() != data:
                print(f"{name}: committed bytes differ from generator", file=sys.stderr)
                status = 1
            else:
                print(f"{name}: {len(data)} bytes sha256 {digest} ok")
        else:
            path.write_bytes(data)
            print(f"{name}: {len(data)} bytes sha256 {digest}")
    return status


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
