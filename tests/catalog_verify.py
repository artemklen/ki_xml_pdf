#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Verify the external catalog example through its installed/public interface."""

import argparse
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
OLD = b"OLD\x00catalog-destination\xff"


class Verification:
    def __init__(self, binary, pdftext, work):
        self.binary = str(binary)
        self.pdftext = pdftext
        self.work = work
        self.successes = 0
        self.rejections = 0

    def run(self, source, output, chunk=None, max_pages=None):
        command = [self.binary]
        if max_pages is not None:
            command += ["--max-pages", str(max_pages)]
        command += [str(source), str(output)]
        if chunk is not None:
            command.append(str(chunk))
        return subprocess.run(
            command, cwd=self.work, capture_output=True, text=True, encoding="utf-8"
        )

    def text(self, output):
        process = subprocess.run(
            [self.pdftext, "-layout", "-nopgbrk", "-q", str(output), "-"],
            cwd=self.work, capture_output=True, text=True, encoding="utf-8",
        )
        require(process.returncode == 0, f"PDF reader failed: {process.stderr}")
        return process.stdout

    def success(self, label, source, expected_lines):
        for chunk in (None, 1, 4096):
            output = self.work / f"{label}-{chunk}.pdf"
            process = self.run(source, output, chunk)
            require(process.returncode == 0, f"{label} chunk={chunk}: {process.stderr}")
            require(output.read_bytes().startswith(b"%PDF-"), f"{label}: missing PDF header")
            actual = logical_lines(self.text(output))
            require(actual == expected_lines,
                    f"{label} chunk={chunk}: text differs\nactual={actual!r}\nexpected={expected_lines!r}")
            self.successes += 1
            output.unlink()

    def reject(self, label, xml, cause, status, offset=None, rule=None, max_pages=None):
        for chunk in (None, 1, 4096):
            directory = self.work / f"{label}-{chunk}"
            directory.mkdir()
            source = directory / "input.xml"
            source.write_text(xml, encoding="utf-8")
            output = directory / "out.pdf"
            output.write_bytes(OLD)
            old_inode = output.stat().st_ino
            before = set(directory.iterdir())
            process = self.run(source, output, chunk, max_pages)
            require(process.returncode == status,
                    f"{label} chunk={chunk}: status={process.returncode}, expected={status}, "
                    f"stderr={process.stderr!r}")
            match = re.search(r"\bcause=(\d+)\b", process.stderr)
            require(match is not None and int(match.group(1)) == cause,
                    f"{label} chunk={chunk}: expected cause={cause}, stderr={process.stderr!r}")
            if offset is not None:
                line, column = position(xml.encode("utf-8")[:offset])
                match = re.search(r"\bat line (\d+), column (\d+), byte (\d+)\b", process.stderr)
                require(match is not None and
                        tuple(map(int, match.groups())) == (line, column, offset),
                        f"{label} chunk={chunk}: expected line={line} column={column} byte={offset}, "
                        f"stderr={process.stderr!r}")
            if rule is not None:
                require(rule in process.stderr,
                        f"{label} chunk={chunk}: missing rule {rule!r}: {process.stderr!r}")
            require(output.read_bytes() == OLD and output.stat().st_ino == old_inode,
                    f"{label} chunk={chunk}: previous output changed")
            require(set(directory.iterdir()) == before,
                    f"{label} chunk={chunk}: temporary output remains")
            self.rejections += 1


def position(consumed: bytes) -> tuple[int, int]:
    """1-based line and Unicode-scalar column after a consumed UTF-8 prefix."""
    text = consumed.decode("utf-8")
    text = text.removeprefix("\ufeff")
    lines = text.replace("\r\n", "\n").replace("\r", "\n").split("\n")
    return len(lines), len(lines[-1]) + 1


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def logical_lines(text):
    # Poppler's empty separators and trailing layout padding are not logical
    # content. Internal/leading spaces remain intact; never split on whitespace.
    return [line.rstrip(" \t") for line in text.splitlines() if line.strip(" \t")]


def profile_cases():
    # Each explicit failing event has its own independent consumed-byte oracle.
    # before + event is the input prefix through the event, not a character count.
    return [
        ("catalog-attr", "", '<catalog extra="x">', "</catalog>",
         "unknown attribute extra on catalog"),
        ("item-attr", "<catalog>", '<item extra="x">',
         "<name/><price/></item></catalog>", "unknown attribute extra on item"),
        ("name-attr", "<catalog><item>", '<name extra="x">',
         "x</name><price/></item></catalog>", "unknown attribute extra on name"),
        ("price-attr", "<catalog><item><name/>", '<price extra="x">',
         "x</price></item></catalog>", "unknown attribute extra on price"),
        ("namespace", "", '<catalog xmlns="urn:example">', "</catalog>",
         "namespaces are not supported"),
        ("prefixed-element", "", "<c:catalog>", "</c:catalog>",
         "namespaces are not supported"),
        ("unknown-element", "<catalog>", "<extra/>", "</catalog>", "unknown element extra"),
        ("price-first", "<catalog><item>", "<price/>",
         "<name/></item></catalog>", "price order or nesting"),
        ("repeat-name", "<catalog><item><name>é</name>", "<name/>",
         "<price/></item></catalog>", "name order or nesting"),
        ("repeat-price", "<catalog><item><name/><price/>", "<price/>",
         "</item></catalog>", "price order or nesting"),
        ("missing-price", "<catalog><item><name/>", "</item>", "</catalog>",
         "item requires name then price"),
        ("empty-item", "<catalog><item>", "</item>", "</catalog>",
         "item requires name then price"),
        ("nested-catalog", "<catalog>", "<catalog>", "</catalog></catalog>", "catalog nesting"),
        ("nested-item", "<catalog><item>", "<item>",
         "<name/><price/></item><name/><price/></item></catalog>", "item nesting"),
        ("nested-field", "<catalog><item><name>", "<price>",
         "x</price></name><price/></item></catalog>", "price order or nesting"),
        ("structural-text", "<catalog>", "oops",
         "<item><name/><price/></item></catalog>", "structural text"),
        ("empty-id", "<catalog>", '<item id=" \t">',
         "<name/><price/></item></catalog>", "empty item id"),
        ("long-unknown-attr", "", '<catalog ' + "x" * 63 + '="1">', "</catalog>",
         "unknown attribute " + "x" * 63 + " on catalog"),
    ]


def verify(args):
    binary = pathlib.Path(args.binary).resolve()
    fixture = pathlib.Path(args.fixture).resolve()
    oracle = pathlib.Path(args.oracle).resolve()
    require(binary.is_file(), f"catalog executable is missing: {binary}; build the example first")
    require(fixture.is_file() and oracle.is_file(), "fixture or oracle is missing")
    pdftext = shutil.which(args.pdftotext)
    require(pdftext is not None, f"PDF reader is missing: {args.pdftotext}")
    pdftext = str(pathlib.Path(pdftext).resolve())

    with tempfile.TemporaryDirectory(prefix="ki-catalog-verify-") as temporary:
        work = pathlib.Path(temporary)
        check = Verification(binary, pdftext, work)
        check.success("fixture", fixture, oracle.read_text(encoding="utf-8").splitlines())

        empty = work / "empty.xml"
        empty.write_text("<catalog/>", encoding="utf-8")
        check.success("empty", empty, ["Catalog", "Items: 0"])

        fields = work / "fields.xml"
        fields.write_text(
            '<catalog><item id="same"><name/><price/></item>'
            '<item id="same"><name>A<![CDATA[  B &#65;]]> C&#x20;D</name>'
            '<price>lexical text</price></item></catalog>',
            encoding="utf-8",
        )
        check.success("fields", fields, [
            "Catalog", "Item same", "Name:", "Price:", "Item same",
            "Name: A  B &#65; C D", "Price: lexical text", "Items: 2",
        ])

        left = "BEGIN" + "a" * 1017 + "é"
        middle = "CDé0123" * 1600
        value = left + middle + "END"
        large_xml = (
            '<catalog><item id="long"><name>' + left + "<![CDATA[" + middle +
            ']]>&#x45;&#78;&#68;</name><price>42&#46;50</price></item></catalog>'
        )
        require(len(value.encode("utf-8")) > 4096, "long-field fixture is too short")
        long_source = work / "long.xml"
        long_source.write_text(large_xml, encoding="utf-8")
        expected = "CatalogItem longName: " + value + "Price: 42.50Items: 1"
        for chunk in (None, 1, 4096):
            output = work / f"long-{chunk}.pdf"
            process = check.run(long_source, output, chunk)
            require(process.returncode == 0, f"long chunk={chunk}: {process.stderr}")
            # Compare EVERY character, keeping spaces. Only physical PDF line
            # and page separators are removed for the known unbroken payload.
            actual = check.text(output).replace("\n", "").replace("\r", "").replace("\f", "")
            require(actual == expected,
                    f"long chunk={chunk}: complete text differs "
                    f"(actual_chars={len(actual)}, expected_chars={len(expected)})")
            check.successes += 1
            output.unlink()

        for label, before, event, after, rule in profile_cases():
            check.reject(label, before + event + after, 7, 1,
                         len((before + event).encode("utf-8")), rule)

        before = large_xml.removesuffix("</catalog>")
        event = '<item unexpected="yes">'
        check.reject("late-profile", before + event + "<name/><price/></item></catalog>",
                     7, 1, len((before + event).encode("utf-8")),
                     "unknown attribute unexpected on item")
        check.reject("late-xml", before + "</wrong>", 1, 1, rule="xml:")
        check.reject("bad-reference",
                     "<catalog><item><name>&#0;</name><price/></item></catalog>", 1, 1)
        check.reject("duplicate-attribute",
                     '<catalog><item id="a" id="b"><name/><price/></item></catalog>', 1, 1)
        check.reject("unsupported-glyph",
                     "<catalog><item><name>&#x1F600;</name><price/></item></catalog>",
                     5, 1, rule="U+1F600")
        check.reject("page-quota", large_xml, 4, 4, max_pages=1)

        output = work / "usage.pdf"
        for extra in ("0", "-1", "nope", "999999999999999999999"):
            process = check.run(fixture, output, extra)
            require(process.returncode == 2 and "cause=2" in process.stderr,
                    f"invalid chunk {extra!r} was accepted: {process.stderr!r}")
            require(not output.exists(), "usage error created output")
        process = check.run(fixture, output, max_pages=0)
        require(process.returncode == 2 and "cause=2" in process.stderr,
                f"invalid page quota was accepted: {process.stderr!r}")
        require(not output.exists(), "usage error created output")
        print(f"catalog_verify ok: success={check.successes} rejected={check.rejections} "
              f"usage=5 long_field_utf8_bytes={len(value.encode('utf-8'))}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", default=str(ROOT / "examples/catalog"))
    parser.add_argument("--fixture", default=str(ROOT / "examples/catalog.xml"))
    parser.add_argument("--oracle", default=str(ROOT / "examples/catalog.expected.txt"))
    parser.add_argument("--pdftotext", default="pdftotext")
    args = parser.parse_args()
    try:
        verify(args)
    except (AssertionError, OSError, UnicodeError) as error:
        print(f"catalog_verify failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

