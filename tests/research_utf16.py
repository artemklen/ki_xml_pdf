#!/usr/bin/env python3
"""OSR-16 research harness: external incremental UTF-16 -> UTF-8 transcoding.

Research tool only; the library, CLI and installed examples do not use it
and gain no new encoding. It builds tests/research_utf16_adapter.c against
libki_xml_pdf.a (run `make -j2 xml2pdf` first), generates synthetic UTF-16
fixtures and checks, against oracles that do not use the adapter:

  dump       transcoded bytes == Python's codec output of the rewritten text,
             for every source chunk size and output cap
  convert    PDF via adapter == PDF of the same text as UTF-8 through the
             same API (adapter --mode utf8); pdftotext has the expected lines
  xml-error  library line/column/byte equal the UTF-8 run; the adapter's
             mapped source byte equals a Python prefix-length oracle
  enc-error  adapter error text and exact source offset; KI_CAUSE_IO;
             ki_convert_to_file keeps the previous destination
  cancel     KI_CAUSE_CANCEL, source descriptor still open after the call
  current    the unchanged xml2pdf on raw UTF-16 input (characterization)
  controls   a lenient-surrogate, 16-entry-ring adapter build must fail the
             enc-error and deep-offset expectations
  large      streamed UTF-16 from a generator pipe, no intermediate file;
             /usr/bin/time RSS against xml2pdf on the UTF-8 equivalent

usage: research_utf16.py --scratch DIR [--large-mib N] [--json OUT]
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parent.parent
CFLAGS = ["-std=c99", "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-O2", "-I", str(ROOT / "include")]

LINES = [
    "Отчёт N 16 — синтетика",
    "Привет, мир; ASCII line",
    "Ёлка «кавычки» … тире – и —",
]
NON_ASCII_DECL = '<?xml version="1.0" encoding="UTF-16" ё?><r/>'
COMMENT = "\U0001F600 \U0001D11E \u4E2D"  # surrogate pairs + CJK, only in a comment


def sha(data):
    return hashlib.sha256(data).hexdigest()


def doc_text(decl_value=None, extra_body="", comment=True):
    head = "" if decl_value is None else f'<?xml version="1.0" encoding="{decl_value}"?>\n'
    items = "".join(f"<line n=\"{i}\">{t}</line>\n" for i, t in enumerate(LINES))
    c = f"<!-- {COMMENT} -->\n" if comment else ""
    return f"{head}<report>\n{c}{items}{extra_body}</report>\n"


def rewritten(text):
    """The adapter's declaration rewrite, as an independent string edit."""
    m = re.match(r'(<\?xml[^>]*?encoding=")(UTF-16(?:LE|BE)?|utf-16)(")', text, re.I)
    if not m:
        return text
    pad = " " * (len(m.group(2)) - 5)
    return text[: m.start(2)] + "UTF-8" + m.group(3) + pad + text[m.end(3):]


def encode(text, variant):
    le = variant.startswith("le")
    body = text.encode("utf-16-le" if le else "utf-16-be", "surrogatepass")
    if variant.endswith("bom"):
        return (b"\xff\xfe" if le else b"\xfe\xff") + body
    return body


def src_offset_oracle(text, utf8_byte, bom):
    """Source byte offset of the first utf8_byte bytes of text as UTF-8."""
    done = 0
    src = bom
    for ch in text:
        if done >= utf8_byte:
            break
        done += len(ch.encode("utf-8", "surrogatepass"))
        src += 4 if ord(ch) > 0xFFFF else 2
    return src, done != utf8_byte


class Runner:
    def __init__(self, scratch):
        self.dir = Path(scratch).resolve()
        self.dir.mkdir(parents=True, exist_ok=True)
        self.n = 0
        self.failures = []
        self.records = []
        self.adapter = self.dir / "adapter"
        self.control = self.dir / "adapter_control"
        src = ROOT / "tests" / "research_utf16_adapter.c"
        lib = ROOT / "libki_xml_pdf.a"
        for out, extra in ((self.adapter, []), (self.control, ["-DLENIENT", "-DRING=16"])):
            subprocess.run(["gcc", *CFLAGS, *extra, str(src), str(lib), "-lz", "-o", str(out)], check=True)
        self.adapter_sha = sha(self.adapter.read_bytes())

    def file(self, name, data):
        p = self.dir / name
        p.write_bytes(data)
        return p

    def run(self, args, binary=None, stdin=None):
        exe = str(binary or self.adapter)
        r = subprocess.run([exe, *map(str, args)], cwd=ROOT, capture_output=True, input=stdin, check=False)
        if r.returncode != 0:
            raise RuntimeError(f"{exe} {args} exited {r.returncode}: {r.stderr.decode()}")
        return json.loads(r.stdout)

    def check(self, case, ok, detail):
        self.records.append({"case": case, "ok": bool(ok), **detail})
        if not ok:
            self.failures.append(case)


def run_dump_matrix(r, fixtures):
    for name, (text, variant) in fixtures.items():
        data = encode(text, variant)
        src = r.file(f"{name}.u16", data)
        want = rewritten(text).encode("utf-8")
        for chunk in (1, 2, 3, 4096):
            for cap in (1, 3, 0):
                out = r.dir / f"{name}.dump"
                res = r.run(["--dump", "--src-chunk", chunk, "--out-cap", cap, src, out])
                got = out.read_bytes()
                r.check(f"dump {name} chunk={chunk} cap={cap}", got == want and res["adapter_error"] is None,
                        {"src_sha256": sha(data), "max_pending": res["max_pending"], "reads": res["reads"]})


def run_convert_matrix(r, fixtures):
    pdf_hashes = set()
    for name, (text, variant) in fixtures.items():
        data = encode(text, variant)
        src = r.file(f"{name}.u16", data)
        u8 = r.file(f"{name}.u8.xml", rewritten(text).encode("utf-8"))
        base = r.dir / f"{name}.base.pdf"
        bres = r.run(["--mode", "utf8", u8, base])
        want = base.read_bytes()
        pdf_hashes.add(sha(want))
        for chunk in (1, 2, 3, 4096):
            for cap in (1, 3, 0):
                out = r.dir / f"{name}.pdf"
                res = r.run(["--src-chunk", chunk, "--out-cap", cap, src, out])
                ok = res["status"] == 0 and out.read_bytes() == want and res["fd_open_after"] == 1
                r.check(f"convert {name} chunk={chunk} cap={cap}", ok,
                        {"pdf_sha256": sha(out.read_bytes()), "base_status": bres["status"],
                         "reads": res["reads"], "max_pending": res["max_pending"],
                         "state_bytes": res["state_bytes"]})
    text = subprocess.run(["pdftotext", str(r.dir / "le_bom.pdf"), "-"], capture_output=True,
                          check=True).stdout.decode()
    r.check("pdftotext le_bom has every line in order",
            all(t in text for t in LINES) and text.index(LINES[0]) < text.index(LINES[2]),
            {"pdftotext_sha256": sha(text.encode())})
    plain = r.file("plain.u8.xml", doc_text("UTF-8").encode("utf-8"))
    cli = r.dir / "plain.cli.pdf"
    subprocess.run([str(ROOT / "xml2pdf"), "--font", "fonts/DejaVuSans.ttf", str(plain), str(cli)], cwd=ROOT,
                   check=True, capture_output=True)
    r.check("CLI on plain UTF-8 declaration gives the same PDF", sha(cli.read_bytes()) in pdf_hashes,
            {"cli_pdf_sha256": sha(cli.read_bytes()), "distinct_pdf_hashes": sorted(pdf_hashes)})


def run_xml_errors(r):
    deep = "".join(f"<l>строка {i} ЁЁЁ</l>\n" for i in range(60000))
    cases = {
        "u0001_in_text": doc_text("UTF-16", "<x>Ж\u0001</x>\n"),
        "ufffe_in_text": doc_text("UTF-16", "<x>ЖЖ\ufffe</x>\n"),
        "undefined_entity_after_cyrillic": doc_text("UTF-16", "<x>Ёжик ёлка &bad;</x>\n"),
        "deep_error_after_2MiB": doc_text("UTF-16", deep + "<x>Ж\u0001" + "Ж" * 3000 + "</x>\n"),
        "supplementary_in_text_font": doc_text("UTF-16", "<x>Ж\U0001F600</x>\n"),
    }
    out = {}
    for name, text in cases.items():
        src = r.file(f"{name}.u16", encode(text, "le_bom"))
        u8 = r.file(f"{name}.u8.xml", rewritten(text).encode("utf-8"))
        res = r.run(["--src-chunk", 3, "--out-cap", 1 if "deep" not in name else 0, src, r.dir / f"{name}.pdf"])
        base = r.run(["--mode", "utf8", u8, r.dir / f"{name}.base.pdf"])
        if name.endswith("_font"):
            ok = res["cause"] == 5 and "U+1F600" in res["message"] and res["adapter_error"] is None
            r.check(f"xml-error {name}", ok, {"message": res["message"], "cause": res["cause"]})
            out[name] = res
            continue
        exp_src, mid = src_offset_oracle(rewritten(text), res.get("utf8_byte", -1), 2)
        same = all(res.get(k) == base.get(k) for k in ("cause", "line", "column", "utf8_byte"))
        ok = res["cause"] == 1 and same and res["src_byte"] == exp_src and not mid and res["mid_scalar"] == 0
        r.check(f"xml-error {name}", ok,
                {"message": res["message"], "utf8_byte": res.get("utf8_byte"), "src_byte": res.get("src_byte"),
                 "read_ahead_utf8": res["utf8_bytes_out"] - res.get("utf8_byte", 0),
                 "oracle_src_byte": exp_src, "base_message": base["message"]})
        out[name] = res
    ctl = r.run(["--src-chunk", 4096, r.dir / "deep_error_after_2MiB.u16", r.dir / "ctl.pdf"], binary=r.control)
    r.check("control: 16-entry ring cannot map the deep error (reports -1, not a wrong offset)",
            ctl.get("src_byte") == -1 and ctl["utf8_bytes_out"] - ctl["utf8_byte"] > 16, {"control_result": ctl})
    return out


def run_enc_errors(r):
    body = doc_text("UTF-16")
    prefix = encode(body[: body.index("<line")], "le_bom")
    tail = encode(body[body.index("<line"):], "le_bom")[2:]
    big = encode("".join(f"<l>{i} Ж</l>\n" for i in range(20000)), "le_bom")[2:]
    cases = {
        "lone_high": (prefix + b"\x00\xd8" + b"A\x00" + tail, "unpaired high surrogate", len(prefix)),
        "lone_low": (prefix + b"\x00\xdc" + tail, "unpaired low surrogate", len(prefix)),
        "high_at_eof": (prefix + tail + b"\x3d\xd8", "high surrogate at end of input", len(prefix + tail)),
        "odd_trailing_byte": (prefix + tail + b"\n", "truncated code unit", len(prefix + tail)),
        "bom_decl_utf8": (encode(doc_text("UTF-8"), "le_bom"), "declared encoding does not match UTF-16 detection",
                          2 + 2 * doc_text("UTF-8").index("UTF-8")),
        "bom_decl_utf16le": (encode(doc_text("UTF-16LE"), "le_bom"),
                             "declared encoding does not match UTF-16 detection",
                             2 + 2 * doc_text("UTF-16LE").index("UTF-16LE")),
        "nobom_decl_utf16": (encode(doc_text("UTF-16"), "le"), "declared encoding does not match UTF-16 detection",
                             2 * doc_text("UTF-16").index("UTF-16")),
        "nobom_le_decl_be": (encode(doc_text("UTF-16BE"), "le"), "declared encoding does not match UTF-16 detection",
                             2 * doc_text("UTF-16BE").index("UTF-16BE")),
        "nobom_no_decl": (encode(doc_text(None), "le"), "not UTF-16: no BOM and no <?xml in UTF-16", 0),
        "utf8_input": (doc_text("UTF-8").encode("utf-8"), "not UTF-16: no BOM and no <?xml in UTF-16", 0),
        "non_ascii_decl": (encode(NON_ASCII_DECL, "le_bom"), "non-ASCII scalar in XML declaration",
                           2 + 2 * NON_ASCII_DECL.index("ё")),
        "lone_high_after_400KiB": (prefix + big + b"\x00\xd8A\x00" + tail, "unpaired high surrogate",
                                   len(prefix + big)),
    }
    for name, (data, msg, at) in cases.items():
        src = r.file(f"{name}.u16", data)
        res = r.run(["--src-chunk", 1 if "400KiB" not in name else 4096, src, r.dir / f"{name}.pdf"])
        dest = r.file(f"{name}.dest.pdf", b"previous destination\n")
        before = set(os.listdir(r.dir))
        fres = r.run(["--to-file", src, dest])
        after = set(os.listdir(r.dir))
        ok = (res["cause"] == 3 and res["adapter_error"] == msg and res["adapter_error_src"] == at
              and fres["cause"] == 3 and dest.read_bytes() == b"previous destination\n" and before == after
              and res["fd_open_after"] == 1)
        r.check(f"enc-error {name}", ok,
                {"src_sha256": sha(data), "message": res["message"], "adapter_error": res["adapter_error"],
                 "adapter_error_src": res.get("adapter_error_src"), "expected_src": at,
                 "callback_output_bytes": res["output_bytes"], "to_file_cause": fres["cause"]})
    ctl = r.run(["--src-chunk", 1, r.dir / "lone_high.u16", r.dir / "ctl.pdf"], binary=r.control)
    r.check("control: lenient adapter misses lone_high (the library then rejects it as XML)",
            ctl["adapter_error"] is None and ctl["cause"] == 1, {"control_result": ctl})


def run_cancel(r):
    text = doc_text("UTF-16", "".join(f"<l>{i} Ж</l>\n" for i in range(5000)))
    src = r.file("cancel.u16", encode(text, "be_bom"))
    res = r.run(["--cancel-after", 3, src, r.dir / "cancel.pdf"])
    dest = r.file("cancel.dest.pdf", b"previous destination\n")
    fres = r.run(["--to-file", "--cancel-after", 3, src, dest])
    ok = (res["cause"] == 6 and res["fd_open_after"] == 1 and fres["cause"] == 6
          and dest.read_bytes() == b"previous destination\n")
    r.check("cancel after 3 reads", ok, {"result": res, "to_file_cause": fres["cause"]})


def run_current(r):
    text = doc_text("UTF-16", comment=False)
    cases = {
        "le_bom": encode(text, "le_bom"),
        "be_bom": encode(text, "be_bom"),
        "le_nobom": encode(doc_text("UTF-16LE", comment=False), "le"),
        "be_nobom": encode(doc_text("UTF-16BE", comment=False), "be"),
        "two_byte_bom_only": b"\xff\xfe",
    }
    out = {}
    for name, data in cases.items():
        src = r.file(f"current_{name}.xml", data)
        p = subprocess.run([str(ROOT / "xml2pdf"), "--font", "fonts/DejaVuSans.ttf", str(src),
                            str(r.dir / f"current_{name}.pdf")], cwd=ROOT, capture_output=True, check=False)
        out[name] = {"exit": p.returncode, "stderr": p.stderr.decode().strip(), "src_sha256": sha(data)}
    r.check("current library rejects all UTF-16 forms", all(v["exit"] == 1 for v in out.values()), {"current": out})
    return out


def generate(n_lines, enc, sink):
    """Writes the large document in 64 KiB pieces; never holds the whole text."""
    sink.write(b"\xff\xfe" if enc == "utf-16-le" else b"")
    decl = '<?xml version="1.0" encoding="UTF-16"?>\n' if enc == "utf-16-le" else \
        '<?xml version="1.0" encoding="UTF-8" ?>\n'
    buf = [decl, "<report>\n"]
    size = 0
    for i in range(n_lines):
        s = f"<l n=\"{i}\">Строка {i}: Ёжик — ascii tail {i % 97}</l>\n"
        buf.append(s)
        size += len(s)
        if size > 65536:
            sink.write("".join(buf).encode(enc))
            buf, size = [], 0
    buf.append("</report>\n")
    sink.write("".join(buf).encode(enc))


def time_rss(cmd, stdin=None):
    t0 = time.monotonic()
    p = subprocess.Popen(["/usr/bin/time", "-f", "RSS_KB=%M", *map(str, cmd)], cwd=ROOT, stdin=stdin,
                         stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    return p, t0


def finish(p, t0):
    out, err = p.communicate()
    rss = int(re.findall(rb"RSS_KB=(\d+)", err)[-1])
    return out, rss, time.monotonic() - t0, p.returncode


def run_large(r, mib):
    n_lines = mib * 1024 * 1024 // 62
    u8 = r.dir / "large.u8.xml"
    with open(u8, "wb") as f:
        generate(n_lines, "utf-8", f)
    res = {"lines": n_lines, "utf8_xml_bytes": u8.stat().st_size, "utf8_xml_sha256": sha(u8.read_bytes())}
    p, t0 = time_rss([ROOT / "xml2pdf", "--font", "fonts/DejaVuSans.ttf", u8, r.dir / "large.cli.pdf"])
    _, rss, sec, rc = finish(p, t0)
    res["xml2pdf_utf8"] = {"rss_kib": rss, "seconds": round(sec, 2), "exit": rc}
    p, t0 = time_rss([r.adapter, "--mode", "utf8", u8, r.dir / "large.base.pdf"])
    out, rss, sec, rc = finish(p, t0)
    res["adapter_utf8_passthrough"] = {"rss_kib": rss, "seconds": round(sec, 2), "result": json.loads(out)}
    p, t0 = time_rss([r.adapter, "-", r.dir / "large.u16.pdf"], stdin=subprocess.PIPE)
    hasher = hashlib.sha256()
    count = [0]

    class Sink:
        def write(self, b):
            hasher.update(b)
            count[0] += len(b)
            p.stdin.write(b)

    generate(n_lines, "utf-16-le", Sink())
    p.stdin.close()
    p.stdin = None
    out, rss, sec, rc = finish(p, t0)
    res["adapter_utf16_pipe"] = {"rss_kib": rss, "seconds": round(sec, 2), "result": json.loads(out),
                                 "utf16_stream_bytes": count[0], "utf16_stream_sha256": hasher.hexdigest()}
    h = {k: sha((r.dir / f"large.{k}.pdf").read_bytes()) for k in ("cli", "base", "u16")}
    res["pdf_sha256"] = h
    res["pdf_bytes"] = (r.dir / "large.u16.pdf").stat().st_size
    ok = (len(set(h.values())) == 1 and res["adapter_utf16_pipe"]["result"]["status"] == 0
          and not (r.dir / "large.u16.xml").exists())
    r.check(f"large {mib} MiB streamed UTF-16 equals UTF-8 PDF", ok, {"large": res})
    for k in ("cli", "base", "u16"):
        (r.dir / f"large.{k}.pdf").unlink()
    u8.unlink()
    return res


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--scratch", required=True)
    ap.add_argument("--large-mib", type=int, default=0)
    ap.add_argument("--json")
    a = ap.parse_args()
    r = Runner(a.scratch)
    fixtures = {
        "le_bom": (doc_text("UTF-16"), "le_bom"),
        "be_bom": (doc_text("UTF-16"), "be_bom"),
        "le_bom_lower": (doc_text("utf-16"), "le_bom"),
        "be_bom_nodecl": (doc_text(None), "be_bom"),
        "le_nobom": (doc_text("UTF-16LE"), "le"),
        "be_nobom": (doc_text("UTF-16BE"), "be"),
    }
    t0 = time.monotonic()
    summary = {"adapter_sha256": r.adapter_sha, "fixtures": {k: sha(encode(*v)) for k, v in fixtures.items()}}
    summary["current"] = run_current(r)
    run_dump_matrix(r, fixtures)
    run_convert_matrix(r, fixtures)
    run_xml_errors(r)
    run_enc_errors(r)
    run_cancel(r)
    if a.large_mib:
        summary["large"] = run_large(r, a.large_mib)
    summary["seconds"] = round(time.monotonic() - t0, 2)
    summary["cases"] = len(r.records)
    summary["failures"] = r.failures
    summary["records"] = r.records
    if a.json:
        Path(a.json).write_text(json.dumps(summary, ensure_ascii=False, indent=1) + "\n")
    for rec in r.records:
        print(("ok   " if rec["ok"] else "FAIL ") + rec["case"])
    print(f"{len(r.records) - len(r.failures)}/{len(r.records)} passed")
    return 1 if r.failures else 0


if __name__ == "__main__":
    sys.exit(main())
