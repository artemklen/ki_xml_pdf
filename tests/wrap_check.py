#!/usr/bin/env python3
"""Independent word-wrap check against Poppler.

Glyph advances come from this script's own TTF reader (font_metrics_check.Ttf);
expected lines come from the documented wrap policy applied to those advances,
not from the converter. pdftotext/pdfinfo read the PDFs produced by the CLI.

    python3 tests/wrap_check.py [--binary xml2pdf]
"""

import argparse
import re
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from font_metrics_check import BUNDLED, ROOT, Ttf, require  # noqa: E402

LINES_PER_PAGE = 50
LEFT = 40
WIDTH_PT = 612 - 2 * LEFT


class Policy:
    """Space and tab are break opportunities that stay on their line; every
    other code point, NBSP included, belongs to a word. A word that does not
    fit after the line's content starts the next line; a word wider than a
    line is cut at a code point boundary before the code point that would
    overflow it."""

    def __init__(self, font):
        self.font = font
        self.limit = WIDTH_PT * font.units // 10

    def adv(self, ch):
        g = self.font.gid(ord(" " if ch == "\t" else ch))
        require(g != 0, f"U+{ord(ch):04X} missing from the font")
        return self.font.advance(g)

    def lines(self, text):
        out, line, units, word, wunits = [], "", 0, "", 0
        for ch in text + " ":
            if ch in " \t":
                if word:
                    if line and units + wunits > self.limit:
                        out.append(line)
                        line, units = "", 0
                    line, units = line + word, units + wunits
                    word, wunits = "", 0
                line, units = line + ch, units + self.adv(ch)
                continue
            a = self.adv(ch)
            if wunits + a > self.limit:
                if line:
                    out.append(line)
                    line, units = "", 0
                out.append(word)
                word, wunits = "", 0
            word, wunits = word + ch, wunits + a
        line = line[:-1]
        if line:
            out.append(line)
        return out


def convert(binary, xml, out):
    r = subprocess.run([str(binary), "--font", str(BUNDLED), "-", str(out)], input=xml.encode(),
                       capture_output=True, timeout=120)
    require(r.returncode == 0, f"exit {r.returncode}: {r.stderr.decode(errors='replace')}")


def pages_of(pdf):
    r = subprocess.run(["pdftotext", "-enc", "UTF-8", str(pdf), "-"], capture_output=True, check=True)
    pages = r.stdout.decode().split("\f")
    require(pages[-1] == "", "text does not end with a page break")
    return [[l.rstrip(" \t") for l in p.split("\n") if l != ""] for p in pages[:-1]]


def page_count(pdf):
    r = subprocess.run(["pdfinfo", str(pdf)], capture_output=True, check=True)
    return int(re.search(rb"Pages:\s+(\d+)", r.stdout).group(1))


def max_word_right(pdf):
    r = subprocess.run(["pdftotext", "-bbox-layout", str(pdf), "-"], capture_output=True, check=True)
    xs = [float(x) for x in re.findall(rb'<word xMin="[^"]+" yMin="[^"]+" xMax="([^"]+)"', r.stdout)]
    require(xs, "no word boxes")
    return max(xs)


def exact_fit(policy):
    """Two ASCII letters whose repetition fills the line exactly."""
    letters = "abcdefghijklmnopqrstuvwxyz"
    for x in letters:
        for y in letters:
            ax, ay = policy.adv(x), policy.adv(y)
            for n in range(1, policy.limit // ax + 1):
                rest = policy.limit - n * ax
                if rest > 0 and rest % ay == 0:
                    return x * n + y * (rest // ay)
    return None


def check(binary, tmp, policy, name, body, expect_pages=None):
    xml = f"<doc>{body}</doc>"
    out = tmp / f"{name}.pdf"
    convert(binary, xml, out)
    want = ["doc"] + [l.rstrip(" \t") for l in policy.lines(body)]
    got = [l for p in pages_of(out) for l in p]
    require(got == want, f"{name}: lines differ\n got  {got[:6]}\n want {want[:6]}")
    pages = pages_of(out)
    require(all(len(p) <= LINES_PER_PAGE for p in pages) and all(pages), f"{name}: bad page membership")
    n = page_count(out)
    require(n == len(pages) == (len(want) + LINES_PER_PAGE - 1) // LINES_PER_PAGE, f"{name}: {n} pages")
    if expect_pages is not None:
        require(n == expect_pages, f"{name}: {n} pages, expected {expect_pages}")
    right = max_word_right(out)
    require(right <= LEFT + WIDTH_PT + 0.01, f"{name}: word box ends at {right}")
    print(f"{name}: {len(want) - 1} lines, {n} pages, max word xMax {right:.2f}")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--binary", type=Path, default=ROOT / "xml2pdf")
    args = ap.parse_args()
    binary = args.binary.resolve()
    policy = Policy(Ttf(BUNDLED))
    english = ("The quick brown fox jumps over the lazy dog while seventeen wizards quietly "
               "vex the jumbled crowd of onlookers near the riverbank. ") * 6
    russian = ("Съешь же ещё этих мягких французских булок да выпей чаю, "
               "широкая электрификация южных губерний даст мощный толчок. ") * 6
    with tempfile.TemporaryDirectory() as t:
        tmp = Path(t)
        check(binary, tmp, policy, "english", english)
        check(binary, tmp, policy, "russian", russian)
        check(binary, tmp, policy, "nbsp", "price 1\u00a0000\u00a0000 rub " * 20)
        check(binary, tmp, policy, "tabs", "col\tcol\tcolumn\t" * 30)
        check(binary, tmp, policy, "oversized-ascii", "short " + "W" * 300 + " tail")
        check(binary, tmp, policy, "oversized-cyrillic", "кратко " + "Ж" * 200 + " хвост")
        fit = exact_fit(policy)
        require(fit is not None, "no exact-fit letter pair")
        check(binary, tmp, policy, "exact-fit", fit)
        check(binary, tmp, policy, "exact-fit-plus-one", fit + "i")
        check(binary, tmp, policy, "exact-fit-two-words", fit + " " + fit)
        # 49 body lines + the root name line fill one page; one more word opens a second.
        word = "x" * (len(fit) // 2)
        lines49 = " ".join([fit] * 49)
        check(binary, tmp, policy, "page-exact", lines49, expect_pages=1)
        check(binary, tmp, policy, "page-next", lines49 + " " + word, expect_pages=2)
        check(binary, tmp, policy, "many-pages", english * 30)
    print("wrap check ok")


if __name__ == "__main__":
    main()
