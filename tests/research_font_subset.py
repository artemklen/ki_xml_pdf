#!/usr/bin/env python3
"""OSR-10 measurement harness: residual embedded-font share after Flate.

Research tool only; it is not a product subsetter. For each xml2pdf PDF it
reports total bytes, the compressed FontFile2 stream bytes and their share.
It then sizes GID-preserving subsets of the font file (unused glyf entries
emptied, numGlyphs/hmtx/cmap kept so CID = GID, /W and ToUnicode stay valid)
for three glyph sets:

  usage       glyph 0 + every GID the page streams reference + composite closure
  repertoire  glyph 0 + every GID xml2pdf can emit (U+0000-04FF, U+2010-2027)
              + composite closure; known at font load, before any page

Each set is measured with all other tables kept ("keep-tables") and with
the shaping-only tables the writer never uses dropped ("drop-layout":
GSUB, GPOS, GDEF, kern, FFTM), and with only the ISO 32000 FontFile2
tables ("pdf-min"). Compression uses the writer's zlib settings.

--splice writes a copy of a PDF whose FontFile2 is the pdf-min subset so
Poppler output can be compared with the original (SET usage, repertoire,
or the negative control notdef-only); --self-test checks the
parser's failure detection on damaged fonts.
"""

import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import sys
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parent))
import pdf_streams  # noqa: E402

LAYOUT_TABLES = {b"GSUB", b"GPOS", b"GDEF", b"kern", b"FFTM"}
# ISO 32000-1 9.9: tables a FontFile2 for a CIDFontType2 needs; cmap only
# matters for symbolic simple TrueType fonts, not for Identity CID fonts.
PDF_TABLES = {b"head", b"hhea", b"hmtx", b"loca", b"maxp", b"glyf", b"cvt ", b"fpgm", b"prep"}
REPERTOIRE = list(range(0x0000, 0x0500)) + list(range(0x2010, 0x2010 + 24))


class FontError(ValueError):
    pass


def deflate(data):
    c = zlib.compressobj(zlib.Z_DEFAULT_COMPRESSION, zlib.DEFLATED, 15, 8, zlib.Z_DEFAULT_STRATEGY)
    return c.compress(data) + c.flush()


def span(data, off, length, what):
    if off < 0 or length < 0 or off + length > len(data):
        raise FontError(f"{what} [{off}, +{length}) outside {len(data)}-byte font")
    return data[off:off + length]


def tables(data):
    head = span(data, 0, 12, "offset table")
    num = struct.unpack(">H", head[4:6])[0]
    out = {}
    for i in range(num):
        tag, _csum, off, length = struct.unpack(">4sIII", span(data, 12 + 16 * i, 16, "table record"))
        span(data, off, length, tag.decode("latin-1"))
        out[tag] = (off, length)
    for need in (b"head", b"maxp", b"loca", b"glyf", b"hmtx", b"cmap"):
        if need not in out:
            raise FontError(f"missing {need.decode()} table")
    return out


def table(data, tabs, tag):
    off, length = tabs[tag]
    return data[off:off + length]


def glyph_spans(data, tabs):
    head = table(data, tabs, b"head")
    if len(head) < 54:
        raise FontError("short head")
    long_loca = struct.unpack(">h", head[50:52])[0]
    maxp = table(data, tabs, b"maxp")
    if len(maxp) < 6:
        raise FontError("short maxp")
    n = struct.unpack(">H", maxp[4:6])[0]
    loca = table(data, tabs, b"loca")
    width = 4 if long_loca else 2
    if len(loca) < (n + 1) * width:
        raise FontError("short loca")
    fmt = ">%d%s" % (n + 1, "I" if long_loca else "H")
    offs = struct.unpack(fmt, loca[:(n + 1) * width])
    if not long_loca:
        offs = [o * 2 for o in offs]
    glyf_len = tabs[b"glyf"][1]
    for i in range(n):
        if offs[i] > offs[i + 1] or offs[i + 1] > glyf_len:
            raise FontError(f"loca entry {i} outside glyf")
    return n, [(offs[i], offs[i + 1]) for i in range(n)]


def cmap_bmp(data, tabs):
    """Unicode BMP -> GID from a (3,1) or (0,x) format 4 subtable."""
    cmap = table(data, tabs, b"cmap")
    if len(cmap) < 4:
        raise FontError("short cmap")
    n = struct.unpack(">H", cmap[2:4])[0]
    for i in range(n):
        if 4 + 8 * i + 8 > len(cmap):
            raise FontError("short cmap records")
        pid, eid, off = struct.unpack(">HHI", cmap[4 + 8 * i:12 + 8 * i])
        if not ((pid == 3 and eid == 1) or pid == 0):
            continue
        if off + 14 > len(cmap) or struct.unpack(">H", cmap[off:off + 2])[0] != 4:
            continue
        length = struct.unpack(">H", cmap[off + 2:off + 4])[0]
        sub = cmap[off:off + length]
        if len(sub) != length:
            raise FontError("cmap format 4 outside table")
        segx2 = struct.unpack(">H", sub[6:8])[0]
        seg = segx2 // 2
        base = 14
        if base + 4 * segx2 + 2 > length:
            raise FontError("cmap format 4 segments outside subtable")
        ends = struct.unpack(">%dH" % seg, sub[base:base + segx2])
        starts = struct.unpack(">%dH" % seg, sub[base + segx2 + 2:base + 2 * segx2 + 2])
        deltas = struct.unpack(">%dh" % seg, sub[base + 2 * segx2 + 2:base + 3 * segx2 + 2])
        ro_at = base + 3 * segx2 + 2
        ranges = struct.unpack(">%dH" % seg, sub[ro_at:ro_at + segx2])
        out = {}
        for s in range(seg):
            for cp in range(starts[s], ends[s] + 1):
                if cp == 0xFFFF:
                    continue
                if ranges[s] == 0:
                    g = (cp + deltas[s]) & 0xFFFF
                else:
                    at = ro_at + 2 * s + ranges[s] + 2 * (cp - starts[s])
                    if at + 2 > length:
                        raise FontError("cmap glyphIdArray outside subtable")
                    g = struct.unpack(">H", sub[at:at + 2])[0]
                    if g:
                        g = (g + deltas[s]) & 0xFFFF
                if g:
                    out[cp] = g
        return out
    raise FontError("no Unicode format 4 cmap")


ARG_WORDS, HAVE_SCALE, MORE, XY_SCALE, TWO_BY_TWO = 0x1, 0x8, 0x20, 0x40, 0x80


def components(glyph):
    if len(glyph) < 10 or struct.unpack(">h", glyph[:2])[0] >= 0:
        return []
    out, at = [], 10
    while True:
        if at + 4 > len(glyph):
            raise FontError("composite glyph truncated")
        flags, gid = struct.unpack(">HH", glyph[at:at + 4])
        out.append(gid)
        at += 4 + (4 if flags & ARG_WORDS else 2)
        at += 8 if flags & TWO_BY_TWO else 4 if flags & XY_SCALE else 2 if flags & HAVE_SCALE else 0
        if not flags & MORE:
            return out


def closure(data, tabs, gids):
    n, spans = glyph_spans(data, tabs)
    glyf_off = tabs[b"glyf"][0]
    keep, todo = set(), [0] + sorted(gids)
    while todo:
        g = todo.pop()
        if g in keep:
            continue
        if g >= n:
            raise FontError(f"GID {g} >= numGlyphs {n}")
        keep.add(g)
        a, b = spans[g]
        todo.extend(components(data[glyf_off + a:glyf_off + b]))
    return keep


def checksum(b):
    b = b + b"\0" * (-len(b) % 4)
    return sum(struct.unpack(">%dI" % (len(b) // 4), b)) & 0xFFFFFFFF


def gid_preserving_subset(data, keep, drop=frozenset()):
    tabs = tables(data)
    n, spans = glyph_spans(data, tabs)
    glyf_off = tabs[b"glyf"][0]
    glyf, loca = bytearray(), []
    for g in range(n):
        loca.append(len(glyf))
        if g in keep:
            a, b = spans[g]
            glyf += data[glyf_off + a:glyf_off + b]
            glyf += b"\0" * (-len(glyf) % 4)
    loca.append(len(glyf))
    new = {tag: table(data, tabs, tag) for tag in tabs if tag not in drop}
    new[b"glyf"] = bytes(glyf)
    new[b"loca"] = struct.pack(">%dI" % len(loca), *loca)
    head = bytearray(new[b"head"])
    head[8:12] = b"\0\0\0\0"
    head[50:52] = struct.pack(">h", 1)
    new[b"head"] = bytes(head)
    tags = sorted(new)
    num = len(tags)
    es = max(i for i in range(16) if (1 << i) <= num)
    out = bytearray(struct.pack(">IHHHH", 0x00010000, num, 16 << es, es, num * 16 - (16 << es)))
    off = 12 + 16 * num
    body = bytearray()
    for tag in tags:
        t = new[tag]
        out += struct.pack(">4sIII", tag, checksum(t), off + len(body), len(t))
        body += t + b"\0" * (-len(t) % 4)
    font = bytearray(out + body)
    head_at = struct.unpack(">I", font[12 + 16 * tags.index(b"head") + 8:][:4])[0]
    font[head_at + 8:head_at + 12] = struct.pack(">I", (0xB1B0AFBA - checksum(bytes(font))) & 0xFFFFFFFF)
    return bytes(font)


def pdf_font_and_gids(pdf):
    font_row, gids = None, set()
    for num, d, length, decoded in pdf_streams.streams(pdf):
        if b"/Length1 " in d:
            font_row = (num, length, decoded)
        elif decoded.startswith(b"BT\n"):
            for h in re.findall(rb"<([0-9A-Fa-f]+)> Tj", decoded):
                gids.update(int(h[i:i + 4], 16) for i in range(0, len(h), 4))
    if font_row is None:
        raise FontError("no FontFile2 stream")
    return font_row, gids


def measure(pdf_path):
    pdf = Path(pdf_path).read_bytes()
    (num, comp_len, ttf), used = pdf_font_and_gids(pdf)
    tabs = tables(ttf)
    n, _ = glyph_spans(ttf, tabs)
    cmap = cmap_bmp(ttf, tabs)
    sets = {
        "usage": closure(ttf, tabs, used),
        "repertoire": closure(ttf, tabs, {cmap[c] for c in REPERTOIRE if c in cmap}),
    }
    variants = {"full": {"font_bytes": len(ttf), "font_flate_bytes": comp_len, "glyphs": n}}
    for name, keep in sets.items():
        pdf_min = frozenset(t for t in tabs if t not in PDF_TABLES)
        for label, drop in (("keep-tables", frozenset()), ("drop-layout", LAYOUT_TABLES),
                            ("pdf-min", pdf_min)):
            sub = gid_preserving_subset(ttf, keep, drop)
            z = len(deflate(sub))
            variants[f"{name}/{label}"] = {
                "font_bytes": len(sub), "font_flate_bytes": z, "glyphs": len(keep),
                "pdf_bytes_estimate": len(pdf) - comp_len + z,
                "saving_bytes": comp_len - z,
                "saving_share_of_pdf": round((comp_len - z) / len(pdf), 6),
            }
    return {
        "pdf": Path(pdf_path).name,
        "pdf_sha256": hashlib.sha256(pdf).hexdigest(),
        "pdf_bytes": len(pdf),
        "font_object": num,
        "font_sha256": hashlib.sha256(ttf).hexdigest(),
        "font_flate_bytes": comp_len,
        "font_share": round(comp_len / len(pdf), 6),
        "used_gids_in_pages": len(used),
        "table_bytes": {t.decode(): l for t, (_, l) in sorted(tabs.items())},
        "table_flate_bytes": {t.decode(): len(deflate(table(ttf, tabs, t))) for t in sorted(tabs)},
        "variants": variants,
    }


def splice(pdf_path, out_path, glyph_set):
    """Rewrites the PDF with the prototype subset as FontFile2 and a new xref."""
    pdf = Path(pdf_path).read_bytes()
    (num, _, ttf), used = pdf_font_and_gids(pdf)
    tabs = tables(ttf)
    if glyph_set == "usage":
        keep = closure(ttf, tabs, used)
    elif glyph_set == "notdef-only":
        keep = closure(ttf, tabs, set())
    else:
        cmap = cmap_bmp(ttf, tabs)
        keep = closure(ttf, tabs, {cmap[c] for c in REPERTOIRE if c in cmap})
    sub = gid_preserving_subset(ttf, keep, frozenset(t for t in tabs if t not in PDF_TABLES))
    z = deflate(sub)
    offsets = pdf_streams.xref_offsets(pdf)
    order = sorted(offsets, key=offsets.get)
    xref_at = pdf.rfind(b"xref\n0 ")
    objs = {}
    for i, o in enumerate(order):
        end = offsets[order[i + 1]] if i + 1 < len(order) else xref_at
        objs[o] = pdf[offsets[o]:end]
    objs[num] = (b"%d 0 obj\n<< /Length %d 0 R /Filter /FlateDecode /Length1 %d >>\nstream\n"
                 % (num, num + 1, len(sub)) + z + b"\nendstream\nendobj\n")
    objs[num + 1] = b"%d 0 obj\n%d\nendobj\n" % (num + 1, len(z))
    first = pdf[:offsets[order[0]]]
    out, new_off = bytearray(first), {}
    for o in order:
        new_off[o] = len(out)
        out += objs[o]
    size = int(re.match(rb"xref\n0 (\d+)\n", pdf[xref_at:]).group(1))
    trailer = pdf[pdf.find(b"trailer", xref_at):pdf.rfind(b"startxref\n")]
    xref = len(out)
    out += b"xref\n0 %d\n0000000000 65535 f \n" % size
    for o in range(1, size):
        out += b"%010d 00000 n \n" % new_off[o] if o in new_off else b"0000000000 65535 f \n"
    out += trailer + b"startxref\n%d\n%%%%EOF\n" % xref
    Path(out_path).write_bytes(out)
    list(pdf_streams.streams(bytes(out)))
    return {"out": str(out_path), "glyphs": len(keep), "font_bytes": len(sub),
            "font_flate_bytes": len(z), "pdf_bytes": len(out)}


def self_test(font_path):
    ttf = Path(font_path).read_bytes()
    tabs = tables(ttf)
    keep = closure(ttf, tabs, {cmap_bmp(ttf, tabs)[0x41]})
    assert 0 in keep, ".notdef must be kept"
    sub = gid_preserving_subset(ttf, keep)
    assert checksum(sub) == 0xB1B0AFBA, "whole-font checksum must match head adjustment"
    st = tables(sub)
    assert glyph_spans(sub, st)[0] == glyph_spans(ttf, tabs)[0], "numGlyphs must be preserved"
    # A composite glyph pulls in its components (DejaVu: U+00C0 A-grave
    # references A and a combining-grave glyph that has no BMP mapping).
    agrave = cmap_bmp(ttf, tabs)[0xC0]
    comp = components(ttf[tabs[b"glyf"][0] + glyph_spans(ttf, tabs)[1][agrave][0]:])
    assert comp and set(comp) <= closure(ttf, tabs, {agrave}), "composite closure"
    assert not set(comp) <= closure(ttf, tabs, set()), "closure control must miss components"
    rejected = 0
    for bad in (ttf[:8], ttf[:tabs[b"loca"][0] + 4], ttf[:tabs[b"glyf"][0] + 10]):
        try:
            closure(bad, tables(bad), {36})
        except FontError:
            rejected += 1
    broken = bytearray(ttf)
    loca_off = tabs[b"loca"][0]
    broken[loca_off + 8:loca_off + 12] = b"\xff\xff\xff\xff"
    try:
        glyph_spans(bytes(broken), tabs)
    except FontError:
        rejected += 1
    assert rejected == 4, f"only {rejected}/4 damaged fonts rejected"
    print(json.dumps({"self_test": "ok", "agrave_components": comp, "damaged_rejected": rejected}))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("pdf", nargs="*")
    ap.add_argument("--splice", nargs=3, metavar=("PDF", "OUT", "SET"))
    ap.add_argument("--self-test", metavar="FONT")
    a = ap.parse_args()
    if a.self_test:
        self_test(a.self_test)
    if a.splice:
        print(json.dumps(splice(*a.splice)))
    for p in a.pdf:
        print(json.dumps(measure(p), indent=1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
