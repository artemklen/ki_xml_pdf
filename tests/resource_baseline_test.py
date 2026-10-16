#!/usr/bin/env python3
"""Unit tests for the OSR-23 fixture recipe and streaming oracle.

They run without the converter. Real-PDF negative controls live in
`resource_baseline.py controls`.
"""

import io
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import resource_oracle  # noqa: E402
import resource_recipe  # noqa: E402


def as_pdftotext(lines, per_page=50):
    """Render lines the way pdftotext -layout lays them out: blank lines
    between some lines and a form feed after each page."""
    out = []
    for n, line in enumerate(lines, 1):
        out.append(line + "\n")
        if n % 7 == 0:
            out.append("\n")
        if n % per_page == 0:
            out.append("\f")
    out.append("\f")
    return io.StringIO("".join(out))


class RecipeTest(unittest.TestCase):
    def fixture(self, profile, target, **kw):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        path = Path(tmp.name) / "in.xml"
        meta = resource_recipe.write_fixture(path, profile, target, **kw)
        return path, meta

    def test_records_fixture_parses_and_matches_metadata(self):
        path, meta = self.fixture("records-v1", 20000)
        self.assertGreaterEqual(meta["input_bytes"], 20000)
        self.assertEqual(path.stat().st_size, meta["input_bytes"])
        root = ET.parse(path).getroot()
        self.assertEqual(root.tag, "records")
        items = list(root)
        self.assertEqual(len(items), meta["records"])
        for i, el in enumerate(items, 1):
            self.assertEqual(el.get("n"), str(i))
            self.assertEqual(el.text, resource_recipe.value(i))

    def test_report_fixture_overshoots_by_less_than_one_record(self):
        path, meta = self.fixture("report-v1", 50000)
        rec = len(resource_recipe.record_bytes("report-v1", meta["records"]))
        self.assertLess(meta["input_bytes"] - 50000, rec)
        root = ET.parse(path).getroot()
        self.assertEqual(root.get("title"), resource_recipe.REPORT_TITLE)
        self.assertEqual(len(root.findall("record")), meta["records"])

    def test_same_recipe_same_hash(self):
        _, a = self.fixture("report-v1", 30000)
        _, b = self.fixture("report-v1", 30000)
        self.assertEqual(a["sha256"], b["sha256"])

    def test_omitted_record_is_absent(self):
        path, meta = self.fixture("records-v1", 20000, omit=17)
        ns = [el.get("n") for el in ET.parse(path).getroot()]
        self.assertNotIn("17", ns)
        self.assertEqual(len(ns), meta["records"] - 1)

    def test_malformed_tail_is_not_well_formed(self):
        path, _ = self.fixture("report-v1", 20000, tail="malformed")
        with self.assertRaises(ET.ParseError):
            ET.parse(path)

    def test_profile_invalid_tail_is_well_formed(self):
        path, _ = self.fixture("report-v1", 20000, tail="profile-invalid")
        root = ET.parse(path).getroot()
        self.assertIsNotNone(root.find("record/nope"))


class OracleTest(unittest.TestCase):
    def lines(self, profile, total, offset=0, limit=0):
        return list(resource_recipe.expected_lines(profile, total, offset, limit))

    def test_accepts_complete_records(self):
        v = resource_oracle.check(as_pdftotext(self.lines("records-v1", 500)), "records-v1", 500)
        self.assertTrue(v.ok, v.error)
        self.assertEqual(v.records_checked, 500)
        self.assertGreater(v.page_breaks, 1)

    def test_accepts_full_report_and_selection(self):
        v = resource_oracle.check(as_pdftotext(self.lines("report-v1", 300)), "report-v1", 300)
        self.assertTrue(v.ok, v.error)
        self.assertEqual(v.records_checked, 300)
        sel = self.lines("report-v1", 5000, 0, 1000)
        self.assertEqual(sel[-3:], ["Всего записей: 5000", "Выбрано записей: 1000",
                                    "Пропущено записей: 4000"])
        v = resource_oracle.check(as_pdftotext(sel), "report-v1", 5000, 0, 1000)
        self.assertTrue(v.ok, v.error)
        self.assertEqual(v.records_checked, 1000)

    def test_rejects_missing_interior_record(self):
        lines = self.lines("records-v1", 500)
        del lines[1 + 3 * 249:1 + 3 * 250]
        v = resource_oracle.check(as_pdftotext(lines), "records-v1", 500)
        self.assertFalse(v.ok)
        self.assertIn("n: 250", v.error)

    def test_rejects_corrupted_interior_value(self):
        lines = self.lines("report-v1", 300)
        i = lines.index(f"v: {resource_recipe.value(150)}")
        lines[i] = lines[i][:-1] + ("0" if lines[i][-1] != "0" else "1")
        v = resource_oracle.check(as_pdftotext(lines), "report-v1", 300)
        self.assertFalse(v.ok)
        self.assertIn("logical line", v.error)

    def test_rejects_swapped_records(self):
        lines = self.lines("report-v1", 50)
        lines[3:5], lines[5:7] = lines[5:7], lines[3:5]
        self.assertFalse(resource_oracle.check(as_pdftotext(lines), "report-v1", 50).ok)

    def test_rejects_truncated_and_extra_output(self):
        lines = self.lines("records-v1", 40)
        self.assertFalse(resource_oracle.check(as_pdftotext(lines[:-1]), "records-v1", 40).ok)
        self.assertFalse(resource_oracle.check(as_pdftotext(lines + ["x"]), "records-v1", 40).ok)

    def test_rejects_wrong_selection_counters(self):
        lines = self.lines("report-v1", 2000, 0, 1000)
        lines[-1] = "Пропущено записей: 999"
        self.assertFalse(resource_oracle.check(as_pdftotext(lines), "report-v1", 2000, 0, 1000).ok)

    def test_rejects_unselected_record_in_selection(self):
        lines = self.lines("report-v1", 2000, 0, 1000)
        lines[-3:-3] = resource_recipe.record_lines("report-v1", 1001)
        self.assertFalse(resource_oracle.check(as_pdftotext(lines), "report-v1", 2000, 0, 1000).ok)


if __name__ == "__main__":
    unittest.main()
